# Scope Cut

## Keep

- `src/analytic_sdf.cpp`
- `src/analytic_sdf.h`
- camera and window loop
- debug quad / slice visualization
- `res/shaders/sdf_analytic.comp`
- `res/shaders/radiance_3d.comp` as the starting point for a reduced cascade shader
- `res/shaders/raymarch.frag` as the starting point for final image

## Freeze for now

- `res/shaders/voxelize.comp`
- `res/shaders/sdf_3d.comp`
- `src/obj_loader.h`
- OBJ scene loading
- sparse voxel data structures
- temporal reprojection logic
- multi-light system
- full cascade hierarchy above 2 cascades

## Delete from immediate milestone thinking

Do not spend time on these until you already have a stable image:

- full 3D JFA
- conservative voxelization
- mesh-to-volume conversion
- 5-cascade tuning
- generalized material system
- broad scene support

## Immediate target scene

Use only:

- Cornell-box-like analytic SDF scene
- box and sphere primitives
- one emissive ceiling or one point light
