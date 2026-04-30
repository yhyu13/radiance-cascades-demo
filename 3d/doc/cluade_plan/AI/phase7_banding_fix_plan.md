# Phase 7 — Fix Cascade Banding and Color Bleeding Quantization

**Date:** 2026-04-30
**Source:** Visual triage of `tools/frame_17775455579415942.png` + shader audit
**Artifacts targeted:**
- Large-scale rectangular contour banding (back wall, ceiling)
- Finer probe-grid spatial quantization in indirect field
- Color bleeding appearing in discrete bands rather than smooth gradients

---

## Root cause analysis

### A. Contour banding — linear cascade blend weight

The cascade merge shader (`radiance_3d.comp`) blends between the current and upper
cascade using a **linear** ramp over the final `blendFraction` of the cascade interval:

```glsl
// radiance_3d.comp ~line 349
float l = 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
rad = hit.rgb * l + upperDir * (1.0 - l);
```

`clamp(t, 0, 1)` produces a linear ramp. When the irradiance between the current and
upper cascade differs (due to resolution or angular sampling mismatch), this linear
transition through a high-contrast boundary produces visible **iso-luminance contour
steps** — the rectangular banding visible on the back wall and ceiling.

Additionally, with `blendFraction = 0.5` (default), only the **outer 50%** of each
cascade interval is blended. The inner 50% is 100% current cascade, creating a hard
switch-over to the pure upper cascade at `tMax`. This is the second boundary that
produces a visible step.

### B. Probe-grid quantization — low directional bin count (D=4)

The directional atlas uses `D=4` per axis → **16 bins** per probe, each covering a
~36° solid angle. Even with directional bilinear interpolation, 16 bins is coarse enough
that the cosine-weighted integral in `sampleProbeDir()` picks up angular jumps as the
dominant direction shifts between bins. This manifests as the **finer rectangular grid
pattern** superimposed on the cascade contours.

At D=4 the bin width is `2/D = 0.5` in octahedral UV space — directional bilinear
(Phase 5f) helps, but at this resolution the blur radius is only half a bin, leaving
visible quantization in smoothly-varying indirect fields.

### C. Color bleeding banding

Color bleeding is geometrically correct (red/green walls → floor/ceiling). The banded
appearance is a **consequence of A and B** — the cascades transport the correct colors,
but deliver them at quantized spatial and angular resolution. Fixing A and B will smooth
the bleeding automatically.

---

## Fix plan

### Fix 1 — Smoothstep cascade blend weight (HIGH IMPACT, minimal cost)

**File:** `src/shaders/radiance_3d.comp`

Replace the linear `clamp` with `smoothstep` for the cascade blend weight:

```glsl
// Before:
float l = 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);

// After:
float t = clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
float l = 1.0 - smoothstep(0.0, 1.0, t);
```

`smoothstep` applies cubic `3t²−2t³` easing: derivative is zero at both endpoints,
so the transition has no visible kink at `tMax - blendWidth` (entry) or `tMax` (exit).
This eliminates the hard contour edge at both boundaries of the blend zone.

**Expected visual change:** The rectangular iso-luminance contour lines on the back wall
and ceiling should soften into a continuous gradient.

**Cost:** One extra `smoothstep()` call per cascade bake ray that hits a surface in the
blend zone. Negligible.

---

### Fix 2 — Widen the blend zone (HIGH IMPACT, no cost)

**File:** `src/demo3d.cpp` default / ImGui slider

Current default: `blendFraction = 0.5` (50% of cascade interval is blended).

Increase to `0.75` or `0.9`. A wider blend zone means less "pure current cascade"
region per interval, so the irradiance transition is spread over more distance.

Combined with Fix 1 (smoothstep), a blend fraction of 0.75–0.9 should produce nearly
seamless cascade transitions.

**Trade-off:** A very wide blend zone (approaching 1.0) means almost the entire cascade
interval is blended with the upper cascade, which dilutes the fine-resolution benefit of
the lower cascade. Visually this manifests as the scene looking slightly lower-resolution
at close range. 0.75 is a reasonable starting point.

