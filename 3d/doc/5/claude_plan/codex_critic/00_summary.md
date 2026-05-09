# Codex Critic Summary - doc/5 GPU SDF

Review timestamp: 2026-05-09T12:01:44+08:00

Targets reviewed:

- `doc/5/claude_plan/gpu_sdf_step8_plan.md`
- `doc/5/claude_plan/gpu_sdf_step8_impl.md`

Output:

- `01_gpu_sdf_step8_plan_review.md` - critique of the Step 8 GPU JFA SDF + dynamic sphere overlay plan against the then-current Step 7/cleanup codebase.
- `02_gpu_sdf_step8_impl_review.md` - critique of the Step 8 implementation note against the actual Step 8 C++/shader changes, logs, screenshots, and a reproduced Release CMake build.

Scope:

- The Claude plan/implementation notes were not edited.
- Plan-review claims were checked against `src/demo3d.cpp`, `src/demo3d.h`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/raymarch.frag`, `res/shaders/radiance_3d.comp`, and `res/shaders/sdf_analytic.comp`.
- Implementation-review claims were checked against the current Step 8 source diffs, `tools/app_run_step8*.log`, `tools/step8*.png`, and `cmake --build . --config Release` from `build/`.

Current verdict:

The implementation materially landed the static GPU JFA SDF path: `sdf_3d.comp` now loads, Cornell-Original and Sponza-master bake through the GPU path, and a Release CMake build succeeds. The main implementation note is still too strong. Dynamic mode does not actually force all cascades because `updateRadianceCascades()` still applies `renderFrameIndex % interval`; disabling the dynamic sphere in the UI can leave the last injected sphere baked into the SDF; and the sphere rasterizer seeds a solid volume rather than a surface SDF primitive. The 24x speedup claim is valid only for the isolated SDF bake section, while the full dynamic-frame cost and CPU/GPU numeric parity remain unmeasured.

Top remaining risks:

1. Dynamic mode sets `forceCascadeRebuild=true`, but no code bypasses the cascade staggering test in `updateRadianceCascades()`.
2. Turning off the dynamic-sphere checkbox does not restore `meshVoxelBaseTexture` or invalidate the SDF/cascades, so the last sphere can persist.
3. `addVoxelSphere()` fills a solid ball, so GPU JFA creates a zero-distance volume rather than a true sphere surface field.
4. `generateMeshSDFGPU()` can return success without validating GL errors or required texture handles.
5. The dynamic timing evidence measures only the GPU JFA query section; texture copy/upload, cascades, raymarch, and readback are outside the headline number.
6. CPU/GPU parity is visual-only; preserved screenshots are close but not identical, and no SDF/image tolerance test landed.
7. The build command succeeds through CMake, but the warning-count claim is not backed by preserved output.
