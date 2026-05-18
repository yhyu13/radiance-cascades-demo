# Critique: Phase 1.5 + Phase 2 Plan (Revision 1, post-critic-07 integration)

**Document reviewed:** `visibility_phase1.5_and_phase2_plan.md` (revision incorporating critic 07's 19 findings)
**Date:** 2026-05-14

---

## How well did the critic-07 fixes land?

| Critic 07 ID | Issue | Fix status in rev 1 | Assessment |
|---|---|---|---|
| H1 | Cone-radius scaling wrong (used `hitDist`, not `max(t, hitDist)`) | **Fixed** — §2.2 now uses `cone_r = max(t, hitDist) * uConeTan` with detailed explanation of why `max` is needed | Correctly fixed. The explanation of "past-wall surfaces use t (cone widens); near-probe surfaces use hitDist (defensive)" is clear and accurate. |
| H2 | θ_half derivation assumes uniform-area octahedral bins | **Fixed** — §2.3 acknowledges non-uniformity (up to ~2× area variation), offers two paths (conservative single value via slider; per-bin LUT fallback) | Adequate. The acknowledgment is honest. The fallback path is reasonable. One remaining gap: the plan doesn't say which direction the conservative single value should bias — over-occlude (use smallest solid angle → narrowest cone) or under-occlude (use largest → widest cone). This matters for the default starting point. |
| H3 | Bake-leak test scene doesn't exist (Cornell-orig is a closed box) | **Fixed** — §3.8 pre-flight task #2 now has three explicit options with cost/benefit analysis, recommends option B (Cornell-with-alcove .obj, ~1h asset work) | Well addressed. The options are concrete, the recommendation is clear, and the cost is estimated. |
| M1 | Path A cost estimate used 1 TFLOP instead of measured 0.47 TFLOP | **Fixed** — §2.5 recalibrated to +95–115% over Mode 0, honest expectation ~21 ms raymarch | Good. The document now says "+105% over Mode 0" in the TL;DR and §2.5 gives the detailed math. The honest framing is a major improvement. |
| M2 | Path B cost prediction asserted, not hedged | **Fixed** — §3.7 and §3.9 step 5 now explicitly say "predicted, not measured — verify in Step 5" | Adequate. The hedge is present. |
| M3 | Q2 forced-choice without data | **Fixed** — §4.2 adds a cost-tolerance table with FPS numbers at 1280×720 Sponza | Good. Now the user can see what +10%, +22%, Mode 0 cost actually means in frame rate terms. |
| M4 | Q1 asks user a question they can't answer | **Fixed** — §4.0 replaces Q1 with a prerequisite 30-min empirical test | Excellent. The most important fix. The plan now doesn't assume prior knowledge; it produces the knowledge first. |
| M5 | Path A doesn't address gi_blur/glDrawElements timing shifts | **Fixed** — §2.6 verification step 2 adds side-check: "if gi_blur or glDrawElements varies >5% across captures, flag as separate timing-noise question" | Adequate. |
| M6 | Decision tree doesn't handle Mode 4 + CLI during Path B | **Fixed** — §4.3 explicitly describes CLI/UI continuity and deprecation timeline | Good. Clear and practical. |
| L1 | Unnecessary trig roundtrip | **Fixed** — §2.3 gives the `sqrt(1-x²)/x` formula directly | Done. |
| L2 | Verification doesn't specify build/branch state | **Fixed** — §7 "Build/branch state" at bottom | Done. |
| L3 | Doc filename convention inconsistency | **Fixed** — §7 now uses `visibility_unified_plan_phase1.5_impl.md` | Done. |
| L4 | Per-cascade α missing from out-of-scope | **Fixed** — §5 now lists per-cascade α as Phase 2.5 | Done. |
| L5 | Behind-probe + far-lateral cone test case | **Fixed** — §6 open risks now has an explicit entry about this | Done. |
| E1 | TL;DR over-sells Path B vs Mode 0 | **Fixed** — TL;DR now says "matches Mode 0 ± single-digit %" and "strictly faster than Mode 4" | Done. |
| E2 | Two source-of-truth docs | **Fixed** — §3.1 says "corresponding section in visibility_unified_plan.md is historical" | Done. |
| E3 | Q3 preference-shaped | **Fixed** — Q3 removed; §4.1 decision branches are now entirely driven by empirical data + cost tolerance | Done. The tree is cleaner. |
| E4 | §6/§7 ownership overlap | **Fixed** — §3.8 promotes bake-leak scene to "pre-flight prerequisite" (not just a risk); §7 lists it as a Path B deliverable | Done. |
| E5 | No happy-path timeline | **Fixed** — §4.4 adds a day-by-day timeline | Done. |

**Overall integration assessment:** All 19 findings are addressed. Most fixes are substantive, not just textual. The plan is materially better than the pre-critic version.

---

## New findings in the revised version

### N1 (MEDIUM) — Path A's cost section §2.5 has an internal inconsistency

§2.5 says:

> "8 corners × D² × 6 ops ≈ 3000 extra ops/pixel"

But §2.2 lists the ops per bin as: 1 vec3 multiply, 1 vec3 subtract, 1 sqrt, 1 max, 1 multiply, 1 compare, 1 logical-and = **7 ops** (not 6). The `length()` call (sqrt) is an op, and the logical-and (`&&`) is an op. So 8 × 64 × 7 = 3584 ops/pixel, not 3000. The downstream 2.8B ops calculation and 6 ms estimate should be ~3.4B ops / ~7.2 ms. Predicted raymarch: ~22–23 ms (not 21–22 ms). This is a minor numerical correction but it propagates into the cost claim.

### N2 (MEDIUM) — §4.2 FPS table extrapolates from a single-frame RenderDoc capture

The table shows "60 FPS baseline" for Mode 0 at 1280×720 Sponza. But the Phase 1 RenderDoc capture gave **51.8 ms total frame** — that's ~19.3 FPS, not 60 FPS. The "60 FPS" number appears to be an assumed baseline, not the measured one. The table's FPS claims (54 FPS for Mode 4, 49 FPS for Path A) are therefore fictional unless the "baseline" refers to a different GPU/resolution than the Phase 1 measurements.

If the intended meaning is "if you're already at 60 FPS, Path A drops you to ~49 FPS" (i.e., relative scaling), that needs to be stated explicitly. As written, the table implies the Phase 1 setup runs at 60 FPS, which contradicts the measured 19.3 FPS.

### N3 (LOW) — §2.2 `lat_vec = delta - axial` introduces a vec3 subtract that wasn't in the original Mode 4

Mode 4 currently computes `t = dot(surfacePos - probeCenter, bdir)`. Path A needs the lateral component, which requires `lat_vec = delta - t*bdir`. But `delta = surfacePos - probeCenter` is not computed in the current Mode 4 code — only the dot product `t` is. So the cone correction actually adds: 1 vec3 subtract (`delta`), 1 vec3 multiply (`t * bdir`), 1 vec3 subtract (`lat_vec`), 1 sqrt (`length`), 1 max, 1 multiply, 1 compare, 1 logical-and = **8 ops** (not 6 or 7). This further bumps the cost estimate.

### N4 (LOW) — §3.4 alpha classification uses `hit.a` but doesn't define what `hit.a` means

```glsl
if      (hit.a > 0.0) alpha = 0.0;   // surface hit → opaque
else if (hit.a == 0.0) alpha = 1.0;  // in-volume miss → transparent
else                   alpha = 1.0;  // sky sentinel
```

What is `hit.a`? The document assumes the reader knows the existing bake shader's ray-result encoding, but a plan that will be the source of truth for Phase 2 implementation should define this. Is `hit.a` a custom encoding (e.g., 1.0 = hit surface, 0.0 = miss, -1.0 = sky)? Or is it the alpha channel of some texture? The classification logic's correctness hinges on what these values mean.

### N5 (LOW) — §4.1 decision table has a row that contradicts the cost-tolerance §4.2

The third row: "No leaks observed + user accepts only ≤ +10% frame cost → default-flip Mode 4 today (without cone correction; secondary RMSE marginal)."

But §4.2 shows Mode 4 at +10% frame cost gives "RMSE 0.019 vs Mode 3" — which the decision gate document already classified as "secondary marginal / lit_floor fails." Default-flipping Mode 4 without cone correction means shipping the known secondary failure. The plan's own Phase 1 decision gate said "DO NOT FLIP DEFAULT" for exactly this scenario. This row contradicts the Phase 1 verdict without explaining what changed.

If the logic is "no bake leaks → secondary RMSE failure becomes acceptable because the failure is aliasing-driven, not occlusion-driven," that reasoning needs to be explicit. As written, the row looks like it's ignoring the Phase 1 decision gate's conclusion.

### N6 (MEDIUM) — §3.5 `sampleProbeDir` reads RGBA but the existing `vec3` fetch sites need enumeration

§3.8 pre-flight task #1 says "Grep uDirectionalAtlas and produce a file:line table." But §3.5 only modifies `sampleProbeDir` to read RGBA. There may be other fetch sites for `uDirectionalAtlas` in the bake shader (`radiance_3d.comp`) that currently read `.rgb` and would silently get wrong values if the atlas format changes to RGBA before the bake code is updated. The plan's sub-commit split (2A format change, 2B bake+render changes) is supposed to handle this, but 2A is described as "no-op" — which means the atlas is RGBA but nobody writes α yet. Any fetch site that reads `.rgb` from a RGBA8 texture would get the correct RGB but the driver might pad differently. The plan should explicitly state that 2A is a **format-only change with no semantic change** and that all existing `.rgb` fetches continue to work identically on RGBA8 (which they do — `rgb` from RGBA8 is the same as `rgb` from RGB8, assuming no swizzle change). But this needs to be verified, not assumed — some drivers pack RGB8 differently from RGBA8.

### N7 (LOW) — §3.9 step 4 bake-leak quantification uses `sum(bin.rgb * bin.a)` but this is post-EMA

The bake-leak test says: "sum(bin.rgb * bin.a) across all bins of all probes in the occluded region. Pre-Phase-2: nonzero (leak). Post-Phase-2: ~0."

But α is fresh-only (never EMA-blended per §3.6). RGB is EMA-blended. So `bin.rgb * bin.a` at any given frame is: (EMA-averaged radiance) × (fresh hit/miss). If α = 0 (surface hit, opaque), the product is 0 regardless of what RGB carries — which is correct. But if the test reads the atlas **during a bake frame** (before EMA convergence), the RGB values are noisy and α is fresh, so the product may be nonzero due to temporal jitter, not a genuine leak. The test should specify: read the atlas **after bake convergence** (e.g., after N=60 EMA frames) to get stable RGB values, then check `bin.rgb * bin.a`.

### N8 (LOW) — §2.3 caveat about octahedral non-uniformity says "up to ~2×" but doesn't cite a source

The claim "bins near the equatorial fold cover up to ~2× the solid angle of bins at the octahedron's vertices" is stated as fact without derivation or reference. For a plan that's supposed to be the source of truth, this should either have an inline derivation (the octahedral Jacobian) or cite the RC paper / a known reference for the area ratio.

---

## Remaining structural concerns (not new, but persisting despite critic 07)

1. **Path A is effectively a dead-end regardless of quality outcome.** If Path A passes quality, the user must still accept +22% frame cost — and the plan's own §4.2 shows that Path B gives the same quality + bake-leak fix at Mode 0 cost. The only scenario where Path A makes sense is "the user needs the quality fix today and can't wait 2–4 days for Path B." The plan doesn't make this temporal urgency explicit. It should add: "Path A is only justified if the default-flip is time-critical; otherwise, Path B dominates on cost-quality tradeoff."

2. **The §4.0 prerequisite test has no failure escape hatch.** The plan says "if the user can't spend 30 min on this, default to Path B." But what if the user does the 30-min test and the results are ambiguous (some viewpoints look slightly leaky, others don't)? The decision tree's rows assume binary "leaks observed" / "no leaks observed." The plan should add a row for "ambiguous / mild leaks" and specify the escalation (likely: Path B, but with lower urgency).

---

## Summary

The revised plan is substantially improved over the pre-critic version. All 19 critic-07 findings are addressed, most with substantive fixes rather than textual patches. The remaining issues are:

- **Actionable:** N1 (op count inconsistency), N2 (FPS table contradicts measured data), N5 (decision row contradicts Phase 1 verdict), N6 (2A format-change safety needs explicit verification)
- **Minor but worth fixing:** N3 (delta subtract adds an extra op), N4 (hit.a undefined), N7 (bake-leak test should specify convergence), N8 (octahedral area ratio uncited)
- **Structural:** Path A is a dead-end unless time-urgent; §4.0 needs an "ambiguous result" escape hatch