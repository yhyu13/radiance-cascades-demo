# Critique: Phase 3 — Bake-Side Leak Fix Implementation (3D WeightedSample) Rev 2/3

**Document reviewed:** `visibility_phase3_impl.md` (rev 2 with critic-14 H1 fix applied; postscript with v2 targeted scope)
**Date:** 2026-05-18
**Prior critics:** [critic 11](critic/11_visibility_phase3_plan_review.md) (plan-level W1/W2 channel semantics), [critic 14](critic/14_visibility_phase3_impl_review.md) (H1 bit-exactness, H2 contradictory verdict)

---

## How critic-14 fixes landed

| Critic 14 ID | Issue | Fix status | Assessment |
|---|---|---|---|
| H1 | Default-OFF not bit-exact (unguarded `* upperDir.a`) | **Fixed** — mode-gated `aFactor = (uUseWeightedSample != 0) ? upperDir.a : 1.0` | Correctly fixed. OFF baseline now matches historical 4373.5 EXACTLY — the strongest available evidence. The multi-line shader comment marking this as load-bearing is appropriate. |
| H2 | Tier 3 + "metric is wrong" contradictory | **Fixed** — verdict rewritten as "INCONCLUSIVE pending revised metric" | Correctly resolved. The honest position is now clear: "Phase 3 v1 doesn't pass the plan's gate, AND the gate may not be a fair test." Both facts acknowledged; no false contradiction. |
| M1 | Diagnosis unverified | **Acknowledged** — flagged as explicit follow-up before v2/v3 | Not yet verified, but the diagnosis section is clearly labeled as hypotheses. Acceptable for an inconclusive-result doc. |
| M2 | Naming mismatch uUpperCellSize vs uUpperProbeCellSize | **Acknowledged** — footnote in impl doc | Minor; adequate. |
| M3 | Cone average-area ± per-bin variance | **Acknowledged** — noted in diagnosis | Adequate. |
| L1 | CLI accepts ≥2 as ON | **Acknowledged** — deferred | Fine for current scope. |
| L2 | GUI tooltip | **Acknowledged** — deferred | Fine. |

All critic-14 items addressed. The doc is materially better than rev 1.

---

## How critic-11 (plan-level W1/W2) was resolved in the impl

**Critic 11 W1/W2 (HIGH):** Phase 2's α-transparency (0/1) vs Phase 3's hit-distance expectation — conflicting `.a` channel semantics.

The impl resolved this by **reading the upper cascade's `.a` as Phase 2's binary α (0 = surface hit, 1 = miss)** and interpreting it through the geometric visibility test differently than the plan anticipated:

```glsl
float lProbeRayDist = texelFetch(uUpperCascadeAtlas, ...).a;
bool visible = (lProbeRayDist < 0.0) || (length(relVec) < lProbeRayDist * cosCorrection + 0.01);
```

With Phase 2's encoding:
- Surface-hit bins: `lProbeRayDist = 0` → `length(relVec) < 0.01` → visible only if probes are <0.01 units apart → effectively **occluded** (almost always invisible).
- Miss bins: `lProbeRayDist = 1` → `length(relVec) < 1 * 0.248 + 0.01 = 0.258` → visible if probes are within 0.258 units.
- Sky sentinel: `lProbeRayDist = 0` (same as surface hit) → the `< 0.0` check NEVER fires → sky is treated as surface hit (occluded).