**Implementation:** Already UI-accessible via the `blendFraction` slider. Set default in
`Demo3D` constructor:

```cpp
// demo3d.cpp constructor
, blendFraction(0.75f)   // was 0.5f
```

---

### Fix 3 — Increase directional resolution D (MEDIUM IMPACT, moderate cost)

**File:** `src/demo3d.cpp` + `src/demo3d.h` + atlas allocation

Current default: `dirRes = 4` → 16 bins/probe, ~36° bin width.
Target: `dirRes = 8` → 64 bins/probe, ~18° bin width.

At D=8, each direction bin covers half the solid angle of D=4. The cosine-weighted
integral in `sampleProbeDir()` is smoother, and the directional bilinear is interpolating
over a much finer grid.

**Atlas size impact:**

| D | Atlas footprint per Z-slice | Atlas total (32 probes Z) |
|---|---|---|
| 4 | 128×128 | 128×128×32 |
| 8 | 256×256 | 256×256×32 |

4× more texture memory for the C0 atlas. At 32³ probes and 16-bit float RGBA:
- D=4: 128×128×32 × 8 bytes = ~4 MB
- D=8: 256×256×32 × 8 bytes = ~16 MB

Acceptable for a developer workstation.

**Bake compute impact:** D² bake rays per probe → 4× more rays per probe bake pass.
With GPU parallelism this is a moderate cost increase, not linear.

**Display impact:** `sampleProbeDir()` iterates all D² bins → 4× more iterations.
This runs once per shaded pixel per frame, so D=8 vs D=4 is measurable but not severe.

**Implementation:**

```cpp
// demo3d.cpp constructor
, dirRes(8)   // was 4
```

The atlas allocation, bake dispatch size, and display loop all read `dirRes` / `D`
dynamically via uniforms — no shader changes required. Rebuild required to reallocate
the atlas texture.

---

### Fix 4 — Blend zone temporal dither (LOW IMPACT, optional)

**File:** `src/shaders/radiance_3d.comp`

Add a small per-probe jitter to `hit.a` before computing the blend weight. This breaks
up any residual iso-luminance contour into noise rather than a coherent band:

```glsl
// Add after blendWidth computation:
float jitter = (fract(sin(dot(vec3(probePos), vec3(127.1, 311.7, 74.4))) * 43758.5) - 0.5)
               * blendWidth * 0.1;   // ±5% of blend zone
float t = clamp((hit.a + jitter - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
float l = 1.0 - smoothstep(0.0, 1.0, t);
```

**Expected visual change:** Residual banding becomes noise rather than a sharp line.
Best applied after Fix 1+2 to catch any remaining artifacts.

**Cost:** One hash per probe — negligible.

---

## Implementation order

| Priority | Fix | Expected outcome |
|---|---|---|
| 1 | Fix 1 — smoothstep blend | Cascade boundary contours soften to gradients |
| 2 | Fix 2 — widen blendFraction to 0.75 | Transition zone wider, seams less visible |
| 3 | Fix 3 — D=8 directional resolution | Probe-grid banding and color-bleeding quantization reduced |
| 4 | Fix 4 — blend zone dither | Mop up residual banding |

Implement and screenshot after each fix to verify improvement and catch regressions.

---

## Verification

After each fix, press `P` in the app and compare the triage output:

| Artifact | Success condition |
|---|---|
| Rectangular contour bands on back wall/ceiling | No longer visible as discrete steps — smooth gradient |
| Probe-grid fine banding | No visible grid at C0 probe spacing on back wall |
| Color bleeding gradient | Red/green bleeding transitions smoothly without visible quantization |
| Quality rating | Should improve from Fair → Good |

---

## Files to change

| File | Fix | Change |
|---|---|---|
| `src/shaders/radiance_3d.comp` | Fix 1, Fix 4 | Smoothstep + optional dither in blend weight |
| `src/demo3d.cpp` | Fix 2, Fix 3 | `blendFraction(0.75f)`, `dirRes(8)` in constructor |
