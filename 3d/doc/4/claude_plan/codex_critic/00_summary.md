# Codex Critic Summary

Review timestamp: 2026-05-06T17:36:42+08:00

Targets reviewed:

- `doc/4/claude_plan/sponza_sdf_impl_plan.md`
- `doc/4/claude_plan/sponza_sdf_step0_impl.md`

Output:

- `01_sponza_sdf_impl_plan_review.md` - current-codebase critique of the Sponza OBJ to SDF implementation plan.
- `02_sponza_sdf_step0_impl_review.md` - critique of the Step 0 UI entry-point implementation note and corresponding current source changes.

Scope:

- The Claude plan was not edited.
- Claims were checked against `src/obj_loader.h`, `src/demo3d.cpp`, `src/demo3d.h`, `res/shaders/sdf_3d.comp`, `res/shaders/voxelize.comp`, `res/shaders/radiance_3d.comp`, and `res/shaders/raymarch.frag`.
- Local Sponza OBJ facts were checked: `res/scene/sponza.obj` is present, has 145185 vertices and 262267 triangular faces, and is about 23.9 MB.

Verdict:

Do not implement the plan as written. The high-level direction is reasonable, especially avoiding the unfinished GPU JFA for the first working path, but the lifecycle wiring and distance-transform proposal would not produce a reliable Sponza SDF in the current app.

Top risks:

1. Calling `generateMeshSDF()` inside `loadOBJMesh()` will be overwritten or cleared by the next `sdfGenerationPass()`.
2. The proposed priority-queue "Euclidean BFS" is actually a 6-connected grid shortest path, so it produces Manhattan distances, not a safe Euclidean SDF for sphere tracing.
3. The voxelizer fix does not expand the triangle voxel bounding box by the distance threshold, so it can still miss thin floors/walls.
4. The plan assumes a 64^3 bake in several places, but the current runtime default is 128^3.
5. The plan only uploads `sdfTexture`; current shading also needs `albedoTexture`, while `voxelGridTexture` is not sampled by the final raymarch/radiance shaders.
6. Step 0 adds OBJ switching UI, but `OBJLoader::load()` still accumulates previous OBJ data and analytic scene buttons do not clear `useOBJMesh`.
