# Phase 8 — Screenshot Analysis and Banding Solutions

**Date:** 2026-05-02
**Image:** `tools/frame_17776513476570812.png`
**Mode:** Mode 0 (full GI, directional enabled, C0 32³, dirRes=4)

---

## Visual observations from screenshot

The image shows the Cornell Box rendered with radiance cascades GI. The most prominent
artifact is a set of **concentric rectangular iso-luminance contour bands** covering
the back wall, floor, and ceiling. They resemble topographic map contour lines.

Key visual facts:

| Observation | Implication |
|---|---|
| Bands are centered on the ceiling light position, not the camera | Artifact is in probe DATA, not view-dependent display path |
| Band spacing is approximately uniform in world space (~12.5 cm) | Spacing matches C0 probe cell size (4 m / 32 probes = 0.125 m) |
| Bands form rectangular "U" shapes on the back wall | Pattern follows Cornell Box SDF iso-distance surface geometry |
| Bands are densest near the light, spacing increases with distance | GI gradient is steepest near the direct illumination source |
| Bands do NOT appear on side walls (red/green) as prominently | Side walls are dominated by strong direct color bleeding — the GI quantization is masked by saturation |
| Boxes on the floor show correct GI (green tint on right box, red on left) | GI color bleeding itself is working; only spatial smoothness is broken |
| Banding persists with D4→D8 and with bake step 0.01→0.001 | Angular resolution and march precision are NOT the cause |

---

## Root cause conclusion (current best understanding)

The GI radiance field in the Cornell Box has a strong spatial gradient that the 32³ C0
probe grid cannot represent continuously. Each C0 probe at 12.5 cm spacing samples the
indirect illumination at its position. Adjacent probes can have meaningfully different
GI values (e.g., one probe's bake rays mostly hit the ceiling/light, the next probe's
rays miss and reach only far-field). The trilinear interpolation between probe centers
is smooth **within** a probe cell, but there is a real GI VALUE STEP between neighboring
probes whenever the scene's GI gradient exceeds the probe sampling rate.

The result is a "staircase" in GI that appears as visible banding at the probe-cell
frequency — exactly what is visible in the screenshot.

This is fundamentally a **spatial aliasing problem** in the probe grid: the GI signal
changes faster in space than the probe density can represent.

---

## What has been eliminated as the cause

| Candidate | How tested | Result |
|---|---|---|
| JFA SDF quantization | Mode 7 smooth t — display march is smooth | Eliminated |
| Display-path trilinear SDF interpolation | Analytic SDF toggle — banding persists | Eliminated |
| Bake ray step-count quantization | 0.01→0.001 min step — banding unchanged | Eliminated |
| Angular directional resolution (D4 bins) | dirRes 4→8 — banding unchanged | Eliminated |
| Display-path cascade blend derivative | Phase 7 smoothstep — minor improvement only | Partially addressed |

---

## Potential solutions (ranked by implementation cost)

### Tier 1 — Zero code, test immediately

**S1: Increase probe density (E4)**
- Change `cascadeC0Res` from 32 to 64 using existing UI combo
- Halves probe spacing from 12.5 cm to 6.25 cm
- If banding halves in spatial frequency → confirms spatial aliasing is the primary cause
- Memory: C0 atlas scales as `(64 × D)² × 64` — roughly 8× larger than 32³ with same D
- This is the most important diagnostic experiment remaining

### Tier 2 — Low code cost, proven in production GI systems

**S2: Temporal accumulation of probe data**
- Each frame, accumulate new probe atlas into running exponential average:
  `atlas_new = mix(atlas_prev, atlas_current, alpha)` where `alpha ≈ 0.05–0.1`
- With a static scene, this converges to a smooth average within ~20 frames
- This is the standard DDGI stabilization technique (used in Lumen, RTXGI)
- Requires: one extra `RGBA16F` atlas texture (same size as current atlas) as history buffer;
  a new compute pass or modified bake shader to blend with history
