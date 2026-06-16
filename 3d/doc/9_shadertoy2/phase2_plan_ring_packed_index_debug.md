# ShaderToy2 Phase 2 Plan — Ring-Packed Atlas Index Debug

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Phase 2A only — prove ShaderToy ring-packed coordinate semantics visually before any radiance tracing, NEE, or feedback.

---

## 1. Goal

Implement the next safe slice toward Phase 2:

```text
ShaderToy ring-packed chart/cascade/probe/direction coordinate debug
```

This phase does **not** compute lighting. It proves that our atlas indexing matches the ShaderToy reference before we attach radiance semantics to it.

The core correction from Critique 01 was:

```text
Do not use surfaceAtlasWidth * dirRes.
Use ShaderToy ring-packed cascade bands first.
```

This phase exists to make that correction executable and visible.

---

## 2. Reference Contract

From `shader_toy/CubeA.glsl`:

```glsl
float probeCascade = floor(mod(UV.y, 1536.) / 256.);
float probeSize = pow(2., probeCascade + 1.);
vec2 probePositions = gRes / probeSize;
vec3 probePos = gPos
             + mod(modUV.x, probePositions.x) * probeSize / 256. * gTan
             + mod(modUV.y, probePositions.y) * probeSize / 256. * gBit;
vec2 probeUV = floor(modUV / probePositions) + 0.5;
vec2 probeRel = probeUV - probeSize * 0.5;
float probeThetai = max(abs(probeRel.x), abs(probeRel.y));
```

Binding interpretation for this project:

```text
atlas width  = 1024
band height  = 256
cascadeCount = 6 initially, matching ShaderToy C0..C5
atlas height = 256 * cascadeCount = 1536
```

Each 256-pixel vertical band is one cascade. Each chart retains its original rectangle width inside every band.

---

## 3. Implementation Plan

### 3.1 New packed debug texture

Add to `SurfaceRC`:

```text
packedDebugTexture RGBA16F 2D
packedWidth = 1024
packedBandHeight = 256
packedCascadeCount = 6
packedHeight = 1536
```

Keep the existing Phase 1 chart debug texture unchanged.

Reason:

```text
Phase 1 chart atlas is a chart-material/debug view.
Phase 2 packed atlas is the actual coordinate contract for future radiance.
Keeping both avoids overwriting a known-good diagnostic.
```

### 3.2 New shader

Add:

```text
res/shaders/surface_ring_debug.comp
```

For each packed atlas texel:

1. Decode chart from `x` and in-band `y`.
2. Decode cascade from `y / 256`.
3. Compute:

```text
probeSize = 2^(cascade + 1)
probePositions = gRes / probeSize
probeCoord = mod(modUV, probePositions)
directionCoord = floor(modUV / probePositions)
probeUV = directionCoord + 0.5
probeRel = probeUV - probeSize * 0.5
probeThetai = max(abs(probeRel.x), abs(probeRel.y))
```

4. Output selectable debug modes.

### 3.3 Debug modes

Add packed atlas debug modes:

```text
0 chart+cascade color
1 probe coordinate within chart
2 direction coordinate within probe tile
3 ring/theta index
4 derived surface probe world position
```

Minimum acceptable visual checks:

```text
mode 0: six horizontal cascade bands visible
mode 1: probe coordinate density halves per cascade
mode 2: direction tile size grows with cascade
mode 3: ring pattern visible inside each probe tile
mode 4: smooth surface-position gradient per chart
```

### 3.4 Rendering

Add a second overlay or switchable overlay target:

```text
Surface overlay target:
  Chart atlas debug
  Ring-packed atlas debug
```

The ring-packed atlas is tall (`1024x1536`), so render it in a viewport preserving aspect where possible:

```text
bottom-left overlay, max 512 high/width constrained by viewport
```

If the overlay is too small, the debug is still useful via screenshot or future readback.

### 3.5 CLI/UI

Keep current flag:

```text
--use-surface-rc=1
```

Add UI controls only; no new CLI needed yet:

```text
Surface debug target: Chart / RingPacked
Ring debug mode: chart+cascade / probe / direction / ring / world position
```

---

## 4. Acceptance Gates

| Gate | Pass condition | Fail action |
|---|---|---|
| Build | `cmake --build build --config Debug` succeeds | Fix compile/shader errors |
| Shader load | `surface_ring_debug.comp` loads successfully | Fix GLSL compile issue |
| Runtime | `--use-surface-rc=1 --exit-frames=1` exits successfully | Fix lifecycle/integration |
| Visual contract | ring-packed overlay shows six cascade bands | Fix y/cascade decode |
| Probe contract | probe coordinate density halves per cascade | Fix `probePositions` / `modUV` math |
| Direction contract | direction/ring pattern grows with `probeSize` | Fix `floor(modUV / probePositions)` math |
| Safety | final mode-0 volumetric path unchanged | Revert integration path |

No EXR metric is required in this phase because no radiance is computed.

---

## 5. Self-Critique of This Plan

### SC1 — This still does not produce radiance

Accepted. The goal is coordinate correctness. The previous programs failed partly because semantics were ported before data layout was proven.

Improvement:

```text
Phase 2A explicitly stops before tracing. Phase 2B will add hit tracing only after ring debug passes.
```

### SC2 — Six cascades may be overkill for the current engine

Accepted. The volumetric path currently uses fewer practical cascades, but ShaderToy uses six 256-pixel bands. Since this phase is layout validation, matching ShaderToy is more valuable than minimizing texture size.

Improvement:

```text
Use cascadeCount=6 for packed debug. Future radiance dispatch may allow fewer active cascades but must preserve the same coordinate formula.
```

### SC3 — Overlay for 1024x1536 texture may be visually compressed

Accepted. A tall texture in a small viewport can hide details.

Improvement:

```text
Expose debug mode and use high-contrast colors. Later add screenshot/readback if visual overlay is insufficient.
```

### SC4 — Direction decode is easy to get reversed

Accepted. The critical ambiguity is whether `floor(modUV / probePositions)` is called direction coordinate or probe tile coordinate. ShaderToy's variable names are confusing.

Improvement:

```text
Document the chosen mapping in shader comments and output separate probe/direction debug modes to catch reversal.
```

### SC5 — World-position debug still uses AABB chart assumptions

Accepted. This inherits Phase 1's limitation.

Improvement:

```text
World-position mode is diagnostic only. Radiance Phase 2B must verify chart normals/planes before NEE or tracing.
```

---

## 6. Improved Final Plan

Implement **Phase 2A: Ring-Packed Index Debug** only:

```text
1. Add packed atlas texture to SurfaceRC.
2. Add surface_ring_debug.comp.
3. Add packed debug dispatch.
4. Add overlay target selection and ring debug mode UI.
5. Build and smoke test.
6. Document implementation + self-critique.
```

Do **not** implement yet:

```text
- radiance tracing
- point-light NEE
- persistent ping-pong feedback
- final raymarch surface GI lookup
```

This is the fastest precise step toward ShaderToy without breaking the current renderer.
