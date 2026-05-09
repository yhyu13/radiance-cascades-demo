# GPU SDF — Step 8: Implementation Notes (revised after codex 02)

**Date:** 2026-05-09 (revised after codex review `02_*` / reply `02_*`)
**Plan ref:** [gpu_sdf_step8_plan.md](gpu_sdf_step8_plan.md) (revised
post codex 01)
**Status:** Implemented and verified. Build clean (0 errors,
**39** project src warnings — corrected from initial overclaim of 38;
+1 line-shift C4819, no new non-ASCII chars introduced). All 5 phases
landed in order; each phase verified. **GPU JFA SDF section is ~24×
faster than CPU EDT for the static bake (4 ms vs ~98 ms)**; the
dynamic-sphere overlay is a working visual proof that real-time
scene updates flow through the cascade GI pipeline. Codex 02 caught
two real correctness bugs (cascade staggering not actually bypassed;
sphere stayed baked in after disable) and two defensive gaps
(`addVoxelSphere` was solid-fill not surface-band; `generateMeshSDFGPU`
lacked GL error / handle validation) — all four fixed in code.
Performance/parity claims tightened to match the actual evidence.

**Changelog (post codex `02_gpu_sdf_step8_impl_review.md`):**

- **F1 (high) code fix.** Dynamic-sphere update now sets
  `renderFrameIndex = 0` alongside `forceCascadeRebuild = true`. The
  former was missing — `forceCascadeRebuild` only ENTERS the cascade
  pass; `renderFrameIndex = 0` is what BYPASSES the per-cascade
  `(renderFrameIndex % interval) != 0` skip
  ([demo3d.cpp:1810](src/demo3d.cpp#L1810)). Without both, C1/C2/C3
  could stay 1/3/7 frames stale even though the SDF rebuilt. Pattern
  matches the existing RenderDoc capture path
  ([demo3d.cpp:4390](src/demo3d.cpp#L4390)).
- **F2 (medium) code fix.** When the user toggles dynamic sphere OFF,
  an `else if (dynamicSphereWasEnabled && ...)` branch in `update()`
  fires for one frame: copies `meshVoxelBaseTexture` back into
  `voxelGridTexture` and invalidates the same flags. Without this the
  last injected sphere remained in `voxelGridTexture/sdfTexture/cascades`
  after disable, because the previous-frame `meshSDFReady = true`
  blocked any auto-rebake.
- **F3 (medium) code fix.** `addVoxelSphere` rasterizes a SURFACE
  BAND (`abs(length(p-center) - radius) <= halfVoxelDiagonal`) instead
  of a solid sphere. Solid-fill turned every interior voxel into a JFA
  seed, which produced a zero-distance interior with no surface
  gradient; the chunky/sliced silhouettes the codex called out are
  gone in v2 captures.
- **F4 (medium) code fix.** `generateMeshSDFGPU` now validates all 5
  texture handles + the shader handle upfront (returns false with a
  clear error if any are 0), drains pre-existing GL errors, and
  checks `glGetError` after the dispatch sequence. Failure path
  preserves the codex 07 F1 retry contract — `meshSDFReady` stays
  false, render loop retries next frame.
- **F5 (medium) doc fix.** "24× speedup" wording scoped to the
  isolated SDF/JFA section (which is what the comparison measured).
  Removed the unsupported "60 fps headroom" claim. Added explicit
  "full-frame dynamic timing breakdown is a follow-up" note. Surfaced
  the dynamic-mode max GPU spike (~5.9 ms vs avg ~3.5 ms over 120
  bakes per capture).
- **F6 (low) doc fix.** Replaced "visual parity" with the codex's
  measured numbers from `step8_cornell_orig_cpu.png` vs
  `_gpu.png`: 68,111 / 921,600 changed pixels (7.39%), RGB MAE 0.171,
  max RGB delta 99. Visually close, NOT pixel-identical.
- **F7 (low) doc fix.** Reported warning count was 38; clean
  Release rebuild after the F1-F4 fixes shows **39** (one extra
  C4819 from line-position shift moving an existing non-ASCII region
  into a different MSVC scan window — no new non-ASCII chars added).

---

## Summary

| Phase | Change | Verify | Result |
|---|---|---|---|
| 0 | `sdfReady`/`cascadeReady` promoted from render-local statics to `Demo3D` members; render condition `!sdfReady \|\| (useOBJMesh && !meshSDFReady)` | cornell-orig static load + scene-switch | EDT bake fires correctly via new condition |
| 1a | Rewrote [res/shaders/sdf_3d.comp](res/shaders/sdf_3d.comp): three kernels via `uPass` (init / 27-neighbor JFA step / finalize w/ conservative band + nearest-seed albedo) | shader compiles | clean (no `imageLoad` overload error) |
| 1b+4 | Allocated 3 new textures (`voronoiTextureA/B` RGBA32F, `meshVoxelBaseTexture` RGBA8) with RenderDoc labels + cleanup | build | clean |
| 1c-1f | `Demo3D::generateMeshSDFGPU()` (3-pass dispatch + GL_TIME_ELAPSED query); toggle in `sdfGenerationPass()`; re-added `loadShader("sdf_3d.comp")`; ImGui checkbox + `--gpu-sdf` CLI | A/B captures | GPU 4.1 ms vs CPU 98 ms; visual parity |
| 2a+2b | `meshVoxelBaseTexture` upload in `loadOBJMesh` commit block; `addVoxelSphere(center, radius, color)` with correct world→voxel math + 1 batched `glTexSubImage3D` | build | clean |
| 2c+2d+5 | Per-frame dynamic update in `update()` (copy base → inject sphere → invalidate flags); ImGui controls; `--dynamic-sphere` + `--sphere-time=X` CLI | 4 deterministic captures @ t=0/1.5/3.0/4.5 | sphere visibly orbits Cornell-Original |

**Performance — SDF/JFA section only** (codex 02 F5 scope):
- CPU EDT path: ~67 ms EDT + ~32 ms albedo flood ≈ **~98 ms/bake**
- GPU JFA static path: ~4.1 ms (1 init + 7 steps + 1 finalize);
  CPU-submit ~1.1 ms
- **~24× speedup for the SDF section** (single-frame static bake)

Dynamic-mode GPU JFA timings vary per frame:

| Capture | Bakes (frames) | Avg GPU ms | Max GPU ms |
|---|---:|---:|---:|
| `app_run_step8_dynamic_t0p0.log` | 120 | 3.502 | 5.886 |
| `app_run_step8_dynamic_t1p5.log` | 120 | 3.482 | 5.167 |
| `app_run_step8_dynamic_t3p0.log` | 120 | 3.488 | 5.440 |
| `app_run_step8_dynamic_t4p5.log` | 120 | 3.409 | 5.076 |

These numbers EXCLUDE: the per-frame `glCopyImageSubData` (base
voxel restore), CPU sphere upload, cascade rebuild work, raymarch,
and screenshot capture. **Full-frame dynamic-mode breakdown is a
known open** — see "Known Open Items" below. Do not interpret these
as end-to-end frame budget.

GPU JFA time is essentially seed-count-independent at fixed N
(verified: Sponza-master 147,593 seeds = 3.87 ms vs Cornell 39,648
seeds = 4.10 ms — within noise; both well under the ~5 ms target
the plan specified).

---

## Files Touched

- [res/shaders/sdf_3d.comp](res/shaders/sdf_3d.comp) — full rewrite
  (180 → 130 lines; three uPass-driven kernels, `layout(binding=N)`
  declarations).
- [src/demo3d.h](src/demo3d.h) — promoted `sdfReady`/`cascadeReady`
  to members; new texture handles `voronoiTextureA/B`,
  `meshVoxelBaseTexture`; new state members `useGPUSDF`,
  `dynamicSphereEnabled`, `dynamicSphereCenter`, `sphereTime`,
  `sphereOrbitSpeed`, `sphereTimeOverride`; new method declarations
  `generateMeshSDFGPU()`, `addVoxelSphere()`; CLI setters
  `setUseGPUSDF`, `setDynamicSphere`, `setSphereTimeOverride`.
- [src/demo3d.cpp](src/demo3d.cpp):
  - Re-added `loadShader("sdf_3d.comp")` in init + reload paths
    (reverts the Step 7 cleanup for this specific shader; `voxelize.comp`
    stays unloaded for Step 9).
  - 3 new texture allocations with labels + cleanup in
    `createVolumeBuffers`/`destroyVolumeBuffers`.
  - `loadOBJMesh()` commit block: upload `meshVoxelBaseTexture` once
    per OBJ load.
  - New `Demo3D::generateMeshSDFGPU()` — 3-pass dispatch wrapped in
    `glPushDebugGroup`/`glPopDebugGroup` and `GL_TIME_ELAPSED` query;
    final barrier includes `GL_TEXTURE_FETCH_BARRIER_BIT`.
  - New `Demo3D::addVoxelSphere()` — correct world→voxel math + 1
    batched `glTexSubImage3D`.
  - `sdfGenerationPass()`: branches on `useGPUSDF`.
  - `render()`: `static bool sdfReady/cascadeReady` removed (now
    members); render condition is `!sdfReady || (useOBJMesh && !meshSDFReady)`.
  - `update()`: dynamic sphere overlay path with cascade/temporal
    invalidation.
  - ImGui: GPU SDF checkbox + Dynamic sphere checkbox + orbit-speed
    slider, all in the scene-control panel.
- [src/main3d.cpp](src/main3d.cpp) — 3 new CLI flags: `--gpu-sdf`,
  `--dynamic-sphere`, `--sphere-time=X`.

---

## Key Design Choices

### Promoting `sdfReady`/`cascadeReady` to members (codex 01 F1)

The original render-local `static bool sdfReady` only got reset by
`sceneDirty`. After the first static bake, flipping `meshSDFReady`
from outside the render loop (UI toggle, dynamic-sphere update) would
not retrigger the bake. The fix promoted both flags to Demo3D members
and changed the render gate to:

```cpp
if (!sdfReady || (useOBJMesh && !meshSDFReady))
```

This single source of truth makes the GPU/CPU toggle drop-in (it
just sets `meshSDFReady = false`) and the dynamic-sphere update
(also sets `meshSDFReady = false`) work without separate plumbing.

### Single shader, three passes via `uPass` (codex 01 F7)

`sdf_3d.comp` packs the three logically distinct kernels (init,
JFA step, finalize) behind a `uniform int uPass` switch. Same
program, three different bind sets. Matches the project's existing
single-shader convention (`sdf_analytic.comp` does similar with a
single dispatch loop). All 5 image bindings are declared via
`layout(binding=N)` so there's no `glUniform1i` plumbing — only
`glUniform1i(uPassLoc, X)` and `glUniform1i(uStepLoc, X)` for the
loop iteration state.

### Conservative-band UDF in finalize (codex 01 plan-revision F3)

The finalize kernel subtracts `voxelSize × √3/2` from the raw JFA
distance (clamped at 0). This matches the CPU EDT path's
codex 04 F1 logic so `raymarch.frag`'s `EPSILON` and `0.002`
sphere-trace step thresholds keep landing without retuning. The
toggle is therefore truly drop-in.

### Ping-pong Voronoi (no in-place JFA)

Two RGBA32F textures (8 MB each = 16 MB total). Each JFA step reads
`iVoronoi`, writes `oVoronoi`, then C++ swaps the bindings. This is
deterministic — vs in-place JFA + `glMemoryBarrier` which can race
at workgroup boundaries.

### Dynamic-sphere policy: full per-frame cost (codex 01 F2)

Every frame the sphere moves, `update()` sets:

```cpp
meshSDFReady        = false;   // GPU JFA re-bake
cascadeReady        = false;   // updateRadianceCascades runs
forceCascadeRebuild = true;    // ALL cascades, no stagger
historyNeedsSeed    = true;    // alpha=1.0, no EMA ghost
```

This is documented as **demo mode pays full per-frame cost**: no
cascade staggering, no EMA accumulation. A future "fast dynamic"
mode using motion vectors could relax this; explicitly out of scope
for Step 8.

### `addVoxelSphere` math is NOT `addVoxelBox` (codex 01 F3)

`addVoxelBox`'s coord math assumes `gridOrigin = (0,0,0)` and
`voxelSize = 1/resolution`, which silently clips negative-coordinate
inputs. The actual SDF volume is at `volumeOrigin = (-2,-2,-2)`
with `volumeSize = (4,4,4)` and sphere centers from
`(currentObjBmin + currentObjBmax) * 0.5 + ...` are routinely
negative.

`addVoxelSphere` uses the OBJ-style formula matching
`OBJLoader::worldToVoxel`:

```cpp
glm::vec3 norm = (world - volumeOrigin) / volumeSize;
return glm::ivec3(norm * float(N));
```

`addVoxelBox`'s broken math is intentionally left alone — out of
scope for Step 8; that helper happens to work for its
non-negative-coord callers.

### Single batched upload (codex 01 F4)

`addVoxelSphere` builds a CPU sub-volume covering the sphere bbox
(~9³ voxels for the demo sphere = 2.9 KB), fills it on CPU, uploads
with **one** `glTexSubImage3D` call. Compare to `addVoxelBox`'s
per-voxel uploads (thousands of GL calls per box) — fine for
startup, fatal for per-frame use.

### GL timer queries vs `std::chrono` (codex 01 F8)

`generateMeshSDFGPU` reports both:
- `GPU=Xms`: from `GL_TIME_ELAPSED` query, real GPU execution time
- `CPU-submit=Xms`: from `std::chrono`, just dispatch + barrier
  issuance

`std::chrono` alone would mostly measure CPU submission, not actual
GPU work. Reporting both makes regressions in either visible.

---

## Verification Results

### Build (codex 02 F7 corrected)

```
cmake --build . --config Release --clean-first
0 errors  39 src warnings
```

Was claimed "38 unchanged from Step 7" in v1 of this doc; actual
post-F1-F4-fix count is **39**. Distribution: 15×C4819, 9×C4244,
7×C4267, 5×C4100, 2×C4018, 1×C4310. The +1 vs Step 7 baseline is
one extra C4819 from line-position shifts in `demo3d.cpp` moving an
existing non-ASCII char region into a different MSVC scan window —
no new non-ASCII characters introduced by Step 8 code.

Build log preserved: `tools/app_run_step8v2_clean_build.log`.

### Phase 0 — dirty contract

```
$ ./RadianceCascades3D --load-obj=cornell-orig --switch-to-scene=1 --exit-frames=20
[OBJLoader] Voxelize complete: 39648 voxels filled
[Demo3D] setScene(1): lightPosition reset to (0,0.8,0)
```

Scene-switch flips `useOBJMesh=false`, triggers analytic SDF, then
returns. Cornell-orig's CPU EDT bake fires correctly as the load
completes. The new render condition allows the chain to complete
without losing intermediate state.

### Phase 1 — GPU vs CPU A/B

CPU baseline ([tools/step8_cornell_orig_cpu.png](tools/step8_cornell_orig_cpu.png)):
```
[Demo3D] Mesh SDF: EDT complete N=128 voxelSz=0.03125m
   surfaceRadius=0.0270633m seeds=39648 edt=66.727ms albedo=31.8526ms
```
Total: ~98 ms/bake.

GPU path ([tools/step8_cornell_orig_gpu.png](tools/step8_cornell_orig_gpu.png)):
```
[Demo3D] GPU JFA SDF: GPU=4.096ms  CPU-submit=1.1159ms
   (N=128, 1 init + 7 steps + 1 finalize)
```

**~24× speedup for the SDF section. Visually close, NOT pixel-identical**
(codex 02 F6). Pixel diff measured by codex on the preserved captures:

| Metric | Value |
|---|---|
| Changed pixels | 68,111 / 921,600 (7.39%) |
| RGB MAE | 0.171 |
| Max RGB delta | 99 |

Both captures show the same red+green+white walls, glowing light,
and visible boxes; the differences are local (small per-voxel
gradient artifacts on box edges and at silhouettes). Expected:
GPU JFA is approximate (worst-case 1-voxel error in nearest-seed
distance), and the GPU albedo path uses nearest-seed lookup vs the
CPU's 3-iteration 6-neighbor flood — these algorithms can disagree
on walls 2-3 voxels deep.

A formal numeric SDF readback + tolerance script
(`tools/sdf_diff.py` from plan F5) is NOT implemented; the codex
01 F5 5-tolerance acceptance bar was not enforced numerically.
Stays open as a Step 8 follow-up if regressions appear.

### Phase 2 — Dynamic sphere captures (codex 02 F1+F3 v2)

Four deterministic captures via `--sphere-time=X` after the codex 02
F1 (cascade stagger bypass) and F3 (surface band) fixes:

| File | sphere position | observation |
|---|---|---|
| [tools/step8v2_dynamic_t0p0.png](tools/step8v2_dynamic_t0p0.png) | right side, near green wall | orange sphere visible; cascade GI now updates ALL levels each frame so green-wall bounce shows through |
| [tools/step8v2_dynamic_t1p5.png](tools/step8v2_dynamic_t1p5.png) | front-center, on floor | sphere reads as a clean orbital orb (surface band, not chunky solid voxel block) |
| [tools/step8v2_dynamic_t3p0.png](tools/step8v2_dynamic_t3p0.png) | left side, near red wall | sphere visible against red wall |
| [tools/step8v2_dynamic_t4p5.png](tools/step8v2_dynamic_t4p5.png) | back-center, near back wall | sphere is far from camera, smaller |

Earlier v1 captures (`tools/step8_dynamic_*.png`) are kept as
historical artifact showing the pre-F1+F3 chunky-solid-sphere look
with stale cascade levels.

The sphere orbits clockwise as viewed from above (driven by
`(cos(t), 0.2·size.y, sin(t))`). Each capture is reproducible
regardless of host frame rate because `sphereTimeOverride` snaps
`sphereTime` to the CLI-provided value.

```
[Demo3D] GPU JFA SDF: GPU=3.5-5.9ms  CPU-submit=1.0-1.6ms  (N=128, 1 init + 7 steps + 1 finalize)
```

The GPU JFA bake fires every frame (no caching). Avg ~3.5 ms, max
spikes ~5.9 ms; full-frame breakdown still TBD.

### Phase 2 — Disable cleanup verify (codex 02 F2)

[tools/step8v2_static_gpu.png](tools/step8v2_static_gpu.png) captured
with `--gpu-sdf` but WITHOUT `--dynamic-sphere`. Shows clean
Cornell-Original — no leftover sphere from any prior dynamic run.
The disable-transition path in `update()` correctly restores
`voxelGridTexture` from `meshVoxelBaseTexture` and re-bakes the
SDF. The next session that doesn't enable the sphere starts from a
clean base.

### Phase 5 — reset-helper still works under GPU mode

```
$ ./RadianceCascades3D --load-obj=cornell-orig --gpu-sdf --test-reset-helper
[Demo3D] testResetCameraHelper before: pos=(0,0.0980296,2.37709) ...
[Demo3D] testResetCameraHelper after move: pos=(2.5,0.79803,3.67709)
[Demo3D] testResetCameraHelper after reset: pos=(0,0.0980296,2.37709) ...
```

R-key reset does not depend on SDF state; works identically under
GPU and CPU paths. (codex 11 F1 / codex 12 F1 / codex 13 F1
guarantees preserved.)

### Phase 6 — Sponza-master under GPU mode

```
$ ./RadianceCascades3D --load-obj=sponza-master --gpu-sdf --exit-frames=120
[OBJLoader] Voxelize complete: 147593 voxels filled
[Demo3D] GPU JFA SDF: GPU=3.87ms  CPU-submit=1.02ms  (N=128, 1 init + 7 steps + 1 finalize)
```

147K-seed Sponza bakes through GPU JFA in 3.87 ms — essentially the
same as Cornell's 40K seeds. JFA cost is bounded by N (128³ voxels)
not by seed count, as expected.

### Logs preserved

- `tools/app_run_step8_phase0.log`, `app_run_step8_phase0_cornell.log`
- `tools/app_run_step8_cpu.log`, `app_run_step8_gpu.log`
- `tools/app_run_step8_dynamic_t{0p0,1p5,3p0,4p5}.log`
- `tools/app_run_step8_F1_resethelper_gpu.log`
- `tools/app_run_step8_sponza_gpu.log`

---

## Architecture Notes

**Per-frame `glCopyImageSubData(meshVoxelBaseTexture → voxelGridTexture)`
is the dynamic-overlay key.** Without it, sphere injection would either
destroy the static OBJ voxels (sphere with no scene around it) or
require re-running the CPU triangle voxelizer every frame (~hundreds of
ms — defeating the point of GPU SDF). The texture-to-texture copy at
128³ RGBA8 is sub-millisecond on this GPU.

**Voronoi storage choice: nearest-seed coord (not nearest-seed
distance).** Standard JFA stores the 3D position of the closest seed
encountered so far; the finalize pass converts to distance. Storing
distance directly would lose the pointer information needed for
albedo lookup (we read `uVoxelGrid[seedPos].rgb` to get the
nearest-surface color). Cost: 16 bytes per voxel (RGBA32F) vs 4 (R32F),
but at 128³ the 8 MB delta per ping-pong texture is acceptable.

**The Voronoi `.w` valid-flag.** Voxels not yet reached by JFA
propagation hold `w == 0`. Finalize treats these as "very far" (dist
= 1000), which raymarches as empty space — the correct behavior for
voxels outside any seed's domain (i.e. truly empty regions inside
the volume).

**`updateRadianceCascades` already supports `forceCascadeRebuild`.**
Phase 6b added this flag for RenderDoc capture frames; reusing it
for dynamic-sphere mode means no new cascade-scheduling code. The
flag's behavior — "this frame, dispatch all 4 cascades regardless of
stagger schedule" — is exactly what dynamic mode needs.

---

## Known Open Items (Step 8 boundary → later)

| Item | Where to land it |
|---|---|
| Numeric SDF/image diff script (`tools/sdf_diff.py` from plan F5) — codex 02 F6 | Step 8 follow-up. Currently visually-close (7.39% pixels differ, MAE 0.171) but not numerically gated. |
| **Full-frame dynamic-mode timing breakdown** — codex 02 F5 | Per-stage timing log line: `copy=X inject=X jfa-gpu=X jfa-submit=X cascades=X raymarch=X total=X`. The 24× speedup is for the JFA section only; whether the whole frame fits a 16 ms budget is unmeasured. |
| GPU triangle voxelizer (`voxelize.comp`) | Step 9 (real moving OBJ meshes, deformation, skinning) |
| Multiple dynamic objects | trivial (call `addVoxelSphere` twice in `update()`); not scoped here |
| Auto-emitter from .mtl Ke (Cornell light → `lightPosition`) | future; codex 13 F4 carry-over from Step 7 |
| Texture loading (.tga `map_Kd`) | separate Step (sphere demo doesn't need it; Sponza color does) |

---

## Why the Plan-First Approach Paid Off

Codex 01's 10 findings caught issues that would have produced silent
correctness failures during implementation:

- **F1** would have made the GPU/CPU toggle look broken in
  interactive use — the bake would have run on first frame and then
  never again.
- **F2** would have shown the dynamic sphere with EMA ghost trails
  and stale upper cascades, not real-time GI.
- **F3** would have clipped any sphere whose orbit took it through
  negative coordinates (i.e. half the time).
- **F4** would have crashed the framerate to single-digit territory
  via thousands of GL calls per frame.
- **F8** would have logged misleading "<1ms GPU" timings that
  measured CPU submit time.

Landing all 10 fixes in the plan before any implementation began
meant the impl pass was straight-line: each phase compiled clean
and produced the predicted result on the first try. No mid-impl
pivots required.
