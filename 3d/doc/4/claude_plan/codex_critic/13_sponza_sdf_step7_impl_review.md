# Critic Review 13 - sponza_sdf_step7_impl.md

Reviewed: 2026-05-08T17:44:12+08:00

Target: `doc/4/claude_plan/sponza_sdf_step7_impl.md`

## Verdict

Step 7 fixes the main Step 6 reset regression in the current happy path: `applyOBJViewPreset()` is now bounds-driven, `resetCameraToScenePreset()` no longer needs a four-way-to-two-way name mapping, and the preserved `--test-reset-helper` logs show Cornell-Original and Sponza-master returning to their computed presets. A fresh clean Release build also completed with 0 errors.

The implementation is still not a clean milestone. The most important source issue is that `currentObjBmin/currentObjBmax` are written before the staged OBJ load has committed, so a later voxelization failure can leave the old mesh active with new, failed-load bounds. The write-up also overstates the visual/runtime evidence: every Step 7 auto-fit camera is outside the SDF volume, the Sponza view changed substantially from Step 6 rather than just slightly widening, the reset-helper test does not actually press `R` or click the ImGui button, and the preserved logs still include the existing `sdf_3d.comp` shader compile failure.

## Evidence Checked

- Current source diff in `src/demo3d.cpp` and `src/demo3d.h`.
- CLI/reset wiring in `src/main3d.cpp`.
- Step 7 logs: `tools/app_run_step7_*.log`.
- Step 7 screenshots: `tools/step7_*.png`, compared against Step 6 screenshots.
- Fresh clean Release build from `build/`: `cmake --build . --config Release --clean-first`, completed with 0 errors.

## What Looks Correct

- `applyOBJViewPreset()` is now parameterless and uses `currentObjBmin/currentObjBmax`.
- `resetCameraToScenePreset()` now calls the parameterless preset for OBJ scenes, so the previous `cornell_orig` / `sponza_master` unknown-key reset bug is fixed on successful loads.
- `loadOBJMesh()` captures post-normalize bounds, stores a four-way `currentOBJPath` label, and applies the computed camera/light preset after committing the mesh.
- The four Step 7 OBJ logs show successful loads and SDF bakes:
  - Cornell old: 64 vertices, 32 faces, 40,878 seeds.
  - Cornell-Original: 8 materials, 64 vertices, 32 faces, 39,648 seeds.
  - Sponza old: 145,185 vertices, 262,267 faces, 147,593 seeds.
  - Sponza-master: 25 materials, 145,185 vertices, 262,267 faces, 147,593 seeds.
- The two `--test-reset-helper` logs demonstrate that the helper moves the camera and then restores the Step 7 computed preset for `cornell_orig` and `sponza_master`.
- Current material logging is better than the issue called out in Review 12: old Cornell now distinguishes legacy fallback colors from true default-gray misses.

## Findings

### 1. Active OBJ bounds are updated before the staged load commits

Severity: High

`loadOBJMesh()` was deliberately structured so a failed new OBJ load preserves the previous visible mesh state until the new voxel data is valid. Step 7 breaks that atomicity for camera reset state: it assigns `currentObjBmin/currentObjBmax` immediately after `normalize()`, before `objLoader.voxelize()` and before the `newVoxelData.empty()` failure check.

Evidence:

- `src/demo3d.cpp`: `currentObjBmin = nbmin; currentObjBmax = nbmax;` happens before voxelization.
- `src/demo3d.cpp`: if `newVoxelData.empty()`, the function returns `false` with the old `meshVoxelData`, `useOBJMesh`, and `currentOBJPath` still active.
- `resetCameraToScenePreset()` now uses `currentObjBmin/currentObjBmax` for the active OBJ preset.

Impact: if a new OBJ opens and normalizes but voxelizes to empty data, the previous mesh remains active, but pressing `R` or clicking Reset Camera will use the failed new mesh's bounds. That can put the camera/light in a mismatched state.

Suggested fix: keep `nbmin/nbmax` local until the same commit block that assigns `meshVoxelData`, `useOBJMesh`, and `currentOBJPath`, then assign `currentObjBmin/currentObjBmax` there.

