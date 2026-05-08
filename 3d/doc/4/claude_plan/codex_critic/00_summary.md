# Codex Critic Summary

Review timestamp: 2026-05-08T17:44:12+08:00

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
- `doc/4/claude_plan/sponza_sdf_step5_plan.md`
- `doc/4/claude_plan/sponza_sdf_step5_impl.md`
- `doc/4/claude_plan/sponza_sdf_step6_impl.md`
- `doc/4/claude_plan/sponza_sdf_step7_impl.md`

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
- `10_sponza_sdf_step5_plan_review.md` - critique of the Step 5 interactive camera plan, including temporal invalidation, OBJ reset behavior, input capture, key binding, and inside-Sponza verification risks.
- `11_sponza_sdf_step5_impl_review.md` - critique of the Step 5 camera-control implementation note, current source changes, Step 5 logs/screenshots, screenshot comparisons, and a local clean Release build attempt.
- `12_sponza_sdf_step6_impl_review.md` - critique of the Step 6 OBJ material/parser implementation note, current source changes, Step 6 logs/screenshots, asset comparisons, screenshot diffs, and a fresh clean Release build.
- `13_sponza_sdf_step7_impl_review.md` - critique of the Step 7 bounds-driven camera/light preset implementation note, current source changes, Step 7 logs/screenshots, screenshot diffs, and a fresh clean Release build.

Scope:

- The Claude plan was not edited.
- Claims were checked against `src/obj_loader.h`, `src/demo3d.cpp`, `src/demo3d.h`, `src/main3d.cpp`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/radiance_3d.comp`, and `res/shaders/raymarch.frag`.
- Step 2 implementation smoke-test logs were checked at `tools/app_run_step2_sponza.log` and `tools/app_run_step2_cornell.log`.
- Step 3 implementation logs/screenshots were checked at `tools/app_run_step3_*.log`, `tools/step3_sponza_mode0.png`, and `tools/step3_cornell_mode0.png`.
- Local Sponza OBJ facts were checked: `res/scene/sponza.obj` is present, has 145185 vertices and 262267 triangular faces, and is about 23.9 MB.
- Step 4 plan claims were checked against current `OBJLoader::normalize()`, `Demo3D::loadOBJMesh()`, `resetCamera()`, `--render-mode` handling, the current post-07 mesh-bake failure path, Step 3 logs/screenshots, and actual Sponza/Cornell OBJ bounds.
- Step 4 implementation claims were checked against the current source diff, `tools/app_run_step4*.log`, `tools/step4*.png`, pixel comparisons, and a fresh Debug MSBuild of `build/RadianceCascades3D.vcxproj`.
- Step 5 plan claims were checked against current `processInput()`, `main3d.cpp` loop ordering, Step 4 post-review fixes, temporal cascade update ownership, visible UI shortcut labels, and the Step 4 reply that records inside-atrium camera as still open.
- Step 5 implementation claims were checked against the current `src/demo3d.cpp`/`src/demo3d.h` diff, `tools/app_run_step5*.log`, Step 4 v2 versus Step 5 screenshot hashes/pixel diffs, current process state, and an attempted clean Release build.
- Step 6 implementation claims were checked against the current `src/obj_loader.h`, `src/demo3d.cpp`, and `src/main3d.cpp` diff, `tools/app_run_step6*.log`, `tools/step6*.png`, Cornell-Original and Sponza-master asset structure, screenshot pixel diffs, and a successful clean Release build.
- Step 7 implementation claims were checked against the current `src/demo3d.cpp`/`src/demo3d.h` diff, `src/main3d.cpp` reset-test wiring, `tools/app_run_step7*.log`, `tools/step7*.png`, Step 6 versus Step 7 screenshot pixel diffs, and a successful clean Release build.

Verdict:

Do not implement the original plan set as written. The current Step 2/3/4 work is a useful checkpoint: it moved to a conservative EDT band, records real Sponza/Cornell counts, prevents the analytic SDF pass from overwriting successful OBJ bakes, propagates mesh-bake failure back to the render loop, and Step 4's per-OBJ Sponza scale now produces a much denser field. Current source has also accepted and fixed the main Step 4 implementation-review findings around light reset, outside-camera validation, and clean CLI screenshots. Step 5's revised camera controls are largely in place, including the scene-aware reset button fix. Step 6 adds useful OBJ ingestion support for MTL `Kd`/`Ke`, relative `mtllib`, negative vertex indices, fan-triangulated n-gons, and four load targets, and Step 7 fixes the Step 6 four-way reset bug on successful loads by making the OBJ preset bounds-driven. The clean Release build now succeeds. The remaining concerns are mostly correctness/evidence hygiene: Step 7 stores bounds before mesh-load commit, the Sponza-master density comparison remains false in the Step 6 record, old-Sponza screenshot parity is overstated, the new Step 7 Sponza view changed substantially rather than slightly, and the runtime logs still include the existing `sdf_3d.comp` shader compile error.

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
21. The Step 4 implementation review found `lightPosition` leakage into analytic scenes; current source fixes it by resetting `lightPosition` in `setScene()`.
22. The Step 4 implementation review found misleading outside-camera alpha validation; current source fixes it by skipping alpha checks for outside-volume camera presets.
23. Step 4 warning counts are configuration-sensitive: Release/project-source counts were documented as 37, a full Debug compile previously showed 39, and an incremental up-to-date Debug build can report 0.
24. The Step 4 implementation review found UI-obscured final captures; current source fixes CLI screenshot timing and preserves Step 4 v2 clean captures/logs.
25. Step 4's Cornell regression is excellent but not byte-identical, and mode 5 is still a step-count heatmap rather than a hit-coverage proof.
26. Step 5's camera controls will not automatically solve the inside-atrium Sponza view; Step 4 still records that inside-camera raymarching can go black.
27. Step 5 should not set `historyNeedsSeed` or rebuild cascades for camera-only motion because cascades are scene-space and the radiance bake has no camera dependency.
28. Step 5's proposed `R` reset should not call `loadOBJMesh()`; it should apply existing camera/light presets without re-parsing, re-voxelizing, and re-baking the mesh.
29. Step 5 needs split keyboard/mouse ImGui capture handling and robust cursor cleanup, otherwise large UI panels and RMB release can make navigation brittle.
30. Step 5's key rebinding must update visible UI labels; `D`, `F1`, and `R` currently have stale or conflicting user-facing meanings.
31. The Step 5 review's ImGui `Reset Camera` bypass was fixed in current source, but Step 6 reintroduced a related reset regression for the new four-way OBJ variant keys.
32. Step 5's preserved interactive log does not prove F1 toggling, R reset, movement, wheel/FOV, ImGui-hover behavior, or cursor cleanup despite the implementation note claiming those user-driven checks.
33. Step 5's Cornell headless capture is byte-identical to Step 4 v2, but the Sponza capture differs in hash and in 465,168 pixels, so the no-input/no-behavior-change claim is overstated.
34. Step 5's clean Release build claim was not reproducible during that review because `RadianceCascades3D.exe` was locked; a later Step 6 clean Release build succeeded with 0 errors.
35. `tools/app_run_step5_helper.log` predates the final F1 label patch and still prints `Press 'D'`, so it should not be used as final label-patch evidence.
36. Step 7 fixes Step 6's four-way OBJ reset bug on successful loads by making `applyOBJViewPreset()` bounds-driven and parameterless.
37. Step 6's Sponza-master density story is wrong for this workspace: the old root `sponza.obj` and `Sponza-master/sponza.obj` logs both show 145,185 vertices and 262,267 faces.
38. Step 6's old-Sponza regression claim is too strong: `step6_sponza_old_mode0.png` differs from the Step 4 v2 Sponza capture in 450,754 pixels, while the old Cornell capture is pixel-identical to Step 5.
39. Step 6 runtime logs still contain the existing `res/shaders/sdf_3d.comp` compile failure, so the implementation note should call that out rather than imply clean runtime logs.
40. Step 6 broadens OBJ face parsing but still lacks resolved-index bounds validation before voxelization dereferences face vertices.
41. Step 6's material-miss logging issue is fixed in current source: old Cornell logs now distinguish legacy fallback colors from true default-gray misses.
42. Step 6's stale CLI comment is fixed in current source: `main3d.cpp` now documents `cornell|cornell-orig|sponza|sponza-master`.
43. Step 7 writes `currentObjBmin/currentObjBmax` before `loadOBJMesh()` commits the staged voxel data, so a failed new voxelization can leave the old mesh active with failed-load reset bounds.
44. Step 7's auto-fit cameras are all outside the SDF volume in preserved logs, so alpha collision validation is skipped and the note should describe them as ray-box-entry cameras rather than inside-volume validated cameras.
45. Step 7's Sponza visual claim is too soft: Step 6 versus Step 7 Sponza captures differ in 618,685 to 715,776 pixels, and the camera moved from `(3.5,0.5,0)` to `(4.73561,0.0794485,0)`.
46. Step 7's generic light placement ignores emissive/material geometry; for Cornell-Original it places the point light near `y=0.588` rather than near the ceiling light emitter.
47. Step 7's `--test-reset-helper` logs verify the shared reset helper, not literal keyboard `R` or ImGui button input; source inspection shows those entry points share the helper, but runtime wording should be narrower.
48. Step 7 runtime logs still contain the existing `res/shaders/sdf_3d.comp` compile failure.
49. Step 7's "no new warnings" claim is not demonstrated by the preserved evidence; the fresh Release build succeeds with 0 errors but still emits project warnings.
