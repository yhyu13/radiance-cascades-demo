# Review: Sponza OBJ to SDF Implementation Plan

Review timestamp: 2026-05-06T17:07:19+08:00

Target: `doc/4/claude_plan/sponza_sdf_impl_plan.md`

Verdict: revise before implementation. The plan correctly identifies that the OBJ path currently stops at voxel upload and that the GPU JFA shader is not ready. However, the proposed CPU SDF path is wired into the wrong place in the render lifecycle, uses the wrong distance-transform algorithm for the stated goal, and misses the albedo/debug access paths needed for a visible Sponza scene.

## What The Plan Gets Right

- `src/obj_loader.h` does parse the local Sponza OBJ successfully in principle. Local checks show `res/scene/sponza.obj` has 145185 vertices and 262267 triangular faces.
- The OBJ is triangulated, so the current `OBJLoader::load()` face parser is not immediately blocked by quads.
- `OBJLoader::normalize()` does scale the mesh into a longest-axis `[-1, 1]` span, which fits inside the current volume bounds.
- `sdf_3d.comp` is not a ready 3D JFA implementation. Its Voronoi seed propagation is commented as TODO, and `sdfGenerationPass()` does not dispatch it.
- Avoiding `voxelize.comp` and `sdf_3d.comp` for the first working Sponza path is a defensible short-term choice.

## Findings

### 1. High - `loadOBJMesh()` is the wrong place to finalize `sdfTexture`

Affected plan sections:

- `Step 2 - CPU BFS Distance Transform`
- `Step 3 - Wire Up in loadOBJMesh()`

The plan says to generate and upload `sdfTexture` at the end of `Demo3D::loadOBJMesh()`. In the current render lifecycle, that texture will not survive the next render update as intended.

Current code evidence:

- `src/demo3d.cpp:442-457` resets `sdfReady=false` when `sceneDirty` is true, then calls `sdfGenerationPass()`.
- `src/demo3d.cpp:1258-1298` regenerates analytic SDF whenever `analyticSDFEnabled` is true.
- `src/demo3d.cpp:1304-1305` clears `sdfTexture` when analytic SDF is disabled.
- `src/demo3d.cpp:4102-4103` sets `sceneDirty=true` and `useOBJMesh=true` after OBJ voxel upload.
- `src/demo3d.cpp:187` defaults `analyticSDFEnabled=true`.

So the proposed sequence is:

```text
loadOBJMesh() uploads voxelGridTexture
loadOBJMesh() would upload mesh sdfTexture
loadOBJMesh() sets sceneDirty=true
next render: sdfGenerationPass() runs
default path: analytic SDF overwrites mesh sdfTexture
disabled-analytic path: sdfTexture is cleared
```

Recommended fix: put the mesh SDF path inside `sdfGenerationPass()` as the `useOBJMesh` branch, or introduce persistent mesh voxel/albedo CPU data and a `meshSDFReady` state that the render lifecycle understands. If generation stays in `loadOBJMesh()`, it must also prevent the next `sdfGenerationPass()` from overwriting it and still force the cascades to rebuild.

### 2. High - The proposed "exact Euclidean BFS" is not Euclidean

Affected plan sections:

- `Step 2 - CPU BFS Distance Transform`
- Algorithm notes around "multi-source BFS (Euclidean approximation)"
- Code snippet using a priority queue and 6-connected neighbors

The plan says the algorithm is exact Euclidean BFS / Meijster / Felzenszwalb. The code snippet is a 6-connected Dijkstra with unit `voxelSz` edge costs:

```cpp
const int dx[] = {1,-1,0,0,0,0};
...
float nd = d + voxelSz;
```

That computes grid L1/Manhattan distance from the nearest seed, not Euclidean distance. For example, a voxel offset by `(1,1,0)` receives distance `2 * voxelSz`; its Euclidean distance is `sqrt(2) * voxelSz`.

This matters because both `raymarch.frag` and `radiance_3d.comp` use SDF sphere-tracing style steps:

- `res/shaders/raymarch.frag:427-428` samples `dist = sampleSDF(pos)`.
- `res/shaders/raymarch.frag:563` advances by `max(dist * 0.9, 0.001)`.
- `res/shaders/radiance_3d.comp:233-269` follows the same distance-guided hit loop.

