# ShaderToy2 Phase 0/1 Implementation — SurfaceRC Debug Atlas

**Date:** 2026-05-28  
**Status:** Implemented + self-critiqued  
**Scope:** Phase 0/1 only — flag, SurfaceRC skeleton, Cornell hardcoded chart debug atlas, debug overlay. Final shading remains volumetric.

---

## 1. What Was Implemented

### New C++ files

```text
src/surface_rc.h
src/surface_rc.cpp
```

`SurfaceRC` currently owns only the Phase 1 debug path:

```text
SurfaceRC
  - allocates a 1024x512 RGBA16F 2D debug atlas
  - tracks supported scene state
  - dispatches surface_cornell_debug.comp
  - renders the atlas overlay with surface_debug.frag
```

It does **not** yet implement:

```text
ring-packed radiance cascade bands
persistent ping-pong feedback
point-light NEE
raymarch final GI lookup
```

Those remain Phase 2+ per plan.

---

## 2. New Shaders

```text
res/shaders/surface_cornell_debug.comp
res/shaders/surface_debug.vert
res/shaders/surface_debug.frag
```

### `surface_cornell_debug.comp`

Hardcoded Cornell chart packing:

```text
x 0..255     floor      256x256
x 256..511   ceiling    256x256
x 512..639   left wall  128x256
x 640..767   right wall 128x256
x 768..895   back wall  128x256
x 896..1023  front wall 128x256
y 0..255     valid charts
y 256..511   reserved for later object/interior charts
```

Debug modes:

```text
0 chart ID color
1 normal
2 world position remapped by current OBJ bounds
3 albedo
4 valid mask
```

Unsupported scenes are tinted magenta while still rendering the chart layout.

### `surface_debug.frag`

Displays the debug atlas as a bottom-left overlay. Invalid/reserved atlas area uses a dark checker.

---

## 3. Integration Points

### Build

`CMakeLists.txt` now includes:

```text
src/surface_rc.cpp
src/surface_rc.h
```

### Demo3D initialization

`Demo3D` now:

```text
- loads surface_cornell_debug.comp
- loads surface_debug.frag
- creates SurfaceRC after debug quad initialization
- allocates the 1024x512 atlas
```

Surface shaders are intentionally non-critical while the path is opt-in.

### Render loop

After the existing cascade update block:

```text
surfaceRC->updateScene(currentOBJPath, currentObjBmin, currentObjBmax, useOBJMesh)
surfaceRC->dispatchDebug(surface_cornell_debug.comp)
```

After existing SDF/radiance debug overlays:

```text
surfaceRC->renderDebug(surface_debug.frag, debugQuadVAO)
```

Final mode-0 rendering is unchanged.

### CLI

New flag:

```text
--use-surface-rc=0|1
```

Current behavior:

```text
1 = enable Phase 1 surface debug overlay
0 = keep disabled
```

This flag does not route final shading through surface RC yet.

### UI

New settings header:

```text
Surface RC (ShaderToy2 experimental)
```

Controls:

```text
Use Surface RC debug path
Show surface atlas overlay
Surface debug mode combo
Atlas/status text
```

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

Warnings are pre-existing MSVC warnings (codepage, sscanf, signed/unsigned, etc.); no new compile errors.

### Runtime smoke test

