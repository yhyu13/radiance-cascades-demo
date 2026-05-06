# Reply to Review 29 — `phase14b_c0_coverage_analysis.md`

**Date:** 2026-05-03  
**Review:** `29_phase14b_c0_coverage_analysis_review.md`

All five findings accepted. The core correction across all of them is the same: the doc
presented a plausible causal chain as if it were a proven diagnosis. No pixel-level or
probe-level A/B exists yet; the evidence is correlational. The doc has been revised
accordingly.

---

## Finding 1 — Overclaims root cause (accepted, doc corrected)

The chain `low surfPct → hit/miss boundary oscillation → temporal shimmer` is a well-reasoned
hypothesis. It is **not** a proven root cause. The sequence captures show shimmer on the
colored walls and the stats show low C0 surfPct, but the two have not been isolated from
other active contributors: directional quantization at D=8, the EMA tuning choices, the GI
bilateral blur response, and upper-cascade merge differences (C1 at 77% surfPct still varies
per-probe).

**Doc changes:**
- "Root cause" heading replaced with "Primary hypothesis"
- All instances of "the root cause is" → "the most likely contributor is"
- "This is why the sequence is Stable at best" → removed (conclusion unsupported without
  pixel-to-probe correspondence)
- `uC0MinRange` section now explicitly labelled "proposed experiment, not settled fix"
- Verification section restated: sequence re-run is the required validation step, not an
  optional confirmation of an expected result

---

## Finding 2 — "Entirely due to reach" is too absolute (accepted, doc corrected)

The C0/C1 surfPct gap (30% vs 77%) is **primarily correlated** with the ray reach difference
(0.125 wu vs 0.5 wu), but "entirely" is wrong. C1 also differs in:
- world-space probe positions (coarser grid = different placement)
- interval start (`tMin = 0.125`, skipping the near-field zone where SDF step quality varies)
- spatial sampling density relative to scene geometry

These additional variables are not independently controlled in the current data. The reach
argument is the dominant factor but the evidence does not exclude the others.

**Doc change:** "The surfPct difference between C0 and C1 is **entirely** the ray reach" →
"Ray reach is the **dominant variable** in the surfPct difference between C0 and C1; other
changed variables (probe placement, interval start, spatial sampling density) are not
independently controlled and may also contribute."

---

## Finding 3 — Performance estimate overstated (accepted, doc corrected)

The "2–3× C0 bake only" estimate is based on sphere-marching step count reasoning (open
space → large SDF → large steps → sub-linear cost vs distance). That reasoning is directionally
sound but the actual multiplier depends on scene geometry, directional resolution, stagger
schedule, and temporal history patterns that have not been measured.

**Doc change:** The cost section is reframed from a near-established claim to an "expected
order-of-magnitude increase to be measured via `cascadeTimeMs` after implementation." The
framing "acceptable increase" is removed — acceptability should be determined by the
measurement, not the estimate.

---

## Finding 4 — Fix is a cascade hierarchy redesign, not a local tweak (accepted, doc corrected)

This is the most significant correction. Extending C0 `tMax` from 0.125 to 0.5 changes:
- C0 stops being "very near field" — it covers the same range as C1
- The blend zone width grows from 0.0525 wu to 0.24 wu (≈ 5×)
- The C0/C1 division of labor is materially rebalanced; C0 takes over hits that were C1's
  responsibility
- The cascade hierarchy structure — fine/coarse separation that motivated having 4 levels —
  is no longer respected between C0 and C1

These consequences are real even if the cascade semantics remain correct (which they do —
the merge path is safe). The doc was wrong to frame `uC0MinRange` as a "local stability
tweak." It is a **structural cascade hierarchy experiment** and should be framed as such.

**Doc change:** Section title "The correct structural fix" → "Proposed structural experiment:
`uC0MinRange`". Introduction paragraph rewritten to acknowledge the hierarchy implications
and that this is an A/B to validate, not a tuning of a known-correct parameter.

---

## Finding 5 — Resolution increase claim is too strong (accepted, doc corrected)

The mathematical claim that going 32³→64³ halves `tMax` (because `baseInterval =
volumeSize/probeRes`) is correct at the metric level. But the practical conclusion — "resolution
increase is definitively worse for the problem" — does not follow. Higher density changes
spatial sampling in ways not captured by `tMax` alone: narrower probe spacing may improve
surface detection for very thin geometry features, and the atlas resolution changes too.

The specific claim "increasing resolution makes things worse" is only validated for the
`surfPct` metric under the current interval formula. It does not generalize to "worse visual
output overall."

**Doc change:** "Increasing probe resolution makes surfPct worse" restated as "Under the
current interval formula, doubling probe resolution halves `tMax`, which is expected to
**worsen C0 surfPct as a metric**. The practical visual impact is not settled by this alone
and would require separate measurement."

---

## Summary of doc changes

| Section | Change |
|---|---|
| Problem statement | "root cause" → "primary hypothesis" throughout |
| Why C1 is 77% | "entirely due to reach" → "reach is the dominant variable; other variables not independently controlled" |
| Miss path / instability | Explicitly labelled as causal hypothesis, not proven chain |
| The proposed fix | Retitled "structural experiment"; hierarchy redesign implications stated explicitly |
| Performance | Reframed from near-established multiplier to expected range to be measured |
| Resolution increase claim | Softened to "expected to worsen C0 surfPct as a metric; practical visual impact not settled" |
| Verification section | Restated as validation of a hypothesis, not confirmation of an expected outcome |

No code changes required — Phase 14b is still "NOT YET IMPLEMENTED."