An overestimated distance is not a safe step. It can jump over thin Sponza surfaces.

Recommended fix: implement a real Euclidean distance transform on the binary surface grid, or store closest seed coordinates and compute true distances. A 26-connected Dijkstra is still only an approximation. If the short-term goal is "good enough," the plan should say that explicitly and lower the correctness claims.

### 3. High - The voxelizer bbox must expand by the distance threshold

Affected plan section:

- `Step 1 - Fix CPU Voxelizer`

Replacing `pointInTriangle()` with closest-point distance is the right direction, but the plan leaves the existing triangle bounding-box loop unchanged.

Current code evidence:

- `src/obj_loader.h:276-286` computes `minPt/maxPt`, converts those exact points to voxel coords, and clamps them.
- `src/obj_loader.h:288-297` only tests voxel centers inside that unexpanded bbox.
- The plan adds a threshold at `sponza_sdf_impl_plan.md:139-142`, but it does not expand the bbox by that same threshold.

For a flat floor or wall, the raw triangle bbox can be only one voxel layer thick on the normal axis. Voxel centers just above or below the triangle may be within `sqrt(3)/2 * voxelSize` of the surface but outside the raw bbox, so they are never tested.

Recommended fix: expand `minPt` and `maxPt` by the marking threshold before `worldToVoxel()`:

```cpp
float threshold = voxelSize * glm::sqrt(3.0f) * 0.5f;
glm::vec3 minPt = glm::min(v0, glm::min(v1, v2)) - glm::vec3(threshold);
glm::vec3 maxPt = glm::max(v0, glm::max(v1, v2)) + glm::vec3(threshold);
```

Then use the closest-point test inside that expanded range.

### 4. High - The plan assumes 64^3, but current default is 128^3

Affected plan sections:

- Performance note
- CPU BFS rationale
- Verification checklist
- "What Is Deliberately Skipped"

The plan repeatedly sizes the CPU path around 64^3 and expects `[Demo3D] Mesh SDF generated: 262144 voxels...`.

Current code evidence:

- `src/demo3d.h:51` sets `DEFAULT_VOLUME_RESOLUTION = 128`.
- `src/demo3d.cpp:113` initializes `volumeResolution(DEFAULT_VOLUME_RESOLUTION)`.
- `src/demo3d.cpp:4090` calls `objLoader.voxelize(volumeResolution, ...)`.

Without an explicit resolution change, Sponza voxelization and SDF generation run at 128^3, not 64^3. That is 2097152 voxels, not 262144, and the triangle bounding-box voxelization cost is much higher.

Recommended fix: either make this plan explicitly add a temporary 64^3 mesh-SDF mode, or estimate and design for the current 128^3 default. The current text says 128^3 is too slow for CPU voxelization, but the proposed code will run at 128^3 anyway.

### 5. Medium - The plan does not populate `albedoTexture`

Affected plan sections:

- `Current State`
- `Step 2`
- `Step 3`
- `What Is Deliberately Skipped`

The plan focuses on `voxelGridTexture` and `sdfTexture`. The current render and bake shaders do not shade surfaces from `voxelGridTexture`; they sample `albedoTexture`.

Current code evidence:

- `res/shaders/radiance_3d.comp:50` declares `uniform sampler3D uAlbedo`.
- `res/shaders/radiance_3d.comp:255` samples albedo for cascade bake shading.
- `res/shaders/raymarch.frag:96` declares `uniform sampler3D uAlbedo`.
- `res/shaders/raymarch.frag:468` samples albedo for final direct shading.
- `src/demo3d.cpp:1288` binds `albedoTexture` as the analytic SDF shader's second output.
- `src/demo3d.cpp:1403-1404` binds `albedoTexture` for `radiance_3d.comp`.
- `src/demo3d.cpp:1666-1667` binds `albedoTexture` for `raymarch.frag`.
- `src/demo3d.cpp:4097-4100` uploads OBJ voxel colors only to `voxelGridTexture`.

If mesh SDF generation only uploads distances, the final Sponza scene may use stale analytic-scene albedo or empty/default texture contents. "All gray is fine" is acceptable, but the code still needs to write gray into `albedoTexture`, ideally near mesh surfaces or as a nearest-surface color field.

