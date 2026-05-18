# Reply: Phase 2 Impl Critic 09 — `09_visibility_phase2_impl_review.md`

**Date:** 2026-05-14
**Status:** All 9 findings accepted. **W1 and W2 are the load-bearing fixes** — the impl doc described the symptom of the v1 over-darkening but didn't diagnose the mechanism precisely, and the "partially fulfilled" framing for bake-time leaks was generous (the honest statement is "render-time fixed; bake-time NOT delivered"). W4 surfaces a real coupling note I missed (v5's renormalization is locked to the current leaky bake; if the bake is fixed later, v4 becomes the correct choice). The remaining findings are documentation-precision improvements.

Code/doc changes applied at the end. **No code changes** — this round is doc updates only; the implementation itself is unchanged. The critic doesn't propose any code fixes.

---

### W1 (HIGH) — v1 over-darkening root cause not diagnosed precisely

Accepted. The doc said "chained α-multiply across C3→C2→C1→C0 terminates radiance at the first opaque cascade, killing far-field multi-bounce" — that's the symptom. The mechanism is more specific:

**Why v1 over-darkened (the diagnosis I should have written):**

In geometrically dense scenes (Sponza), most C0 probes have most bins hitting walls within the C0 interval. The textbook interval merge says: surface hit → α=0 → `rad = thisRad + 0×upperDir = thisRad` (only local hit-radiance, no upper contribution). For a probe near a wall, this means EVERY surface-hit bin loses the upper-cascade radiance, even though the upper cascades represent geometry FARTHER OUT than the wall (cascade hierarchy is distance-bounded, not visibility-bounded).

The original (pre-Phase-2) bake formula `rad = hit.rgb * l + upperDir * (1-l)` was implicitly performing a **cascade-handoff smoothing** — when the hit happened near the far edge of the interval (smoothstep `l < 1`), some of the upper cascade's contribution was mixed in. This wasn't the paper's interval merge; it was a heuristic that happened to preserve multi-bounce energy at cascade transitions because the upper cascade's data is "what's beyond the interval boundary," not "what's beyond the wall."

The interval merge formula is **geometrically correct under the assumption that probes along the ray are stacked at increasing distances, all looking in the same direction.** With non-co-located probes (Phase 5d), the upper cascade's probe is OFFSET from this cascade's probe — the upper cascade's `bdir` ray from a different position might not see the same wall. So the upper cascade's radiance for that bin direction can legitimately represent radiance the wall didn't block (because the upper probe sees past the wall from a different angle).

The textbook interval merge ignores this — it treats upper cascade as "what's beyond MY ray's first hit," but with offset probes, that interpretation is wrong. The original smoothstep heuristic accidentally preserved the right behavior by mixing in upper-cascade contribution near the interval boundary.

**Implication for future "full interval merge" work:** the fix is NOT to delete the smoothstep, it's to **derive an α and a radiance formula that account for non-co-located probes.** Possible directions: probability-weighted α based on probe-to-wall geometry, or a "cone of upper-cascade visibility" test that captures whether the upper cascade probe can see past the local wall. Neither is trivial; both are research-paper-level work.

**Doc revision:** added a "Why v1 over-darkened" subsection with this diagnosis. Future revisitors now have a starting hypothesis instead of repeating the same failure.

---

### W2 (HIGH) — Bake-time leak fix NOT delivered; "partially fulfilled" framing is generous

Accepted. The doc soft-pedaled this. Honest statement:

> **Render-time leaks: FIXED.** The α channel at render time correctly gates per-bin contribution; surfaces don't pick up cross-wall radiance through directly-occluded bins.
>
> **Bake-time leaks: NOT FIXED.** The bake's `rad = hit.rgb * l + upperDir.rgb * (1 - l)` formula is unchanged. When the smoothstep `l` is < 1 (within `blendWidth` of the interval far edge), upper-cascade radiance is mixed into this bin's stored `rad`. If the upper cascade's radiance includes contribution from "beyond the wall," that contribution lands in the atlas. The render-side α=0 then **hides** this leaked value from the rendered output — the user doesn't see it — but **the leaked value still lives in the atlas**.