### 2. The auto-fit cameras are all outside the SDF volume

Severity: Medium

The write-up says the new positions remain cleanly inside the ray-march-able region. The logs are more specific: all four tested camera presets are outside the SDF volume and skip the alpha collision check.

Evidence:

- Cornell old: `pos=(0,0.0981431,3.43526) OUTSIDE SDF volume`, `uvw.z=1.35881`.
- Cornell-Original: `pos=(0,0.0980296,3.43578) OUTSIDE SDF volume`, `uvw.z=1.35895`.
- Sponza old/master: `pos=(4.73561,0.0794485,0) OUTSIDE SDF volume`, `uvw.x=1.6839`.

This can still render because `raymarch.frag` performs a ray-box intersection, but it is not the same as being inside the validated SDF volume. The implementation note should state that these are outside-volume cameras relying on ray-box entry, just like the earlier hardcoded presets.

### 3. The Sponza visual comparison is overstated

Severity: Medium

Step 7 intentionally changes the Sponza camera from the Step 6 preset, but the note describes the result as only slightly wider and visually consistent. The actual delta is large.

Evidence:

- Step 6 old Sponza vs Step 7 old Sponza: 715,776 changed pixels out of 921,600, summed absolute RGB difference 141,995,311.
- Step 6 Sponza-master vs Step 7 Sponza-master: 618,685 changed pixels, summed absolute RGB difference 105,675,926.
- The camera moved from the Step 6 hardcoded Sponza position `(3.5, 0.5, 0)` to `(4.73561, 0.0794485, 0)`, changing both distance and height.
- The Step 7 Sponza screenshot is nonblank, but it is a much smaller exterior/frontal view with large empty/sky regions, not just a marginally wider version of the Step 6 view.

Impact: the screenshots are enough to prove a nonblank render, but they do not prove good framing or preservation of the Step 6 Sponza composition. The note should soften the framing claim or add an explicit visual acceptance screenshot with the intended view.

### 4. Bounds-derived light placement ignores emitter/material semantics

Severity: Medium

The new light formula is generic, but the implementation note frames it as naturally appropriate. For Cornell-Original, the asset now has material data including an emissive ceiling light, while the direct-light point is placed at `center.y + 0.3 * size.y`, around `y=0.588`, not near the ceiling light at the top of the box.

Impact: this can be acceptable as a camera convenience preset, but it should not be treated as asset-correct lighting. The next material/emission step should decide whether point-light placement should come from emissive material bounds, not just overall mesh height.

### 5. Reset-helper runtime verification is narrower than the note says

Severity: Low

The `--test-reset-helper` logs prove `resetCameraToScenePreset()` works for the two new variants. They do not literally verify a keyboard `R` press or an ImGui button click at runtime.

Source inspection does show both user-facing entry points call the helper, so the behavior is likely covered structurally. The implementation note should phrase this as "helper verified; key/button share that helper in source" rather than "R key and button verified runtime".

### 6. Runtime logs still contain the existing shader compile failure

Severity: Low

Every inspected Step 7 runtime log still contains the `res/shaders/sdf_3d.comp` compile error around `imageLoad(...)`, after which the app continues.

Impact: this appears to be the same pre-existing runtime issue from earlier steps, not a Step 7 regression. It should still be called out in verification notes so "implemented and verified" does not read as fully clean runtime logs.

### 7. Warning hygiene is still not demonstrated

Severity: Low

The clean Release build completed with 0 errors, but the build output still contains project warnings such as C4819, C4018, C4244, C4310, and C4267. The Step 7 note says no new warnings, but it does not provide a warning-count baseline or filtered project-source comparison.

This is not a blocker for Step 7, but the documentation should avoid "no new warnings" unless the warning baseline is captured in a reproducible way.

## Bottom Line

The core Step 7 direction is good: successful OBJ loads no longer need name-keyed camera presets, and the Step 6 reset bug is fixed on the normal path. Before treating this as a durable architecture cleanup, move bounds assignment into the mesh commit block and soften the verification claims around outside-volume cameras, Sponza framing, key/button runtime coverage, and the still-present shader runtime error.
