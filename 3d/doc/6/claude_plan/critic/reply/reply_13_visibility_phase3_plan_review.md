# Reply: Phase 3 Plan Critic 13 — `13_visibility_phase3_plan_review.md`

**Date:** 2026-05-15
**Status:** All 9 findings accepted; all applied to [visibility_phase3_plan.md](../../visibility_phase3_plan.md) rev 2. The HIGH findings would have shipped a broken algorithm if missed (H1 wrong-sign reads nonsense atlas data; H2 references nonexistent uniforms; H3 silently regresses Phase 5f directional bilinear). Cerebrum gets two new entries (sign-translation rule + verified cascade ordering) so the lessons survive the plan doc.

---

### H1 (HIGH) — `dirToLower` sign error

**Accepted, fixed.** The original `dirToLower = -normalize(relVec)` was wrong: in 3D world-space the direction from upper-probe U to lower-probe L is `+normalize(L - U) = +normalize(relVec)`, not the negation. ShaderToy's `-dot(relVec, gTan)` in CubeA.glsl:27 is part of the wall-attached phi atan2 axis convention (which axis is "positive" in the wall's local frame), NOT a "direction toward upper" semantic. Translating that negation by mechanical copy to a 3D world-space relVec would have flipped the look-back bin to point AWAY from the lower probe.

**Fix in plan rev 2** ([§3 algorithm spec](../../visibility_phase3_plan.md), `dirToLower = normalize(relVec)`): drop the negation; add a multi-line shader-side comment block explaining ShaderToy's convention vs ours, explicitly forbidding a future reader from "fixing" it back to `-normalize(relVec)`.

**Cerebrum entry** (2026-05-15): generalized rule — "Don't blanket-translate ShaderToy's `-dot(relVec, gTan)` negation to 3D world-space" — so the lesson outlives this plan doc.

This was the most failure-prone line in the algorithm; if shipped, would have read effectively-random atlas bins for the visibility test, making the bake-leak metric improvement either nil or accidental.

### H2 (HIGH) — Missing uniforms

**Accepted, fixed.** The plan referenced `uUpperGridOrigin` and `uUpperGridSize` as "existing uniforms (may need to add)." A grep confirms neither exists. The "may need to add" hedge buried what is actually a non-trivial plumbing sub-task.

**Initial fix attempt: rejected.** I considered passing `uniform vec3 uUpperProbeWorld[8]` (per-corner world positions computed CPU-side). On reflection that doesn't work — there's no fixed set of 8 corners; each lower-probe thread has a different `triP000`, so corner indices vary per thread. Uniform arrays can't carry per-thread data.

**Fix in plan rev 2** ([§2 plumbing](../../visibility_phase3_plan.md)): pass two new vec3 uniforms `uUpperGridOrigin` + `uUpperCellSize`, named to disambiguate from this cascade's `uGridOrigin`/`uGridSize`. Shader computes per-thread corner world positions inline. Plumbing detailed (~30 min C++ change in `updateSingleCascade`); 3a budget grew 1d → 1.5d to cover.

### H3 (HIGH) — Single-bin merge regresses Phase 5f directional bilinear

**Accepted, fixed.** The plan's spec used `texelFetch(uUpperCascadeAtlas, forwardBin, 0)` for the forward sample — a single-bin nearest read. ShaderToy's `WeightedSample` sums 4 forward bins around the target direction, equivalent to our existing `sampleUpperDir(...)` helper with `uUseDirBilinear == 1` (the Phase 5f default). My single-fetch would have silently disabled Phase 5f's bilinear merge for any user with the default setting — likely a Tier 2/3 quality regression masking the bake-leak win.

**Fix in plan rev 2** ([§3 algorithm spec](../../visibility_phase3_plan.md)): forward sample calls the existing `sampleUpperDir(cornerPos, rayDir, Du)` helper. The look-back fetch is still a single `texelFetch` of the `.a` channel only (visibility test has no need for 4-bin sum on the look-back; just one stored ray distance).

This restores Phase 5f compatibility and matches ShaderToy more faithfully (separate look-back for visibility vs forward sum for radiance).

### M1 (MEDIUM) — Cost contradiction between TL;DR and §6

**Accepted, fixed.** TL;DR said ~0.5 ms; §6 corrected to ~1.2 ms based on 2 fetches per corner instead of 1. **Fix**: TL;DR in plan rev 2 now says "~+1.2 ms bake (~3% of current 38 ms bake)" matching §6. Standby doc's original 0.5 ms estimate was the source of the discrepancy — also flagged in §6 as 2× optimistic.

### M2 (MEDIUM) — Cone-correction formula referenced but not written

**Accepted, fixed.** Plan rev 2 [§3 algorithm spec](../../visibility_phase3_plan.md) writes the formula explicitly:

```
cos(theta_half) = 1 - 2/D²
sin(theta_half) = sqrt(1 - cos²) = sqrt(1 - (1 - 2/D²)²)
```

Concrete values per cascade tabulated:
- D=4 → sin(theta_half) ≈ 0.484
- D=8 → sin(theta_half) ≈ 0.248
- D=16 → sin(theta_half) ≈ 0.124

Computed CPU-side, passed as `uUpperBinConeSin`. v1 uses this average-area value; v2 fallback (per-bin LUT) per critic 7 H2 if non-uniformity matters.

