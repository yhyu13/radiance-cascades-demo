# Review: Sponza SDF Step 2 Plan

Review timestamp: 2026-05-06T20:07:40+08:00

Target: `doc/4/claude_plan/sponza_sdf_step2_plan.md`

Verdict: do not implement as written. The move from a 6-neighbor Dijkstra plan to a separable Euclidean distance transform is the right direction, and keeping the bake at `volumeResolution` avoids the immediate 64^3-to-128^3 texture mismatch. The plan still confuses distance-to-seed-voxel-centers with an exact distance-to-mesh surface field, and that breaks the current raymarch/radiance shaders' hit and albedo assumptions.

## What Matches Current Source

- `src/demo3d.h:51` sets `DEFAULT_VOLUME_RESOLUTION = 128`.
- `src/demo3d.cpp:1976-1989` allocates `voxelGridTexture`, `sdfTexture`, and `albedoTexture` at `volumeResolution`.
- `src/demo3d.cpp:4066-4117` currently voxelizes OBJ data into a local `voxelData` and uploads only `voxelGridTexture`; persistent `meshVoxelData` and `generateMeshSDF()` do not exist yet.
- `src/demo3d.cpp:1250-1310` still runs the analytic SDF path whenever `analyticSDFEnabled` is true, so Step 2 alone will not prevent analytic overwrites.

## Findings

### 1. High - The EDT output is not an exact mesh-surface distance field

Affected plan lines:

- `sponza_sdf_step2_plan.md:11-25`
- `sponza_sdf_step2_plan.md:189-198`

Felzenszwalb EDT is exact for the binary grid sites it is given. In this plan, those sites are voxel centers whose alpha was set by `OBJLoader::voxelize()`. That produces an exact distance to occupied voxel centers, not an exact distance to the original triangle surface.

Because Step 1 marks voxels whose centers are within a half-voxel diagonal of a triangle, the center-site EDT can overestimate true triangle distance by roughly that same radius. At 128^3 over the current 4m volume, `voxelSize = 0.03125m` and the half diagonal is about `0.0271m`. The plan's "never overestimates" and "sphere-trace safe" claims are therefore too strong.

Recommended correction:

```cpp
float surfaceRadius = voxelSz * std::sqrt(3.0f) * 0.5f;
sdfData[i] = std::max(0.0f, std::sqrt(sq[i]) * voxelSz - surfaceRadius);
```

This makes the texture a conservative distance-to-occupied-cell band rather than a mathematically exact triangle SDF. Document it that way. If a true mesh-distance field is required, the EDT seed stage must preserve nearest triangle distance, not only binary occupancy.

### 2. High - Current shader hit thresholds are incompatible with a sparse UDF texture

Affected plan lines:

- `sponza_sdf_step2_plan.md:231-238`
- `sponza_sdf_step2_plan.md:244-245`

Current shader facts:

- `res/shaders/raymarch.frag:430` hits only when `dist < EPSILON`, where `EPSILON = 1e-6`.
- `res/shaders/radiance_3d.comp:243` hits only when `dist < 0.002`.
- `res/shaders/raymarch.frag:564` advances by `max(dist * 0.7, 0.01)`.
- `res/shaders/radiance_3d.comp:270` advances by `max(dist * 0.9, 0.001)`.

With a center-site EDT, zero distance exists at occupied voxel centers, not over the continuous triangle surface. A primary ray or cascade ray will rarely land exactly on a zero-valued texel center, and trilinear filtering only makes the exact-zero condition less likely unless all neighboring texels are also zero.

This means Step 2 can upload a plausible-looking distance volume while the final render and cascade bake still miss most OBJ surfaces.

Recommended correction:

- Either upload a conservative zero band by subtracting the Step 1 marking radius, or change the hit tests to a grid-aware threshold on the order of the surface band.
- Keep final raymarch and cascade raymarch thresholds consistent.
- Add a verification item that mode 0 and `radiance_3d.comp` both report actual hits, not only that SDF debug shows rings.

### 3. High - Skipping nearest-color albedo propagation will make OBJ lighting black or unstable

Affected plan lines:

- `sponza_sdf_step2_plan.md:171-174`
- `sponza_sdf_step2_plan.md:205`

The plan uploads `meshVoxelData` directly to `albedoTexture` and leaves all non-surface voxels black. Current shading does not treat alpha as a validity mask:

- `res/shaders/raymarch.frag:468` samples `texture(uAlbedo, uvw).rgb`.
- `res/shaders/radiance_3d.comp:255` samples `texture(uAlbedo, uvw).rgb`.

Lighting is multiplied by that sampled RGB. If a hit lands in the conservative band or in a trilinear blend with empty neighbors, the albedo can be black or artificially dark. This is not only a future material-quality issue; it directly affects whether the OBJ scene is visible and whether GI gets injected.

Recommended correction:

- Propagate nearest seed color alongside the EDT, or store nearest seed index during the transform.
- A simpler interim path is to perform nearest-neighbor lookup from the closest occupied seed when writing `sdfData/albedoData`.
- At minimum, do not call black non-surface albedo "acceptable for GI validation"; it is a known source of false-negative GI.

### 4. Medium - `generateMeshSDF()` needs failure semantics and seed validation

Affected plan lines:

- `sponza_sdf_step2_plan.md:119-183`
- `sponza_sdf_step2_plan.md:231-238`

The proposed function assumes `meshVoxelData` is valid and contains at least one seed. It should explicitly verify:

- `meshVoxelData.size() == N3 * 4`
- `seedCount > 0`
- the final `sdfData` contains finite values
- all GL uploads are followed by an error check or at least a visible failure log

If seed count is zero, uploading `sqrt(INF) * voxelSz` everywhere leaves the render path in a misleading "ready" state. Return `false` and make Step 3 honor that result.

### 5. Medium - The `edt1d()` declaration and helper shape are inconsistent

Affected plan lines:

- `sponza_sdf_step2_plan.md:54-59`
- `sponza_sdf_step2_plan.md:75-112`

The header section declares `static void edt1d(...)` as a `Demo3D` private member, but the implementation snippet defines a file-scope `static void edt1d(...)`. Pick one:

- file-scope helper in `demo3d.cpp`, with no header declaration, or
- `static void Demo3D::edt1d(...)` declared and defined as a class helper.

The `denom == 0` guard is also not the relevant degeneracy in this algorithm: `q` and `v[k]` are distinct indices, so the denominator should not be zero. More useful safeguards are all-INF row handling, finite checks, and avoiding uninitialized `s` if the loop is later edited.

### 6. Medium - The performance estimate is too confident for the proposed implementation

Affected plan lines:

- `sponza_sdf_step2_plan.md:25`
- `sponza_sdf_step2_plan.md:225-227`

The asymptotic count is fine, but the snippet allocates a new `std::vector<float>` for every row/column/pillar. At 128^3 that is 49,152 small heap allocations plus copies. It may still be acceptable, but the plan should not claim 5-20 ms without a measured local run.

Recommended correction:

- Preallocate reusable line buffers, or use flat indexed sweeps.
- Time voxelization and EDT separately.
- Keep the acceptance threshold as measured data, not an assumption.

## Recommended Fix Order

1. Rename the result as a conservative voxel UDF unless a true triangle-distance bake is added.
2. Subtract the surface marking radius or otherwise create a coherent surface band.
3. Propagate nearest albedo or nearest seed index with the distance field.
4. Add seed count, finite-value, GL upload, and timing validation.
5. Only then wire Step 3 into `sdfGenerationPass()`.