This is **not** the WeightedSample algorithm as specified in the plan. The plan expected `.a` = hit-distance (real-valued, negative for sky). The impl got `.a` = binary α. The result is a degenerate form where:
- Surface-hit upper bins → always rejected (correct: there's a wall there).
- Miss upper bins → accepted within a 0.258-unit radius (approximate: the "cone test" becomes a fixed-radius proximity test, not a geometric cone).
- Sky bins → always rejected (different from plan spec, which said sky should always pass).

**This is a working but compromised algorithm.** It produces real effects (−11.2% C0 leak, 2% global darkening) but its geometric semantics are not what the plan specified. The document should acknowledge this more explicitly than it does — the "Plumbing notes" section explains the uniform simplification, but nowhere does it address that `.a` channel semantics changed from the plan's hit-distance assumption to Phase 2's binary α.

**The postscript/v2 section partially addresses this** by identifying that WeightedSample is the "wrong primitive for volumetric probes" — but this is a *different* diagnosis than "the `.a` channel doesn't carry hit-distance." Both are true, and the document should distinguish them. The channel-encoding issue is an implementation constraint; the primitive issue is an architectural constraint. They compound each other.

---

## New findings in rev 2/3

### N1 (HIGH) — The "INCONCLUSIVE" verdict is operationally unstable: the next session can flip it either way depending on metric choice

The document commits to "INCONCLUSIVE pending revised metric." But no specific revised metric is defined. The "What's next" section says "revise the leak metric so it weights bins by `(1 - l)`" — but this is one candidate, not a committed instrument. If the next session implements a `(1 - l)`-weighted metric and it shows >50% reduction on leverage bins, the verdict flips to "ship." If the next session implements a different metric (e.g., per-cascade directional-bin `upperDir.a` weighted by trilinear corner count), it might show <50%. The INCONCLUSIVE verdict is **metric-dependent** — which means it's not a verdict at all, it's a deferred decision that will be determined by the choice of measurement tool, not by the underlying reality.

This is a structural problem: the plan's Tier system is defined by the OLD metric, and the impl says the old metric is wrong, but no new metric is defined. Until a new metric is formalized and its thresholds are set, the INCONCLUSIVE tag is meaningless — any future session can make Phase 3 "pass" or "fail" by choosing the metric that produces the desired outcome.

**Fix:** Before closing this iteration, formally define: (a) the revised metric formula, (b) the Tier 1/2/3 thresholds under the revised metric, and (c) the rationale for those thresholds. Otherwise "INCONCLUSIVE" is a placeholder, not a verdict.

### N2 (HIGH) — v2's targeted scope (miss branch skips `* aFactor`) is a semantic change that the document under-explains

The v2 change is described as:

> the miss branch (`hit.a == 0`) now uses `rad = upperDir.rgb` unconditionally, dropping the `* aFactor` factor. Hit branch (`hit.a > 0`) keeps `* aFactor`.

This is described with a 3-bullet rationale (miss = no wall, hit-close = redundant, hit-transition = correct attenuation). The rationale is sound, but the **implication for the bake-leak metric** is not analyzed. v2's C0 leak shows −11.2% — essentially identical to v1's −11.2%. This means the miss-branch `aFactor` was NOT contributing to leak reduction; removing it preserved the same leak metric. But v2's Cornell brightness is −5.9% vs OFF (vs v1's −30%). This means the miss-branch `aFactor` was contributing MASSIVELY to GI-killing darkening but NOT contributing to leak reduction. This confirms the diagnosis that WeightedSample over-rejects miss bins.

**What's under-explained:** v2's targeted scope effectively partitions the bake into two regimes — "miss bins: Phase 3 does nothing" and "hit bins: Phase 3 attenuates upper contribution by `aFactor`." But the v2 leak reduction (−11.2%) is ENTIRELY from the hit regime. The hit regime is the smoothstep transition zone (`0 < l < 1`), which the document earlier identified as a small fraction of bins. If the leak reduction comes from a small fraction of bins, a `(1 - l)`-weighted metric would show a MUCH larger percentage reduction. This would make Phase 3 v2 look like it's succeeding on the metric it was designed for — but the *absolute* leak reduction (−11.2% of 4373.5 = −490 units) is still small in absolute terms. The document should calculate: "how much absolute leak (in units) was removed from the transition-zone bins specifically?" This would tell us whether Phase 3 is solving the right problem at a meaningful scale or just optimizing a corner case.

### N3 (MEDIUM) — The `.a` = 0 for sky vs `.a` = 0 for surface-hit indistinguishability issue (critic 09 W3) now has concrete consequences

In the WeightedSample algorithm:
```glsl
bool visible = (lProbeRayDist < 0.0) || (...);
```

With Phase 2's encoding, sky α = 0 (same as surface hit). The `< 0.0` check never fires. This means **sky-exit bins are treated as occluded** — the upper cascade's bins that reached sky (the "open" direction) are rejected. In an open Cornell box, many bins face sky through the top opening. WeightedSample rejects those bins, reducing upper-cascade contribution in the miss regime, contributing to v1's 30% darkening.

v2 fixed the GI-kill symptom by skipping `aFactor` for miss bins, but the **underlying encoding problem** remains: the visibility test can't distinguish sky from surface. If sky were encoded as α = −1 (or any negative sentinel, as Mode 4 originally used), the `< 0.0` check would fire and sky bins would be treated as "always visible" — which is geometrically correct (nothing occludes the sky direction). This would improve the algorithm's accuracy without needing the targeted-scope hack.

The document should acknowledge this as a **design constraint that limits Phase 3's ceiling**. Even with v2's targeted scope, the hit-regime `aFactor` is computed from a visibility test that treats sky as surface-hit. Any upper cascade bin that reached sky but is in a hit-regime direction for the lower probe gets a wrong `aFactor` (it says "occluded" when it should say "visible — nothing blocks sky"). This may partially explain the residual 6% Cornell darkening even in v2.

**Recommendation:** Phase 3's future iterations should consider restoring a sky sentinel in `.a` (e.g., α = −1 for sky, 0 for surface, 1 for miss) — or adding a separate hit-distance channel (per critic 11 W1). The current binary α encoding is a ceiling constraint on the algorithm's geometric correctness.

### N4 (MEDIUM) — The postscript diagnosis ("WeightedSample is wrong primitive for volumetric probes") is correct but doesn't account for the encoding constraint

The postscript argues that ShaderToy's wall-attached probes make "can U and L see each other?" the right question, but volumetric probes make it the wrong question. This is architecturally correct. However, **even if WeightedSample were the right primitive, the current `.a` encoding prevents it from executing correctly** — you can't do a geometric cone test on binary 0/1 values. The two problems (wrong primitive + wrong encoding) are independent and compounding. The document treats them as one problem ("wrong primitive") when they're two:

