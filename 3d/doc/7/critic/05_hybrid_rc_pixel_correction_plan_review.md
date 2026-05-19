# Critic Review 05 — `hybrid_rc_pixel_correction_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-19
**Verdict:** Plan structurally sound — single stochastic raymarch + temporal accumulation is the right "RC + correction" architecture, and the analysis correctly identifies what the correction captures (exact bounce-1) vs misses (bounce-2+). **But four HIGH issues will cause v1 to either be broken or mis-sell itself**: (H1) the correction reads cascade-baked albedo at the HIT, but the hit shading formula DUPLICATES what the cascade already bakes — risk of double-counting bounce 1; (H2) the blend formula `mix(cascade, correction, w)` is wrong — it should ADD not REPLACE; (H3) hybrid claims to "PT-bounce-1-equivalent" but it samples from CAMERA-VISIBLE pixels, not from random surface points like PT does — different sampling distribution. (H4) the per-pixel cost estimate (30-60 ms/frame at 1080p) is for a feature that promises to deprecate the cascade work it competes with — needs honesty about whether this REPLACES or COMPLEMENTS. **4 HIGH, 3 MEDIUM, 2 LOW.**

---

## HIGH severity

### H1 — Double-counting bounce 1: cascade's bake already does what the correction does

The plan §2.1 says the correction is:
```glsl
vec3 hit_radiance = h.albedo * (diff * uLightColor + vec3(uAmbientBakeStrength));
correction = albedo * hit_radiance;
```

This is `surface_albedo × (hit_albedo × direct_lit_at_hit)`. That's exactly the **same expression cascade bake stores at each probe bin**: when a probe ray hits a surface, the bin stores `hit.rgb = hit_albedo × (diff × lightColor + ambient)`. Then display reads that bin (cosine-weighted hemisphere average), multiplies by surface albedo → `surface_albedo × hit_albedo × ...`.

**Both cascade and hybrid compute the SAME quantity for bounce 1.** They differ only in HOW it's integrated (cascade via probe grid + hemisphere avg; hybrid via per-pixel MC). Same expected value, different variance.

So if blend is `correction + cascade × (1-w)`, we're double-counting bounce 1 at weight `(1-w) + w = 1.0` (no double-count). But if the cascade's atlas ALSO contains bounce-2+ via MB feedback, then `cascade = bounce_1_lossy + bounce_2+_lossy`. Hybrid correction = exact bounce_1. The blend `mix(cascade, correction, w)` gives:
- bounce 1: `(1-w) × lossy_b1 + w × exact_b1`
- bounce 2+: `(1-w) × lossy_b2+ + 0` (correction has none)

So as w→1, we lose multi-bounce entirely. As w→0, we get all-cascade. There's no "best of both" point.

**Fix paths**:
- (A) Composite formula = `direct + correction_bounce1 + cascade_bounce2+`. Requires SEPARATING cascade's bounce-1 from bounce-2+ contributions (don't have this; cascade's atlas has them merged).
- (B) `correction_bounce1` REPLACES cascade's bounce-1 entirely. But we can't disable cascade's bounce-1 baking without disabling the whole cascade.
- (C) Accept partial double-count: when blend w=1, hybrid is "exact bounce 1 only." When w<1, both cascade and hybrid contribute bounce 1, technically double-counting. User chooses tradeoff.
- (D) Don't compute bounce 1 in hybrid; instead, compute the DIFFERENCE between PT estimate and cascade estimate at the pixel. That subtracts cascade's contribution and adds PT's — net = exact replacement of bounce 1. But requires reading cascade at the pixel AGAIN to subtract; complex.

**Recommendation: rewrite the algorithm doc to acknowledge the double-counting issue clearly, document option (C) as v1 behavior, and propose option (D) as v2.** Current plan blandly says "blend at 0.7" without acknowledging the math is fuzzy.

### H2 — `mix(cascade, correction, w)` is the wrong formula for compositing two estimates

`mix(a, b, w) = a × (1-w) + b × w`. This is interpolation between two values. But cascade indirect and correction indirect are not "alternatives" — they're both ESTIMATES of the same physical quantity (indirect at this pixel).

