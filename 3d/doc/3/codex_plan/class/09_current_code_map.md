# 09 Current Code Map

This is the practical "where do I look?" guide.

## Main files

`src/main3d.cpp`
Starts the 3D app, fixes the working directory if it was launched from `build/`, preloads RenderDoc on Windows, parses `--auto-analyze`, `--auto-sequence`, and `--auto-rdoc`, and brackets the frame with RenderDoc capture calls.

`src/demo3d.h`
Declares the main state: scene settings, probe/cascade settings, debug toggles, and GPU resource handles.

`src/demo3d.cpp`
Main implementation. If you want the real story of the branch, this is the most important file.

`res/shaders/radiance_3d.comp`
The core probe-bake compute shader. This is where the cascade intervals, directional atlas writes, and upper-cascade merge live.

`res/shaders/reduction_3d.comp`
Averages atlas bins back into `probeGridTexture`.

`res/shaders/raymarch.frag`
Final image shader. Handles direct shading, direct shadow, optional directional GI, final display modes 0-8, and separated direct/GI outputs for blur.

`res/shaders/temporal_blend.comp`
Fallback temporal EMA shader for atlas/grid history when the fused bake path is not used.

`res/shaders/gi_blur.frag`
Bilateral blur and composite shader. It blurs indirect GI using depth, normal, and optional luminance weights, then recombines it with direct light.

`res/shaders/radiance_debug.frag`
Atlas/debug visualization shader.

`src/rdoc_helper.cpp/h`
Windows-only RenderDoc DLL loader kept separate from Raylib headers to avoid `windows.h` symbol clashes.

`tools/analyze_screenshot.py`
Claude vision analysis for single screenshots, burst captures, and sequence captures.

`tools/rdoc_extract.py`
Runs inside `qrenderdoc.exe --py` to extract GPU timing and named texture slices from `.rdc` captures.

`tools/analyze_renderdoc.py`
Runs in regular Python after extraction and writes the RenderDoc pipeline report.

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
- `res/shaders/raymarch.frag`
- `src/demo3d.cpp` for CPU-side wiring and toggles

### Phase 6

- `src/main3d.cpp` for clean screenshot timing and RenderDoc frame bracketing
- `src/demo3d.cpp` screenshot, analysis, RenderDoc, object-label, and debug-group paths
- `src/rdoc_helper.cpp/h`
- `tools/analyze_screenshot.py`
- `tools/rdoc_extract.py`
- `tools/analyze_renderdoc.py`

### Phase 7-8

- `res/shaders/raymarch.frag` analytic SDF option and render modes 7/8
- `src/demo3d.cpp` render-mode UI and live `dirRes` rebuild controls

### Phase 9-10

- `src/demo3d.cpp:updateRadianceCascades()`
- `src/demo3d.cpp:updateSingleCascade()`
- `res/shaders/radiance_3d.comp`
- `res/shaders/temporal_blend.comp`
- `res/shaders/gi_blur.frag`
- `res/shaders/reduction_3d.comp`

### Phase 12-14

- `src/demo3d.cpp` burst/sequence capture state machines, stats JSON, and quality sliders
- `src/demo3d.h` capture state and temporal/quality settings
- `res/shaders/radiance_3d.comp` `uCnMinRange` C0/C1 ray-reach override
- `res/shaders/gi_blur.frag` luminance edge-stop

## Best linear code-reading order

If you want to read code after reading these notes:

1. `src/demo3d.cpp`
   Focus on constructor, `update()`, `initCascades()`, `updateRadianceCascades()`, `updateSingleCascade()`, `raymarchPass()`, the capture functions, and the cascade/debug UI sections.
2. `res/shaders/raymarch.frag`
   Understand how the final image consumes GI, direct shadow, the optional directional atlas path, render modes 0-8, and separated GI blur outputs.
3. `res/shaders/radiance_3d.comp`
   Understand how probes are baked, merged, temporally blended, jittered, and range-limited.
4. `res/shaders/reduction_3d.comp`
   Understand how directional data becomes the isotropic grid the final shader samples.
5. `res/shaders/gi_blur.frag`
   Understand how the postfilter smooths indirect lighting without blurring direct light.
6. `res/shaders/radiance_debug.frag`
   Understand how the debug views expose the atlas and hit semantics.
7. `src/main3d.cpp`, `src/rdoc_helper.*`, and `tools/*.py`
   Understand the capture and automated-analysis pipeline.

## Current one-paragraph summary

Today, the branch raymarchs a Cornell Box from an SDF, computes direct lighting per visible surface, can shadow that direct term, and adds indirect lighting from a 4-level directional probe hierarchy. The hierarchy stores per-direction radiance in atlases, merges upper-cascade data directionally, uses non-co-located spatial trilinear interpolation by default, reduces atlas data back into isotropic grids, and can temporally accumulate jittered probe bakes into history. The final shader can read either isotropic grids or directional atlases, can expose render modes 0-8, and can route direct/GI into a bilateral blur composite. The surrounding tooling now matters too: screenshots, burst/sequence captures, stats JSON, and RenderDoc pipeline analysis are part of how the current branch is debugged.
