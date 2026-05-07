# Review: Sponza SDF Step 2 Implementation Note

Review timestamp: 2026-05-07T12:23:50+08:00

Target: `doc/4/claude_plan/sponza_sdf_step2_impl.md`

Verdict: mostly accepted as a CPU EDT bake smoke test, but not accepted as "end-to-end verified" or as a settled render-path implementation. The implementation does correct the main Step 2 plan problems: it bakes at `volumeResolution`, subtracts a half-voxel diagonal conservative radius, uploads albedo, returns `bool`, and records real Sponza/Cornell seed counts. The remaining issues are about lifecycle truth, verification scope, and a few overclaimed implementation details.

## What Matches Current Source

- `src/demo3d.cpp:53-98` defines file-scope `edt1d()` in an anonymous namespace.
- `src/demo3d.cpp:1313-1440` defines `Demo3D::generateMeshSDF()` with size validation, seed counting, three EDT sweeps, conservative radius subtraction, a limited albedo dilation pass, texture uploads, GL error checks, and timing logs.
- `src/demo3d.h:627-633` declares `meshVoxelData`, `meshSDFReady`, and `generateMeshSDF()`.
- `src/demo3d.cpp:4307-4314` adds the temporary `loadOBJMesh()` scaffold that copies voxel data, calls `generateMeshSDF()`, and sets `meshSDFReady` on success.
- `src/main3d.cpp:169-183` adds `--load-obj=...`; `src/main3d.cpp:239-242` adds the `--exit-frames` loop exit.
- `tools/app_run_step2_sponza.log` reports 145185 vertices, 262267 faces, 37757 filled voxels, and `edt=69.0749ms albedo=31.5748ms`.
- `tools/app_run_step2_cornell.log` reports 64 vertices, 32 faces, 40878 filled voxels, and `edt=70.2543ms albedo=32.9062ms`.

## Findings

### 1. High - `meshSDFReady` can become stale immediately after the smoke-test bake

Affected note lines:

- `sponza_sdf_step2_impl.md:17`
- `sponza_sdf_step2_impl.md:78`
- `sponza_sdf_step2_impl.md:107-110`
- `sponza_sdf_step2_impl.md:125`

The note says `meshSDFReady` means the current mesh SDF was successfully baked, and that Step 3 will move the scaffold into `sdfGenerationPass()`. In the current Step 2 source, the flag is set before `sceneDirty` causes the render loop to regenerate SDF:

- `src/demo3d.cpp:4311-4314` sets `meshVoxelData`, clears `meshSDFReady`, calls `generateMeshSDF()`, then sets `meshSDFReady = true`.
- `src/demo3d.cpp:4316-4317` sets `sceneDirty = true` and `useOBJMesh = true`.
- `src/demo3d.cpp:497-512` sees `sceneDirty`, calls `sdfGenerationPass()`, then marks its local `sdfReady = true`.
- `src/demo3d.cpp:1451-1491` still runs the analytic SDF compute path while `analyticSDFEnabled` is true.

The Step 2 run logs confirm the sequence: the mesh bake completes, then the next frame prints `Generating analytic SDF...` and `Analytic SDF generation complete.`

Impact: after the first frame, `meshSDFReady == true` can coexist with `sdfTexture/albedoTexture` containing the analytic scene, not the mesh bake. If Step 3 later adds an OBJ branch that trusts `meshSDFReady` to skip work, it can preserve the overwritten analytic texture and never restore the mesh field.

Recommended correction:

- For the temporary scaffold, do not set `meshSDFReady = true`, or reset it before setting `sceneDirty = true`.
- In Step 3, set `meshSDFReady = true` only from the mesh branch inside `sdfGenerationPass()` after `generateMeshSDF()` succeeds.
- Reset `meshSDFReady = false` on analytic `setScene()` and before any analytic SDF overwrite.
- Treat the current logs as "bake function smoke-tested", not "ready flag lifecycle validated".

### 2. High - Verification is scoped too broadly

Affected note lines:

- `sponza_sdf_step2_impl.md:5`
- `sponza_sdf_step2_impl.md:18-19`
- `sponza_sdf_step2_impl.md:84`
- `sponza_sdf_step2_impl.md:91-99`
- `sponza_sdf_step2_impl.md:111`

The logs support "the CPU EDT bake ran for both OBJ inputs and did not print a `generateMeshSDF()` upload error." They do not support "verified end-to-end."

Reasons:

- Both logs contain the pre-existing `res/shaders/sdf_3d.comp` compile failure: `[GL Error] Compute shader compilation failed` followed by `Failed to load shader: res/shaders/sdf_3d.comp`.
- Both logs show the analytic SDF pass overwriting the just-uploaded mesh SDF on the next frame.
- No rendered OBJ hit validation is possible yet because Step 3 wiring has not landed.
- The note cites "GL errors: none" in the table, but the run logs do contain a GL shader compilation error. The intended claim is narrower: no GL upload error was reported by `generateMeshSDF()`.

Recommended correction:

Replace the status with something like:

