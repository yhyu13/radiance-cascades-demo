# Reply: Decision-Gate Critic 07 — `07_visibility_unified_plan_phase1_decision_gate_review.md`

**Date:** 2026-05-14
**Status:** All 7 W findings accepted + 3 minor accepted. **No verdict change** — recommendation to NOT flip the default to Mode 4 stands. The critic's gaps are real (threshold un-justified, single-sample timing presented as conclusive, op-count waved off as causal, etc.) but they tighten the supporting evidence rather than overturn the conclusion.

**Important context the critic didn't have:** the follow-up Phase 1.5/Phase 2 plan ([visibility_phase1.5_and_phase2_plan.md](../../visibility_phase1.5_and_phase2_plan.md), revised against its own critic 07 with 19 findings) was produced **after** this decision-gate doc and **already addresses W4 (bake-leak test protocol, §4.0) and W5 (cone correction full algorithm + cost recalibration, §2.2 + §2.5)**. So the response to those two is "see the next doc in the chain" — but I'm also adding forward references in the decision-gate doc itself so a reader hitting it standalone is not stranded.

The remaining items (W1, W2, W3, W6, W7, minor) are local fixes to the decision-gate doc and applied at the bottom of this reply.

---

### W1 (HIGH) — 0.02 RMSE threshold not justified

Accepted. The threshold was carried forward from `visibility_unified_plan.md:126` ("per-region RMSE on three crops < 0.02") **without origin or perceptual derivation**. Looking back, it was a generic vision-paper convention smuggled in as if it were a project-specific bound. The fact that Mode 0 vs Mode 3 in lit_floor (0.024) also exceeds the threshold is direct evidence the threshold is mis-calibrated for that crop, not that Mode 4 has a real regression there.

The critic's proposed correction — `m4-vs-m3 ≤ m0-vs-m3 × 1.3` — is essentially what the doc already does informally in the "honest secondary metric" paragraph (we computed the +27%/+13%/equal deltas), but never promoted to the actual gate. **Doc revision:**

- Add a paragraph under the per-region results table acknowledging the 0.02 threshold's un-justified origin, the data showing it's too tight for high-frequency bright crops at 1280×720 single-frame, and proposing the corrected criterion `m4-vs-m3 ≤ m0-vs-m3 × 1.3` (or equivalently, "no worse than the natural per-mode baseline + 30%").
- Re-evaluate the three regions against the corrected criterion in a small additional table:
  - lit_floor: 0.030 vs 0.024 × 1.3 = 0.031 → **PASS** (barely)
  - shadowed_corner: 0.011 vs 0.0095 × 1.3 = 0.012 → **PASS**
  - right_wall_cols: 0.0077 vs 0.0078 × 1.3 = 0.010 → **PASS**
- **Net effect on the verdict:** secondary criterion is no longer a blocker if we accept the corrected threshold. The remaining blocker is the cost criterion (raymarch +50%). Verdict is unchanged but the failure mode is sharper.

This change has implications upstream: `visibility_unified_plan.md:126` should also be updated to use the relative criterion. Filing as a follow-up edit (will land in the same commit as this reply).

---

### W2 (HIGH) — Single-sample +50% raymarch claim presented as conclusive

Accepted. I had `gi_blur` flagged as "suspicious noise" two paragraphs above the raymarch +50% claim, then asserted +50% as fact without applying the same skepticism. The doc's "Honest residuals" section technically caveats it ("a 3-run average would tighten the confidence interval") but the body text presents +50% as established. Reasonable readers will take the body text, not the caveat.

