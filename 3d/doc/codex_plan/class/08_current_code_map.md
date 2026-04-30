# 08 Current Code Map

This is the practical "where do I look?" guide.

## Main files

`src/main.cpp`
Starts the app and main loop.

`src/demo3d.h`
Declares the main state: scene settings, probe/cascade settings, debug toggles, and GPU resource handles.

`src/demo3d.cpp`
Main implementation. If you want the real story of the branch, this is the most important file.

`res/shaders/radiance_3d.comp`
The core probe-bake compute shader. This is where the cascade intervals, directional atlas writes, and upper-cascade merge live.

`res/shaders/reduction_3d.comp`
Averages atlas bins back into `probeGridTexture`.

`res/shaders/raymarch.frag`
Final image shader. Consumes the reduced isotropic probe grid.

`res/shaders/radiance_debug.frag`
Atlas/debug visualization shader.

## Where each phase mostly lives

### Phase 1

- `src/demo3d.cpp`
- `res/shaders/raymarch.frag`
- scene setup paths

### Phase 2

- `res/shaders/radiance_3d.comp`
- `src/demo3d.cpp:updateSingleCascade()`
- `src/demo3d.cpp:raymarchPass()`

### Phase 3

- `src/demo3d.cpp:initCascades()`
- `src/demo3d.cpp:updateRadianceCascades()`
- `src/demo3d.cpp:updateSingleCascade()`

### Phase 4

- `res/shaders/radiance_3d.comp`
- `src/demo3d.cpp` debug and UI sections
- `res/shaders/radiance_debug.frag`

### Phase 5

- `res/shaders/radiance_3d.comp`
- `res/shaders/reduction_3d.comp`
- `res/shaders/radiance_debug.frag`
- `src/demo3d.cpp` for CPU-side wiring and toggles

## Best linear code-reading order

If you want to read code after reading these notes:

1. `src/demo3d.cpp`
   Focus on constructor, `initCascades()`, `updateSingleCascade()`, `raymarchPass()`, and the cascade/debug UI sections.
2. `res/shaders/raymarch.frag`
   Understand how the final image consumes GI.
3. `res/shaders/radiance_3d.comp`
   Understand how probes are baked and merged.
4. `res/shaders/reduction_3d.comp`
   Understand how directional data becomes the isotropic grid the final shader samples.
5. `res/shaders/radiance_debug.frag`
   Understand how the debug views expose the atlas and hit semantics.

## Current one-paragraph summary

Today, the branch raymarchs a Cornell Box from an SDF, computes direct lighting per visible surface, and adds indirect lighting from a 4-level probe hierarchy. The hierarchy now stores per-direction radiance in an atlas, merges upper-cascade data directionally, reduces that atlas back into an isotropic probe grid for final rendering, and exposes multiple atlas/debug views to inspect the result. The main remaining complexity is no longer "how do cascades work at all?" but "which Phase 5 layout, angular-resolution, and debug semantics are actually validated and worth keeping?"
