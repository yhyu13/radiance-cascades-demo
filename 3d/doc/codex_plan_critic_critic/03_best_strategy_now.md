# Best Strategy Now

## Primary goal

Get the simplest believable visual result as fast as possible.

That means the strategy should optimize for:
- visible progress
- short debug loops
- minimal interacting systems
- easy rollback when a step fails

## Best strategy

### Stage 1: Lock the problem down

Do not work on these yet:
- voxelization
- OBJ import path as a rendering dependency
- 3D JFA SDF generation
- sparse voxel structures
- temporal reprojection
- more than two cascades
- multi-light support

Keep exactly one scene path active:
- analytic SDF Cornell-box-like scene

### Stage 2: Make the renderer honest

Before touching radiance cascades further, make the final image path real:
- raymarch primary camera rays through analytic SDF
- compute normals from SDF gradient
- shade with one direct light
- display stable material colors and shadows

If this is not working, everything else is noise because you cannot judge lighting quality properly.

### Stage 3: Add one cascade only

Once direct-lit raymarching works:
- build one small probe volume
- cast a tiny fixed ray set per probe
- accumulate low-frequency radiance
- sample that field at surface hits during final shading

The purpose is not correctness perfection. The purpose is to create visible indirect contribution and prove that the cascade machinery can affect the image.

### Stage 4: Add a second cascade only if the first one is useful

The second cascade should only exist to extend reach, not to satisfy the original architecture.

If one cascade already gives a convincing result for the demo scene, it is acceptable to stop there.

## Why this is the best strategy

Because every later feature depends on having a trustworthy picture.

Without a real final raymarch result, you cannot answer basic questions like:
- are normals correct
- is the scene SDF correct
- is the light placement correct
- are probes helping or hurting
- are artifacts from the cascade or from the scene representation

The fastest route is therefore not "implement more systems".
It is "remove uncertainty from the image path first".

## Recommended strategic rule

At every point, prefer the option that keeps the project debuggable by one person in one evening.

If a feature violates that rule, defer it.
