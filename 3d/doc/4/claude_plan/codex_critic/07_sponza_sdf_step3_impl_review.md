# Review: Sponza SDF Step 3 Implementation Note

Review timestamp: 2026-05-07T15:16:34+08:00

Target: `doc/4/claude_plan/sponza_sdf_step3_impl.md`

Verdict: mostly accepted for the main wiring fix, but the note overstates the verification level. The code now stages OBJ voxel data before committing, bakes mesh SDF from the `sdfGenerationPass()` mesh branch, prevents the analytic SDF dispatch from overwriting OBJ textures, forces grid raymarching for OBJ mode, and resets temporal state on scene changes. The remaining gaps are about failure semantics, shader-contract proof, Sponza visibility, and build/runtime-status wording.

## What Matches Current Source

- `src/demo3d.cpp:1460-1470` adds the OBJ branch at the top of `sdfGenerationPass()` and returns before the analytic compute path when `useOBJMesh && !meshVoxelData.empty()`.
- `src/demo3d.cpp:4338-4378` stages `newVoxelData`, commits `meshVoxelData`, sets `meshSDFReady = false`, sets `useAnalyticRaymarch = false`, reseeds temporal history, sets `sceneDirty = true`, and logs the OBJ commit.
- `src/demo3d.cpp:2369-2385` clears mesh state and reseeds temporal history in `setScene()`.
- `src/demo3d.cpp:2952-2963` disables the analytic raymarch checkbox in OBJ mode and adds OBJ-mode tooltip wording.
- `src/main3d.cpp:176-178` parses `--screenshot=...`; `src/main3d.cpp:251-256` writes a screenshot before exiting through `--exit-frames`.
- Step 3 run logs show the expected OBJ commit and mesh bake lines, and no post-load `Generating analytic SDF...` line for successful Sponza/Cornell loads.
- A local MSBuild Debug run on 2026-05-07 completed with 0 errors.

## Findings

### 1. High - Mesh bake failure still leaves the render loop in a misleading ready state

Affected note lines:

- `sponza_sdf_step3_impl.md:90-99`
- `sponza_sdf_step3_impl.md:276-289`

The implementation fixes the simple bug from the Step 3 plan review: `meshSDFReady` is no longer set true when `generateMeshSDF()` returns false. However, the outer render loop still does this unconditionally:

- `src/demo3d.cpp:508-513` calls `sdfGenerationPass()`, then sets local static `sdfReady = true` and `cascadeReady = false`.
- `src/demo3d.cpp:1461-1467` logs mesh bake failure and returns without making that failure visible to the caller.
- `src/demo3d.cpp:692-705` can then rebuild cascades from whatever `sdfTexture/albedoTexture` state remains.

The note calls this acceptable because the previous SDF stays on screen. That is not guaranteed. `generateMeshSDF()` can fail after partially changing GL state or after uploading `sdfTexture` but before `albedoTexture` succeeds. Even for earlier validation failures, the app has already committed `useOBJMesh = true` and `useAnalyticRaymarch = false`, so the final render may use stale analytic or previous-mesh textures while the UI says OBJ mode is active.

Recommended correction:

- Change `sdfGenerationPass()` to return `bool` and only set `sdfReady = true` after success, or explicitly roll back to analytic/previous state on mesh-bake failure.
- At minimum, set `sceneDirty = true` or keep a separate pending-retry state when the mesh branch fails.
- Add an injected failure test for `generateMeshSDF()` itself; the current renamed-file test only covers load failure before OBJ state is committed.

### 2. High - Sponza rendering is not verified yet

Affected note lines:

- `sponza_sdf_step3_impl.md:5-8`
- `sponza_sdf_step3_impl.md:171-179`
- `sponza_sdf_step3_impl.md:214-219`
- `sponza_sdf_step3_impl.md:247-265`

For Sponza, the logs prove that the OBJ loads, the mesh SDF bake runs, and the analytic SDF overwrite is gone. They do not prove that the Sponza scene renders correctly. The note's own screenshot result is "3D area dark."

That should not be framed as simply "not a Step 3 wiring failure" yet. It may be a camera issue, but it can also be a field/normal/albedo/ray-hit issue, a too-sparse voxelization issue, or a lighting issue. Because this is the Sponza SDF path, a dark Sponza mode-0 image is still an acceptance gap for the implementation record.

Recommended correction:

- Split the status: "Step 3 wiring verified for Sponza; Sponza visual rendering still unresolved."
- Add a headless camera-position override or a known-good Sponza camera before declaring visual verification.
- Capture at least mode 1, mode 4, mode 5/7, and mode 6 for Sponza to distinguish primary-hit, normal, direct-light, SDF-step, and GI failures.

### 3. Medium - The Cornell screenshot does not prove the cascade shader hit path

Affected note lines:

- `sponza_sdf_step3_impl.md:205-212`
- `sponza_sdf_step3_impl.md:230-238`

Visible Cornell walls in mode 0 are useful evidence that the final raymarch path can hit the conservative mesh UDF and sample non-black albedo. They do not by themselves prove that `radiance_3d.comp` is hitting OBJ surfaces with its `dist < 0.002` threshold.