Recommended fix: have the mesh SDF path also populate `albedoTexture`. A first version can upload constant gray everywhere or use the voxelized material color for surface voxels and nearest-surface propagation alongside the distance transform.

### 6. Medium - Verification references non-existent access paths and wrong debug mode

Affected plan section:

- `Verification Checklist`

The checklist says to run with `--scene sponza` or toggle via an ImGui OBJ button. Current code does not expose either path:

- `src/main3d.cpp:153-163` only handles `--auto-analyze`, `--auto-sequence`, and `--auto-rdoc`.
- `src/demo3d.cpp:3433-3449` exposes Empty Room, Cornell Box, Simplified Sponza, and Cornell Box OBJ. There is no Sponza OBJ button.
- `src/demo3d.cpp:3449` hardcodes `loadOBJMesh("res/scene/cornell_box.obj")`.

The checklist also calls render mode 5 an "SDF heatmap." Current render mode 5 is step-count heatmap:

- `res/shaders/raymarch.frag:571-576` labels mode 5 as SDF step-count heatmap.
- The dedicated SDF debug visualization is the separate `sdf_debug.frag` path toggled by `D`, with `sdfVisualizeMode`.

Recommended fix: include the access path in the implementation plan: add a Sponza OBJ UI button or CLI argument, and use the SDF debug overlay for SDF-value validation. Use render mode 5 only to inspect raymarch step counts.

### 7. Medium - The plan's distance-unit instructions contradict themselves

Affected plan section:

- `Step 2 - CPU BFS Distance Transform`

The algorithm text says to normalize distances to `[0, 1]`, but the code snippet uploads raw world-space distances. The note below then says raymarch reads world-space units and no shader change is needed.

Current shader behavior expects world-space-ish distances:

- `res/shaders/raymarch.frag:563` uses the sampled value directly as a ray step.
- `res/shaders/radiance_3d.comp:269` keeps a minimum step but otherwise advances by sampled distance.

Recommended fix: remove the normalization instruction. Upload world-space distances in the same unit system as `volumeOrigin` / `volumeSize`, or change the shaders consistently. Do not normalize to `[0, 1]` for the existing raymarch path.

### 8. Low - The implementation details are missing a few mechanical compile items

Affected plan section:

- `Step 2 - CPU BFS Distance Transform`

If the snippet is implemented as shown, `demo3d.cpp` will need additional headers such as `<queue>`, `<functional>`, and `<utility>` depending on how the priority queue is written. `demo3d.h` needs the private method declaration. This is easy to fix, but it should be included in a plan meant to be directly implemented.

## Recommended Revised Plan

1. Add a real Sponza entry point first:
   - UI button: `Sponza (OBJ)`, or
   - CLI flag: `--scene sponza`.

2. Change `loadOBJMesh()` to load and store mesh voxel/albedo source data, not to finalize SDF behind the render lifecycle's back.

3. Add a `useOBJMesh` branch inside `sdfGenerationPass()`:
   - If mesh data is active, generate/upload mesh `sdfTexture` and `albedoTexture`.
   - If analytic mode is active, use `sdf_analytic.comp`.
   - Avoid the current disabled-analytic clear path for active mesh scenes.

4. Fix CPU voxelization conservatively:
   - Use closest-point-on-triangle distance.
   - Expand the triangle bbox by the marking threshold before converting to voxel coords.
   - Track unique filled voxels if the fill count is used for validation, because the current counter can double count overlapping triangles.

5. Replace the 6-connected Dijkstra with a real Euclidean distance transform or downgrade the claim to "approximate grid-distance field" and validate that it does not skip thin Sponza features.

6. Decide resolution explicitly:
   - Temporary 64^3 mesh mode for rapid iteration, or
   - 128^3 design with realistic CPU cost and progress reporting.

7. Update verification:
   - Confirm SDF through `sdf_debug.frag` / SDF debug overlay.
   - Confirm render mode 5 only as a step-count/performance view.
   - Confirm albedo by checking direct-only mode and GI bake output.

## Bottom Line

The plan's broad direction is pragmatic, but the exact implementation would likely fail in two ways: the generated mesh SDF would be overwritten/cleared by the existing scene-dirty pipeline, and the proposed "Euclidean" distance field would not be a safe Euclidean SDF. Fix those before writing code.
