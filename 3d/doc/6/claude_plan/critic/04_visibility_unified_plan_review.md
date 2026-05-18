# Critic Review 04 — visibility_unified_plan.md

**Reviewing:** [visibility_unified_plan.md](../visibility_unified_plan.md)
**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-12
**Verdict:** Plan is sound at the strategic level (the two-phase sequencing is genuinely well-motivated). Several **specific technical claims and implementation details are wrong, hand-wavy, or unverified**. None of these invalidate the phasing, but several would silently produce broken code if implemented as written. Phase 2's "~1 day" scope estimate is also inherited rather than re-derived and is probably understated.

---

## Substantive issues (would change the implementation)

### C1. Phase 2's α-merge formula is wrong relative to the paper

The plan writes (Phase 2, "Bake changes"):

```glsl
rad   = hit.rgb * l + upperDir.rgb * upperDir.a * (1.0 - l);
alpha = mix(thisAlpha, upperDir.a, 1.0 - l);   // or paper's exact merge
```

The RC paper's interval merge is **multiplicative for transparency, additive-with-β-weighting for radiance**:

```
L_{a,c} = L_{a,b} + β_{a,b} * L_{b,c}
β_{a,c} = β_{a,b} * β_{b,c}
```

The proposed `mix(thisAlpha, upperDir.a, 1-l)` is a **linear** blend of α; the paper's `β_{a,b} * β_{b,c}` is **multiplicative**. These produce different results. Specifically: if `thisAlpha = 0` (occluded near interval) and `upperDir.a = 1` (transparent far interval), `mix(0, 1, 1-l)` says the merged interval becomes partially transparent depending on `l`. The paper says the merged interval is `0 * 1 = 0` — **fully occluded** because the near interval blocks everything regardless of what's beyond it.

This is not a stylistic disagreement — it is a correctness bug. The "or paper's exact merge" hedge in the comment acknowledges the problem but defers it. **Phase 2 should pin the merge formula before implementation, not after.**

The smoothstep `l` is also a different operation than the paper's interval merge. The paper assumes a sharp interval boundary (near interval ends where far interval begins). The current `l` smoothstep is a hack to soften the cascade-handoff seam. Combining a smoothstep with α-gating without thinking through the interaction will produce subtle artifacts at the cascade boundary.

### C2. Mode 4's "eliminates banding by construction" claim is too strong

Phase 1 quality: > "Per-direction-bin granularity (matches mode 3 — eliminates dot banding by construction)."

This is overstated. Mode 4's per-bin visibility test is per-bin **within a single corner**, but across the 8 trilinear corners the test still produces a binary 0/1 decision per (corner, bin). If two adjacent corners disagree on a bin's visibility (one says visible, one says occluded), the trilinear blend produces a sharp transition at the cell boundary just like mode 1 — only at finer (per-bin) granularity.

Mode 3 has the same property in principle but in practice the SDF re-trace from the surface position varies continuously with surface position, smoothing the corner-to-corner transition. Mode 4's `wvis = (distSP < a.a*cosCone) ? 1 : 0` is a **hard threshold on `distSP`**, which **does** vary continuously with surface position — so the per-pixel result transitions smoothly even when adjacent corners disagree.

So Mode 4 probably does eliminate banding — but for a different reason than "per-bin granularity". The reason is "the threshold variable (`distSP`) is per-pixel-continuous, not per-corner-binary". The plan's stated mechanism is wrong even if the conclusion happens to hold.

### C3. `cosCone = cos(π/D/2)` is asserted without derivation

The plan picks `cosCone = cos(π / D / 2)` from the probe plan, which itself picked it from a flatland ShaderToy formula. The verification step enumerates three tuning attempts (`π/D`, `π/D*0.25`, default). None of these are derived from the actual 3D octahedral mapping geometry.

The honest answer: **the right cone angle depends on probe spacing relative to the bin's solid angle and on the per-cascade probe density, not just D**. A geometrically derived value would be `cos(half_angle)` where `half_angle = sqrt(4π / (D*D)) / 2` for octahedral mapping (each bin covers 4π/D² steradians, half-angle of equivalent cone is `acos(1 - 2/D²)`).

If Mode 4's quality fails, the plan's three tuning attempts will all be wrong because they vary the same fundamentally-ungrounded constant by small factors. **Add a fourth attempt: derive from octahedral solid angle.**

### C4. EMA + α interaction not addressed in Phase 2

`radiance_3d.comp:428` has both an EMA path (`vec4(blended, hit.a)`) and a plain path. After Phase 2, the α channel becomes meaningful (was just a passenger). If EMA blends the α channel — which it would by default if the shader writes `vec4(blended_rgb, blended_alpha)` — two consecutive frames classifying the same bin differently (hit at frame N, miss at frame N+1) would produce α=0.5, which the rest of the pipeline interprets as "half-transparent".

