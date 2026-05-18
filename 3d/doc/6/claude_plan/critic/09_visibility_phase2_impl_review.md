# Critique: Phase 2 — Implementation Notes (Pragmatic α-Only Variant)

**Document reviewed:** `visibility_phase2_impl.md`
**Date:** 2026-05-14

---

## Strengths

1. **Radical honesty about what didn't work.** The v1→v5 iteration log is one of the most valuable sections. Showing that the textbook RC interval merge over-darkened by 23%, then systematically testing four variants before settling on the pragmatic compromise, is exemplary engineering documentation. Most implementation docs would just present the final result and bury the failures.

2. **Honest downgrading of plan promises.** "Bake-time leaks NOT fully fixed" is stated prominently, not hidden. The document explicitly acknowledges that the plan over-promised and explains why (chained α-multiply kills far-field multi-bounce). This is the kind of admission that saves future readers from rediscovering the same failure.

3. **Sky α=0 as terminal — justified by iteration data.** The plan said α=1 for sky (transparent); implementation found that double-added sky+upper-which-ends-in-sky, so α=0 (terminal/opaque) was the fix. The reasoning is clear and the v1→v3 jump data confirms it.

4. **Clean 2C cleanup with bit-exact verification.** Deleting ~120 lines of raymarch.frag, the entire mode dispatch, and the C++ visibility infrastructure, then verifying RMSE 0.000000 vs pre-cleanup — this is the gold standard for a cleanup commit. No behavioral change, just dead code removal.

