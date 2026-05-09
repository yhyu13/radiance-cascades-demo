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