Mode 0 can show surfaces through direct lighting alone:

- `res/shaders/raymarch.frag:430-468` handles primary hits and albedo sampling.
- `res/shaders/raymarch.frag:528-545` adds direct lighting and only then optional indirect GI.
- `res/shaders/radiance_3d.comp:243-263` is the separate cascade ray hit path that needs its own validation.

The note says Step 2's no-shader-changes hypothesis is confirmed for the Cornell OBJ case, including both `EPSILON = 1e-6` and `0.002`. The screenshot only supports the final-primary part of that claim.

Recommended correction:

- Add mode 3 or mode 6 screenshots with GI blur disabled, or use probe stats that show nonzero surface-hit and luminance data after the OBJ bake.
- Keep the claim as: "Cornell mode 0 confirms primary raymarch hits and visible albedo; cascade-hit verification remains pending."

### 4. Medium - Build and runtime status are worded too cleanly

Affected note lines:

- `sponza_sdf_step3_impl.md:22`
- `sponza_sdf_step3_impl.md:229`

A local Debug MSBuild run completed successfully, but it was not warning-free. It ended with 36 warnings and 0 errors, including C4819 encoding warnings, C4244 conversions, C4018 signed/unsigned mismatch in `obj_loader.h`, C4100 unused parameters, C4310 truncation, and C4702 unreachable-code warnings.

The Step 3 run logs also still contain the known runtime shader compile failure for `res/shaders/sdf_3d.comp`:

- `[GL Error] Compute shader compilation failed`
- `[ERROR] Failed to load shader: res/shaders/sdf_3d.comp`

That shader failure may be pre-existing and unrelated to the CPU EDT path, but the implementation note should not read like the headless runs are clean.

Recommended correction:

- Replace "Build clean, zero new warnings" with "Build succeeds; warning count unchanged/acceptable" only if a baseline warning count is cited.
- Record the current warning count or attach the build log.
- Explicitly mention that the headless runs still report the pre-existing `sdf_3d.comp` compile failure.

### 5. Medium - Interactive and temporal gates are not guaranteed by static inspection

Affected note lines:

- `sponza_sdf_step3_impl.md:234-243`

The static code direction is right: `historyNeedsSeed`, `renderFrameIndex`, and `temporalRebuildCount` are reset in both OBJ and analytic scene paths, and the analytic checkbox is gated by `BeginDisabled(useOBJMesh)`. But the note says these deferred gates are "guaranteed by static code review."

That is too strong for temporal behavior. The actual cascade update path also includes jitter, staggered updates, fused EMA, history handle swaps, and display reads from history textures:

- `src/demo3d.cpp:1535-1542` stagger-gates cascade updates and clears `historyNeedsSeed` after the rebuild pass.
- `src/demo3d.cpp:1602-1608` selects fused EMA and alpha.
- `src/demo3d.cpp:1681-1690` swaps atlas/grid history handles.
- `src/demo3d.cpp:1871-1917` reads temporal history for display and directional GI.

Those mechanics make the reset plausible, but not a proof of no ghost lighting. Similarly, the UI gate exists in code, but hover/tooltips and re-enable behavior still need one interactive pass.

Recommended correction:

- Downgrade the table entries to "static wiring present; runtime verification pending."
- Add one scripted or manual scene-switch test with temporal accumulation enabled and a screenshot sequence before treating ghosting as verified.

### 6. Low - Some implementation-note line references are stale

Affected note lines:

- `sponza_sdf_step3_impl.md:16-20`
- `sponza_sdf_step3_impl.md:28-32`

Several cited locations no longer match the current file positions:

- `loadOBJMesh()` stage-and-commit is around `src/demo3d.cpp:4338-4378`, not `4310-4343`.
- The UI gate is around `src/demo3d.cpp:2952-2963`, not `2926-2935`.
- `setScene()` invariants are around `src/demo3d.cpp:2369-2385`, not `2357-2372`.
- The screenshot save hook is around `src/main3d.cpp:251-256`, not `253-256` in the note's exact citation, and line 151 is still the `--load-obj` comment rather than a `--screenshot` comment.

Recommended correction:

- Refresh line references after the implementation settles, or cite function names rather than line numbers in the main implementation note.

## Recommended Fix Order

1. Make mesh-bake failure observable to `render()` or explicitly roll back committed OBJ state on failure.
2. Reframe Sponza as wiring-verified but visually unresolved.
3. Add cascade-specific validation for OBJ mode, not only final mode-0 primary visibility.
4. Correct build/runtime wording with the actual warning count and known shader compile error.
5. Downgrade deferred interactive gates from "guaranteed" to "static wiring present."
6. Refresh stale line references.

## Bottom Line

The important Step 3 lifecycle fix is real: successful OBJ loads no longer get overwritten by the analytic SDF pass. The note should be less conclusive about everything around that fix. It has verified the wiring path and Cornell primary visibility, not full Sponza rendering, cascade-hit correctness, warning-free builds, or temporal/UI behavior.