The plan claims Phase 2 starts with binary α. It cannot, if EMA is on. Three options:

1. Disable EMA for the α channel (write `vec4(blended_rgb, hit.a)`, not `vec4(blended_rgb, blended_alpha)`).
2. Accept soft α from day one — change the verification expectations.
3. Snap EMA-blended α to 0/1 with a threshold (`alpha > 0.5 ? 1 : 0`), which discards soft information for no benefit.

**Phase 2 must pick one.** The plan picks none.

### C5. "+33% atlas memory" is misleading

GL_RGB8 textures are typically padded to 4-byte alignment by drivers; the on-GPU footprint of GL_RGB8 and GL_RGBA8 is often **identical**. The +33% assumption is based on logical bytes, not allocated bytes.

Conversely, if the existing GL_RGB8 atlas was being read via a 3-byte stride somewhere on the CPU side, switching to RGBA8 will fix a hidden inefficiency (no 33% cost — possibly a cost reduction).

**Action:** measure actual VRAM allocation before and after on the target GPU. Don't budget against a synthetic 33%.

### C6. Mode 5 fallback is a deferral, not a plan

Decision gate: > "Mode 4 mostly works but specific surfaces (columns, arches) misbehave → fall back to Mode 5 = Mode 4 + 1 confirmation shadow ray."

Mode 5 has no implementation sketch, no cost estimate, no quality estimate, and no ImGui/CLI integration. If the gate triggers, the plan effectively says "go invent a new mode". That is not a plan branch — it is an unplanned escape hatch.

Either:
- Drop Mode 5 from the gate and make the failure outcome "proceed to Phase 2", or
- Spec Mode 5 inline in Phase 1.

### C7. "Per-pixel work" cost table conflates fetch count with cost

Phase 1 cost table:

| Mode | Per-pixel work | vs mode 0 |
|---|---|---:|
| 4 depth-aware | 8 × D² × vec4 fetch (same bandwidth) + ~5 ALU/bin | ~1.05× |

"5 ALU/bin" × 8 corners × D² (=64) = **2560 ALU/pixel** of pure overhead, plus 8× `length()` (≈ 8 sqrts). At 1080p that's ~5.3B ALU ops/frame just for visibility. Whether this is 5% of the existing budget depends on what the existing budget *is*. The plan doesn't cite mode 0's existing per-pixel cost, so the "~1.05×" is a guess.

The correct claim is "negligible relative to existing texture bandwidth" — but the verification step (RenderDoc timing) is the only thing that will actually establish this. Until it does, **drop the "~1.05×" cost claim from the plan and replace with "expected to be small; verified in Step 5".**

### C8. Cross-cascade behavior not analyzed

The atlas exists per cascade (5 cascades). `cosCone` is cascade-independent in the proposed shader. Probe spacing varies by cascade (C0 dense, C4 sparse). Whether the same `cosCone` works at all cascades is not analyzed.

Concretely: at C4 (sparse probes, large cells), `distSP` between surface and probe is large. The test `distSP < hitDist*cosCone` becomes "is the surface inside a roughly probe-sized cone of the bin's hit ray". For a cascade where probe spacing is comparable to scene depth, this test is nearly always true (everything looks visible) — defeating the whole point.

**Add a Phase 1 verification step:** capture per-cascade visibility heatmaps to confirm the test actually does something at every cascade level, not just C0.

---

## Smaller technical / scoping issues

### S1. "~1 day" Phase 2 estimate inherited from H5/H6 scope

The visibility plan said "~1 day of work per the H5/H6 scope assessment". The unified plan repeats this without re-deriving against the actual Phase 2 surface area: atlas allocation, every shader fetch site (grep `uDirectionalAtlas`), bake inheritance formula (which is currently wrong, see C1), ImGui cleanup, CLI cleanup, baseline regen, regression captures. **Realistic estimate: 2–4 days.** The "~1 day" sets up false expectations.

### S2. Internal inconsistency about when modes 1/2/3 are deleted

Phase 2 "Render changes" says: > "Delete: probeVisibility(), uVisibilityMode, the entire mode 0..4 switch"

Phase 2 "What survives" table is consistent with delete.

But the verification protocol says: > "Keep modes 1/2/3 only briefly during validation; remove once Phase 2 lands clean."

So which is it — delete in the same commit as the merge, or keep through validation? Pick one.

### S3. Atlas format change has no rollback plan

