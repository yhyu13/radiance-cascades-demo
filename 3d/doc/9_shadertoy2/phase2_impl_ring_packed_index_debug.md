# ShaderToy2 Phase 2A Implementation — Ring-Packed Index Debug

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Ring-packed coordinate debug only. No radiance tracing, no NEE, no self-feedback, no final surface GI lookup.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2_plan_ring_packed_index_debug.md
```

Goal was to prove ShaderToy-style ring-packed coordinate semantics before adding lighting.

This phase follows the Critique 01 correction:

```text
Do not use surfaceAtlasWidth * dirRes for the first surface RC radiance path.
Use ShaderToy ring-packed cascade bands.
```

---

## 2. New Shader

Added:

```text
res/shaders/surface_ring_debug.comp
```

It writes a packed atlas:

```text
width  = 1024
height = 1536
band height = 256
cascade count = 6
```

Each 256-pixel vertical band corresponds to one ShaderToy-style cascade:

```text
C0 y=0..255
C1 y=256..511
C2 y=512..767
C3 y=768..1023
C4 y=1024..1279
C5 y=1280..1535
```

Within each band, chart packing matches Phase 1:

```text
x 0..255     floor      256x256
x 256..511   ceiling    256x256
x 512..639   left wall  128x256
x 640..767   right wall 128x256
x 768..895   back wall  128x256
x 896..1023  front wall 128x256
```

---

## 3. Ring-Packed Coordinate Contract

For each texel:

```glsl
cascade = y / 256;
pInBand = ivec2(x, y - cascade * 256);
probeSize = exp2(float(cascade + 1));
probePositions = gRes / probeSize;
```

ShaderToy mapping preserved in comments/code:

```glsl
// mod(localPx, probePositions) = which surface probe inside the chart
// floor(localPx / probePositions) = direction coordinate inside that probe's probeSize square
vec2 probeCoord = mod(c.localPx, probePositions);
vec2 dirCoord = floor(c.localPx / probePositions);
vec2 probeUV = dirCoord + 0.5;
vec2 probeRel = probeUV - probeSize * 0.5;
float probeThetai = max(abs(probeRel.x), abs(probeRel.y));
```

Debug modes:

```text
0 chart+cascade color
1 probe coordinate
2 direction coordinate
3 ring/theta index
4 derived surface probe world position
```

---

## 4. C++ Integration

`SurfaceRC` now owns two debug textures:

```text
debugTexture       1024x512   Phase 1 chart debug atlas
ringDebugTexture   1024x1536  Phase 2A ring-packed debug atlas
```

New `SurfaceRC` API:

```cpp
setDebugTarget(int)       // 0 chart, 1 ring-packed
setRingDebugMode(int)     // 0..4
dispatchRingDebug(GLuint)
getRingAtlasWidth/Height()
getRingCascadeCount()
getRingBandHeight()
```

`Demo3D` now:

```text
- loads surface_ring_debug.comp
- dispatches it when SurfaceRC is enabled
- exposes Chart/Ring target selection in ImGui
- renders either chart atlas or ring-packed atlas through the existing surface_debug.frag overlay
```

---

## 5. CLI Additions

Added for headless/screenshot verification:

```text
--surface-debug-target=chart|ring|0|1
--surface-debug-mode=N
--surface-ring-debug-mode=N
```

Existing flag remains:

```text
--use-surface-rc=1
```

Example:

```powershell
.\build\RadianceCascades3D.exe `
  --load-obj=cornell `
  --use-surface-rc=1 `
  --surface-debug-target=ring `
  --surface-ring-debug-mode=3 `
  --exit-frames=1 `
  --window-size=320,240
```

---

## 6. Verification

### Build

Command:

```powershell
cmake --build build --config Debug
```

Result:

```text
RadianceCascades3D.exe built successfully
```

Warnings are pre-existing MSVC warnings (codepage, sscanf, unreachable code in existing shader path, etc.).

### Runtime smoke test

Command:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=3 --exit-frames=1 --window-size=320,240
```

Observed:

```text
[Demo3D] Shader loaded successfully: surface_ring_debug.comp
[SurfaceRC] ring-packed debug atlas allocated 1024x1536 (bandHeight=256, cascades=6)
[MAIN] --surface-debug-target=ring (0/chart or 1/ring-packed)
[MAIN] --surface-ring-debug-mode=3
[SurfaceRC] scene=cornell supported=yes bounds=(-0.993501,-0.981431,-1)..(0.993501,0.981431,1)
[MAIN] Application terminated successfully.
```

