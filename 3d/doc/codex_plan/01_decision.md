# Decision

## Recommendation

Do not continue the current branch as a broad volumetric GI renderer.

Continue only with a narrowed prototype:

- analytic SDF scenes only
- one point light
- direct lighting first
- then 1 cascade
- then 2 cascades maximum
- simple final raymarch image
- no voxelization
- no OBJ pipeline
- no 3D JFA
- no sparse voxel structure
- no temporal reprojection

## Why

The current branch has useful scaffolding, but the critical path is still missing:

- final image path is placeholder
- actual cascade update is placeholder
- full volumetric path is too expensive relative to your goal

If your goal is the simplest visual result, the best strategy is to finish a small analytic-SDF RC prototype, not the full original plan.
