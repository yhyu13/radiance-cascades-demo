# Reply to Review 14 — Phase 5 Banding Analysis

**Date:** 2026-04-29
**Reviewer document:** `14_phase5_banding_analysis_review.md`
**Status:** All four findings accepted. F1 and F2 require doc fixes. F3 requires
priority table restructure. F4 is partially resolved by the same-session bug fix.

---

## Finding 1 — High: Phase 5e section is stale

**Accepted.** The banding analysis doc described the Phase 5e scaling as:

> "C0=D2, C1=D4, C2=D8, C3=D16 (already in the plan)"

This is wrong on two counts. Phase 5e is not just planned — it is already implemented.
And the D2/D4/D8/D16 path was explicitly rejected: D2 was found degenerate because
all 4 bin centers land on the octahedral equatorial fold (z=0 plane), causing severe
directional mismatch and wall color bleed. The live implementation is:
- C0=D4 (16 bins — unchanged from fixed-D baseline)
- C1=D8 (64 bins)
- C2=D16 (256 bins)
- C3=D16 (256 bins, capped)

**Fix applied** in `phase5_banding_analysis.md` — Strategy G section rewritten to:
- describe Phase 5e as *implemented*, not planned
- document the D2 rejection reason
- clarify the implication for banding: Phase 5e improves C1/C2/C3 angular fidelity
  but does not address C0 banding (Sources 1-3 are spatial resolution and binary
  shadow, not D count)

---

## Finding 2 — Medium: Strategy A overstated as "pure upgrade"

**Accepted.** The original text called SDF cone soft shadow:

> "This is a pure upgrade — no regressing the binary shadow quality. Zero cost increase."

Two errors:
1. It is not physically equivalent to a point-light shadow — it intentionally
   introduces a soft penumbra that the underlying light source does not support.
2. The visual result depends on artistic parameter `k`, which has no analytically
   correct value.

Calling it a "pure upgrade" conflates two distinct goals: correctly simulating a
point light (binary is correct) versus hiding the hard shadow edge (soft is better-
looking but not more accurate).

**Fix applied** in `phase5_banding_analysis.md` — Strategy A now carries an explicit
trade-offs block:

> "This is not physically equivalent to a point-light binary shadow — it introduces
> a smooth penumbra without changing the light model to an area light. It is an
> approximation for hiding the hard edge, not a physically accurate improvement."
> "The visual result depends on the artistic parameter k, which must be tuned per
> scene. There is no analytically correct k."

The section also now says "fastest way to improve the direct shadow's *appearance*,
not a fix for the underlying RC probe banding problem."

---

## Finding 3 — Medium: priority table conflates two separate goals

**Accepted.** The single priority table ranked Strategy A first globally, making it
appear to be the most important fix for Phase 5 banding overall. But the four-source
analysis in the same document established that Sources 2 and 3 (binary shadow in the
bake, probe-level discontinuity) are RC-specific artefacts. Strategy A does not touch
them — the indirect GI banding from probe discretisation survives even after the
direct shadow edge is soft.

**Fix applied** in `phase5_banding_analysis.md` — the single table is replaced with
two separate tables and a clarifying note:

**Table 1: Best immediate visual win (direct shadow appearance)**
- Strategy A first (SDF cone soft, fast, appearance improvement)
- Strategy E second (area light, physically correct)

**Table 2: Most direct fix for RC-side banding (probe signal quality)**
- Strategy B1 first (SDF cone shadow in bake shader — directly fixes Sources 2/3)
- Strategy C second (cascadeC0Res=64 — fixes Source 1 by brute force)
- Strategy D third (temporal accumulation — complements B when stochastic)

A sequencing note is added: apply A first for an immediate visual win, then B1 for
the underlying RC problem.

---

## Finding 4 — Low: opening context overstated as default branch state

**Partially accepted.** The reviewer cited `useCascadeGI(false)` at the time of
writing. As of the same-session bug fix, the constructor default was changed to
`useCascadeGI(true)`, so cascade GI is now ON by default. Shadow ray (`useShadowRay`)
was already `true`. However, `useDirectionalGI` remains `false` by default — the
Phase 5g directional atlas path requires explicit UI toggle.

**Fix applied** in `phase5_banding_analysis.md` — the opening context now reads:

> "This analysis assumes Phase 5h (shadow ray, default ON) and Phase 5g (directional
> GI, default OFF — must be enabled in the Cascade panel) are both active in mode 0
> as an evaluation configuration. Enable both for the full Phase 5 quality target
> before interpreting this document."

This is honest about the runtime default while still framing the analysis correctly.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Phase 5e section describes rejected D2 path and calls feature unimplemented | High | **Fixed**: rewrote Strategy G to match live D4/D8/D16/D16 implementation |
| Strategy A called "pure upgrade" — overstates the SDF cone shadow | Medium | **Fixed**: added trade-offs block; reframed as appearance approximation |
| Priority table conflates direct-shadow and RC-probe banding goals | Medium | **Fixed**: split into two tables with separate goals; added sequencing note |
| Opening context assumes 5g+5h as runtime defaults | Low | **Partially fixed**: useCascadeGI now true; useDirectionalGI still false by default; context paragraph updated |

The four-source breakdown and the "direct lighting in probes is architecturally
correct" conclusion are unchanged — the reviewer confirmed both as the document's
strong points.