1. **Architectural:** WeightedSample tests point-to-point visibility, which is irrelevant for volumetric probes sharing far-field radiance.
2. **Encoding:** `.a` carries binary α, not hit-distance, so even a correct primitive can't execute a proper geometric test.

The postscript's recommended future paths ("test occlusion along `rayDir` from L's endpoint"; "use `hit.a` to gate WeightedSample only when `0 < l < 1`") address #1 but not #2. The document should add a third path: "restore hit-distance to the atlas (or a separate channel) so any visibility primitive can execute geometrically correct tests."

### N5 (MEDIUM) — v2's temporal stability test is superficially reassuring but doesn't test the important failure mode

The 10-consecutive-frame test shows RMSE 0.0003-0.0004, max-per-pixel 1/255. This proves v2 doesn't introduce new temporal instability. But the important failure mode for Phase 3 is **not frame-to-frame jitter** — it's **bake convergence behavior**. Phase 2's EMA blend stabilizes over ~240 frames. If Phase 3's `aFactor` (which depends on the upper cascade's baked α) creates a feedback loop where upper α changes affect lower radiance which then changes the next bake iteration's upper contribution, the convergence profile could be different (slower, oscillating, or settling to a different equilibrium). The 10-frame consecutive test doesn't cover this because it starts from an already-converged bake.

A proper convergence test would: start from a fresh bake (no history), run with Phase 3 ON, capture frames 1, 10, 30, 60, 120, 240, and check that the convergence curve is similar to OFF-mode. This was not done.

### N6 (LOW) — The OFF baseline "4373.5 EXACTLY" matching the historical baseline is coincidental precision

The document states the OFF baseline matches the historical 4373.5 "EXACTLY." The bake-leak metric is a sum of `bin.rgb * something` over thousands of bins — floating-point arithmetic across two separate runs (different sessions, different bake convergence timing) producing exactly the same value to the last decimal is unlikely unless the metric is integer-based (count of bins exceeding a threshold) or the same exact binary state was reproduced. If it's truly floating-point and matches exactly, that suggests deterministic bake convergence — worth noting but worth verifying. If the metric is actually integer (e.g., count of bins where `rgb > threshold`), the "EXACTLY" claim should say "integer count" rather than implying float-exactness.

### N7 (LOW) — v2's C0 leak value (3875.7) differs from v1's C0 leak value (3881.7) by 6 units

v1 and v2 differ only in the miss-branch `aFactor`. The leak metric measures surface bins (α < 0.001 facing the light). Miss bins don't appear in the leak metric (they have α = 1). So v2 should produce EXACTLY the same C0 leak as v1. The 6-unit difference (3875.7 vs 3881.7) is small but nonzero — suggesting either: (a) the metric isn't purely surface-bin-filtered (some bins near the boundary get reclassified), or (b) the miss-branch change slightly alters the EMA convergence trajectory, producing a slightly different converged state. This should be noted as a minor inconsistency.

### N8 (LOW) — Open question #3 (blendFraction=1.0 when WeightedSample is ON) is plausible but risky

The suggestion to widen `blendFraction` to give Phase 3 more leverage is architecturally questionable. The smoothstep `l` exists to soften the cascade handoff seam — widening it would produce visible seam artifacts (banding at cascade boundaries). Using blendFraction as a tuning parameter for Phase 3's leverage would couple the visual quality of the cascade handoff to the bake-side leak reduction, creating a tradeoff that can't be independently optimized. The document should note this coupling risk explicitly.

---

## Summary

The impl has improved substantially from rev 1. Critic-14 H1 (bit-exactness) and H2 (verdict clarity) are both fixed. The v2 targeted scope (miss branch skips aFactor) is a pragmatic fix that preserves GI while keeping the leak reduction where it matters. The postscript diagnosis (wrong primitive for volumetric probes) is architecturally sound.

The remaining concerns are:

1. **N1 (HIGH):** "INCONCLUSIVE" verdict is metric-dependent — without defining the revised metric and its thresholds, the verdict is a placeholder, not a decision.
2. **N2 (HIGH):** v2's targeted scope isolates Phase 3's effect to the transition zone; the absolute leak reduction (−490 units) should be computed for that zone specifically to determine if Phase 3 solves a meaningful problem or optimizes a corner case.
3. **N3 (MEDIUM):** Sky α=0 = surface α=0 indistinguishability limits the visibility test's correctness; the residual 6% darkening in v2 may be partially caused by this. Restoring a sky sentinel in `.a` or a separate hit-distance channel would improve the algorithm's ceiling.
4. **N4 (MEDIUM):** The postscript treats "wrong primitive" and "wrong encoding" as one problem; they're independent and both need addressing.
5. **N5 (MEDIUM):** Temporal stability test doesn't cover bake convergence behavior — the important failure mode for Phase 3.