Statistical correctness: if both are unbiased estimators with different variance, the optimal combination is INVERSE-VARIANCE WEIGHTING, not linear blend. Linear blend wastes information from the lower-variance estimator.

But more importantly, both `cascade_indirect` and `correction` claim to be "the indirect at this pixel." `mix()` makes them mutually exclusive. The plan doesn't justify why this is the right composition.

**Per H1**, the actual decomposition is:
- cascade_indirect ≈ lossy_bounce_1 + lossy_bounce_2+
- correction ≈ exact_bounce_1

So the composition should be SPECIFIC about what each provides. Not a generic mix.

**Fix**: the algorithm should be explicit about what it composes:
```
finalIndirect = correction          // exact bounce 1 (hybrid)
              + cascade_bounce_2plus // bounce 2+ from cascade (need to estimate)
```

How to get cascade_bounce_2plus? With MB feedback, atlas contains 1 + 2+ merged. Could approximate:
- `cascade_bounce_2plus ≈ (cascade_with_MB) - (cascade_without_MB)` per pixel
- Requires running cascade twice (once with MB, once without). Expensive (~2× cascade cost).

Simpler approximation: `cascade_bounce_2plus ≈ cascade_indirect × multibounce_fraction`, where multibounce_fraction is the empirical MB-OFF vs MB-ON brightness ratio. For Cornell ~7-10%. Not great but tractable.

**Even simpler v1**: ignore bounce-2+ entirely; use ONLY correction (`w = 1.0`). Document that this loses ~10% on white-walled scenes but is correct.

**Recommendation: rewrite §2.4 with explicit decomposition; default w=1.0 (pure correction); keep cascade as a fallback/lower-bound estimate.**

### H3 — Hybrid claims "PT-bounce-1-equivalent" but samples differently than PT

Plan §4.5 says:
> The hybrid correction uses [...] Same cosine sampling as PT. Same `traceSDF`. Same direct lighting formula. Same shadow trace. This makes hybrid correction a PT-bounce-1-equivalent.

This is half-true. Both use cosine sampling + SDF intersection + direct shading. But the **STARTING POINT** is different:
- **PT samples from camera-visible pixel positions** — random direction from each visible surface, integrated over many frames.
- **Hybrid samples from THE SAME camera-visible pixel positions** — one direction per pixel per frame, EMA'd.

So at the same pixel, both should produce the same expected indirect-bounce-1 value. OK so this part is correct.

**But hybrid only samples PIXELS THAT ARE CAMERA-VISIBLE.** A surface point not visible to the camera gets no correction. A point partially-visible (e.g., hidden behind another object) gets correction only for the visible part.

For DISPLAY this is fine — we only need correction at visible pixels. But it means the correction CANNOT be reused as cascade-bake-input. PT can serve as a reference for bake-leak metrics; hybrid cannot (it has no information at non-visible surfaces).

**Implication**: the plan's downstream claim "investigation of cascade integration losses becomes optional" (§10) is wrong. Cascade's atlas-side leak (per Phase 3 / Mode 14) is unaffected by hybrid; only DISPLAY is corrected. Anyone wanting "cascade is correct everywhere" still needs to fix cascade.

**Fix**: clarify in plan that hybrid is a DISPLAY-PATH-ONLY correction. Atlas remains the cascade's lossy version; any code that reads atlas (Phase 3 evaluation, etc.) gets cascade's content, not hybrid.

### H4 — Cost estimate for a feature that could deprecate cascade work needs honest framing

Plan §3 estimates 30-60 ms/frame at 1080p without mitigations. That's WAY more than the cascade pipeline (~16.5 ms bake + 5 ms display = 21 ms).

If hybrid replaces cascade GI display, then we're trading 21 ms (cascade) for 30-60 ms (hybrid). Net SLOWER. The only reason to keep this is "hybrid gives PT-quality results that cascade can't match" — but cascade defaults already match PT_GI in AVERAGE on cornell-orig (per sweep §A). The per-pixel error is what hybrid fixes.

