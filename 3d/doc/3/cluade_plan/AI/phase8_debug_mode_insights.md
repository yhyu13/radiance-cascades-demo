# Phase 8 — Debug Mode Insights and Diagnostic Methodology

**Date:** 2026-05-02

---

## Why mode 5 (step count) produces false banding

Mode 5 visualizes the raymarcher's integer loop counter at the moment of surface hit.

```
Ray march trace (SDF sphere-tracing):

  t=0    t=0.30  t=0.52  t=0.65  t=0.73  t=0.78   HIT
  ●──────●───────●───────●───────●───────●──────── ✕
  step 1  step 2   step 3   step 4   step 5   step 6

  pixel A: hit at t=0.78, 6 steps  →  color = 6/MAX
  pixel B: hit at t=0.76, 5 steps  →  color = 5/MAX
  Delta = 1/MAX ≈ 0.008  →  visible band even though hit positions differ by 2mm
```

The step count changes by ±1 whenever the sphere-tracing geometry crosses a step
boundary — not when the geometry changes. The integer quantization imposes artificial
iso-contours on an otherwise smooth SDF surface.

**Mode 5 is a diagnostic for march convergence speed, not for SDF accuracy or GI quality.**
It was retired as a GI artifact diagnostic in Phase 7.

---

## Why mode 7 (ray travel distance) is smooth

Mode 7 stores the continuous float `t` at the hit point, normalized against the
**primary-ray volume segment** — the camera ray's entry and exit distances with the
scene bounding box (from `intersectBox()` in `raymarch.frag`):

```glsl
// res/shaders/raymarch.frag:446
float tNorm = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.0, 1.0);
// tNear = where the camera ray enters the SDF volume bounding box
// tFar  = where the camera ray would exit the volume if it hit nothing
// t     = where the ray actually hit a surface
```

```
  pixel A: t_hit = 0.78  →  tNorm = (0.78 - tNear) / (tFar - tNear) ≈ smooth value
  pixel B: t_hit = 0.76  →  tNorm = (0.76 - tNear) / (tFar - tNear) ≈ nearby smooth value
  Delta is tiny  →  smooth gradient, nearly invisible
```

Note: `tNear`/`tFar` here are NOT cascade bake intervals (those live in
`radiance_3d.comp` as `tMin`/`tMax`). Mode 7 shows "how far into the volume did
this ray travel before hitting a surface" as a fraction of the total camera-ray
traversal depth.

Mode 7 color is a **continuous function of a float**, so it is as smooth as the
underlying SDF geometry. It confirmed:

1. The display-path sphere tracer is converging correctly to the surface
2. Hit positions are spatially smooth — no step-snapping artifact in the final march
3. Therefore the mode 0 GI banding is NOT caused by display-path raymarching

---

## Diagnostic heuristic: integer vs float visualization

In this specific case, mode 5's integer loop counter adds quantization that does not
exist in the underlying geometry. Mode 7's float distance does not add that
quantization.

```
Mode 5: loop counter (int)    →  banded  →  artifact is in the diagnostic, not the data
Mode 7: hit distance (float)  →  smooth  →  no extra quantization at this stage
```

**Practical lesson:** when diagnosing a continuous-field artifact, prefer a
normalized float output that cannot introduce its own discretization. Integer
diagnostics are still useful for other purposes (e.g., per-triangle ID, cascade
level coloring) — the issue is specifically using an integer accumulator to infer
spatial field quality.

---

## Where the leading hypothesis points (unconfirmed — E4 needed)

Every pipeline stage upstream of the probe atlas has been cleared by experiment.
The leading hypothesis is that the banding lives in the **probe atlas data**:

```
Pipeline stages and diagnostic status:

  [SDF texture]
       │  ← mode 7 smooth: display-path hit positions are accurate
       ▼
  [Bake raymarch in radiance_3d.comp]
       │  ← min step 0.01→0.001: banding unchanged → step snapping NOT the cause
       ▼
  [Probe atlas (C0: 32³ probes × D×D directions)]
       │  ← LEADING HYPOTHESIS: discontinuity lives here
       │    Each probe stores a discrete sample of the GI field.
       │    Adjacent probes may have significantly different values
       │    when the GI gradient exceeds the probe spacing (12.5 cm).
       │    E4 (cascadeC0Res 32→64) is needed to confirm.
       ▼
  [Reduction → probeGridTexture]
       │
       ▼
  [Display raymarch in raymarch.frag]
       │  ← trilinear probe interpolation: smooth WITHIN a cell,
       │    but cannot recover missing GI values BETWEEN probe samples
       ▼
  [Final image]  ←  banding visible as probe-grid-frequency iso-contours
```

This is not yet confirmed. E1 and E4 remain to be run.

---

## What each experiment proved

| Experiment | Variable isolated | Result | Conclusion |
|---|---|---|---|
| Mode 5 vs mode 7 | Integer step count vs float distance | Mode 5 banded, mode 7 smooth | Step count is a false diagnostic; display march is accurate |
| Analytic SDF toggle | Texture trilinear vs analytic formula | Banding persists | SDF texture trilinear interpolation is NOT the cause |
| dirRes 4 → 8 (E2) | Angular bin resolution D | Banding unchanged | Angular quantization (D4 bins) is NOT the dominant cause |
| Bake min step 0.01→0.001 | Ray march precision in bake shader | Banding unchanged | Step snapping in bake is NOT the cause |
| E1 (directional GI OFF) | Display-path atlas vs isotropic reduction | Not yet run | Would isolate display-path lookup contribution |
| E4 (cascadeC0Res 32→64) | Probe spatial density | Not yet run | Would confirm or deny spatial aliasing hypothesis |

---

## Key insight: probeGridTexture is NOT an independent isotropic bake

`probeGridTexture` is produced by `reduction_3d.comp`, which spatially averages the
directional atlas. It is NOT a separate isotropic bake pass. This means:

- Toggling Directional GI OFF (E1) switches the display path from atlas lookup to
  isotropic grid lookup
- But if the atlas has banding, the isotropic grid inherits that banding (it is
  derived from the same atlas data)
- E1 therefore tells you about the **display-path lookup method**, not about whether
  the underlying banding is fundamentally angular or spatial in the bake

Similarly, `useDirBilinear` controls how the **upper cascade** is read during the
**bake merge** step in `radiance_3d.comp`. It does not affect the display-path
integration in `sampleDirectionalGI()` which always integrates all D×D bins.

---

## Interpretive framing: spatial aliasing (not yet confirmed)

**The following is an interpretive model, not a proven finding. E4 is the test.**

The banding pattern is consistent with spatial aliasing: the GI radiance field may
vary faster in space than the 12.5 cm probe grid can represent. If so, the probe
grid cannot capture the variation continuously, and the trilinear interpolation
produces a staircase at the probe-cell frequency — exactly what is visible.

This framing predicts: if `cascadeC0Res` is doubled (32→64, probe spacing 12.5→6.25 cm),
the band spacing should halve. That prediction remains to be tested by E4.

```
IF the spatial aliasing model is correct, solutions map to standard AA strategies:

  Increase sampling rate  →  more probes (E4)
  Temporal supersampling  →  accumulate jittered frames
  Low-pass filter first   →  SH probes or irradiance smoothing
  Post-process filter     →  screen-space GI blur
  Better reconstruction   →  visibility-weighted probe interpolation (DDGI)
```

For candidate solutions and their cost/impact ranking, see
`phase8_screenshot_analysis.md`. The ranking there is conditional on E4 confirming
the spatial aliasing hypothesis.
