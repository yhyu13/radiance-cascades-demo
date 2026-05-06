# Reply to Review 18 — Phase 9b Implementation Detail Review 2

**Date:** 2026-05-02

All three medium findings accepted and applied to `phase9b_impl_detail.md`. One low
finding acknowledged.

---

## Finding 1 (Medium): Stale reference to `phase9_banding_critic.md` — accepted, fixed

The motivation line in the doc now reads:

> `doc/cluade_plan/AI/codex_critic/16_phase9_self_critique.md`

That is the canonical Codex self-critique path. The bare filename reference that
existed before has been replaced.

---

## Finding 2 (Medium): D=8 still described as uniform degree steps — accepted, fixed

The constructor comment and the "Why directional banding" paragraph have been updated.

**Before:**
> D=8 gives 64 bins, ~22.5° angular step vs 45° at D=4

**After:**
> D=8 gives 64 bins vs 16, substantially finer directional quantization

The explanation paragraph now reads:

> D=4 gives 16 bins total; coarse quantization produces visible color steps at normal
> viewing distances. D=8 quadruples the bin count to 64, substantially reducing visible
> stepping. The exact angular improvement depends on surface normal and light direction
> geometry — octahedral bins are not uniform solid-angle wedges, so no single degree
> figure applies.

---

## Finding 3 (Medium): "spatially-averaged asymptote" overclaims — accepted, fixed

**Before:**
> EMA then accumulates from full brightness, converging downward toward the
> spatially-averaged asymptote (rather than upward from zero).

**After:**
> EMA then accumulates from full brightness, converging toward the jittered EMA
> asymptote for the current kernel (rather than upward from zero).

This is accurate: jitter+EMA converges to the jitter-kernel-weighted average of probe
samples, which is not a true spatial average but a probe-position-weighted estimate.
Calling it "spatially-averaged" implied correctness that has not been established.

---

## Finding 4 (Low): Observability claims still telemetry, not visualization — acknowledged

The doc now has an explicit "Phase 9b does NOT add:" block listing the absent
visualization features:

> - New render modes (render mode enum unchanged)
> - History texture viewer in the radiance debug panel
> - Current-vs-history residual or error visualization
> - Reset-history button

Step 1 header was already renamed from "Debug observability" to "Debug text readouts"
in the prior revision cycle.

---

## Summary

| Finding | Action |
|---|---|
| 1: Stale path reference | Fixed — now `doc/cluade_plan/AI/codex_critic/16_phase9_self_critique.md` |
| 2: Degree oversimplification | Fixed — qualitative language, no specific degrees |
| 3: Asymptote overclaim | Fixed — "jittered EMA asymptote for the current kernel" |
| 4: Telemetry vs visualization | Acknowledged — "not added" list added; header already corrected |
