# Critic Review 04 - load_path_step9_impl.md

Reviewed: 2026-05-09T17:43:50+08:00

Target: `doc/5/claude_plan/load_path_step9_impl.md`

## Verdict

Step 9 is a real implementation, not just a paper update. The parser rewrite, cache, GPU voxelizer, owner-index resolve pass, `gpuVoxelGridReady` SDF contract, CLI flags, and ImGui voxelizer reload path all exist in the current source. A Release CMake build also succeeds from `build/`.

The implementation note is still too broad in its verification and lifecycle claims. The most important source risk is that the GPU voxelizer failure path is not an all-or-nothing rollback even though the note says the previous scene is preserved. There is also an interactive state bug: after a GPU voxelize + GPU SDF scene, turning GPU SDF off can send CPU EDT into a mesh with no `meshVoxelData`, even though the cache already paid a readback on first load. The cache and timing claims are useful but should be framed as `loadOBJMesh()` timings, not full scene-ready timings.

## Evidence Checked

- `doc/5/claude_plan/load_path_step9_impl.md`.
- Current `src/obj_loader.h` parser, material logic, `buildTriangles()`, and CPU voxelizer.
- Current `src/demo3d.cpp` `loadOBJMesh()`, `voxelizeOBJ_GPU()`, ImGui toggles, cache handling, and `sdfGenerationPass()`.
- Current `src/demo3d.h` cache and toggle state.
- Current `src/main3d.cpp` `--gpu-voxelize` / `--cache-hit-test` handling.
- Current `res/shaders/voxelize.comp`.
- Preserved Step 9 logs and screenshots under `tools/`.
- Fresh build: `cmake --build . --config Release` from `build/` succeeded.

## What Looks Good

- The Step 9 plan-review F1 issue is fixed in source: `sdfGenerationPass()` now accepts `gpuVoxelGridReady` as an OBJ branch input, so GPU/GPU no longer falls through to analytic SDF by construction.
- The loader now exposes `buildTriangles()`, and the GPU voxelizer reuses the same material-color chain as CPU voxelization.
- The R32UI owner texture plus resolve pass is the right correction to the earlier RGBA8 atomic-alias idea.
- The GPU voxelizer uses `imageAtomicMin()` on triangle index, which is deterministic in the way the plan revision wanted.
- The ImGui GPU voxelizer toggle is better than the implementation note wording: the source maps the four stored OBJ keys back to real paths before calling `loadOBJMesh()` (`src/demo3d.cpp:4326-4338`), instead of literally passing the key string.
- The Sponza-master timings are plausible for the measured section: preserved logs show parse around 241-251 ms, first GPU load around 363-370 ms, and GPU cache hit around 4.9 ms.

## Findings

### 1. GPU voxelizer failure does not actually roll back to the prior scene

Severity: High

The implementation note says `voxelizeOBJ_GPU()` runs after commit and that if it fails, "we roll back the OBJ commit so the user sees the prior scene rather than an empty volume." The source does not restore the prior scene.

On a GPU-voxelize cache miss, `loadOBJMesh()` commits the new scene state before running `voxelizeOBJ_GPU()`:

- `meshVoxelData` is cleared for GPU voxelize at `src/demo3d.cpp:5251`.
- `useOBJMesh`, `currentOBJPath`, `currentObjBmin/currentObjBmax`, dirty flags, and `gpuVoxelGridReady=false` are assigned at `src/demo3d.cpp:5253-5263`.
- Only then does `voxelizeOBJ_GPU()` run at `src/demo3d.cpp:5280-5281`.

If it fails, the rollback only sets `useOBJMesh=false`, clears `currentOBJPath`, clears `gpuVoxelGridReady`, and returns false (`src/demo3d.cpp:5281-5288`). It does not restore the previous OBJ's CPU vector, GL textures, bounds, SDF readiness, analytic/raymarch mode, camera preset state, or previous `useOBJMesh` value. If the user was already viewing an OBJ, that previous scene is not preserved.

Suggested fix: stage GPU voxelization before committing externally visible scene state, or snapshot and restore every field and texture that the commit mutates. A cleaner approach is to GPU-voxelize into a temporary owner/grid texture, then copy into the live textures only after success.

### 2. Turning GPU SDF off after a GPU/GPU load can strand CPU EDT without input data

Severity: High

The command-line GPU voxelize + CPU EDT combo works because the load happens while `useGPUSDF=false`, so the GPU readback is copied into `meshVoxelData` at `src/demo3d.cpp:5313-5314`.

The interactive sequence is different:

1. Load an OBJ with `useGPUVoxelize=true` and `useGPUSDF=true`.
2. The GPU path clears `meshVoxelData` on commit (`src/demo3d.cpp:5251`).
3. Cache population reads back bytes, but keeps them only in `CachedMesh` when `useGPUSDF=true` (`src/demo3d.cpp:5304-5314`).
4. The GPU/GPU cache-hit path also clears `meshVoxelData` (`src/demo3d.cpp:5122-5124`).
5. If the user unchecks "GPU SDF", the ImGui handler only invalidates `meshSDFReady` and `cascadeReady` (`src/demo3d.cpp:4317-4320`), matching the inline setter behavior in `src/demo3d.h:768`.
6. The next `sdfGenerationPass()` chooses CPU EDT and immediately fails because `meshVoxelData` is empty (`src/demo3d.cpp:1914-1923`).

That means "all 4 voxelizer/SDF combos render correctly" is true for the CLI matrix, but not for this natural runtime transition.

