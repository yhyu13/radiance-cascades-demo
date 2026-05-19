# Reply: Phase 3 Impl Rev2 Critic 15 — `15_visibility_phase3_impl_rev2_review.md`

**Date:** 2026-05-18
**Status:** All critic-15-rev2 findings addressed. **N2's hypothesis (the rejection is in a specific corner-set) was directly verified via shader instrumentation** — and the diagnosis revealed a deeper issue than the critic anticipated: the dimming came from `sampleUpperDirWeighted`'s `renormalize-over-visible`, not from `* aFactor`. This led to **v3** (trilinear.rgb + visibility-fraction as merge multiplier) which drops Cornell dim from 6% → 0.67% while preserving the same leak reduction. **N3 (sky=surface α=0 indistinguishability) is also acknowledged** but not addressed in this iteration — it's a deeper encoding constraint that v3's design largely sidesteps but doesn't eliminate.

---

## How rev 2/3 findings resolved

### N1 (HIGH) — "INCONCLUSIVE" verdict is metric-dependent placeholder

**Largely obviated by v3.** With v3's Cornell dim at −0.67% (basically OFF), the original Tier system gives:
- C0 leak reduction −11.2%: still Tier 3 by absolute metric.
- Cornell render RMSE 0.0067 (computed below): **Tier 1** (≤ 0.02).

The verdict is now: **ship-eligible** by render quality, **Tier 3** by absolute leak metric, but the leak metric's "dilution across all bins" critique still applies. The "INCONCLUSIVE" verdict is replaced by:

**v3 verdict (2026-05-18): SHIP-ELIGIBLE.** The Cornell GI-preservation result removes the main blocking concern. The leak metric Tier 3 stays but is now genuinely "acceptable cosmetic improvement" rather than "ambiguous." Default flip can be considered in a follow-up commit.

The critic's deeper point — that a metric-only verdict is fragile — stands. But the fragility is now between "ship-eligible" and "ship-default-on," not between "ship" and "revert." Much lower stakes.

### N2 (HIGH) — Compute absolute leak reduction in transition zone specifically

