# Codex Critic Summary

Review timestamp: 2026-05-07T15:16:34+08:00

Targets reviewed:

- `doc/4/claude_plan/sponza_sdf_impl_plan.md`
- `doc/4/claude_plan/sponza_sdf_step0_impl.md`
- `doc/4/claude_plan/sponza_sdf_step1_impl.md`
- `doc/4/claude_plan/sponza_sdf_step2_plan.md`
- `doc/4/claude_plan/sponza_sdf_step3_plan.md`
- `doc/4/claude_plan/sponza_sdf_step2_impl.md`
- `doc/4/claude_plan/sponza_sdf_step3_impl.md`

Output:

- `01_sponza_sdf_impl_plan_review.md` - current-codebase critique of the Sponza OBJ to SDF implementation plan.
- `02_sponza_sdf_step0_impl_review.md` - critique of the Step 0 UI entry-point implementation note and corresponding current source changes.
- `03_sponza_sdf_step1_impl_review.md` - critique of the Step 1 CPU voxelizer implementation note and current `obj_loader.h` changes.
- `04_sponza_sdf_step2_plan_review.md` - critique of the Step 2 CPU EDT mesh SDF plan.
- `05_sponza_sdf_step3_plan_review.md` - critique of the Step 3 render-pipeline wiring plan.
- `06_sponza_sdf_step2_impl_review.md` - critique of the Step 2 CPU EDT implementation note, current source changes, and preserved Sponza/Cornell smoke-test logs.
- `07_sponza_sdf_step3_impl_review.md` - critique of the Step 3 render-pipeline implementation note, current source changes, build output, and preserved Step 3 logs/screenshots.

Scope:

- The Claude plan was not edited.
- Claims were checked against `src/obj_loader.h`, `src/demo3d.cpp`, `src/demo3d.h`, `src/main3d.cpp`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/radiance_3d.comp`, and `res/shaders/raymarch.frag`.
- Step 2 implementation smoke-test logs were checked at `tools/app_run_step2_sponza.log` and `tools/app_run_step2_cornell.log`.
- Step 3 implementation logs/screenshots were checked at `tools/app_run_step3_*.log`, `tools/step3_sponza_mode0.png`, and `tools/step3_cornell_mode0.png`.
- Local Sponza OBJ facts were checked: `res/scene/sponza.obj` is present, has 145185 vertices and 262267 triangular faces, and is about 23.9 MB.

Verdict:

Do not implement the original plan set as written. The current Step 2/3 implementation is a useful checkpoint: it moved to a conservative EDT band, records real Sponza/Cornell counts, and now prevents the analytic SDF pass from overwriting successful OBJ bakes. It is still not full end-to-end Sponza verification because Sponza mode 0 remains dark, cascade-hit validation is not proven, and mesh-bake failure still does not propagate back to the render loop.

Top risks:

1. Calling `generateMeshSDF()` inside `loadOBJMesh()` will be overwritten or cleared by the next `sdfGenerationPass()`.
2. The proposed priority-queue "Euclidean BFS" is actually a 6-connected grid shortest path, so it produces Manhattan distances, not a safe Euclidean SDF for sphere tracing.
3. The voxelizer fix does not expand the triangle voxel bounding box by the distance threshold, so it can still miss thin floors/walls.
4. The plan assumes a 64^3 bake in several places, but the current runtime default is 128^3.
5. The plan only uploads `sdfTexture`; current shading also needs `albedoTexture`, while `voxelGridTexture` is not sampled by the final raymarch/radiance shaders.
6. Step 0 adds OBJ switching UI; the loader accumulation and analytic-scene reset issues are fixed in the current source, but the OBJ SDF lifecycle is still unwired.
7. Step 1's voxelizer fix is mostly aligned with the revised plan, and the current source includes the later file-open and degenerate guards; runtime voxel-count and timing validation still needs a cited run.
8. Step 2's EDT plan computes exact distance to occupied voxel centers, not exact distance to triangle surfaces; as written it can overestimate the real surface distance and is not sphere-trace safe.
9. Step 2/3 assume no shader changes, but current hit thresholds (`dist < EPSILON` in final raymarch, `dist < 0.002` in cascade bake) are too strict for a sparse voxel-center UDF unless Step 2 writes a conservative zero band.
10. Uploading only surface-voxel colors to `albedoTexture` leaves most samples black; current final and radiance shaders multiply by `uAlbedo`, so nearest-color propagation is needed for meaningful OBJ lighting.
11. Step 3's pipeline branch ignores `generateMeshSDF()` failure, does not reset temporal cascade history on scene changes, and can still be bypassed visually if `useAnalyticRaymarch` is enabled.
12. The Step 2 implementation currently sets `meshSDFReady = true` in `loadOBJMesh()` before the next frame's analytic `sdfGenerationPass()` overwrites the mesh SDF, so the flag can become stale immediately.
13. The Step 2 implementation note's verification language is too broad: the preserved logs prove the CPU bake ran, but they also contain the pre-existing `sdf_3d.comp` GL compile failure and the expected analytic overwrite.
14. The Step 3 implementation fixes the successful-load analytic overwrite, but mesh-bake failure still leaves the outer render loop marking `sdfReady = true` and can rebuild cascades from stale or partial textures.
15. The Step 3 implementation note overclaims visual/shader verification: Cornell mode 0 supports primary OBJ hits, but does not prove `radiance_3d.comp` cascade hits, and Sponza mode 0 is still dark.
16. Current Debug MSBuild succeeds with 0 errors but 36 warnings, so the note's "clean, zero new warnings" wording needs either a baseline citation or correction.
