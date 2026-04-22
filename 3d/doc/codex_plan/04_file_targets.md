# File Targets

## First files to edit

### `src/demo3d.cpp`

Priority work:

- make `raymarchPass()` real
- make `updateSingleCascade()` real for one cascade
- simplify `injectDirectLighting()` to one point light
- keep `sdfGenerationPass()` analytic only
- stop calling unfinished paths unless behind explicit debug flags

### `res/shaders/raymarch.frag`

Target:

- direct analytic-SDF surface render
- surface normal from SDF gradient
- direct light shading
- optional sample from one radiance texture

### `res/shaders/radiance_3d.comp`

Target:

- single cascade first
- fixed low ray count
- no temporal reprojection
- optional coarse cascade sampling only after single cascade works

### `res/shaders/inject_radiance.comp`

Target:

- simplify to one point light or one emissive source
- remove extra lighting types until needed

## Files to avoid touching now

- `res/shaders/voxelize.comp`
- `res/shaders/sdf_3d.comp`
- `src/obj_loader.h`
- any sparse-octree code path

## Recommended parameter defaults

- volume resolution: 64 or 96, not 128 initially
- cascades: 1 first, then 2
- rays per probe: 4 to 8
- one point light
- one Cornell box scene only
