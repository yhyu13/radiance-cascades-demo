# Codex Critic Summary

Review timestamp: 2026-05-07T19:19:02+08:00

Targets reviewed:

- `doc/4/claude_plan/sponza_sdf_impl_plan.md`
- `doc/4/claude_plan/sponza_sdf_step0_impl.md`
- `doc/4/claude_plan/sponza_sdf_step1_impl.md`
- `doc/4/claude_plan/sponza_sdf_step2_plan.md`
- `doc/4/claude_plan/sponza_sdf_step3_plan.md`
- `doc/4/claude_plan/sponza_sdf_step2_impl.md`
- `doc/4/claude_plan/sponza_sdf_step3_impl.md`
- `doc/4/claude_plan/sponza_sdf_step4_plan.md`
- `doc/4/claude_plan/sponza_sdf_step4_impl.md`

Output:

- `01_sponza_sdf_impl_plan_review.md` - current-codebase critique of the Sponza OBJ to SDF implementation plan.
- `02_sponza_sdf_step0_impl_review.md` - critique of the Step 0 UI entry-point implementation note and corresponding current source changes.
- `03_sponza_sdf_step1_impl_review.md` - critique of the Step 1 CPU voxelizer implementation note and current `obj_loader.h` changes.
- `04_sponza_sdf_step2_plan_review.md` - critique of the Step 2 CPU EDT mesh SDF plan.
- `05_sponza_sdf_step3_plan_review.md` - critique of the Step 3 render-pipeline wiring plan.
- `06_sponza_sdf_step2_impl_review.md` - critique of the Step 2 CPU EDT implementation note, current source changes, and preserved Sponza/Cornell smoke-test logs.
- `07_sponza_sdf_step3_impl_review.md` - critique of the Step 3 render-pipeline implementation note, current source changes, build output, and preserved Step 3 logs/screenshots.
- `08_sponza_sdf_step4_plan_review.md` - critique of the Step 4 OBJ visibility plan, including volume-utilization math, per-OBJ scale risk, camera-preset risk, and verification gaps.
- `09_sponza_sdf_step4_impl_review.md` - critique of the Step 4 implementation note, current source changes, Step 4 logs/screenshots, and a fresh local MSBuild run.

Scope:

- The Claude plan was not edited.
- Claims were checked against `src/obj_loader.h`, `src/demo3d.cpp`, `src/demo3d.h`, `src/main3d.cpp`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/radiance_3d.comp`, and `res/shaders/raymarch.frag`.
- Step 2 implementation smoke-test logs were checked at `tools/app_run_step2_sponza.log` and `tools/app_run_step2_cornell.log`.
- Step 3 implementation logs/screenshots were checked at `tools/app_run_step3_*.log`, `tools/step3_sponza_mode0.png`, and `tools/step3_cornell_mode0.png`.
- Local Sponza OBJ facts were checked: `res/scene/sponza.obj` is present, has 145185 vertices and 262267 triangular faces, and is about 23.9 MB.
- Step 4 plan claims were checked against current `OBJLoader::normalize()`, `Demo3D::loadOBJMesh()`, `resetCamera()`, `--render-mode` handling, the current post-07 mesh-bake failure path, Step 3 logs/screenshots, and actual Sponza/Cornell OBJ bounds.
- Step 4 implementation claims were checked against the current source diff, `tools/app_run_step4*.log`, `tools/step4*.png`, pixel comparisons, and a fresh Debug MSBuild of `build/RadianceCascades3D.vcxproj`.

Verdict:

Do not implement the original plan set as written. The current Step 2/3/4 work is a useful checkpoint: it moved to a conservative EDT band, records real Sponza/Cornell counts, prevents the analytic SDF pass from overwriting successful OBJ bakes, propagates mesh-bake failure back to the render loop, and Step 4's per-OBJ Sponza scale now produces a much denser field. Step 4 is still not fully verified as written: the new `lightPosition` member leaks from OBJ loads into later analytic scenes, outside-volume camera validation is misleading, current Debug MSBuild reports 39 warnings, final Sponza evidence lacks preserved logs/clean screenshots, and the note overstates Cornell byte identity and mode-5 hit coverage.

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
14. The Step 3 implementation review's mesh-bake failure finding has since been fixed in current code by making `sdfGenerationPass()` return `bool` and only marking `sdfReady = true` on success; docs that still describe the old behavior need updating.
15. The Step 3 implementation note overclaims visual/shader verification: Cornell mode 0 supports primary OBJ hits, but does not prove `radiance_3d.comp` cascade hits, and Sponza mode 0 is still dark.
16. The Step 3 Debug MSBuild checkpoint succeeded with 0 errors but 36 warnings, so Step 3's "clean, zero new warnings" wording needed either a baseline citation or correction.
17. Step 4's volume-utilization plan overstates Sponza seed-count gain: the current voxelizer fills a surface band, so scaling by 1.9 should be expected to behave closer to area growth than 8x volume growth.
18. Step 4's proposed `normalize(1.9)` call is global inside `loadOBJMesh()` and would rescale Cornell too, making the Cornell regression check ambiguous.
19. Step 4's Sponza camera preset is plausible because local bounds confirm X-long/Y-up, but the exact `(1.6,0.1,0)` camera point is not proven to be in free atrium space.
20. Step 4 should treat camera/scale as leading hypotheses for dark Sponza mode 0, not as isolated proof; direct-only, normals, depth, step-count, and ray-distance captures are still needed.
21. Step 4 implementation fixed the per-OBJ scale issue, but its new `lightPosition` member is not reset by `setScene()`, so Sponza OBJ lighting can leak into later analytic scenes.
22. Step 4's camera alpha validation clamps outside-volume cameras to boundary voxels, so `alpha=0` does not prove the final outside Sponza/Cornell cameras are in free space.
23. Current Debug MSBuild succeeds with 0 errors but 39 warnings, not the Step 4 note's claimed unchanged 37-warning baseline.
24. Final Sponza verification is weakly preserved: final Sponza logs are missing, CLI screenshots include ImGui overlays, and no final mode-3 capture backs the partial-GI claim.
25. Step 4's Cornell regression is excellent but not byte-identical, and mode 5 is still a step-count heatmap rather than a hit-coverage proof.