- Does NOT require changing probe density — smooths the quantization across time
- Caveat: if the scene is animated, alpha must be higher → less smoothing
- Implementation: `reduction_3d.comp` accumulation OR a new `temporal_blend.comp`

**S3: Stochastic probe jitter + temporal accumulation**
- Each frame, offset all probe positions by a random sub-cell jitter (±0.5 probe cells)
- Combined with temporal accumulation → each accumulated frame samples at slightly
  different world positions → temporal average spans a wider footprint than one probe
- Effectively increases spatial resolution without increasing probe count
- Implementation: add `uniform vec3 uProbeJitter` to bake shader, set from CPU
  with frame-to-frame random offset, combine with S2

**S4: Post-process spatial GI blur (E6 from plan)**
- After the display raymarching pass, apply a bilateral / depth-aware box filter over
  the GI contribution buffer
- Preserve direct lighting edges (sharp) while blurring the low-frequency indirect signal
- Simplest version: 3×3 or 5×5 screen-space blur on the GI-only output (mode 6) before
  compositing with direct
- This is the "last resort" option from the Phase 8 plan — it is a screen-space hack
  that does not fix the probe data, but can effectively hide the artifact

### Tier 3 — Significant architectural change

**S5: Visibility-weighted probe interpolation (DDGI-style)**
- Current trilinear probe blend weights only by spatial distance
- Problem: a surface near a probe boundary may blend with a probe on the "other side"
  of a wall, whose GI value comes from a completely different lighting environment
- DDGI fixes this with Chebyshev visibility test: probe contribution is attenuated if
  the surface is farther from the probe than the probe's mean depth suggests (i.e.,
  the probe is probably occluded from the surface)
- Would significantly reduce the "leaking" that amplifies inter-probe GI steps
- Implementation: store per-probe mean/variance of ray hit distances during bake
  (one `RG16F` volume alongside the atlas); sample in display path to compute
  Chebyshev weight per probe corner

**S6: Spherical Harmonics (SH) probe representation**
- Replace the D×D octahedral bin atlas with per-probe L1 SH coefficients (9 floats per probe)
- SH L1 is band-limited by construction — it cannot represent high-frequency angular signal,
  so bin-boundary artifacts are impossible
- Does NOT fix spatial aliasing between probes, but does remove any residual angular artifact
- Implementation cost is high: new bake accumulation (project each ray into SH basis),
  new atlas format, new display-path evaluation
- Not recommended unless the angular hypothesis is confirmed by E1

---

## Recommended action sequence

```
E4 (cascadeC0Res 32→64, UI only)
  → If banding halves: spatial aliasing confirmed.
      → Implement S2 (temporal accumulation) — most impactful fix
      → Optionally add S3 (jitter) on top
  → If banding unchanged: cascade interval tMax transition is the cause.
      → Investigate blend zone width and cascade interval scaling
      → S4 (spatial blur) as interim display fix

E1 (directional GI OFF, UI only) — run in parallel with E4
  → If banding changes: display-path directional sampling contributes
  → If unchanged: confirmed probe DATA issue regardless of display path
```

**Highest expected return: S2 (temporal accumulation).**
It is the standard solution to probe-grid spatial aliasing in production GI systems
and can be implemented with one extra texture and one simple compute pass.
It does not require increasing memory permanently — it amortizes the probe budget
across time rather than space.

---

## Memory notes

| Option | Extra memory | Extra GPU time per frame |
|---|---|---|
| S1 (64³ probes) | ~8× atlas size | ~8× bake time |
| S2 (temporal) | 1 extra atlas (same size) | ~1 ms extra (blend pass) |
| S3 (jitter + temporal) | Same as S2 | Same as S2 |
| S4 (screen blur) | 1 screen-res buffer | Negligible |
| S5 (visibility weights) | 1 RG16F volume | Minor (per probe Chebyshev) |
| S6 (SH probes) | Same or less than atlas | Similar bake cost |