5. **Cost dramatically better than Mode 4.** Raymarch +12.9% vs Mode 0 (vs Mode 4's +50%), total frame +3.4% (vs +10.5%). This is the plan's key deliverable and it landed well. The comparison table is clear and directly addresses the Phase 1 cost concern.

---

## Weaknesses / Concerns

### W1 (HIGH) — The "pragmatic variant" is architecturally incomplete and its darkening root cause is unresolved

The document states that the textbook interval merge over-darkened by 23%, and the fix was to drop the bake merge entirely, keeping only the α-derivation and render-side gate. But **the root cause of the over-darkening is not diagnosed.** The document says "the chained α-multiply across C3→C2→C1→C0 terminates radiance at the first opaque cascade, killing far-field multi-bounce" — but that's what the interval merge is *supposed* to do (opaque surface stops radiance propagation). The problem is that most C0 bins in Sponza hit something, so `thisAlpha = 0` in most bins, and the `rad = thisRad + 0 × upperDir.rgb` means only local radiance survives — the `upperDir` contribution that carried indirect bounce from farther surfaces is zeroed.

This is not a bug in the formula; it's a **correctness-vs-energy-conservation tradeoff in the Sponza scene specifically.** A closed geometric scene (Cornell) has fewer "first opaque hit kills far-field" problems because the geometry is simpler. The document should have diagnosed this more precisely: the over-darkening is caused by **near-cascade opacity starving far-cascade radiance contribution in geometrically dense scenes.** The pragmatic fix sidesteps this by simply not using α in the radiance merge — but this means the bake still leaks radiance through walls (α only gates at render). The document acknowledges this, but doesn't propose a path to resolve the underlying tension between interval-correctness and energy-preserving bake.

**Why this matters:** anyone revisiting "full interval merge" (listed as future work) will hit the same over-darkening unless they understand the root cause. The document should have a "Why v1 over-darkened" subsection that diagnoses the mechanism precisely, so the future work has a starting hypothesis.

### W2 (HIGH) — Bake-time leak fix is the plan's headline promise, and it's not delivered — but the document frames this as "partially fulfilled"

The plan §3.1 promised: "Eliminate `probeVisibility()` and `uVisibilityMode` entirely by making the atlas store radiance intervals. Fixes: render-time correctness + bake-time leaks." The implementation only delivers render-time correctness. Bake-time leaks remain because the bake still uses `hit.rgb * l + upperDir * (1-l)` — radiance that hit a wall is blended with upper-cascade radiance, and if the wall is thin or the smoothstep `l` is wide, radiance bleeds through.

Calling this "partially fulfilled" is generous. A render-side α-gate cannot fix a bake-side leak — the α tells the renderer "this bin is opaque," but the bake already wrote radiance that came through the wall into that bin. The α=0 just means the renderer ignores that bin entirely, which is correct for the bin at the wall surface, but **the bins behind the wall** (in the upper cascade's contribution) still carry leaked radiance, and the α=0 for the wall surface means those bins are not weighted at all — the renderer just doesn't sample them.

Actually, re-reading more carefully: the α=0 at the wall surface means `w = wcos × 0 = 0`, so that bin contributes zero. But the **near-cascade** bins that hit the wall are the ones that get α=0. The **far-cascade** bins, sampled from the same probe, that represent directions where the ray went through the wall — those have α=1 (miss) and carry the leaked radiance from the upper cascade. So the render-side α-gate *does* fix render-time leak (directions that went through the wall now get α=1 and carry upper cascade data, which was always the case). The bake-side leak is when the upper cascade's radiance bleeds *through* the wall during the cascade merge — that's the `upperDir.rgb` in `hit.rgb * l + upperDir * (1-l)` where `l` is smoothstep near the hit distance. If `l` < 1 (which it always is in the smoothstep zone), some upper-cascade radiance leaks through. This is still present in the shipped code.

**The honest framing should be:** "Render-time leaks fixed. Bake-time leaks NOT fixed — the smoothstep blend zone in the bake still allows upper-cascade radiance to bleed through walls. This was a known limitation of the pragmatic variant; the textbook fix would have resolved it but over-darkened."

### W3 (MEDIUM) — α encoding for sky (hit.a < 0) is α=0, same as surface hit — but the semantics are different

The bake writes:
```glsl
if (hit.a > 0.0)      alpha = 0.0;   // surface hit → opaque
else if (hit.a < 0.0) alpha = 0.0;   // sky terminal → opaque (nothing beyond)
else                  alpha = 1.0;   // in-volume miss → transparent
```

Both surface hit and sky exit produce α=0. At render time, `w = wcos × a.a = 0` for both — the renderer treats them identically (occluded). This is **semantically correct for the render** (sky exit bins should contribute zero indirect irradiance; they're a terminal condition). But it creates a problem for any future work that needs to distinguish "opaque because a wall" from "opaque because sky exit" — e.g., soft α near surfaces (Phase 2.5) would want to apply a smoothstep to surface-hit α but keep sky α hard at 0. With the current encoding, there's no way to tell them apart in the atlas.

The document should note this encoding ambiguity as a **future-work blocker** and propose a resolution (e.g., reserve α=0 for surface hit, α=−1 or a separate channel for sky, or store the classification in a separate metadata texture).

### W4 (MEDIUM) — The v4 rejection reasoning is weak

v4 (cos-only normalization, `wsum += wcos`) was rejected because "RMSE 0.121 vs Mode 4, mean ratio 0.64 — significantly worse." But v4 is **geometrically correct** for hemisphere integration: you weight by cosine, sum over visible directions, and divide by total cosine weight. The fact that it's "worse than Mode 4" is because Mode 4's renormalization-over-visible-directions artificially brightens by concentrating energy into fewer directions — which is the over-bright bias the document itself acknowledges ("preserves the over-bright bias the pre-Phase-2 bake encoded").

The document should acknowledge that v4 is the *correct* hemisphere integral, v5 is the *empirically matching* one, and the reason v5 matches better is that the **bake's radiance values are already biased** by the smoothstep merge formula. If someone later fixes the bake (e.g., full interval merge with corrected energy), v5's renormalization would over-brighten again, and v4 would become the correct choice. The current v5 choice is locked to the current (leaky) bake formula.

### W5 (MEDIUM) — reduction_3d +42% dismissed too quickly

The document says "+42% (~280 μs absolute) is in the single-run-noise range" and "doesn't affect the verdict." But 42% is well outside the 5-10% noise band the document elsewhere cites. At 280 μs absolute it's negligible for FPS, but if it's real (not noise), it means something in the reduction pass is reacting to α values in the atlas. The reduction pass (`reduction_3d`) computes per-probe statistics from the atlas — if it's now reading RGBA instead of RGB (same fetch, but different data in the α channel), and if any downstream computation in reduction accidentally incorporates α, that's a latent bug.

The document should at least flag: "if reduction_3d timing remains elevated in multi-run averages, investigate whether the reduction shader's per-probe summary computation accidentally reads or processes the new α values."

### W6 (LOW) — Single-frame RenderDoc timing for cost claims

Same issue as Phase 1 decision gate: all timing numbers are single-frame captures. The Phase 1 critic flagged this and recommended 3-run averages. Phase 2 still uses single-frame. The +12.9% raymarch claim is more trustworthy than Phase 1's +50% (the delta is smaller, so noise-band overlap is less likely to flip the conclusion), but it's still a point estimate.

### W7 (LOW) — "RMSE 0.000000" for bit-exact verification is suspiciously precise

The smoke test claims RMSE 0.000000 between pre-cleanup and post-cleanup captures. This means pixel-for-pixel identical output — which is expected if 2C only removes dead code paths that aren't executed. But the precision of "0.000000" (6 zeros) implies 64-bit float RMSE, which in linear sRGB with 16F atlas values should have at least some rounding noise. If this is integer-exact (same pixel values in the PNG), that's fine — but the document should say "pixel-identical PNG" rather than "RMSE 0.000000," which implies a float computation that can't be that precise.

### W8 (LOW) — v3 and v5 are listed as identical but the table shows v3 RMSE 0.0644 vs v5 RMSE 0.0644

The iteration log says "v3 = v5 — same code, two test runs to confirm the v4 reversion was clean." If v3 and v5 are the same code, why are they listed as separate variants? This is fine for the iteration narrative, but it inflates the table with a redundant row. A note like "v5 is a re-run of v3 to confirm v4 reversion" would clarify.

### W9 (LOW) — The bake-leak quantitative test was deferred but is the key evidence for "render-side leak fix works"

The document says "visual A/B at the auto-fit camera was substituted" for the formal atlas-inspection test. But "visual A/B" can't confirm that the atlas is leak-free — it only confirms that the *rendered output* looks right. The bake could still be leaking radiance through walls (smoothstep blend zone), and the render-side α-gate could be hiding the leak by gating out the affected bins. The formal test (inspect atlas bins in an occluded region, verify `sum(bin.rgb × bin.a) ≈ 0`) is the only way to prove the α-gate actually works as intended. Deferring it means the Phase 2 headline claim ("render-time leaks fixed") is supported only by visual inspection, not by data.

---

## Summary

A strong implementation document that honestly documents failures, iteration, and tradeoffs. The main concerns are:

1. **W1/W2 (HIGH):** The textbook interval merge's over-darkening root cause is not diagnosed — future revisitors will hit the same wall. And bake-time leak fix is not delivered, though the document frames it as "partially fulfilled" rather than "not delivered for the bake side."
2. **W3 (MEDIUM):** Sky α=0 and surface α=0 are indistinguishable in the atlas, blocking future soft-α work.
3. **W4 (MEDIUM):** v5's renormalization is empirically tuned to the current (leaky) bake; if the bake is later fixed, v5 will over-brighten and v4 would become correct. This coupling should be documented.
4. **W5 (MEDIUM):** reduction_3d +42% may be real, not noise — worth a follow-up investigation.
5. **W9 (LOW):** Bake-leak quantitative test deferred; visual A/B is insufficient evidence for the "render-side leak fix" claim.