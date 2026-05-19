# Reply: Hybrid RC + Per-Pixel Correction Plan — Critic 05

**Date:** 2026-05-19
**Status:** All 9 findings addressed. **H1 (bounce-1 double-counting) and H2 (wrong mix formula) forced a fundamental rewrite of the composition math**: default blend weight changed from 0.7 → 1.0 (pure correction, no double-count); cascade's MB contribution explicitly traded away for exact bounce-1. **H3 reframed the feature scope**: display-path-only, doesn't replace cascade workstreams. **H4 reframed the framing**: use-case-specific feature (PT-quality interactive viewer) at a cost, not a cascade replacement. Medium fixes added concrete specs for RNG, helper duplication contract, and invalidation.

---

## How each finding was addressed

### H1 (HIGH) — Bounce-1 double-counting

**Accepted, fundamental composition rewrite.** The critic correctly identified that cascade bake stores `hit_albedo × (direct + ambient)` per bin, and the hybrid correction computes EXACTLY THE SAME product per-pixel. Both estimate bounce-1; naively mixing them double-counts at any `w < 1`.

**Fix in plan rev 2 §2.4**:
- Rewrote the composition section to explicitly decompose: `cascade_indirect ≈ lossy_bounce_1 + lossy_bounce_2+`; `correction = exact_bounce_1`.
- Default `w = 1.0` (pure correction; no double-count; loses bounce-2+).
- User can dial `w < 1` to mix in cascade for bounce-2+ at cost of bounce-1 double-counting. Tradeoff documented.
- v2 future path: estimate cascade's bounce-2+ separately by running cascade twice (with/without MB) and pixel-differencing. Adds `correction + cascade_bounce_2plus` cleanly. Deferred.

### H2 (HIGH) — `mix(cascade, correction, w)` is wrong combination

**Accepted, formula rewritten.** Both terms are estimators of the same physical quantity, not interpolatable alternatives.

**Fix**: `mix()` is now used in the OPPOSITE direction: `mix(correction, cascadeIndirect, max(0, 1-w))`. At `w=1` → pure correction. At `w<1` → biases toward cascade (with all its bounce-2+ but also double-counting bounce-1).

This isn't statistically optimal (inverse-variance weighting would be), but variance estimates aren't cheaply available; biased linear interpolation is the pragmatic v1.

### H3 (HIGH) — Hybrid is display-path-only; over-claimed downstream effects

**Accepted, scope reframed.** Plan rev 2 §10 no longer claims hybrid "deprecates cascade work." Atlas-side concerns (Phase 3, Mode 14, bake-leak metric) are unchanged.

**Key reframing**: hybrid is a **parallel** render-time feature. Cascade still bakes per its existing rules; hybrid samples PT-bounce-1-equivalent in display. The TWO PATHS COEXIST:
- Cascade: atlas-side multi-bounce (via MB) + lossy bounce-1 + lossy bounce-2+
- Hybrid: display-side exact bounce-1 (no multi-bounce)

Cascade investigations (mode 18 atlas analysis, Phase 3 visibility) still matter for users who DON'T enable hybrid (most users; hybrid is opt-in).

### H4 (HIGH) — Cost framing dishonest

**Accepted, use-case framing added.** Plan rev 2 TL;DR now states hybrid is "for users who want PT-quality per-pixel GI in interactive viewers at ~10-30 ms/frame cost. NOT a replacement for cascade architecture."

This positions hybrid correctly:
- Cascade-only: ~16.5 ms bake + 5 ms display = ~21 ms total for "approximate GI"
- Cascade + hybrid: ~21 ms cascade + ~10-20 ms hybrid = ~30-40 ms total for "PT-quality bounce-1 + approximate bounce-2+"

The cost is real but justified by per-pixel exactness. Users who don't need it stay on cascade-only.

---

## MEDIUM + LOW fixes

| ID | Fix landed |
|---|---|
| M1 (HIGH-impact MEDIUM) | Added `hybridHash` function spec in §4.2b (PCG pattern matching PT + MB v2) |
| M2 | Helper duplication contract in §4.2c (cosineSample, genTB, PCG, traceSDF, Hit struct) with explicit "must match pt_reference.comp" comment template |
| M3 | Invalidation spec in §4.2d (camera-delta threshold, scene changes, light changes, weight changes) |
| L1 | `uHybridMaxDist` default = `length(uGridSize)` (concrete; was vague "scene diagonal") |
| L2 | Half-resolution accumulator spec mirrors `ptAccumTexture` pattern (parallel allocator + clear + bilinear upsample) |

### Scope adjustment

Critic-05 cross-cutting note ("scope honesty: realistic 5-7 days") accepted. Plan rev 2 §5 sequencing updated:
- Day 1 prototype, Day 2 accumulator, Day 3 GUI/invalidation, Day 4 Mode 19 verification, Day 5 perf, Days 6-7 buffer + Sponza testing
- Total: 5-7 days realistic vs rev 1's optimistic 3-4

---

## Honest framing summary (rev 2)

The hybrid feature is now described as:
- **PURE correction at default w=1.0**: exact bounce-1, no double-count, loses cascade's bounce-2+
- **Display-path-only**: doesn't fix atlas; doesn't deprecate cascade investigation work
- **Use-case-specific**: for users who want PT-quality per-pixel GI in interactive viewers
- **Cost is real**: ~10-30 ms/frame at 720p with mitigations
- **Cascade architecture stays the primary path**: for users who want fast approximate GI without per-pixel cost

This framing is honest about the trade and positions the feature for the right users.

---

## Summary

| Critic 05 ID | Severity | Action |
|---|---|---|
| H1 | HIGH | Composition rewritten; default w=1.0 = pure correction, no double-count |
| H2 | HIGH | `mix()` direction reversed; explicit decomposition into bounce_1 + bounce_2+ |
| H3 | HIGH | Scope clarified: display-path-only; atlas/Phase 3 work unaffected |
| H4 | HIGH | Use-case framing added; not "cascade replacement" |
| M1 | MEDIUM | hybridHash function spec'd |
| M2 | MEDIUM | Helper duplication contract enumerated |
| M3 | MEDIUM | Invalidation triggers spec'd explicitly |
| L1 | LOW | uHybridMaxDist = length(uGridSize) |
| L2 | LOW | Half-res accumulator spec mirrors PT pattern |

**Most impactful change**: H1 + H2 + default w=1.0 means hybrid v1 IS a clean "exact bounce-1 replacement," not a "magic blend that fixes everything." The honest constraint (loses bounce-2+) is documented; future v2 can address it via separate cascade dispatch.

**Critic value**: H1 alone would have caused user confusion ("why doesn't dialing the slider work the way I expect?"). H3 + H4 prevented the feature from being over-sold and disappointing in practice. Round well-earned.
