# ShaderToy2 Phase 2B-1 Implementation — Surface Radiance Skeleton Debug

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Separate radiance skeleton atlas and compute path. No tracing, no NEE, no feedback, no final GI lookup.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b1_plan_radiance_skeleton_debug.md
```

This phase establishes a future-radiance data path that is separate from the Phase 2A/2B-0 ring-index debug path:

```text
surface_radiance_debug.comp
  -> radianceDebugTexture 1024x1536 RGBA16F
  -> visualized through existing surface_debug.frag overlay
```

---

## 2. C++ Changes

Updated:

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.h
src/demo3d.cpp
src/main3d.cpp
```

### SurfaceRC texture/resource additions

Added:

```cpp
GLuint radianceDebugTexture;
int radianceDebugMode;
```

Allocated as:

```text
1024 x 1536 RGBA16F
same dimensions as the ring-packed atlas
```

Startup log now includes:

```text
[SurfaceRC] radiance skeleton debug atlas allocated 1024x1536
```

### New dispatch

Added:

```cpp
SurfaceRC::dispatchRadianceDebug(GLuint computeProgram)
```

It uploads:

```text
uAtlasSize
uDebugMode
uSceneSupported
uChartActive[6]
uSceneBoundsMin
uSceneBoundsMax
uRayBias = 0.01
```

and writes into `radianceDebugTexture`.

### Debug target selection

Debug target now supports:

```text
0 chart atlas
1 ring-packed atlas
2 radiance skeleton atlas
```

CLI:

```text
--surface-debug-target=radiance|2
--surface-radiance-debug-mode=N
```

UI:

```text
Radiance debug mode:
  Ray Origin
  Hemisphere Direction
  Normal
  Active / Chart Mask
```

---

## 3. Shader Changes

Added:

```text
res/shaders/surface_radiance_debug.comp
```

It duplicates the verified chart/ring decode from `surface_ring_debug.comp`:

```text
BAND_HEIGHT = 256
CASCADE_COUNT = 6
chartActive[6]
probeSize = 2^(cascade + 1)
probePositions = gRes / probeSize
probeCoord = mod(localPx, probePositions)
dirCoord = floor(localPx / probePositions)
probeWorldPos = chartToWorld(...)
rayOrigin = probeWorldPos + normal * uRayBias
worldDir = debugHemisphereDirection(TBN, probeRel)
```

Output modes:

```text
0 ray origin remapped to scene bounds
1 hemisphere/world direction * 0.5 + 0.5
2 normal * 0.5 + 0.5
3 active mask / chart color
```

Alpha:

```text
1 for active charts
0 for inactive/invalid charts
```

Chart 6/front is inactive through the same `uChartActive[6]` mask used by the ring debug path.

---

## 4. Verification

### Build

Command:

```powershell
cmake --build build --config Debug
```

Result:

```text
RadianceCascades3D.exe built successfully
```

Warnings are pre-existing MSVC warnings.

### Smoke/capture commands

Captured:

```text
tools/phase2b1_visual/radiance_m0.png
tools/phase2b1_visual/radiance_m1.png
tools/phase2b1_visual/radiance_m2.png
tools/phase2b1_visual/radiance_m3.png
```

Commands used:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=0 --screenshot=tools/phase2b1_visual/radiance_m0.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=1 --screenshot=tools/phase2b1_visual/radiance_m1.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=2 --screenshot=tools/phase2b1_visual/radiance_m2.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=3 --screenshot=tools/phase2b1_visual/radiance_m3.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

A `System.Drawing` check split the visible ring overlay into:

```text
active region:   x 0..335  (charts 1..5)
inactive region: x 336..383 (chart 6/front)
overlay y range: 193..767
```

Result:

```text
radiance_m0.png activeUnique4=298 inactiveUnique4=2 inactiveBright=0/432
radiance_m1.png activeUnique4=691 inactiveUnique4=2 inactiveBright=0/432
radiance_m2.png activeUnique4=5   inactiveUnique4=2 inactiveBright=0/432
radiance_m3.png activeUnique4=5   inactiveUnique4=2 inactiveBright=0/432
```

