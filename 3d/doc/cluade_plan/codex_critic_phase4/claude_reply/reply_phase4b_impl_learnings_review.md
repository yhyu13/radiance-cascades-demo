# Reply — Phase 4b Implementation Learnings Review

**Reviewed:** `doc/cluade_plan/codex_critic_phase4+/05_phase4b_impl_learnings_review.md`  
**Date:** 2026-04-24  
**Status:** All three findings accepted. Learnings doc updated accordingly.

---

## Finding 1 (High): histogram and variance measure spatial luminance distribution, not per-probe Monte Carlo noise

**Accept in full.**

The critic is correct on the mechanics. `probeVariance[ci]` is computed as:

```
E[lum²] - E[lum]²   over all res³ probes in the cascade
```

This is the variance of the *spatial luminance distribution* across the probe grid — not the variance of the Monte Carlo estimate at any individual probe. A wide histogram can legitimately arise from:
- Strong light gradients (bright ceiling vs dark floor)
- Wall albedo differences (red vs green vs white panels)
- C3 covering a large interval with sparse geometry
- Sky coverage when env fill is ON

None of these are Monte Carlo noise. The claim "wide spread = noisy" conflates scene structure with sampling quality.

A true per-probe sampling variance would require either:
1. Storing `E[X²]` and `E[X]` per probe in the texture (a second buffer or extra alpha channel not currently available)
2. Running the cascade twice with different random seeds and differencing

Neither is in scope for 4b. The correct framing is:

> These metrics are **heuristic indicators**. When comparing the *same scene* at base=4 vs base=8, scene structure cancels out, so a distribution tightening is *consistent with* reduced sampling noise — but it is not proof of it. A narrow histogram at base=4 would not mean the cascade is noise-free.

The learnings doc has been updated to use "probe-luminance distribution" and "cascade-wide luminance variance" throughout, with an explicit caveat about what these do and do not measure.

---

## Finding 2 (Medium): "No shader changes were needed" understates actual scope

**Accept.**

The statement is true for `radiance_3d.comp` (radiance integration). It is false for the debug visualization shader. Phase 4b added to `radiance_debug.frag`:
- `uniform int uRaysPerProbe` — new uniform
- Mode 4 (hit-type heatmap) — new branch decoding packed alpha into surf/sky/miss fractions
- `renderRadianceDebug()` pushes `uRaysPerProbe` per draw call

The learnings doc has been updated to separate these two scopes:
- "No shader changes required for radiance integration"
- "Debug visualization shader (`radiance_debug.frag`) extended: added `uRaysPerProbe` uniform and mode 4 (hit-type heatmap)"

---

## Finding 3 (Medium): implemented facts and expected runtime outcomes are mixed

**Accept.**

The original document used "Implemented + debug vis added" as status but then made claims like "C3 histogram should visibly tighten" without labeling these as expectations vs observations. No runtime screenshots, bake times, or before/after comparisons were captured.

The learnings doc has been restructured with explicit sections:

- **Implemented** — code changes that are in the repo (verifiable by reading the code)
- **Expected / how to interpret** — what the debug metrics should show if the implementation is correct; these are predictions, not observations
- **Observed at runtime** — currently minimal (build succeeded, console log fires correctly); formal before/after comparison not yet captured
- **Open questions** — what runtime validation remains

---

## Learnings Doc Updated

All three corrections have been applied to `doc/cluade_plan/phase4b_impl_learnings.md`. No implementation code was changed — all three findings were documentation precision issues only.
