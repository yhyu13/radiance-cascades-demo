# Critic Review 02 - gpu_sdf_step8_impl.md

Reviewed: 2026-05-09T12:01:44+08:00

Target: `doc/5/claude_plan/gpu_sdf_step8_impl.md`

## Verdict

The implementation did land the important static GPU path: `sdf_3d.comp` now loads, the GPU JFA dispatch writes `sdfTexture`/`albedoTexture`, the CPU path still works, and a Release build succeeds via CMake. The preserved Step 8 logs also show Cornell and Sponza OBJ meshes baking on the GPU in about 3.9-4.1 ms for the JFA section.

The implementation note is still too optimistic. The biggest remaining source bug is that dynamic mode does not actually force "ALL cascades, no stagger"; it sets `forceCascadeRebuild`, but `updateRadianceCascades()` still skips cascades based on `renderFrameIndex`. There is also a reversibility bug in the ImGui dynamic-sphere toggle, and the dynamic sphere is a solid voxel volume, not a surface SDF primitive. The performance and parity claims are useful smoke evidence, but they are not yet regression-grade verification.

## Findings

### 1. Dynamic mode does not bypass cascade staggering

Severity: High

The implementation note says dynamic sphere mode sets:

```cpp
forceCascadeRebuild = true;    // ALL cascades, no stagger
historyNeedsSeed    = true;    // alpha=1.0, no EMA ghost
```

The source comment says the same at `src/demo3d.cpp:621`, but the actual scheduler never receives a "force all cascades" input. The render pass clears `forceCascadeRebuild` before calling `updateRadianceCascades()` (`src/demo3d.cpp:849-860`), and `updateRadianceCascades()` still applies:

```cpp
if ((renderFrameIndex % interval) != 0) continue;
```

at `src/demo3d.cpp:1807-1811`.

Only the RenderDoc path resets `renderFrameIndex = 0` when `rdocForceRebuildCount > 0` (`src/demo3d.cpp:843-848`). The dynamic-sphere update does not reset `renderFrameIndex`. Therefore C1/C2/C3 can remain 1/3/7 frames stale while the SDF changes every frame, and the implementation note's "no cascade staggering, no EMA accumulation" claim is false.

Suggested fix: add an explicit `forceAllCascades` parameter/state that bypasses the interval test, or set `renderFrameIndex = 0` for every dynamic rebuild if the intended policy is truly full cost per frame. Keep `historyNeedsSeed` true until every active cascade has actually rebuilt.

### 2. Disabling the dynamic sphere leaves the last sphere baked in

Severity: Medium

The dynamic path restores `meshVoxelBaseTexture`, injects the sphere, and invalidates the mesh SDF only while this gate is true:

```cpp
if (dynamicSphereEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture) {
```

at `src/demo3d.cpp:594`.

The ImGui checkbox at `src/demo3d.cpp:4107-4110` only toggles `dynamicSphereEnabled` and logs. It does not restore `voxelGridTexture` from `meshVoxelBaseTexture`, mark `meshSDFReady=false`, or invalidate cascades on the transition from enabled to disabled.

Impact: after a frame with the dynamic sphere enabled, the last injected sphere can remain in `voxelGridTexture`, `sdfTexture`, and the cascades after the user turns the overlay off. Because `meshSDFReady` and `cascadeReady` were set true by the last render, no automatic base-scene rebake happens.

Suggested fix: track the previous dynamic-sphere enabled state. On disable, copy `meshVoxelBaseTexture` back into `voxelGridTexture`, set `meshSDFReady=false`, `cascadeReady=false`, `historyNeedsSeed=true`, and force a full cascade rebuild.

### 3. The dynamic sphere is a solid seed volume, not a surface SDF primitive

Severity: Medium

`addVoxelSphere()` fills every voxel whose center lies inside the radius (`src/demo3d.cpp:3158-3169`). The GPU init pass treats every alpha voxel as a seed (`res/shaders/sdf_3d.comp:67-72`). That means the generated field is zero across the entire interior of the sphere, not just on a surface band.

This is enough to make a visible orange blocker in the demo screenshots, but it is not equivalent to adding a sphere SDF. It can create zero-gradient interior regions and chunky/sliced silhouettes, which are visible in `tools/step8_dynamic_t0p0.png` and `tools/step8_dynamic_t3p0.png`. The implementation note should not present this as a general dynamic SDF primitive.

Suggested fix: either rasterize only a shell band around `abs(length(p-center)-radius) <= halfVoxelDiagonal`, or composite an analytic sphere distance into the final SDF/albedo output. If the solid voxel volume is intentionally just a blocker smoke test, document that limitation explicitly.

### 4. `generateMeshSDFGPU()` reports success without GL error or texture-handle validation

Severity: Medium

`createVolumeBuffers()` only logs if `voronoiTextureA`, `voronoiTextureB`, or `meshVoxelBaseTexture` allocation fails (`src/demo3d.cpp:2493-2508`). `generateMeshSDFGPU()` then binds those handles and returns `true` after dispatch/timer logging without checking:

- whether `voronoiTextureA/B`, `voxelGridTexture`, `sdfTexture`, and `albedoTexture` are non-zero;
- whether uniform locations are valid;
- whether `glBindImageTexture`, `glDispatchCompute`, or the timer query produced a GL error.

The CPU EDT path checks texture upload errors before returning success (`src/demo3d.cpp:1602-1622`); the GPU path should have comparable failure propagation. Otherwise the render loop can set `meshSDFReady=true` after a failed GPU bake and preserve stale output.

Suggested fix: fail early on missing texture handles and add `glGetError()` checks around the dispatch sequence, at least in debug/verification builds. If the GPU path fails, return `false` so the existing render loop retry/fallback behavior can work.

### 5. The performance claim is JFA-only, not full dynamic-frame cost

Severity: Medium

The 24x speedup claim is reasonable for the isolated SDF bake section: the logs show CPU EDT + albedo at about 98.6 ms (`tools/app_run_step8_cpu.log:138`) and static GPU JFA at 4.096 ms (`tools/app_run_step8_gpu.log:138`).

But the dynamic-sphere headline is stronger than the evidence. Each deterministic dynamic run logs 120 GPU JFA bakes, with averages around 3.4-3.5 ms and max spikes above 5 ms:

| Log | Count | Avg GPU ms | Max GPU ms |
|---|---:|---:|---:|
| `app_run_step8_dynamic_t0p0.log` | 120 | 3.502 | 5.886 |
| `app_run_step8_dynamic_t1p5.log` | 120 | 3.482 | 5.167 |
| `app_run_step8_dynamic_t3p0.log` | 120 | 3.488 | 5.440 |
| `app_run_step8_dynamic_t4p5.log` | 120 | 3.409 | 5.076 |

Those timings exclude the texture copy, CPU sphere upload, cascade rebuild work, final raymarch, readbacks, and screenshot capture. `glGetQueryObjectui64v(..., GL_QUERY_RESULT, ...)` also blocks the CPU until the query result is available (`src/demo3d.cpp:1702-1704`), which is acceptable for measurement but not free runtime instrumentation.

Suggested fix: keep the 24x claim scoped to "SDF bake/JFA section" and add a full-frame dynamic timing breakdown before claiming 60 fps headroom.

### 6. CPU/GPU parity is only a visual smoke check

Severity: Low

The implementation note admits no formal numeric diff script landed, but the summary table still says "visual parity" in a way that reads like verification. A quick pixel comparison of the preserved screenshots found:

```text
tools/step8_cornell_orig_cpu.png vs tools/step8_cornell_orig_gpu.png
changed pixels: 68,111 / 921,600 (7.39%)
RGB MAE: 0.171
max RGB delta: 99
```

The images are visually close and the low MAE is a good smoke result, but they are not identical. This is expected because GPU JFA is approximate and the GPU albedo path uses nearest-seed lookup while CPU uses a 3-iteration 6-neighbor dilation (`src/demo3d.cpp:1571-1597`, `res/shaders/sdf_3d.comp:120-133`).

Suggested fix: downgrade the wording to "visually close in the checked camera" and add either an SDF readback tolerance test or a screenshot diff threshold before treating CPU/GPU parity as regression coverage.

### 7. Build verification is mostly reproduced, but the warning-count claim is unsupported

Severity: Low

I reproduced the implementation note's CMake build command:

```text
cmake --build . --config Release
RadianceCascades3D.vcxproj -> ...\build\RadianceCascades3D.exe
```

That succeeded. A direct MSBuild solution target returned failure while reporting `0 warnings` and `0 errors`, which appears to be a target-selection quirk, not a compiler error. The implementation note's "`warnings unchanged from Step 7 baseline 38`" claim is not supported by the preserved Step 8 logs or the incremental CMake output I saw.

Suggested fix: either preserve a clean build log that actually emits the warning count, or state only that the Release CMake build completed successfully.

## What Looks Solid

- Re-enabling and rewriting `sdf_3d.comp` fixed the previous runtime shader compile failure; Step 8 logs show `Shader loaded successfully: sdf_3d.comp`.
- The final GPU barrier includes both image-access and texture-fetch visibility before later shader sampling (`src/demo3d.cpp:1693-1695`).
- The GPU path uses a real `GL_TIME_ELAPSED` query instead of relying only on CPU submission time.
- `addVoxelSphere()` uses the actual `volumeOrigin`/`volumeSize` world-to-voxel mapping and one subvolume upload rather than `addVoxelBox()`'s per-voxel uploads.
- The static GPU path handles both Cornell-Original and Sponza-master bake logs (`tools/app_run_step8_gpu.log:138`, `tools/app_run_step8_sponza_gpu.log:138`).

## Verification I Ran

- Read the target implementation note and compared it with `src/demo3d.cpp`, `src/demo3d.h`, `src/main3d.cpp`, and `res/shaders/sdf_3d.comp`.
- Checked the preserved Step 8 app logs and screenshots under `tools/`.
- Ran `cmake --build . --config Release` from `build/`; it succeeded.
- Compared preserved CPU/GPU Cornell screenshots with a simple pixel diff.