Interpretation:

```text
Chart 6 is dark/inactive in all radiance skeleton modes: inactiveBright=0/432.
Ray origin mode has nontrivial active-chart variation: activeUnique4=298.
Direction mode has strong active-chart variation: activeUnique4=691.
Normal and chart-mask modes have five active discrete colors, matching five active charts.
```

---

## 5. Self-Critique

### SC1 — Decode duplication can drift

Accepted.

`surface_radiance_debug.comp` duplicates chart decode and probe math from `surface_ring_debug.comp`. This is acceptable for a short-lived skeleton, but it is a drift risk.

Improvement required before more complex radiance work:

```text
Extract shared chart/ring helpers into surface_common.glsl or keep future changes synchronized with explicit grep/diff review.
```

### SC2 — Initial CLI substring bug caused all radiance captures to use mode 0

Accepted and fixed.

Bug:

```cpp
arg.substr(31)
```

for `--surface-radiance-debug-mode=` was off by one. It parsed an empty string and produced mode 0 for all captures.

Fix:

```cpp
arg.substr(30)
```

After the fix, the capture sweep was rerun and modes 0..3 produced distinct expected structure.

### SC3 — An attempted C++ readback helper was unsafe

Accepted and reverted before final build.

A temporary helper used `glGetTexImage(..., nullptr)` and `glGetTextureSubImage`, which was both unsafe and questionable under this OpenGL 3.3 + extension context. It was removed.

Current verification uses screenshots and external `System.Drawing` checks instead.

### SC4 — Hemisphere direction is still debug-only

Accepted.

This phase still uses the disk-to-hemisphere debug mapping from Phase 2B-0. It proves data path/TBN variation, not final ShaderToy angular distribution.

### SC5 — Ray bias is still hardcoded

Accepted.

`uRayBias` is uploaded as `0.01`. It is now a uniform instead of a literal in shader code, but it is still not derived from scene scale.

Future improvement:

```text
derive bias from scene bounds, voxel size, or chart texel size before actual tracing.
```

---

## 6. Improvements Applied After Self-Critique

1. Fixed CLI parsing for `--surface-radiance-debug-mode=`.
2. Removed unsafe C++ texture readback helper.
3. Rebuilt after the cleanup.
4. Reran the radiance skeleton screenshot sweep.
5. Verified inactive chart mask and active variation with structure-aware checks.

---

## 7. Current Limitations

Still not implemented:

```text
- real surface_radiance.comp
- SDF/mesh tracing from surface probes
- hit distance alpha
- point-light NEE
- direct lighting at hit points
- persistent ping-pong feedback
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT metrics
```

Known debug limitations:

```text
- Direction mode is debug hemisphere mapping, not exact ShaderToy angular distribution.
- Decode logic is duplicated between two shaders.
- Bias is uniform but fixed at 0.01.
```

---

## 8. Next Implementation Slice

Proceed to **Phase 2B-2: Surface Trace Skeleton, No Lighting**.

Recommended scope:

```text
1. Add a trace mode to the radiance skeleton shader or create surface_trace_debug.comp.
2. Cast rays from rayOrigin along worldDir into existing SDF/mesh distance field.
3. Output hit/miss classification only:
   - RGB by hit type
   - alpha as normalized hit distance or 0 for inactive
4. No point-light NEE.
5. No feedback.
6. No radiance accumulation.
7. Verify with structure checks and a few known captures.
```

Do not add direct light or feedback until hit/miss classification is sane.

---

## 9. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.h
src/demo3d.cpp
src/main3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2b1_plan_radiance_skeleton_debug.md
doc/9_shadertoy2/phase2b1_impl_radiance_skeleton_debug.md
tools/phase2b1_visual/radiance_m0.png
tools/phase2b1_visual/radiance_m1.png
tools/phase2b1_visual/radiance_m2.png
tools/phase2b1_visual/radiance_m3.png
```