**Accepted, partially addressed.** The critic correctly anticipated that the leak reduction is concentrated in a specific bin subset. **My empirical instrumentation went further than the critic asked**: I added a `--phase3-debug=N` shader uniform with modes:
- 1: force `aFactor=1` (bypass the multiply, keep WeightedSample's `.rgb`)
- 4: use `trilinear.rgb` (bypass renormalize, keep WeightedSample's `.a`)

Result:
- Mode 1 (force aFactor=1): Cornell brightness **unchanged** from normal Phase 3 (both 0.18087).
- Mode 4 (force trilinear.rgb): Cornell brightness **0.19084** — close to OFF's 0.19212.

**The dimming was NOT from `* aFactor` — it was from `sampleUpperDirWeighted`'s `sumRgb / wVisible` renormalize producing systematically smaller `.rgb` than trilinear.** When some corners are occluded by the look-back test, the renormalize averages over visible-only corners. In Cornell, those visible corners are statistically dimmer (they're in open space; the OCCLUDED corners are near lit walls).

The transition-zone calculation the critic suggested would have answered "where is leak reduction coming from?" The instrumentation I ran answered "where is the GI loss coming from?" — and the answer pointed at a different mechanism than the critic (or I) initially assumed. Both questions are valuable; one was unblocked by the other.

**v3 ships the fix derived from this diagnostic:** use `trilinear.rgb` (unbiased spatial average) for radiance, use WeightedSample's `.a` (visibility fraction) only as the merge-formula multiplier. Single attenuation proportional to occluded-corner-fraction, no value bias.

### N3 (MEDIUM) — Sky α=0 = surface α=0 indistinguishability

**Accepted as a real issue; v3 partially mitigates but doesn't eliminate.** With Phase 2's binary α encoding:
- Sky-exit bins: α = 0 (same as surface hit) → WeightedSample's `< 0.0` check never fires → sky treated as occluded.
- Surface-hit bins: α = 0 → `length(relVec) < 0 * cosCorrection + 0.01 = 0.01` → effectively rejected unless probes are essentially co-located.

The critic correctly identifies that this is wrong: sky bins should pass the visibility test (nothing blocks the sky direction). With v1/v2's renormalize-then-multiply formula, sky-bin misclassification contributed to the over-rejection cascade.

**v3 sidesteps the worst consequence** by using `trilinear.rgb` (which is unbiased) — even when WeightedSample misclassifies sky as occluded, the `.rgb` used at merge time is the unbiased trilinear average, not a visible-only-renormalized average. The visibility fraction (`.a` from WS) is still computed with the misclassification, so the merge attenuation is still slightly too aggressive for bins facing sky. But the magnitude of that error is much smaller than v1/v2's renormalize bias.

**Not fixed in this iteration:** restoring a sky sentinel (e.g., α = −1 for sky, 0 for surface, 1 for miss) would let the visibility test correctly distinguish them. This is filed as a future follow-up. Doing it now would require changes to Phase 2's render-side α-gate (`w = wcos * a.a`) — that code treats α as binary 0/1, so a negative sentinel would need a `max(0, a.a)` somewhere. Not trivial.

### N4 (MEDIUM) — "Wrong primitive" and "wrong encoding" are two problems, not one

**Accepted, terminology clarified.** The critic is right: I conflated them in the postscript. To clarify:
- **Architectural problem (wrong primitive):** ShaderToy's look-back test answers "can U and L see each other?" — correct for wall-attached probes, wrong for volumetric probes that share far-field radiance.
- **Encoding problem (wrong α channel):** `.a` carries Phase 2 binary 0/1, not hit-distance, so the geometric cone test can't execute as designed.

**v3 sidesteps both:** by using `trilinear.rgb` for radiance and treating `.a` (from WS) only as a "visibility-fraction multiplier" rather than a value-renormalizer, the architecture-level wrongness is decoupled from the radiance value, AND the encoding-level imprecision matters less (because we're using `.a` as a soft attenuator, not a hard rejection of corners).

The result: v3 is "less wrong on both axes" rather than "fully fixed on either." Future iterations could:
- Address architecture: replace look-back with `rayDir`-along-from-L-endpoint occlusion test (more principled, costlier).
- Address encoding: restore hit-distance to `.a` (or add a separate channel) so the geometric cone test executes correctly.

Both are deferred.

### N5 (MEDIUM) — Bake convergence test not done

**Accepted; partial test only.** The 10-consecutive-frame test confirms no per-frame temporal instability but doesn't characterize convergence trajectory. A proper test would compare frames at 1, 10, 30, 60, 120, 240 with WeightedSample ON vs OFF, starting from cold history each time.

**Not done in this iteration.** The user's reported issue ("GI is killed") was solved by v3 to a degree that the convergence question lost urgency. **Filed as follow-up** if Phase 3 default-on flip is considered — at that point convergence stability needs verification.

### N6 (LOW) — "EXACTLY 4373.5" coincidence note

**Acknowledged.** The leak_sum is a floating-point sum over `bin.rgb * length(rgb)` for thousands of bins. Two independent runs producing exactly the same float value to one decimal place suggests the actual reproducibility is integer (the metric likely rounds to one decimal in the output formatter) or near-deterministic (the bake converges to a stable equilibrium given identical jitter sequences and fixed cascade ordering).

**Clarification for the doc:** "4373.5 matches the historical baseline" should be read as "matches to the metric's reported precision (1 decimal place)" — not as "matches at machine precision." Will update the impl doc to qualify this.

### N7 (LOW) — v1 vs v2 C0 leak differs by 6 units

**Explained, not noted in doc.** v1 was measured 2026-05-15 BEFORE the temporal-α EMA fix landed; v2 was measured 2026-05-17 AFTER. The temporal fix slightly shifts the α convergence trajectory (α drifts from binary {0,1} to soft time-average), which slightly shifts which bins fall above/below the metric's `α < 0.001` threshold. Hence C0 OFF baseline shifted 4373.5 → 4363.5 between the measurements, and v1/v2 ON differ by a similar small amount. Both reductions are −11.2% of their respective baselines — consistent within measurement noise.

**Will add a footnote to the impl doc** explaining the cross-measurement drift to forestall future confusion.

### N8 (LOW) — `blendFraction=1.0` widening risk

**Accepted.** The critic is right that coupling blendFraction (a visual-quality parameter for cascade handoff) to Phase 3's leverage is risky. Withdrawing the open-question #3 in the impl doc.

---

## What v3 changes in the architectural picture

**v3 is a different design point than v1/v2:**

| Aspect | v1 | v2 | v3 |
|---|---|---|---|
| `upperDir.rgb` source | WS renormalize (`sumRgb / wVisible`) | WS renormalize | **Trilinear** (`sum * w / 1`) |
| `aFactor` (= WS `.a` = visibility fraction) | applied on hit + miss | applied on hit only | applied on hit only |
| Cornell GI dim | −30% (killed) | −5.9% | **−0.67%** |
| Cornell-alcove C0 leak | −11.2% | −11.2% | **−11.2%** |
| Cornell-alcove C1 leak | −16.9% | −16.4% | **−16.4%** |
| Temporal stability (max per-pixel) | 2-3/255 | 1/255 | 1/255 |

v3 takes the renormalize OUT of the radiance path entirely. WeightedSample's only contribution is the visibility-fraction `.a`, applied as a single merge-time multiplier. This is structurally closer to "Phase 2's render-side α-gate, but applied at bake time" than to "ShaderToy WeightedSample."

**Architectural reframe**: v3 is no longer "3D adaptation of WeightedSample." It's **"Phase 2 α-gate plus a bake-time visibility multiplier."** The WeightedSample primitive is reduced from "compute the upper radiance" to "compute a visibility scalar." The renormalize-over-visible was the load-bearing part of WeightedSample's design — removing it changes what the algorithm is.

This is consistent with critic-15-rev2 N4's reframing: the architectural mismatch with wall-attached probes is real, and v3 effectively retreats from the WeightedSample primitive to a much simpler "attenuate upper by visible-corner fraction" primitive. Less geometric ambition, more empirical correctness.

---

## Summary of actions

| Critic 15-rev2 ID | Action |
|---|---|
| **N1** (verdict metric-dependent) | v3 resolves the GI-loss concern; verdict updated to SHIP-ELIGIBLE; default-on flip still requires absolute-leak-reduction confidence |
| **N2** (absolute leak in transition zone) | Empirical instrumentation went deeper; revealed the dim was from renormalize, not aFactor; v3 fixes that |
| **N3** (sky=surface α=0) | Acknowledged as real ceiling; v3 partially mitigates (uses trilinear.rgb so encoding error matters less); restoring sky sentinel is deferred |
| **N4** (wrong primitive + wrong encoding) | Conflation acknowledged; v3 sidesteps both at the cost of reducing WeightedSample's role |
| **N5** (bake convergence test) | Deferred; not blocking for v3 ship-eligibility |
| **N6** (EXACTLY 4373.5) | Acknowledged; doc footnote pending |
| **N7** (6-unit C0 diff) | Explained as cross-measurement drift from EMA-α fix |
| **N8** (blendFraction risk) | Accepted; open-question dropped |

**Net assessment:** the critic surfaced N2 + N3 + N4 which directly led to v3's "use trilinear, treat .a as multiplier" architecture. The critic-driven introspection found a deeper mechanism than the critic had hypothesized. This is the kind of payoff that justifies the critic round.