**The recommendation (don't flip) is robust to ±10%** — even if the true delta were +40% or +60%, it still fails the implicit ~10% raymarch bound from `probe_visibility_acceleration_plan.md`. So the action is unchanged. But the **+50% point estimate** should be tagged as such. **Doc revision:**

- Wrap the timing table caption with "**Single-frame capture; treat per-pass deltas as point estimates with ~5–10% noise** (see `gi_blur` +17% anomaly below for evidence of noise floor)."
- Replace the `+50.1%` cell with `+50.1% (±~5%)` and add a footnote: "Single-frame capture. To obtain a tight confidence interval, capture N=3 frames and average — deferred to Phase 1.5 verification (per [visibility_phase1.5_and_phase2_plan.md §2.6](../../visibility_phase1.5_and_phase2_plan.md))."
- The Phase 1.5/2 plan already calibrates against this measurement (§1: "Measured GPU effective throughput ~0.47 TFLOP, derived from Mode 4's ~2.4B added ops over ~5.1 ms"). If the 3-run re-measurement materially shifts that number, both this decision-gate doc and the Phase 1.5/2 plan's cost predictions need recalibration.

---

### W3 (MEDIUM) — "Extra ops/pixel" calc presented as causal

Accepted. The "8 corners × D²=64 bins × (1 dot + 1 compare) per pixel = ~510 extra ops/pixel × 921k pixels = ~470M extra ops" calc is a **first-order arithmetic check** that the +5ms is in the right ballpark for the algorithm's added work — it is **not** a root-cause explanation of why the GPU spent +5ms in this pass. As the critic notes, real GPU performance is dominated by warp scheduling, memory latency, branch divergence, and register pressure, none of which the multiply-and-add ops/pixel model captures.

**Doc revision:** reframe the paragraph as a **sanity check**, not causation:

> "**First-order op-count sanity check** (not a root-cause explanation): 8 corners × D²=64 bins × (1 dot + 1 compare) per pixel ≈ 510 extra ops/pixel × 921k pixels ≈ 470M extra ops. At ~0.47 TFLOP-effective measured throughput in this shader (derived from this very measurement — circularity acknowledged), that's ~1 ms of pure-ALU work. The actual +5 ms is ~5× larger, consistent with the per-bin work being dominated by memory-side effects (cache pressure from D² fetches, branch divergence on the per-bin compare) rather than raw ALU. **Treat the op-count calc as 'the right order of magnitude,' not as 'root cause.'**"

This also explicitly notes the throughput-from-this-measurement circularity that critic 07 of the Phase 1.5/2 plan flagged separately (M1 — the headline 1 TFLOP figure I'd used in the plan's first draft was wrong; the calibrated 0.47 TFLOP came from this measurement).

---

### W4 (MEDIUM) — Bake-time leak untested but is the gating question for Path B

Accepted with cross-reference. The critic is right that the original doc has Path B contingent on a question (bake leaks) that wasn't directly tested. **This is already addressed in the Phase 1.5/2 plan** ([§4.0](../../visibility_phase1.5_and_phase2_plan.md) — "Run a 30-min manual bake-leak A/B session before any path branches"; [§3.8](../../visibility_phase1.5_and_phase2_plan.md) — "Bake-leak test scene authoring" with three options including a Cornell-with-alcove asset). So the work was deferred to the next doc in the chain, but a standalone reader of the decision-gate doc can't tell that.

**Doc revision:** add a new sub-section "Bake-leak test protocol — deferred to Phase 1.5 plan" right after "Honest residuals" with:

> "The unified plan's bake-time-leak gating question for Path B priority was **not** directly tested this round. A concrete test protocol is specified in [visibility_phase1.5_and_phase2_plan.md §4.0](../../visibility_phase1.5_and_phase2_plan.md) (30-min manual A/B at 3–4 viewpoints in Sponza-master and Cornell-orig, toggling `--visibility-mode={0,4}`, recording observations). A permanent regression scene (Cornell + free-standing interior partition) is scoped as Phase 2 pre-flight work in §3.8 option B (~1h asset authoring). Until that test runs, Path B priority over Path A is determined heuristically (no observed leak → Path A first; any observed leak → Path B mandatory)."

---

### W5 (MEDIUM) — Path A cone correction underspecified

Accepted with cross-reference. The decision-gate doc's one-sentence treatment of cone correction was indeed too thin to back the "half a day" estimate. **The full algorithm (revised against critic 07 H1+H2 of the Phase 1.5/2 plan) is in [visibility_phase1.5_and_phase2_plan.md §2.2–2.5](../../visibility_phase1.5_and_phase2_plan.md):** specific shader file (`raymarch.frag`), function (`sampleProbeDirDepthAware`), uniform (`uConeTan`), per-bin op count (~6 extra ops), recalibrated cost prediction (raymarch ~+105% over Mode 0, frame total ~+22%, **NOT free** as the decision-gate doc implied), octahedral non-uniformity caveat with per-bin LUT fallback, and a `uConeTan` sweep verification protocol.

The critic's read that "a few extra ALU ops" understates the cost was correct — recalibration in the Phase 1.5/2 plan (§2.5) puts cone correction at +5–6 ms on top of Mode 4's +5 ms, so Path A is roughly **2× more expensive than the decision-gate doc implied**. This materially changes whether Path A is "cheap de-risk" or "almost as expensive as Path B" — and the answer is closer to the latter.

**Doc revision:** rewrite the "Path A — Phase 1.5 cone correction" sub-section to:

- Replace the "few extra ALU ops" / "without further cost increase" claim with the recalibrated prediction (+5–6 ms additional raymarch, ~+22% total frame cost vs Mode 0).
- Add a forward reference: "Full algorithm, cost recalibration, and verification protocol in [visibility_phase1.5_and_phase2_plan.md §2.2–2.7](../../visibility_phase1.5_and_phase2_plan.md)."
- Replace the "half day of shader work" estimate with "half day of shader work + verification, **with a real possibility that Path A is wasted work** if (a) lit_floor RMSE turns out to be aliasing-driven, or (b) the user rejects +22% frame cost for the quality gain. Reading [visibility_phase1.5_and_phase2_plan.md §4.0–4.2](../../visibility_phase1.5_and_phase2_plan.md) before starting Path A is mandatory."

---

### W6 (LOW) — Crop coordinates in numpy slice notation

Accepted. The current `[500:680, 300:900]` notation locks the crops to 1280×720 implicitly. **Doc revision:** add a UV column to the crop table:

| Region | Slice (1280×720) | UV (x_min, y_min, x_max, y_max) | Pixels |
|---|---|---|---:|
| lit_floor | `[500:680, 300:900]` | (0.234, 0.694, 0.703, 0.944) | 108,000 |
| shadowed_corner | `[0:200, 0:300]` | (0.000, 0.000, 0.234, 0.278) | 60,000 |
| right_wall_cols | `[150:550, 950:1280]` | (0.742, 0.208, 1.000, 0.764) | 132,000 |

Also adding a note: "If re-running at a different resolution, derive pixel slices from the UVs above to keep the test region semantically equivalent."

---

### W7 (MEDIUM) — No absolute image quality assessment

Accepted. This is a real gap — the entire analysis is relative RMSE, no human read of the actual pixels. I went back and read [tools/phase1_sponza_m{0,3,4}.png](../../../../tools/) just now (cam.md viewpoint, 1280×720). Subjective notes:

- **Mode 0:** noticeably brighter overall — bright corridor floor, ceiling lit, columns brightly lit on the side facing the light. Visible cross-wall light through the right-side column gaps (the leak the visibility modes are meant to fix). Looks "warm and over-bright."
- **Mode 3:** dimmer overall, columns more shadowed, ceiling darker. The corridor floor reads as "indirect-only lit" rather than "direct + bouncy." More physically plausible for an enclosed corridor.
- **Mode 4:** visually very close to Mode 3 — cannot distinguish them at single-image inspection. The lit_floor crop at high zoom shows minor differences in step/railing edge brightness (consistent with the 0.030 RMSE). No banding, no hard discontinuities, no obvious artifacts. **Subjective verdict: Mode 4 looks like Mode 3 to the eye.**

**Doc revision:** add a new sub-section "Subjective image read" between Test 1 and Test 2 with these notes. Acknowledge that subjective and RMSE agree (Mode 4 ≈ Mode 3, both noticeably different from Mode 0) and that the lit_floor RMSE delta is **not visually objectionable**.

This is a Phase-1 limitation: subjective is one human, one viewpoint, one resolution, single frame. A formal user-test or FLIP-based perceptual metric would be the proper escalation. Filed in the Phase 1.5/2 plan §6 open risks already.

---

### Minor — emoji status markers, `tools/.env` rename hack, path conventions

All accepted.

- **Emoji markers:** kept as-is for now (this doc isn't likely to be re-edited many times) but added a note at the top of "Quick reference" that markers are emoji and may not render in plain-text editors. Future docs of this type can use `[PASS]/[FAIL]/[WARN]/[PENDING]` per the critic's suggestion.
- **`tools/.env` rename hack:** removed the operational-detail paragraph from the body of Test 2; moved to a new appendix "Operational notes for re-running" so the body of the doc stays focused on the decision.
- **Path conventions:** standardized the "Files produced this round" table to use repo-relative markdown links (`[tools/captures/phase1_m0.rdc](../../../../tools/captures/phase1_m0.rdc)` form) for everything. Where files are named with brace-expansion shorthand (`phase1_m{0,4}.rdc`), expanded to two rows so each link is clickable.

---

## Doc updates applied to `visibility_unified_plan_phase1_decision_gate.md`

Concrete edits landing in the same commit as this reply:

1. **W1** — New paragraph under per-region results table: 0.02 threshold un-justified, propose `m4-vs-m3 ≤ m0-vs-m3 × 1.3` corrected criterion, re-evaluate three regions (all pass under corrected criterion). Note that `visibility_unified_plan.md:126` should be updated upstream too.
2. **W2** — Timing table caption + footnote on the +50.1% raymarch cell, tagging single-sample point-estimate with ±~5% noise; defer 3-run averaging to Phase 1.5 verification.
3. **W3** — Rewrite op-count paragraph as first-order sanity check, not causation; explicitly acknowledge the throughput-from-this-measurement circularity; flag memory-side effects (cache, divergence) as the likely actual driver.
4. **W4** — New "Bake-leak test protocol — deferred to Phase 1.5 plan" sub-section after "Honest residuals" with forward reference to [visibility_phase1.5_and_phase2_plan.md §4.0 and §3.8](../../visibility_phase1.5_and_phase2_plan.md).
5. **W5** — Rewrite "Path A — Phase 1.5 cone correction" sub-section: replace "few extra ALU ops" / "without further cost increase" with the recalibrated +5–6ms / ~+22% frame cost; forward-reference Phase 1.5/2 plan §2.2–2.7; revise the "half day" cost estimate to acknowledge real possibility of wasted work.
6. **W6** — Add UV column to crop coordinates table; add re-run-at-different-resolution note.
7. **W7** — New "Subjective image read" sub-section between Test 1 and Test 2 with notes from looking at the actual `phase1_sponza_m{0,3,4}.png` captures.
8. **Minor** — Emoji-marker note at top of "Quick reference"; move `tools/.env` rename hack to a new "Operational notes for re-running" appendix; standardize "Files produced this round" table paths to markdown links.

Items NOT applied:

- The upstream `visibility_unified_plan.md:126` threshold edit. Filed as a follow-up — landing it in this commit would couple unrelated docs. Will land separately.
- 3-run timing re-measurement. Deferred to Phase 1.5 verification per the Phase 1.5/2 plan §2.6 step 2 — out of scope for this reply.

---

## Summary

The critic's biggest contribution was forcing me to confront that the **0.02 RMSE threshold has no perceptual basis** (W1) — the doc had been treating an arbitrary number as a real gate, then explaining away its failure rather than fixing the gate. With the corrected `m4-vs-m3 ≤ m0-vs-m3 × 1.3` criterion, the secondary RMSE actually passes; the only remaining gate-blocker is cost. The verdict (don't flip) is unchanged but the failure mode is now cleanly cost-only, which is a more honest reading of what the data actually shows.

W4 and W5 (bake-leak test, cone correction spec) are real gaps the critic was right to flag — but they were already filled by the follow-up Phase 1.5/2 plan, so the decision-gate doc just gains forward references. W2 (timing point-estimate), W3 (op-count is sanity check, not causation), W6 (UV coords), and W7 (subjective image read) are local doc fixes. Subjective image read confirms RMSE: Mode 4 is visually indistinguishable from Mode 3 at this viewpoint and resolution.

Net change to the decision-gate doc: **honest threshold (corrected criterion now applied), tagged point estimates, sanity-check framing for op-count, forward references to deferred work, UV coords for repro at other resolutions, subjective read on actual pixels.** No code changes — the implementation and the verdict are unchanged.