### M3 (MEDIUM) — Cascade ordering claim never verified

**Accepted, fixed.** 5-minute grep of [demo3d.cpp:2257](../../../../src/demo3d.cpp#L2257) confirms:

```cpp
for (int i = cascadeCount - 1; i >= 0; --i) {
    ... updateSingleCascade(i);
}
```

**Top-down dispatch confirmed** (C3 → C2 → C1 → C0). Each lower cascade reads a freshly-baked upper cascade; no stale-data or first-frame-flicker concern.

**Fixes in plan rev 2:**
- TL;DR adds verified-ordering note pointing to demo3d.cpp:2257.
- §3.6 iteration backlog: removed v4 (cascade re-ordering) — unnecessary.
- §6 risks: cascade-ordering risk struck through as "NO LONGER A RISK."
- Cerebrum entry (2026-05-15): "Cascade dispatch is top-down" recorded for future reference.

### M4 (MEDIUM) — Iteration budget too aspirational

**Accepted, fixed.** v1→v4 (now v1→v3 after M3) at 1 day per cycle = 3-4 days for iteration alone. Plus initial v1 implementation = ~1.5 days. Realistic 3b: 5–7 days.

**Fix in plan rev 2:**
- TL;DR: total estimate 5d → 7–8d.
- §3 header: "~3 days" → "~5–7 days."
- §3.6 iteration budget: "up to 5 days" → "up to 5–7 days" with rationale.
- §5 commit shape table: 3b row 3d → 5–7d.
- §6 risks: explicit iteration-budget risk added with the "commit to v1-only with hard-revert" alternative for tighter scope.

### L1 (LOW) — `triF` semantics

**Accepted; verified, no change needed.** `triF` is the lower probe's fractional position within the upper-cascade cell (Phase 2.5d trilinear convention, unchanged by Phase 5d/5f). The per-corner `wx = (corners[i].x == 0) ? (1 - triF.x) : triF.x` formula is correct for axis-aligned corner offsets ∈ {0, 1}, which is what the spec uses. Plan rev 2 adds an inline comment in §3 noting the formula correctness depends on this offset-ε{0,1} assumption.

### L2 (LOW) — `wTotalSpatial` / visibility fraction unused downstream

**Accepted, fixed.** The accumulator was computed but the bake's existing merge formula (`rad = hit.rgb * l + upperDir.rgb * (1 - l)`) only consumed `.rgb`, not `.a`. Without a downstream consumer, the entire visibility check would have been a no-op for what gets baked into `rad`.

**Fix in plan rev 2** (new "Bake merge formula change" subsection in [§3](../../visibility_phase3_plan.md)): the bake's merge formula is updated to gate the upper contribution by visibility fraction:

```glsl
// Before (Phase 2): unconditional upper trust
rad = hit.rgb * l + upperDir.rgb * (1.0 - l);

// After (Phase 3b): gate upper contribution by visible-fraction
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * upperDir.a;
```

When all 8 corners visible (`upperDir.a == 1.0`), behavior matches Phase 2 exactly. When some occluded, upper contribution proportionally attenuated. When all occluded, only the local hit is stored (no leak from upper).

**Flagged in §6 risks as load-bearing**: a future change that drops the `* upperDir.a` factor (thinking it's a no-op since `upperDir.a` was always 1.0 pre-Phase-3) would silently regress the entire Phase 3 work into a slower no-op. Worth a shader-side comment at the merge site when implementing.

---

## What this critic chain bought

Without critic 13, plan rev 1 would have been implemented and shipped with three confirmed bugs (H1 + H2 + H3) and one effective no-op (L2). Specifically:

- **H1**: bake-leak metric likely unchanged or accidentally-improved (depending on whether the wrong-direction look-back bin happened to read sky/miss); algorithm semantically broken.
- **H2**: implementation would block on missing uniforms; ~half day of detective work to discover what plumbing the plan glossed over.
- **H3**: render quality regression for Phase 5f users (default `uUseDirBilinear == 1`); Tier 2/3 fail likely on Sponza A/B.
- **L2**: even if H1+H2+H3 fixed, the visibility check would have no effect on baked `rad` — Phase 3 would have been a slower Phase 2.

The MEDIUMs (M1-M4) caught documentation/budget errors that would have caused stakeholder-trust issues if discovered late ("you said 5 days, it's been 8" / "you said 0.5 ms, it's 1.2 ms"). M3 verification eliminated a phantom v4 iteration that wouldn't have been needed.

**Cost of the critic round**: ~20 minutes self-critic + ~30 minutes applying fixes. **Value**: avoided 5-7 days of debugging post-implementation. ROI is straightforward.

## Cerebrum entries added

Two 2026-05-15 entries in [.wolf/cerebrum.md](../../../../.wolf/cerebrum.md):

1. **"Don't blanket-translate ShaderToy's `-dot(relVec, gTan)` negation to 3D world-space"** — the H1 lesson generalized, so the next person porting ShaderToy direction code doesn't repeat the wrong-sign bug.
2. **"Cascade dispatch is top-down"** — the M3 verification, so future bake-side merge designs can rely on freshly-baked upper-cascade state.

The first entry is the more valuable one — it's a true Do-Not-Repeat lesson about a subtle 2D→3D translation pitfall that's likely to recur as the project pulls more from the ShaderToy reference.