"Audit each `uDirectionalAtlas` fetch site" is the entire migration plan. No checklist, no estimated count, no recovery story if Phase 2 regresses (the rollback isn't `git revert` because re-allocating the atlas means re-baking, which means a frame of black until the bake catches up).

Add: enumerate fetch sites in a pre-flight pass; commit Phase 2 in two sub-commits (format change + semantic change) so rollback is granular.

### S4. CLI/scripting breaking-change surface not addressed

`--visibility-mode=N` appears in user logs (`tools/app_run_sponza_visibility_mode{0,1,2,3}.log`). Removing it breaks the user's existing scripting. At minimum: keep the CLI flag as a no-op with a deprecation warning for one release, even after the modes are gone.

### S5. No baseline-regeneration plan

Phase 1 says "compare mode 4 to mode 3" but doesn't say "regenerate mode-0 and mode-3 captures first against current main" — without that, the comparison is against stale baselines from before recent changes.

### S6. Bake-leak verification is not operationalizable

Phase 2 verification step 3: > "Test by zeroing out probeVisibility-equivalent paths and confirming the atlas itself no longer carries cross-wall radiance."

After Phase 2 there is no `probeVisibility()` to zero out. The intended test is unclear. Concrete proposal: capture the atlas contents (RenderDoc or `glReadPixels`) for a closed-room scene where some bins should be 0 (no light reaches them) and confirm those bins are actually 0 in the atlas, not just gated to 0 at render time.

### S7. The "Phase 1 informs Phase 2" claim is weakly supported

The plan says Phase 1 turns Phase 2 into a "confidence-bounded refactor". Phase 1 tests render-side visibility using `hit.a`. Phase 2 changes the atlas format and the bake-side merge formula. The information learned in Phase 1 (does `cosCone` work, does Mode 4 look ok) is **not** information that bounds Phase 2 confidence — Phase 2 deletes Mode 4 and uses a different formula. The phases are sequenceable but not informationally connected in the way the plan claims.

A more honest framing: "Phase 1 buys time and ships a quality fix immediately. Phase 2 is the architectural endpoint and is justified independently of Phase 1's outcome."

---

## Editorial / framing issues

### E1. The plan asserts what it should verify

Phrases like "by construction", "guaranteed", "matches mode 3" appear without proof. Phase 1 verification has not been run. Hedge these to "expected to" until verification lands.

### E2. The "Out of scope" table is too aggressive

It marks Strategies 3–7 obsolete after Phase 2. Half-resolution visibility (Strategy 6) might still matter at very high cascade counts even after Phase 2, since the per-bin α-fetch count grows with D². Don't pre-reject optimizations for a future regime that hasn't been measured.

### E3. Decision gate covers 4 outcomes but doesn't quantify "acceptable"

"Mode 4 ≈ mode 3 quality" — measured how? Pixel-diff threshold? RMSE? Eyeball A/B? Without a metric, the gate is a coin flip in disguise.

Concrete proposal: pre-commit to a metric (e.g., "FLIP score < 0.05 against mode 3 reference at cam.md viewpoint") and capture it as part of the Phase 1 verification.

---

## What the plan got right (worth keeping)

- **Two-phase sequencing is correct.** Mode 4 first as a render-only quick win is the lowest-risk path that addresses the user's immediate complaint.
- **Identifying that Phase 1 doesn't fix bake-time leaks** is genuinely important and was a gap in the original probe plan.
- **Recommending separate commits per phase** is sound advice for the rollback story.
- **Calling out `cosCone` calibration as the fragile knob** correctly identifies the highest-risk assumption in Phase 1.
- **Deleting `uVisibilityMode` in Phase 2** is the right cleanup — keeping a 5-mode switch around when one mode is correct is technical debt.
- **The decision gate at end of Phase 1** is the right structural device, even if its outcomes need tightening (see E3, C6).

---

## Recommended revisions before implementing

Priority order:

1. **Fix C1** — pin the Phase 2 α-merge formula to the paper's `β_{a,c} = β_{a,b} * β_{b,c}` and update the snippet. This is a correctness bug.
2. **Fix C4** — decide EMA + α interaction explicitly.
3. **Fix C2** — restate Mode 4's mechanism correctly (per-pixel threshold continuity, not per-bin granularity).
4. **Tighten E3** — pick a quality metric for the decision gate.
5. **Fix S1, S2, S3** — re-estimate Phase 2 scope, resolve the delete-modes-when ambiguity, plan the atlas format rollback.
6. **Fix C7** — drop the "~1.05×" claim until measured.
7. **Address C8** — add per-cascade verification step.
8. **Fix C6** — either spec Mode 5 inline or remove it from the gate.

Items 1, 2, and 4 should land before any code is written. The rest can be patched into the plan as Phase 1 progresses.
