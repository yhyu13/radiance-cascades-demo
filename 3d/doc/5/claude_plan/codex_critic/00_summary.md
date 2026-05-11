# Codex Critic Summary - doc/5 GPU SDF

Review timestamp: 2026-05-09T17:43:50+08:00

Targets reviewed:

- `doc/5/claude_plan/gpu_sdf_step8_plan.md`
- `doc/5/claude_plan/gpu_sdf_step8_impl.md`
- `doc/5/claude_plan/load_path_step9_plan.md`
- `doc/5/claude_plan/load_path_step9_impl.md`

Output:

- `01_gpu_sdf_step8_plan_review.md` - critique of the Step 8 GPU JFA SDF + dynamic sphere overlay plan against the then-current Step 7/cleanup codebase.
- `02_gpu_sdf_step8_impl_review.md` - critique of the Step 8 implementation note against the actual Step 8 C++/shader changes, logs, screenshots, and a reproduced Release CMake build.
- `03_load_path_step9_plan_review.md` - critique of the Step 9 OBJ load-path acceleration plan against the current Step 8/v2 loader, cache, shader, and SDF ownership contracts.
- `04_load_path_step9_impl_review.md` - critique of the Step 9 implementation note against the current parser/cache/GPU-voxelizer source, preserved Step 9 logs/screenshots, and a reproduced Release CMake build.

Scope:

- The Claude plan/implementation notes were not edited.
- Plan-review claims were checked against `src/demo3d.cpp`, `src/demo3d.h`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/raymarch.frag`, `res/shaders/radiance_3d.comp`, and `res/shaders/sdf_analytic.comp`.
- Implementation-review claims were checked against the current Step 8 source diffs, `tools/app_run_step8*.log`, `tools/step8*.png`, and `cmake --build . --config Release` from `build/`.
- Step 9 plan claims were checked against `Demo3D::loadOBJMesh()`, `Demo3D::sdfGenerationPass()`, `OBJLoader`, `gl::createTexture3D()`, and the dormant `voxelize.comp`.
- Step 9 implementation claims were checked against `OBJLoader::load()` / `buildTriangles()`, `Demo3D::loadOBJMesh()`, `Demo3D::voxelizeOBJ_GPU()`, `Demo3D::sdfGenerationPass()`, `res/shaders/voxelize.comp`, `src/main3d.cpp`, `tools/app_run_step9*.log`, `tools/step9*.png`, and `cmake --build . --config Release` from `build/`.

Current verdict:

Step 8 materially landed the static GPU JFA SDF path, and Step 9 materially lands the OBJ load-path acceleration architecture: fast face-aware parsing, per-voxelizer mesh cache, a real GPU triangle voxelizer with an R32UI owner texture, and the `gpuVoxelGridReady` SDF branch contract. The current Release CMake build succeeds. The implementation note is still too optimistic on lifecycle and verification: GPU voxelizer failure is not a true rollback, toggling GPU SDF off after a GPU/GPU load can leave CPU EDT without `meshVoxelData`, the cache key is still the raw input string rather than a canonical path, cached "scene-switch" timings omit the following SDF/cascade work, and one CPU/CPU verification log contains a transient `GL 0x501` bake failure before retrying successfully.

Top remaining risks:

1. `loadOBJMesh()` commits GPU-voxelized scene state before `voxelizeOBJ_GPU()` succeeds, and the failure path does not restore the previous scene/textures.
2. A GPU voxelize + GPU SDF scene can fail after the user disables GPU SDF because CPU EDT requires `meshVoxelData`, which the GPU/GPU path clears.
3. Step 9 cache keys are raw caller strings despite the `canonicalPath` name and implementation-note wording.
4. Cached 4-5 ms timings are `loadOBJMesh()` wall times; CPU/GPU SDF bake and cascade readiness still need separate first-correct-frame timing.
5. The CPU/CPU Step 9 verification log is not clean because it records a `GL 0x501` SDF upload failure before a later retry succeeds.
6. `voxelizeOBJ_GPU()` should validate required uniform locations, not only GL errors after dispatch.
7. The parser's `vn`/`vt` negative-index explanation is inaccurate; current safety comes from not consuming normals/texcoords, not from using their counts.
8. The Step 9 follow-up document references "codex 03" for Step 8 fixes (should be codex 01 or 02), has an off-by-4 line number anchor, omits the `has("dif")` curtain pattern branch, misrepresents single-character substring matches as "variant" suffixes, and uses `[0.3, 0.7]` notation where the implementation has strict `<` on the upper bound. The two high-severity codex 04 findings (GPU voxelizer rollback, GPU SDF toggle state bug) remain unfixed and unacknowledged in the follow-up doc.
9. The Step 10 plan has a significant line-reference error (alpha-validation pointed at RenderDoc init code at line 4604 instead of the actual validation at lines 5026-5050). Proposed GI diagnostic modes 9 and 10 duplicate existing modes 4 and 6 with negligible alpha-compositing differences. The `uSeparateGI` early return bypasses the proposed insertion point, so modes 9/10/11 would not work when GI blur is active. The plan also lacks discussion of FOVY reset interaction, pitch clamping in `rebuildCameraTargetFromYawPitch()`, and CLI flag ordering for analytic scenes.
10. The Step 11 plan correctly identifies that the `vec3(0.05)` ambient floor baked into cascade probes at `radiance_3d.comp:262` makes mode 6's "GI only" output contain ambient-floor energy from source surfaces. The `uStripAmbientFloor` toggle + rebake approach is architecturally sound. However: the plan describes heatmap modes as "early-returns matching mode 4/6/7 pattern" but they actually consume the main-path `directColor`/`indirectColor` (structurally different from modes 4/6 which compute their own terms), doesn't discuss what mode 6 or mode 0 look like with the strip toggle on (nearly black vs. colored bounce), has no `uUseCascade` dependency guard (toggle meaningless when cascades disabled), and doesn't justify the `/0.5` heatmap normalization divisor against typical Sponza indirect magnitudes. Line references for heatmap palette code and `forceCascadeRebuild` pattern are shifted/wrong.
11. The Step 11 implementation is a genuine landing with all 10 screenshots/logs present. The diagnostic outcome (mixed A/B: right wall/floor nearly black with strip, left pillars retain dim structure) is well-argued. However, `setStripAmbientFloorBake()` includes `meshSDFReady = false` which forces an unnecessary SDF rebake (~3-7 ms wasted) on every toggle — the strip is a lighting-only change that doesn't affect geometry. The doc conflates this with the Step 8 dynamic-sphere 5-line "canonical" pattern (which legitimately needs `meshSDFReady = false` for geometry changes). Several line references are stale (pre-Step-11). Mode 12 divisor `/0.05` is correctly identified as saturated but left in source.
12. **Runtime verification at cam.md viewpoint reveals NaN/Inf contamination in probe atlas (P0 blocker).** C0 `meanLum=NaN`, `maxLum=inf`; C2/C3 negative mean luminance (-376, -320). Cascades are severely under-occupied (C0 `anyPct ≈ 3.5%`, C1 `≈ 0.02%`). The heatmap spatial patterns DO match geometric intuition (warm ceiling bounce, brick wall bleed), but the GI magnitude is too low (~2% of surface brightness vs. expected ~10-30%). The 0.05 ambient floor amplifies through bounce to make GI appear stronger than it really is. Fixes needed: (P0) NaN/Inf clamping in radiance_3d.comp output + history seeding, (P1) increase probeJitterScale from 0.06→0.25 and temporalAlpha from 0.05→0.12, (P2) reduce c0MinRange from 1→0.
13. The Step 11 follow-up zero-init plan correctly identifies the root cause: `gl::createTexture3D` passes `data=nullptr` to `glTexImage3D`, leaving GPU memory uninitialized. The misleading "zero-initialized" comment at `demo3d.cpp:2842` is confirmed. The single touchpoint fix in `createTexture3D` is architecturally sound. However: `glewIsSupported`/`glewGetProcAddress` are referenced as available in `gl_helpers.h` but aren't actually there (GLEW provides them globally after `glewInit`), `glClearTexImage` is already used at `demo3d.cpp:2009` without any extension check (codebase assumes GL 4.4+), the fallback 64 MB allocation claim is overstated (actual largest atlas is ~16 MB), and the plan doesn't specify whether `bytesPerPixel` is a public or static helper or where the `GLEW_ARB_clear_texture` runtime check should happen.
14. The GI pass 1080p perf analysis plan is a well-structured measurement-only step. The `--window-size=WxH` CLI flag is minimal and correct. However: the line reference for `rdocForceRebuildCount` is wrong (`:2721-area` vs actual `:4721`), `SetWindowSize()` is not used in the codebase — the plan should set dimensions at `InitWindow` time rather than resizing afterward (Demo3D construction reads screen dimensions for viewport/camera/blur setup), the 1080p raymarch scaling estimate `~2.25×` may be non-linear due to variable march step counts, the plan says "6 dispatch sites" but there are 10 `glPushDebugGroup` labels, and Config C can't run until the `--window-size` flag is implemented.
15. The GI pass scaling experiment plan is well-designed (4 experiments varying window, probe-res, step count, and blur radius independently). However: `volumeResolution` is NOT compile-time (it's a runtime member initialized from a constexpr default — the plan's rationale for deferring volume-res scaling is wrong), no public setters exist for the 3 proposed CLI knobs (`setCascadeC0Res`, `setRaymarchSteps`, `setGIBlurRadius` are all absent), changing `cascadeC0Res` triggers full `destroyCascades + initCascades` reallocation (~1-2 s per data point), `giBlurRadius` default is 8 in source but the flag table says 1, and the `setCascadeC0Res` setter MUST replicate the full geometry-invalidation chain (destroyCascades + initCascades + 5-line pattern), not just `cascadeReady = false`.

Targets reviewed (updated):

- `doc/5/claude_plan/step9_followups_impl.md`
- `doc/5/claude_plan/camera_gi_diagnostic_step10_plan.md`
- `doc/5/claude_plan/gi_bake_strip_heatmap_step11_plan.md`
- `doc/5/claude_plan/gi_bake_strip_heatmap_step11_impl.md`
- Runtime heatmap verification at `cam.md` viewpoint

Output (added):

- `05_step9_followups_impl_review.md` - critique of the Step 9 follow-up implementation note (addVoxelBox rewrite + Sponza name-based color hints) against current `src/demo3d.cpp`, `src/obj_loader.h`, and verification screenshots.
- `06_camera_gi_diagnostic_step10_plan_review.md` - critique of the Step 10 camera state UI/CLI + GI diagnostic modes plan against current `src/demo3d.cpp`, `src/demo3d.h`, `src/main3d.cpp`, `res/shaders/raymarch.frag`, and `res/shaders/radiance_3d.comp`.
- `07_gi_bake_strip_heatmap_step11_plan_review.md` - critique of the Step 11 GI bake-strip toggle + heatmap diagnostic modes plan against current `res/shaders/radiance_3d.comp`, `res/shaders/raymarch.frag`, `src/demo3d.cpp`, `src/demo3d.h`, and `src/main3d.cpp`.
- `08_gi_bake_strip_heatmap_step11_impl_review.md` - critique of the Step 11 implementation note against current `res/shaders/radiance_3d.comp`, `res/shaders/raymarch.frag`, `src/demo3d.h`, `src/demo3d.cpp`, `src/main3d.cpp`, and all 10 verification screenshots/logs.
- `09_step11_heatmap_verify_report.md` - runtime heatmap verification at `cam.md` viewpoint. 8 captures + RenderDoc frame + AI triage analysis + probe stats JSON. Found NaN/Inf contamination (P0 blocker), under-occupied cascades (P1), and low GI magnitude (2% vs. expected 10-30%).