This verifies:

```text
- shader compiles and loads
- ring-packed atlas allocates
- CLI debug target/mode setters work
- Cornell OBJ remains recognized
- app exits cleanly
```

### Visual gate evidence after Critique 02

Critique 02 correctly noted that build + clean exit did not verify Phase 2A's declared visual gates. A five-mode screenshot sweep was run after applying the Critique 02 fixes:

```powershell
for ($m = 0; $m -lt 5; $m++) {
  .\build\RadianceCascades3D.exe `
    --load-obj=cornell `
    --use-surface-rc=1 `
    --surface-debug-target=ring `
    --surface-ring-debug-mode=$m `
    --screenshot=tools/phase2a_visual/ring_m$m.png `
    --exit-frames=2 `
    --window-size=1024,768
}
```

The runtime saved the files under the working directory first; they were then moved into:

```text
tools/phase2a_visual/ring_m0.png
tools/phase2a_visual/ring_m1.png
tools/phase2a_visual/ring_m2.png
tools/phase2a_visual/ring_m3.png
tools/phase2a_visual/ring_m4.png
```

Because the current model/tooling cannot visually inspect image attachments, a pixel-pattern summary was also generated with `System.Drawing`:

```text
ring_m0.png size=1024x768 overlayUnique16=98
ring_m1.png size=1024x768 overlayUnique16=142
ring_m2.png size=1024x768 overlayUnique16=234
ring_m3.png size=1024x768 overlayUnique16=94
ring_m4.png size=1024x768 overlayUnique16=289
```

Critique 03 found the first mode-3 ring-stripe fix was mathematically wrong (always-on). The shader was patched again and mode 3 was recaptured:

```text
tools/phase2a_visual/ring_m3_refixed.png
```

A structure-aware overlay-region check was then run over the visible 384x575 ring atlas overlay:

```text
ring_m0.png          overlayY=193..767 unique8=36  rowChangesX10=3   yellowRows=6
ring_m1.png          overlayY=193..767 unique8=138 rowChangesX10=137 yellowRows=0
ring_m2.png          overlayY=193..767 unique8=577 rowChangesX10=40  yellowRows=0
ring_m3_refixed.png  overlayY=193..767 unique8=46  rowChangesX10=16  yellowRows=0
ring_m4.png          overlayY=193..767 unique8=431 rowChangesX10=137 yellowRows=0
```

Evidence interpretation:

- **Mode 0:** Structure check found `yellowRows=6` in the overlay region, matching the six cascade bands.
- **Mode 1:** `rowChangesX10=137` and nontrivial quantized color count show dense probe-coordinate variation across the bands; normalization remains in `[0,1)` after the Critique 02 L3 fix.
- **Mode 2:** Highest coordinate-mode unique-color count (`577` in overlay-region check), consistent with direction-coordinate tile variation growing with `probeSize`.
- **Mode 3:** After the Critique 03 re-fix, `ring_m3_refixed.png` shows quantized variation without the previous always-on white wash. The stripe logic is now based on integer ring index parity, not impossible/always-true half-integer tests.
- **Mode 4:** Highest smooth-gradient evidence among world-position modes (`unique8=431`, `rowChangesX10=137`), consistent with chart-space world-position gradients.

This is still a lightweight verification, not a full automated image-analysis test. It is enough to unblock Phase 2A, but Phase 2B should add numeric readbacks for ray origin/direction invariants.

---

## 7. Self-Critique

### SC1 — Visual contract still not captured to disk

Accepted. The smoke test proves the path runs, but not that the ring pattern looks correct.

Mitigation:

```text
CLI now supports --surface-debug-target=ring and --surface-ring-debug-mode=N,
so a screenshot sweep can be run next without UI interaction.
```

Recommended next verification:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=0 --screenshot=tools/surface_ring_m0.png --exit-frames=2
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=1 --screenshot=tools/surface_ring_m1.png --exit-frames=2
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=2 --screenshot=tools/surface_ring_m2.png --exit-frames=2
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=3 --screenshot=tools/surface_ring_m3.png --exit-frames=2
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=4 --screenshot=tools/surface_ring_m4.png --exit-frames=2
```