A second consumer of the atlas (e.g., the existing "atlas debug viewer" render mode that shows raw atlas tiles) would display the leaked values directly. Any future feature that reads atlas RGB without honoring α would resurrect the leak.

**The pragmatic variant traded "fix bake-side leak" for "don't over-darken Sponza."** That trade was the right call for shipping today, but it should be documented as a NOT-DELIVERED, not as "partially fulfilled."

**Doc revision:** rewrote the framing. Changed "Bake-time leaks NOT fully fixed" (which read as "mostly fixed, a few residuals") to "Bake-time leaks: NOT FIXED — the render-side α-gate hides them, but the atlas still contains them."

(Re critic's middle paragraph in W2 — the analysis of "near vs far cascade bins" is partially right but somewhat mixed up. The simpler statement is what I just wrote: the smoothstep formula puts upper-cascade radiance into the bin's stored rad value; α=0 hides it at render time but doesn't remove it from storage.)

---

### W3 (MEDIUM) — Sky α=0 and surface α=0 indistinguishable

Accepted. Both encode α=0 with different semantic intent. For render output the renderer treats them identically (occluded = contributes zero), which is correct. But for **future Phase 2.5 soft-α work**, this matters: you'd want surface bins to use a smoothstep-derived soft α (gradual occlusion at near-surface boundaries), but sky bins should stay hard at α=0 (no soft transition into the void).

Possible resolutions (none implemented in this commit):

- **Sentinel-α encoding**: use α = `-1.0` for sky (RGBA16F supports negative values; existing renderer treats `< 0` differently from `0` if explicitly checked).
- **Reserve α range**: sky writes `α = 0.0` strictly; surface hit writes `α = 0.0001` (tiny positive epsilon). Soft α extends from 0.0001..1.0 for surfaces; 0.0 strict means sky.
- **Separate metadata texture**: 1-bit sky-vs-surface mask. Adds memory; clean separation.

**Doc revision:** added under "What was NOT done" → "Sky/surface α encoding ambiguity blocks Phase 2.5 soft-α work without a resolution."

---

### W4 (MEDIUM) — v4 is geometrically correct; v5 is empirically tuned to the leaky bake

Accepted. This is a real coupling note that I should have made explicit. v4 (`wsum += wcos`) is the **textbook hemisphere integral** — sum cosine-weighted radiance over visible directions, divide by total cosine weight. Occluded directions correctly contribute 0 to the integral.

v5 (`wsum += wcos × a.a`) is the "renormalize over visible directions" formula from Modes 1/2/3/4 — it artificially concentrates the visible-direction radiance, brightening the result relative to v4. v5 matches Mode 4 because Mode 4 ALSO renormalized this way, and Mode 4 was over-bright relative to a true hemisphere integral.

**The shipped choice (v5) is COUPLED to the current (leaky) bake.** If a future "full interval merge" fixes the bake to produce energy-conserving radiance values (no smoothstep over-bright bias), v5's renormalization would amplify the now-correct values into over-bright territory. v4 would become the correct choice at that point.

**Doc revision:** added a "Coupling" subsection under the v3-v5 iteration log:

> **v5 is coupled to the current bake formula.** v5's `wsum += wcos × a.a` renormalization preserves the over-bright bias the pre-Phase-2 bake encoded via the smoothstep blend. If a future commit replaces the bake formula with energy-conserving interval composition, v5 would over-brighten the corrected values; v4 (`wsum += wcos`) would then become the correct hemisphere integral. **Don't change the bake formula without simultaneously switching the render normalization to v4.**

---

### W5 (MEDIUM) — `reduction_3d` +42% dismissed too quickly

Accepted. The 42% is well outside the 5-10% single-frame noise band cited elsewhere. ~280 μs absolute is small but suspicious. The reduction pass computes per-probe statistics from the atlas; with α now meaning "transparency" instead of "hit distance," any reduction-shader code that summed or thresholded on α values would behave differently.

I haven't audited `reduction_3d.comp`. The honest statement: this is a possible latent bug, not confirmed-noise.

**Doc revision:** changed "doesn't affect the verdict" to "**flag for follow-up: re-run with N=3 averaged captures; if still elevated, inspect reduction_3d.comp for any code path that reads or thresholds atlas alpha (the channel changed semantics from hit-distance to transparency in 2B).**"

---

### W6 (LOW) — Single-frame timing

Accepted. Same Phase 1 critique, same answer: 3-run average would tighten the confidence interval. The +12.9% raymarch is well outside noise band so the verdict is robust, but the specific number is a point estimate.

**Doc revision:** added a one-line caveat under the timing table.

---

### W7 (LOW) — "RMSE 0.000000" implies float precision the test doesn't have

Accepted. PNG captures are 8-bit per channel; "RMSE 0.000000" really means "every pixel byte-identical." The wording was lazy.

**Doc revision:** changed "RMSE 0.000000" to "**pixel-identical PNG (every byte matches)**" in the verification section.

---

### W8 (LOW) — v3 and v5 listed as separate variants but identical

Accepted. The iteration log has them as separate rows for narrative completeness (showing the v4 reversion), but they're the same code. Confusing.

**Doc revision:** collapsed v3 and v5 into one row labeled "v3/v5 (shipped — v5 is a re-run of v3 to confirm v4 reversion)" with a single set of numbers.

---

### W9 (LOW) — Bake-leak quantitative test deferred; visual A/B insufficient

Accepted, and this strengthens the W2 finding: the doc claims "render-side leak fix works" but the only evidence is visual inspection at one viewpoint. The formal test (atlas inspection via RenderDoc + `sum(bin.rgb × bin.a)` in occluded region) is the only way to:

1. **Prove the α-gate actually zeroes out leaked radiance at render** (visual can't distinguish "α-gate worked" from "leaked radiance is below visible threshold").
2. **Quantify the bake-time leak that's still in the atlas** (W2 — even after Phase 2, the bake stores leaked radiance; the formal test would measure how much).

**Doc revision:** strengthened the "Honest residuals" entry from "deferred" to "**deferred — visual A/B is NOT sufficient evidence for either the render-side leak fix or the absence of bake-side leaks. The formal test is the only way to validate Phase 2's headline claims; not running it leaves Phase 2 with weaker evidence than the plan called for.**"

---

## Doc updates applied to `visibility_phase2_impl.md`

1. **W1** — added "Why v1 over-darkened" subsection diagnosing the mechanism (non-co-located probes + textbook merge incompatibility) and pointing to research-level future work for a corrected interval merge.
2. **W2** — rewrote "bake-time leaks" framing throughout: "NOT FIXED — render-side α-gate HIDES them but atlas still contains them." Affects TL;DR, "What Phase 2 actually does", "Honest residuals."
3. **W3** — added "Sky/surface α encoding ambiguity blocks Phase 2.5 soft-α" entry under "What was NOT done."
4. **W4** — added "Coupling" subsection under iteration log: v5 locked to current leaky bake; future bake fix requires switching to v4.
5. **W5** — escalated `reduction_3d +42%` from "doesn't affect verdict" to "flag for follow-up; possibly latent bug from α semantic change."
6. **W6** — single-frame caveat added to timing table.
7. **W7** — "RMSE 0.000000" → "pixel-identical PNG (every byte matches)."
8. **W8** — collapsed v3/v5 in iteration table to one row with explanation.
9. **W9** — strengthened the bake-leak-test deferral framing in "Honest residuals."

Items NOT applied (filed for future):
- The actual bake-leak quantitative test (W9) — requires RenderDoc capture work; deferred per W9's nature.
- Investigation of `reduction_3d` +42% (W5) — requires multi-run averaging + shader audit.
- Sky/surface α disambiguation (W3) — requires Phase 2.5 design work.
- Full interval merge revisit (W1) — research-level; out of scope this round.

---

## Summary

The critic correctly identified that my impl doc described what happened during v1→v5 iteration without diagnosing WHY v1 failed precisely (W1) and softened the bake-leak failure into "partially fulfilled" (W2). Both are real documentation gaps that would have left future readers stumbling. The W4 coupling note is a genuine surprise — I hadn't realized that v5 is locked to the current bake formula and would need re-derivation if the bake is ever properly fixed.

Net: **stronger diagnosis, more honest framing, explicit coupling notes, four future-work items filed.** The implementation itself is unchanged; only the docs around it improve.