The plan should be honest about this:
- For users who care about per-pixel accuracy: hybrid is the answer at 30-60 ms cost
- For users who care about average accuracy (e.g., for static A/B): cascade defaults are already fine

Doesn't make hybrid wrong. Just means the feature is for a SPECIFIC use case: high-quality per-pixel GI in interactive viewers. Document that.

Mode 19's role also shifts: pre-hybrid, Mode 19 was the diagnostic showing cascade's per-pixel gap. Post-hybrid, Mode 19 should go nearly white = success metric. The plan correctly states this in §10 but should also note: "Mode 19 ceases to be a CASCADE diagnostic once hybrid replaces the display."

---

## MEDIUM severity

### M1 — RNG seed `hashPixelFrame(gl_FragCoord.xy, uFrameIndex)` not specified

Plan §2.1 uses pseudocode but doesn't define the seed function. PT's stochastic helpers use PCG seeded from pixel+frame. Same approach should work but needs explicit code in the impl spec.

### M2 — `cosineSample(normal, ...)` is in `pt_reference.comp`, needs porting to `raymarch.frag`

Plan §4.5 mentions "share via duplication contract" but doesn't enumerate. Same pattern as Mode 17/18/19 (cosine sample, PCG RNG, traceSDF helpers). Each ports ~30 lines into the fragment shader. Should be a §4 sub-section detailing what's duplicated and the contract comment.

### M3 — EMA accumulator needs camera-move invalidation; plan mentions but doesn't spec

Plan §4.3 says "reset on camera move / scene change / hybrid toggle" but doesn't say HOW. Pattern exists (PT does this), but spec it explicitly: `hybridDirty` flag, threshold-based camera-delta check, etc.

---

## LOW severity

### L1 — `uHybridMaxDist` default "scene diagonal" is vague

Scene diagonal of cornell-orig ≈ 3 units. Of sponza-master ≈ 20+. Should be `length(uGridSize)` (= bbox diagonal) as a concrete default.

### L2 — No discussion of half-resolution implementation specifics

Plan §3 mentions "half-resolution correction + bilinear upsample" as mitigation. But where does the half-res accumulator live? Half-res of WHAT — the framebuffer? The PT accumulator pattern is half-res; hybrid should mirror it. Should be explicit.

---

## Cross-cutting: scope honesty

The plan's TL;DR says "~3-4 days." With H1/H2 redesign + half-resolution infrastructure + temporal accumulator + GUI + validation, realistic is 5-7 days. Same pattern as previous plans (over-confident initial estimates).

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | Double-counting bounce 1 between cascade and hybrid; blend math is fuzzy |
| H2 | HIGH | `mix(cascade, correction, w)` is statistically wrong combination of two estimators |
| H3 | HIGH | Hybrid is display-path-only; downstream claims about replacing cascade quality work are over-stated |
| H4 | HIGH | Cost estimate undermines the "RC is the cheap baseline" framing; needs use-case framing |
| M1 | MEDIUM | RNG hashing function not specified |
| M2 | MEDIUM | Helper duplication contract not enumerated |
| M3 | MEDIUM | Camera-move invalidation spec missing |
| L1 | LOW | Default uHybridMaxDist should be `length(uGridSize)` explicitly |
| L2 | LOW | Half-resolution implementation details missing |

---

## Top actions for plan revision

1. **Fix H1**: rewrite §2.4 to be explicit about what's composed. Default `w = 1.0` (pure correction, no double-count). Document the multi-bounce-loss trade.
2. **Fix H2**: replace `mix` formula with explicit "correction REPLACES cascade indirect at this pixel; cascade's multi-bounce contribution is separate problem."
3. **Fix H3**: clarify hybrid is DISPLAY-PATH-ONLY; doesn't fix cascade's atlas. Atlas-side work (Phase 3, Mode 14) is unaffected.
4. **Fix H4**: frame as "use-case-specific feature for per-pixel PT-quality GI at cost"; don't over-sell as deprecating cascade.
5. **Fix M1-M3**: enumerate the duplicated helpers, the RNG hash, the invalidation logic.
6. **Adjust scope**: 5-7 days realistic.

Then ship plan rev 2.