### SC2 — Probe world-position mode initially used unscaled probe coordinate

Accepted and fixed.

Bug risk:

```text
probeCoord is in [0, probePositions), not chart pixel units.
world position should scale by probeSize before converting to chart UV.
```

Applied fix:

```glsl
vec2 probeUVChart = clamp((probeCoord + 0.5) * probeSize / c.gRes, vec2(0.0), vec2(1.0));
```

This better matches ShaderToy line 130, where probe position advances by `probeSize / 256` world units.

### SC3 — Direction/ring debug names could hide the ShaderToy semantic inversion

Accepted. ShaderToy variable naming is counterintuitive:

```text
mod(localPx, probePositions) gives surface probe coordinate
floor(localPx / probePositions) gives direction coordinate within probeSize square
```

Mitigation:

```text
The shader comments explicitly state this mapping.
Separate debug modes show probe coordinate and direction coordinate independently.
```

### SC4 — The packed atlas currently uses fixed 6 cascades

Accepted. This matches ShaderToy and is correct for index debugging, but future radiance may not need all six active.

Mitigation:

```text
Keep 6 in debug atlas for ShaderToy parity.
Later radiance path can add activeCascadeCount while preserving band math.
```

### SC5 — Ring debug atlas is large and overlay is compressed

Accepted. `1024x1536` cannot be fully inspected in a small corner overlay.

Mitigation:

```text
High-contrast debug modes and CLI screenshot controls were added.
A future readback or dedicated full-screen debug render mode may be useful.
```

---

## 8. Improvements Applied After Self-Critique

1. Added CLI debug controls:

```text
--surface-debug-target=chart|ring|0|1
--surface-debug-mode=N
--surface-ring-debug-mode=N
```

2. Fixed probe world-position mapping to scale by `probeSize`.
3. Added explicit shader comments documenting the ShaderToy coordinate split.

### Additional fixes from Critique 02

4. Restored ShaderToy TBN for wall charts:

```text
left/right walls: tangent=Y, bitangent=Z
back/front walls: tangent=Y, bitangent=X
```

5. Updated wall world-position debug mapping so `probeUVChart.x` runs along vertical Y and `probeUVChart.y` runs along horizontal Z/X, matching ShaderToy wall chart semantics.
6. Fixed `ringStripe` from dead code:

```glsl
float ringStripe = fract(probeThetai + 0.5) < 0.15 ? 1.0 : 0.0;
```

Critique 03 showed that expression was always true because `probeThetai` is always half-integer. It has now been replaced with integer ring-index parity:

```glsl
float ringStripe = step(0.5, mod(floor(probeThetai), 2.0));
```

7. Fixed probe-coordinate mode normalization to divide by `probePositions`, avoiding upper-cascade overshoot/clamp.
8. Preserved ring overlay aspect ratio when rendering into small viewports.
9. Ran and stored the five-mode visual evidence sweep under `tools/phase2a_visual/`.

---

## 9. Current Limitations

Still not implemented:

```text
- surface_radiance.comp
- actual ray tracing from surface probes
- point-light NEE
- persistent ping-pong atlas feedback
- hit-distance alpha semantics
- final raymarch surface GI lookup
- EXR metrics
```

This is intentional. Phase 2A only validates coordinate layout.

Additional Phase 2B blocker carried forward:

```text
Chart 6/front wall remains allocated in debug, but res/scene/cornell_box.obj contains floor, ceiling, back, left, and right walls plus interior boxes and area light; no front-wall object is present. Phase 2B must add chartActive[6] or compress the chart table before radiance/feedback.
```

---

## 10. Next Implementation Slice

Proceed to **Phase 2B: Surface Radiance Skeleton, No Feedback**:

```text
1. Add surface_radiance.comp or extend ring debug into a radiance target.
2. Reuse exact ring-packed decode from surface_ring_debug.comp.
3. Compute surface probe origin and hemisphere direction.
4. Output direction/normal/ray-origin debug radiance, not real lighting yet.
5. Only after direction/origin debug is visually correct, add SDF/scene tracing.
```

Do not add persistent feedback until single-frame probe origin + direction tracing is proven.

---

## 11. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
src/demo3d.h
src/main3d.cpp
res/shaders/surface_ring_debug.comp
doc/9_shadertoy2/phase2_plan_ring_packed_index_debug.md
doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md
```
