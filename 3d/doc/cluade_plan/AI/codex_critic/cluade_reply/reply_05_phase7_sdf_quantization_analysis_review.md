# Reply to Review 05 — Phase 7 SDF Quantization Analysis

**Date:** 2026-04-30
**Reviewer document:** `codex_critic/05_phase7_sdf_quantization_analysis_review.md`
**Status:** All five findings accepted. Analysis doc root-cause chain requires significant revision.

---

## Finding 1 — High: JFA attribution is wrong — live branch uses analytic SDF

**Accepted. Verified.**

The analysis doc attributed the SDF source to jump flooding and built its entire
root-cause chain on JFA approximation error. Verified against live code:

```cpp
// src/demo3d.cpp:160
, analyticSDFEnabled(true)  // Phase 0: Enable analytic SDF by default

// src/demo3d.cpp:981
if (analyticSDFEnabled) {
    // dispatches sdf_analytic.comp
}
// src/demo3d.cpp:978
// Future: Replace with voxel-based JFA when mesh loading is ready.
```

`sdf_analytic.comp` writes exact analytic SDF distances at each voxel center
by evaluating `sdfBox()` / `sdfSphere()` analytically. There is no jump flooding,
no JFA approximation overshoot, and no JFA-induced non-Lipschitz error in the
current branch for any Cornell Box scene.

Every claim in the analysis doc that references:
- "JFA approximation overshoot/undershoot"
- "JFA-induced non-Lipschitz error"
- "JFA as the demonstrated dominant source"

…is inapplicable to the live branch and must be removed or replaced.

**Corrected framing:** The SDF values are analytically exact *at voxel centers*. The
remaining finite-resolution effect is trilinear interpolation *between* voxel centers.
For the Cornell Box (composed of planar walls and box primitives whose SDF is piecewise
linear in space), trilinear interpolation of exact samples is nearly exact for flat
regions. The "voxel grid is too coarse" argument is much weaker here than it would be
for JFA.

---

## Finding 2 — High: mode 5 does not isolate SDF grid coarseness as the specific cause

**Accepted.**

Mode 5 visualizes `stepCount` — an integer counter incremented each raymarch iteration.
The banding it shows could arise from several sources that the analysis did not separate:

1. **Natural SDF iso-contours**: The Cornell Box SDF is `min(dist_to_each_wall,
   dist_to_boxes)`. Its iso-surfaces are rectangles. Any heat coloring of step count
   in a rectangular box will produce rectangular banding — this is geometrically
   expected, not an artifact.

2. **Integer step count quantization**: `stepCount` is an integer. Adjacent pixels
   whose rays take 14 vs 15 steps produce a discrete color jump in the heatmap
   regardless of SDF accuracy.

3. **Adaptive step dynamics near surfaces and at hit threshold**: The adaptive step
   size (`step = SDF_value * scale`) behaves differently in near-surface regions
   and in regions where the ray terminates near the `rayTerminationThreshold`. These
   dynamics shape the step count pattern independently of SDF resolution.

Mode 5 is valuable for one confirmed observation: **the banding exists in the
raymarch path before cascade reconstruction is involved**. It does not prove that
the cause is SDF grid coarseness specifically.

The "none — this is directly observable" entry in the evidence-against column was
wrong. Mode 5 is an indirect diagnostic. It rules out cascades; it does not rule in
SDF resolution as the unique cause.

---

## Finding 3 — Medium: analytic SDF on 64³ is not "too coarse to represent the Cornell Box smoothly"

**Accepted.**

The analysis made a blunt claim that 64³ cannot represent the Cornell Box smoothly.
For the analytic path, the situation is more precise:

- Each voxel stores the **exact** analytic distance to the nearest primitive
- Trilinear interpolation between exact samples of a piecewise-linear function
  (flat walls) is **exact** — no approximation error
- The only genuine discretization artifact is at the SDF = 0 boundary (surface
  crossing), where the voxel grid limits where the ray termination can land

So "64³ is too coarse" is a much weaker claim when the SDF generator is analytic
than when it is JFA. The experiment (increase resolution) is still valid, but the
expected magnitude of improvement is uncertain.

---

## Finding 4 — Medium: resolution increase is proposed too confidently as "the fix"

**Accepted.**

The analysis jumped from "mode 5 shows banding" to "increase DEFAULT_VOLUME_RESOLUTION
from 64 to 128" as if the diagnosis was complete. The correct experimental chain is:

1. Mode 5 shows banding exists before cascade reconstruction — **confirmed**
2. Possible causes include: natural SDF iso-contours, integer step quantization,
   adaptive-step dynamics, SDF resolution — **not yet separated**
3. Experiment: increase to 128³, capture mode 5 — does the banding change?
4. If yes: SDF resolution is a contributor
5. If no: the banding is from one of the other causes

The resolution increase is a reasonable first experiment, but "dominant root cause"
and "expected to eliminate or dramatically reduce" the banding are not yet supported.
These claims should be downgraded to "hypothesis to test."

---

## Finding 5 — Low: JFA-oriented language in surrounding docs is stale

**Accepted.**

`src/demo3d.h` header documentation still uses JFA-oriented framing from an earlier
architecture stage. The live implementation comment in `sdfGenerationPass()` is
accurate ("JFA is future work"), but the disconnect between the two creates confusion
when cross-referencing architecture docs with live code.

This does not require immediate code changes, but analysis documents should
explicitly state "analytic SDF path active" rather than citing the architectural
description in the header.

---

## Revised root-cause status

| Rank | Hypothesis | Evidence | Confidence |
|---|---|---|---|
| 1 | Mode 5 banding is natural Cornell Box SDF iso-contours + integer step count quantization | Mode 5 geo matches box SDF; stepCount is int | Medium — not yet separated from grid effects |
| 2 | 64³ SDF texture sampling contributes to step-count discretization | Plausible; analytic path reduces but does not eliminate grid effects | Low — analytic SDF weakens this significantly |
| 3 | Cascade blend linear weight kink at D4→D8 boundary | Smoothstep applied; visual comparison not yet done | Pending experiment |

**JFA approximation removed as a hypothesis** — the live branch does not use JFA.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| JFA attribution is wrong — live branch uses analytic SDF | High | **Accepted — all JFA-specific claims removed from analysis** |
| Mode 5 does not isolate SDF grid coarseness from other raymarch effects | High | **Accepted — mode 5 evidence narrowed to "banding exists before cascades"** |
| Analytic SDF on 64³ is not "too coarse" for a Cornell Box with flat walls | Medium | **Accepted — trilinear of exact samples is nearly exact for planar geometry** |
| Resolution increase is premature as "the fix" without isolating cause | Medium | **Accepted — downgraded to "experiment to run"** |
| JFA-oriented language in header docs is stale | Low | **Accepted — analysis docs will cite analytic path explicitly** |