Command:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --exit-frames=1 --window-size=320,240
```

Observed:

```text
[Demo3D] Shader loaded successfully: surface_cornell_debug.comp
[Demo3D] Shader loaded successfully: surface_debug.frag
[SurfaceRC] debug atlas allocated 1024x512 (valid charts=6, valid texels=262144)
[SurfaceRC] enabled: Cornell hardcoded charts (Phase 0/1)
[SurfaceRC] scene=cornell supported=yes bounds=(-0.993501,-0.981431,-1)..(0.993501,0.981431,1)
[MAIN] Application terminated successfully.
```

This confirms:

```text
- shaders compile/load
- atlas allocates
- CLI flag works
- Cornell OBJ is recognized as supported after load
- app exits cleanly
```

---

## 5. Self-Critique

### SC1 — First implementation still uses a debug atlas, not ShaderToy ring-packed radiance

Accepted. This is intentional Phase 1 scope, but the code must not be mistaken for the Phase 2 radiance atlas.

Mitigation already in code/docs:

```text
SurfaceRC comments say Phase 2 starts the actual ring-packed persistent radiance atlas.
UI/CLI text says final shading unchanged.
```

### SC2 — Hardcoded chart extents are bound to current OBJ bounds, but not yet semantically verified against true Cornell wall orientation

Partially accepted.

`surface_cornell_debug.comp` maps charts from `currentObjBmin/currentObjBmax`, which satisfies the plan's requirement to avoid unbound guessed world extents. However, this is still an axis-aligned bounding-box approximation. It assumes the Cornell OBJ is normalized and aligned as expected.

Needed before Phase 2:

```text
- visually inspect normal mode
- compare chart world-position mode against known Cornell planes
- verify back/front wall normal convention against raymarch hit normals
```

### SC3 — The chart table includes a front wall even if some Cornell variants are open

Accepted.

The debug atlas reserves a front wall chart to keep a stable 1024-wide layout and ShaderToy-like six-surface room packing. This may be invalid for open-front captures.

Mitigation for Phase 2:

```text
front wall should be maskable per scene variant
surface radiance should not treat non-existent front wall as a bounce source
```

### SC4 — Surface debug dispatch currently runs every frame when enabled

Accepted.

This is cheap for a 1024x512 debug compute pass, but unnecessary once Phase 1 is stable.

Potential improvement:

```text
dirty flag keyed by scene/debugMode/bounds
```

Not necessary before Phase 2 unless performance becomes visible.

### SC5 — SurfaceRC is integrated into Demo3D rather than a fully isolated subsystem

Accepted.

The integration is intentionally small:

```text
one unique_ptr
one CLI setter
one shader load pair
one dispatch call
one overlay call
one UI block
```

This is acceptable for Phase 1. Future Phase 2+ should keep radiance logic inside `SurfaceRC` to avoid further expanding `Demo3D`.

### SC6 — Runtime smoke test did not save or inspect the overlay image

Accepted.

The smoke test validated shader load, dispatch path, and clean exit, but did not produce visual evidence. Before Phase 2, capture a screenshot in each debug mode or use a texture readback summary.

Recommended command later:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --screenshot=tools/surface_phase1_chart.png --exit-frames=2
```

---

## 6. Improvements Applied After Self-Critique

### I1 — Avoid false unsupported-scene warning during CLI parse

Initial smoke test showed `--use-surface-rc=1` was parsed before `--load-obj=cornell`, producing a misleading warning while the app was still on the default analytic scene.

Fix:

```text
SurfaceRC::setEnabled() no longer warns during the transient analytic/default startup state.
SurfaceRC::updateScene() emits unsupported-scene warnings after actual scene state is known.
```

### I2 — Explicitly document Phase 1 limitations

This implementation doc states that final rendering is unchanged and Phase 2 owns real ring-packed radiance.

---

## 7. Current Limitations

1. No actual surface radiance bake yet.
2. No ShaderToy ring-packed cascade-band atlas yet.
3. No persistent ping-pong/self-feedback yet.
4. No point-light NEE yet.
5. No raymarch hit-to-chart final GI lookup yet.
6. No EXR metric protocol implementation yet.
7. Debug chart includes a front wall unconditionally.
8. Visual screenshot verification still pending.

---

## 8. Next Implementation Slice

Next slice should be Phase 2 preparation, not a generic refactor:

```text
1. Freeze exact ShaderToy ring-packed atlas coordinate helpers in GLSL.
2. Add CPU/GPU constants for Cornell chart bands and cascade bands.
3. Implement surface_radiance.comp skeleton with ring decode only.
4. Output direction/probe/cascade debug colors before tracing radiance.
5. Add screenshot/readback verification for chart/cascade indexing.
```

Do **not** add point-light NEE or feedback until ring-packed indexing is visually proven.

---

## 9. Files Changed

```text
CMakeLists.txt
src/demo3d.h
src/demo3d.cpp
src/main3d.cpp
src/surface_rc.h
src/surface_rc.cpp
res/shaders/surface_cornell_debug.comp
res/shaders/surface_debug.vert
res/shaders/surface_debug.frag
doc/9_shadertoy2/phase1_impl_surface_rc_debug_atlas.md
```
