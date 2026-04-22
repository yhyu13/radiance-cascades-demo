# Done Definition

## Good enough first result

You are done with the first milestone when all of these are true:

- opening the app shows a stable Cornell-box-like image
- direct lighting works from an analytic light source
- SDF debug slice still works
- one cascade can be toggled on and visibly changes the final image
- frame time is acceptable for iteration
- there is no dependency on voxelization or 3D JFA

## Good enough second result

You are done with the second milestone when:

- two cascades work
- coarse cascade visibly fills missing distant contribution
- visual result is better than one cascade
- code path is still simple enough to reason about

## Not required for success

These are explicitly not required for the first visual win:

- generalized mesh voxelization
- OBJ support
- full volumetric GI correctness
- temporal stability system
- 5-cascade hierarchy
- production-ready memory efficiency

## If this fails

If the reduced volumetric path still takes too long to produce a good image, pivot to a 3D scene plus screen/surface-space RC approach. That is the faster and more reliable fallback.
