# ShaderToy2 Phase 2B-1 Plan — Surface Radiance Skeleton Debug

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Add a separate surface-radiance skeleton atlas/compute shader that reuses the verified chart mask + ring decode + probe origin + hemisphere direction, but still does not trace rays or compute lighting.

---

## 1. Goal

Create the first future-radiance atlas target without adding radiance yet:

```text
surface_radiance_debug.comp
  -> ring-packed surface radiance skeleton texture
  -> debug modes for ray origin / world direction / normal / active mask
```

This phase establishes the data path that later `surface_radiance.comp` can extend with tracing, hit distance, point-light NEE, and feedback.

---

## 2. Non-Goals

Do **not** implement:

```text
- SDF or mesh tracing
- hit distance in alpha
- point-light NEE
- direct lighting at hit points
- previous-frame feedback
- merge from upper cascade
- final raymarch surface GI lookup
- PT/EXR quality metrics
```

Reason:

```text
The next failure source is likely data-path divergence between debug decode and future radiance decode. First prove the future radiance texture receives the same geometry/ray data as the ring debug path.
```

---

## 3. Implementation Plan

### 3.1 Add radiance skeleton texture

Add to `SurfaceRC`:

```cpp
GLuint radianceDebugTexture;
int radianceDebugMode; // 0 origin, 1 direction, 2 normal, 3 active mask
```

Texture layout:

```text
1024 x 1536 RGBA16F
same ring-packed dimensions as Phase 2A/2B-0
```

Do not replace `ringDebugTexture`; keep this as a separate future-radiance path.

### 3.2 Add compute shader

New shader:

```text
res/shaders/surface_radiance_debug.comp
```

It must duplicate or reuse the exact Phase 2B-0 decode semantics:

```text
BAND_HEIGHT=256
CASCADE_COUNT=6
chart decode
chartActive[6]
probeSize
probePositions
probeCoord
projected probeWorldPos
rayOrigin = probeWorldPos + normal * bias
worldDir = hemisphereDebugDirection(TBN, ring direction)
```

Initial debug output modes:

```text
0 ray origin remapped to scene bounds
1 world direction * 0.5 + 0.5
2 normal * 0.5 + 0.5
3 active mask / chart ID
```

Alpha:

```text
1 for active charts
0 for inactive/invalid charts
```

### 3.3 Add dispatch and target selection

Add:

```cpp
SurfaceRC::dispatchRadianceDebug(GLuint computeProgram)
```

Dispatch it when SurfaceRC is enabled, after ring debug dispatch.

Extend debug target selection:

```text
0 chart atlas
1 ring-packed atlas
2 radiance skeleton atlas
```

Reuse `surface_debug.frag` for display.

### 3.4 Add CLI/UI controls

CLI:

```text
--surface-debug-target=radiance|2
--surface-radiance-debug-mode=N
```

UI:

```text
Surface debug target: Chart / Ring-packed / Radiance skeleton
Radiance debug mode: Ray Origin / Hemisphere Direction / Normal / Active Mask
```

### 3.5 Verification

Build:

```powershell
cmake --build build --config Debug
```

Smoke test:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=1 --exit-frames=1 --window-size=1024,768
```

Screenshots:

```text
tools/phase2b1_visual/radiance_m0_origin.png
tools/phase2b1_visual/radiance_m1_direction.png
tools/phase2b1_visual/radiance_m2_normal.png
tools/phase2b1_visual/radiance_m3_active.png
```

Structure checks:

```text
inactive chart 6 must remain dark/alpha-zero in all modes
active charts must show nontrivial variation for origin/direction
normal mode must show discrete chart-normal colors
```

Optional readback if cheap:

```text
sample a few atlas texels and report RGB/alpha:
- floor center origin/debug value
- ceiling center origin/debug value
- chart 6 inactive alpha/value
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if still failing: stop feature work and write diagnosis before tracing/radiance work
```

---

## 5. Self-Critique of This Plan

### SC1 — Decode duplication risks drift

Accepted. Duplicating chart/ring decode in a second shader can reintroduce the exact drift we are trying to avoid.

Improvement:

```text
For this phase, duplication is acceptable but must be kept line-for-line close. The implementation doc must call this out. A later phase should extract shared GLSL helpers into surface_common.glsl or a generated include once the semantics settle.
```

### SC2 — Debug hemisphere direction is still not exact ShaderToy sampling

Accepted. This phase inherits Phase 2B-0's debug hemisphere mapping.

Improvement:

```text
Do not claim final angular correctness. Only claim TBN/data-path correctness.
```

### SC3 — Numeric readback may be too expensive if full texture is read every frame

Accepted.

Improvement:

```text
If implemented, readback must be one-shot and diagnostic-only, not per-frame steady-state behavior.
```

### SC4 — A separate radiance skeleton target increases memory

Accepted. `1024x1536 RGBA16F` is about 12 MiB. This is acceptable during debug and avoids conflating index-debug and future-radiance paths.

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Add radiance skeleton texture to SurfaceRC.
2. Add surface_radiance_debug.comp with same chart/ring decode.
3. Add dispatchRadianceDebug().
4. Add debug target 2 and radiance debug mode UI/CLI.
5. Build + smoke test.
6. Capture m0..m3 screenshots.
7. Run active/inactive region structure checks.
8. Document self-critique and limitations.
```

Stop before ray tracing or lighting.