```text
Implemented and smoke-tested: `generateMeshSDF()` bakes and uploads for Sponza and Cornell OBJ without reporting mesh-upload errors. Render-path validation is pending Step 3; logs still include the pre-existing `sdf_3d.comp` compile failure and the expected analytic overwrite.
```

### 3. Medium - The implementation still performs per-line heap allocation

Affected note lines:

- `sponza_sdf_step2_impl.md:66`
- `sponza_sdf_step2_impl.md:108`

The note says the three sweeps use preallocated `rowBuf` with "no per-row heap allocation." `rowBuf` itself is preallocated, but every call to `edt1d()` allocates:

- `std::vector<int> v(n, 0)`
- `std::vector<float> z(n + 1, 0.f)`
- `std::vector<float> d(n)`

Those allocations are at `src/demo3d.cpp:65-67`. At `N=128`, `generateMeshSDF()` calls `edt1d()` `3 * N * N = 49152` times, so this path still performs roughly 147456 small vector allocations per bake. The measured 69-70 ms EDT time is therefore not surprising.

Recommended correction:

- Reword the note to "preallocated row value buffer, but `edt1d()` still allocates scratch vectors per line."
- If this becomes a real budget issue, pass scratch buffers into `edt1d()` or use flat indexed sweeps with reusable scratch storage. Merely replacing row extraction with flat indexing will not remove the `v/z/d` allocations unless the helper signature changes too.

### 4. Medium - The albedo dilation description is mathematically overclaimed

Affected note lines:

- `sponza_sdf_step2_impl.md:74`
- `sponza_sdf_step2_impl.md:109`
- `sponza_sdf_step2_impl.md:117`

Three iterations of 6-neighbor dilation reach Manhattan distance 3, not Chebyshev radius 3. Diagonal cells at Chebyshev distance 3 can be much farther than three 6-neighbor steps, so the note's radius wording is incorrect.

The implementation is also first-neighbor propagation, not nearest-color propagation. That is acceptable as an interim visibility scaffold, but it should not be described as enough for the surface band until a render or albedo debug pass proves it. The loop also skips boundary voxels (`1` through `N - 2`), which is fine for the current normalized Cornell/Sponza placement but should be documented as a limitation rather than implied full-volume coverage.

Recommended correction:

- Reword as "three iterations of 6-neighbor/L1 color dilation."
- Add a count of newly filled voxels or a near-band coverage metric if this remains the chosen interim path.
- Keep full nearest-seed color propagation as the real fix if visible black seams or color bleeding appear.

### 5. Medium - "No shader changes" is still a hypothesis, not a verified result

Affected note lines:

- `sponza_sdf_step2_impl.md:72`
- `sponza_sdf_step2_impl.md:116-118`

The conservative band is the right fix for the previous center-site EDT problem, but the current shader contract is stricter than the note's wording suggests:

- `res/shaders/raymarch.frag:430` hits only when `dist < EPSILON`, with `EPSILON = 1e-6`.
- `res/shaders/radiance_3d.comp:243` hits at `dist < 0.002`.
- `res/shaders/raymarch.frag:261-266` and `res/shaders/radiance_3d.comp:247-250` estimate normals from SDF finite differences.

The note says trilinear samples in the band read "approximately zero." That may be enough for the cascade threshold, but the final raymarch threshold is effectively exact zero. Also, the uploaded field is an unsigned, clamped zero-band UDF rather than a signed SDF, so normal quality at hit points still needs runtime validation.

Recommended correction:

- Keep "no shader changes" conditional until Step 3 shows mode 0 actually hits OBJ surfaces with `uUseAnalyticSDF == 0`.
- Add validation for normal debug mode and at least one GI/radiance mode, because a hittable field with broken normals still fails the renderer's lighting contract.
- If final raymarch misses while cascade rays hit, align the primary hit threshold with the grid-aware mesh UDF contract.

### 6. Low - CLI and line-number details need cleanup

Affected note lines:

- `sponza_sdf_step2_impl.md:16`
- `sponza_sdf_step2_impl.md:30-31`

`--load-obj=NAME` is documented as `cornell|sponza`, but `src/main3d.cpp:178-180` maps every non-`sponza` value to Cornell. That is acceptable for a private smoke-test hook, but misleading for a CLI flag.

The file-touch line for the frame-counter exit hook is also off: the hook is currently at `src/main3d.cpp:239-242`, not `218-222`.

Recommended correction:

- Reject unknown `--load-obj` names with a visible error.
- Update the cited line numbers if the implementation note is meant to remain a precise record.

## Recommended Fix Order

1. Change the Step 2 note's status to "bake smoke-tested" rather than "end-to-end verified."
2. Fix the `meshSDFReady` lifecycle before Step 3 trusts it.
3. Reword the heap-allocation and albedo-radius claims.
4. Add Step 3 validation for final ray hits, normal quality, albedo sampling, and cascade hits.
5. Clean up the smoke-test CLI and stale line references.

## Bottom Line

The code is a useful Step 2 checkpoint: the corrected EDT bake runs and produces plausible Sponza/Cornell counts. The implementation note is too optimistic about what that proves. The next revision should be explicit that the current work validates the bake function only, while render-path correctness, ready-state lifecycle, and shader-contract validation remain pending.
