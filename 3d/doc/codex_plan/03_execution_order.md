# Execution Order

## Phase 1: Get a real image

1. Make `raymarchPass()` produce a real analytic-SDF render.
2. Ignore radiance cascades completely in this phase.
3. Validate:
   - camera works
   - normals from SDF gradient work
   - one direct light works
   - Cornell box is readable on screen

## Phase 2: Single-cascade proof

1. Implement `updateSingleCascade()` for exactly one cascade.
2. Use a small 3D probe grid only for the current analytic scene.
3. For each probe:
   - cast a small fixed ray set
   - march only analytic SDF
   - store hit radiance in the probe texture
4. In final raymarch shading, sample this single cascade and modulate surface lighting.

## Phase 3: Two-cascade merge

1. Add one coarser cascade only.
2. If fine cascade ray gets no contribution, sample coarse cascade.
3. Do not add temporal reprojection.
4. Do not add more than two cascades until the image is clearly better than one cascade.

## Phase 4: Cleanup

1. Remove obvious placeholder logs and dead branches.
2. Lock a stable demo scene and default parameters.
3. Add one debug toggle for:
   - SDF slice
   - cascade volume slice
   - final shaded image

## Stop conditions

If Phase 2 does not produce a visibly improved image quickly, stop investing in volumetric RC and switch to a screen/surface-space 3D path instead.