Suggested fix: when GPU SDF is disabled while `useOBJMesh && useGPUVoxelize`, hydrate `meshVoxelData` from the current cache entry or reload the current OBJ through the existing cache. The simpler memory tradeoff is to keep the CPU mirror after the GPU readback even for GPU/GPU loads.

### 3. The cache key is not canonical despite the note and field name

Severity: Medium

The implementation note says the cache key is `(canonicalPath, voxelizerKind)`. The header field is also named `canonicalPath` (`src/demo3d.h:705-715`). The actual key is the caller-provided string:

```cpp
MeshCacheKey key{ filename, useGPUVoxelize ? 1 : 0 };
```

at both cache lookup and populate (`src/demo3d.cpp:5102`, `src/demo3d.cpp:5300`).

Because `loadOBJMesh()` searches multiple paths after that (`src/demo3d.cpp:5156-5174`), the same resolved file can occupy multiple cache entries depending on whether the caller used an alias, a relative path, or a different equivalent path. Current UI and CLI paths are mostly stable, so this is not breaking the preserved tests, but the documentation overstates the cache's source-awareness.

Suggested fix: key cache entries by `successfulPath` normalized to an absolute or weakly canonical path, plus voxelizer kind and voxel settings. If the intentionally supported contract is "exact input string", rename the field and update the implementation note.

### 4. Cache-hit timings are `loadOBJMesh()` timings, not scene-ready timings

Severity: Medium

The note repeatedly uses "scene-switch" language for the 4-5 ms cache hit. The logs show those numbers are only the `loadOBJMesh()` return path.

Examples:

- CPU Sponza cache hit is 3.9631 ms, but the next CPU EDT/albedo bake still logs 69.7953 ms + 26.831 ms (`tools/app_run_step9_phase2_cache_sponza.log:140-144`).
- GPU Sponza cache hit is 4.8842 ms, but the next GPU JFA still logs 3.7417 ms CPU-blocked query timing (`tools/app_run_step9_cache_hit_gpu.log:141-144`).

The note's table does label some rows as `loadOBJMesh wall`, but the headline says Sponza-master scene-switch dropped to 4-5 ms cached. That is stronger than the evidence unless first-correct-frame timing also excludes or includes SDF/cascade work explicitly.

Suggested fix: keep three separate numbers: `loadOBJMesh()` wall time, SDF bake wall/GPU time, and first correct rendered frame wall time. If a true scene-ready cache hit is the target, cache or reuse `sdfTexture`/`albedoTexture` too.

### 5. The CPU/CPU verification artifact is not clean

Severity: Medium

The note says all four Cornell-orig voxelizer/SDF combinations render correctly. The CPU/CPU log eventually succeeds, but it first records an actual SDF upload failure:

```text
[ERROR] generateMeshSDF: sdfTexture upload failed (GL 0x501)
[ERROR] sdfGenerationPass: mesh SDF bake failed (path=CPU)
```

at `tools/app_run_step9_cornell_orig_cpucpu.log:141-148`, followed by a later successful CPU EDT bake at line 168. That may be a stale GL error from earlier work or a transient upload-state issue, but either way it is not a clean verification log.

Suggested fix: find the source of the `GL_INVALID_VALUE`, or if it is intentionally a stale error, drain and report pre-existing GL errors before the CPU upload path the same way the GPU paths do. Then rerun the four-combo matrix and preserve clean logs.

### 6. `voxelizeOBJ_GPU()` does not validate required uniform locations

Severity: Low

`voxelizeOBJ_GPU()` looks up all required uniforms (`src/demo3d.cpp:1814-1819`) and immediately calls `glUniform*()` (`src/demo3d.cpp:1821-1828`) without checking for `-1`. `glGetError()` later will not catch a missing uniform location; `glUniform*(-1, ...)` is ignored by design.

This is less likely to break with the current shader, but it weakens the implementation note's failure-handling claim. A shader rename or optimized-away value could produce empty or incorrectly scaled voxel output while the function still returns true.

Suggested fix: validate every required uniform location once after lookup and return false with a targeted error if any are missing.

### 7. The `vn`/`vt` negative-index rationale is inaccurate

Severity: Low

The implementation note says skipping `vn`/`vt` value parsing is safe because only the count is needed for negative-index resolution. The source counts `vn` and `vt` lines (`src/obj_loader.h:107-152`), but `parseFaceLine()` explicitly ignores both counts (`src/obj_loader.h:512`). It also no longer stores parsed normals or texcoords, so its negative `t`/`n` resolution branches use empty vectors (`src/obj_loader.h:529-530`).

This is safe for the current voxelizer because neither CPU nor GPU path consumes normals or texcoords. The note should say that instead. If future work uses `t`/`n` indices, the parser will need either real counts in `parseFaceLine()` or actual stored arrays again.

## Verification Gaps To Add

- Add an automated runtime test for GPU/GPU load -> turn GPU SDF off -> CPU EDT bake succeeds without restarting or reloading manually.
- Inject or simulate a `voxelizeOBJ_GPU()` failure after a previous OBJ scene is active, then assert the previous scene state and textures are still intact.
- Add a cache alias test that loads the same OBJ through two equivalent paths and verifies whether one or two cache entries are expected.
- Report first-correct-frame time separately from `loadOBJMesh()` time for both CPU and GPU SDF paths.
- Rerun the four-combo Cornell matrix after fixing the CPU/CPU `GL 0x501` log.
