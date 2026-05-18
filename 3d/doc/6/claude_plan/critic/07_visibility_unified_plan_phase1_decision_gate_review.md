# Critique: Phase 1 — Decision Gate Results: Mode 4 Default-Flip

**Document reviewed:** `visibility_unified_plan_phase1_decision_gate.md`
**Date:** 2026-05-14

---

## Strengths

1. **Brutally honest verdict.** The document opens with "DO NOT FLIP DEFAULT" and backs it with data, even though the implementation effort presumably wanted a positive outcome. This is rare and valuable in decision-gate documents.

2. **Clear separation of primary vs secondary criteria.** Primary (aggregate RMSE) passes; secondary (per-region) fails in one region — and the document immediately investigates whether the failure is a genuine regression or a threshold problem. The m0-vs-m3 baseline comparison (0.024) showing that Mode 0 also "fails" the same threshold in lit_floor is a strong argument that the 0.02 threshold is mis-calibrated for that crop, not that Mode 4 is broken.

3. **Two cost definitions confronted.** The document doesn't paper over the ambiguity between "raymarch pass cost" (+50%) and "total frame cost" (+10.5%). It presents both, identifies that the original plan was ambiguous, and notes that the stricter reading (probe plan's ~10% raymarch bound) is violated. This is the correct approach — don't pick the interpretation that makes the result look good.

4. **"Honest residuals" section.** Acknowledging single-run timing noise, single viewpoint, untested bake-time leaks, speculative cone-correction path, and lack of FLIP metric — all in one section. This prevents any reader from over-interpreting the results.

5. **Path A/B recommendation with clear gating logic.** The "try cone correction first, if it doesn't help skip to Phase 2" path is pragmatic. Half a day of shader work to de-risk is cheap compared to jumping straight to a 2–4 day architectural change.

---

## Weaknesses / Concerns

1. **The 0.02 RMSE threshold is never justified.** Where does 0.02 come from? Is it perceptual (just-noticeable-difference in sRGB)? Is it a statistical convention? The document shows it's too tight for high-frequency bright regions and too loose for dark ones — but the root problem is that the threshold was apparently chosen without reference to any perceptual model. A threshold that fails Mode 0 vs Mode 3 (the "good" baseline) is ipso facto mis-calibrated. The document should have either: (a) cited the threshold's origin and acknowledged its known limitations upfront, or (b) proposed a corrected threshold derived from the data (e.g., "secondary passes if m4-vs-m3 ≤ m0-vs-m3 × 1.3").

2. **Single-frame GPU timing is presented as conclusive for the +50% raymarch claim.** The document acknowledges ~5–10% noise, then says "+50% is well outside that noise band so the conclusion holds." This is hand-waving. A 50% delta on a single-sample measurement is not a statistically confident claim — you need at least 3 runs to establish that the mean is genuinely +50% and not, say, +40% or +60%. The document itself flags the gi_blur +17% as suspicious noise, but doesn't apply the same skepticism to the raymarch number. The recommendation to "not flip default" is probably correct regardless, but the specific +50% figure should be treated as a point estimate, not a fact.

3. **The "extra ops/pixel" calculation is suspiciously precise.** "8 corners × D²=64 bins × (1 dot + 1 compare) per pixel = ~510 extra ops/pixel × 921k pixels = ~470M extra ops." This is presented as a causal explanation for the +5ms, but GPU shader execution is not a simple multiply of ops × pixels — you have warp scheduling, memory latency, branch divergence, register pressure, and throughput saturation. The 1-dot-1-compare-per-bin model ignores that Mode 4 likely changes register occupancy and branch patterns, which can cause throughput drops far larger than the raw op count suggests. The document should acknowledge this is a first-order approximation, not a root-cause analysis.

4. **Bake-time leak is flagged as "untested" but is the gating question for Path B.** The document says Phase 2's interval atlas "addresses bake-time leaks that Mode 4 cannot — a problem this round didn't directly test for." This is circular: Path B is recommended if cone correction fails, but the key question that would make Path B urgent (bake-time leaks) hasn't been tested. The document should have at least described a quick test protocol (e.g., "set camera inside alcove, compare m3 vs m4 at wall behind camera light") so the next session can resolve this immediately rather than proceeding speculatively.

5. **"Path A — cone correction" is underspecified.** The document says "add a lateral-distance cone test (`tan(half_angle) × hitDist`) on top of the current signed projection." This is one sentence. For a half-day effort estimate, the document should sketch: which shader file, which function, which uniform/constant the cone half-angle comes from, and what the expected per-bin ALU cost is. Without this, "half a day" is a guess, not an estimate.

6. **Crop coordinates are given in numpy slice notation but not in screen-space or UV coordinates.** `[500:680, 300:900]` is implementation-specific. Anyone re-running the test with a different resolution, or validating visually, needs to know what fraction of the screen these crops cover. Adding normalized coordinates (e.g., `(0.23–0.70, 0.69–0.94)` in UV space) would make the regions reproducible across resolution changes.

7. **No absolute image quality assessment.** The entire analysis is relative (m4 vs m3, m0 vs m3). There's no mention of what the images actually look like — is Mode 4 visibly banding-free? Does the lit_floor look acceptable to a human? RMSE is a proxy; for a decision gate that blocks a default flip, someone should have looked at the pixels and recorded a subjective judgment.

---

## Minor

- The "quick reference" emoji status markers (✅⚠❌⏳) are convenient but fragile if the document is updated in a text editor that doesn't render emoji consistently. Consider plain-text markers (`[PASS]`, `[WARN]`, `[FAIL]`, `[PENDING]`).
- The `tools/.env` rename hack (`.env.bak.phase1`) to prevent Claude API calls during RenderDoc capture is an operational detail that should live in a runbook, not a decision-gate document. It's noise here.
- The file listing mixes absolute paths (`tools/captures/phase1_m0.rdc`) with relative markdown links (`[tools/analysis/...]`). Pick one convention.

---

## Summary

A strong, honest decision-gate document that correctly blocks a default flip on cost grounds. The main gaps are: (1) unjustified RMSE threshold, (2) single-sample timing treated as conclusive, (3) bake-time leak untested despite being the Path B gating question, (4) cone correction underspecified. The recommendation (don't flip, try cone correction first) is likely correct, but the supporting evidence needs tightening before the next iteration.