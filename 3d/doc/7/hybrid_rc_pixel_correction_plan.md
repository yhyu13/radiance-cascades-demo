# Plan: Hybrid RC + Per-Pixel Correction GI — rev 2

**Date:** 2026-05-19 (rev 1) → 2026-05-19 (rev 2 post critic-05)
**Goal:** Close the per-pixel cascade-vs-PT GI gap (Mode 19's dominant blue) by adding a single short stochastic raymarch per display pixel that REPLACES cascade's bounce-1 contribution with exact MC-integrated bounce-1. RC remains as the bake-side baseline; hybrid is a **display-path-only correction** for users who need per-pixel PT-quality GI in interactive viewers.
**Status:** Plan only.
**Critic chain extends:** [critic 05](critic/05_hybrid_rc_pixel_correction_plan_review.md) → revision 2. 4 HIGH + 3 MEDIUM + 2 LOW. All applied.
**Predecessors:** [PT reference](pt_reference_plan.md), [Multi-bounce v2 stochastic](multi_bounce_temporal_plan.md), [Mode 19 GI delta](mode_19_gi_delta_impl.md).

---

## TL;DR (rev 2)

- **Add ONE stochastic SDF raymarch per display pixel** in `raymarch.frag` after computing cascade indirect.
- Direction: cosine-weighted hemisphere around the surface normal (same MC as MB v2 stochastic).
- Contribution: `albedo × L_hit` where `L_hit` is freshly computed direct lighting at the hit (NOT cascade lookup — would re-introduce bias).
- Temporal accumulation in a new half-res RGBA32F `hybridAccumTexture` so per-frame stochastic noise averages over ~10 frames via EMA.
- **Composition (rev 2 per critic-05 H1/H2)**: `final = direct + correction`. The correction REPLACES cascade's bounce-1; cascade's multi-bounce (via Phase MB) is NOT added to avoid double-counting. **Default `w = 1.0`** (pure correction). Lower `w` keeps some cascade indirect for "approximate multi-bounce" but at cost of double-counted bounce-1.
- **Scope (rev 2 per critic-05 H3)**: display-path-only. Cascade atlas remains lossy; Phase 3 / Mode 14 / bake-leak metric all see the cascade's content. Hybrid does NOT replace the cascade workstreams; it's a parallel render-time feature.
- **Use case (rev 2 per critic-05 H4)**: for users who want PT-quality per-pixel GI in interactive viewers at ~10-30 ms/frame cost. NOT a replacement for cascade architecture; cascade still provides multi-bounce + bake-side state.
- Default OFF; opt-in via `--hybrid-correction=N` or GUI checkbox.
- Expected: per-pixel Mode 19 delta drops dramatically (~70-90% blue pixel reduction) at ~10-20 ms/frame cost at 720p with half-res accumulator.
- **Realistic budget: 5-7 days** (was 3-4 in rev 1; critic-05 scope creep correction).

---

## 1. Motivation

The parameter sweep on cornell-orig + directional light showed:

| Preset | Cascade_GI avg | vs PT_GI | Per-pixel RMSE |
|---|---:|---:|---:|
| Cheap (defaults, MB OFF) | 0.076 | **0.99×** (matches!) | **0.115** (large local errors) |
| MB g=1.0 | 0.116 | 1.52× | 0.162 |
| MB g=2.0 | 0.227 | 2.98× | 0.321 |

**The cascade defaults already match PT_GI in AVERAGE brightness, but local per-pixel RMSE is huge.** MB pushes the average UP everywhere uniformly; can't selectively fix dim regions without also over-brightening regions that already match. **MB is a blunt tool.**

What we need: a mechanism that **adds GI radiance specifically to the per-pixel regions that need it**. That's per-pixel correction.

PT's strength is per-pixel exactness; PT cost is the depth of recursion. A SINGLE short raymarch per pixel captures the most-impactful "what does the camera-visible surface actually see in this specific direction" without paying PT's full multi-bounce cost.

## 2. Algorithm

### 2.1 Where the correction lives

In `raymarch.frag` main render path, after computing cascade `indirectColor`:

```glsl
// Existing cascade indirect
vec3 indirect = sampleDirectionalGI(pos, normal).irrad;  // cascade approximation
vec3 indirectColor = albedo * indirect;

// NEW: per-pixel correction
vec3 correction = vec3(0.0);
if (uHybridCorrection != 0) {
    // 1. Generate cosine-weighted random direction around normal
    uint rng = hashPixelFrame(gl_FragCoord.xy, uFrameIndex);
    vec3 randDir = cosineSample(normal, rand2(rng));
    
    // 2. Cast ONE short SDF raymarch (max ~50 steps; max distance ~half scene diagonal)
    Hit h = traceSDF(pos + normal * 0.002, randDir, 0.001, uHybridMaxDist);
    
    // 3. Compute DIRECT lighting at the hit (no cascade lookup; avoid bias amplification)
    if (h.ok) {
        vec3 lightDir = ...; // dominant light
        float diff = max(dot(h.normal, lightDir), 0.0) * (1.0 - shadowFact(h.pos));
        vec3 hit_radiance = h.albedo * (diff * uLightColor + vec3(uAmbientBakeStrength));
        correction = albedo * hit_radiance;  // MC: cosine cancels with PDF
    } else if (h.sky) {
        correction = albedo * uSkyColor;
    }
    // No contribution if ray missed in-volume.
    
    // 4. EMA-blend into per-pixel correction texture (temporal smoothing)
    vec3 prevCorrection = texture(uHybridAccum, vUV).rgb;
    correction = mix(prevCorrection, correction, uHybridEMAAlpha);
    // Write back via imageStore in a parallel pass (or via fragGI / framebuffer attachment)
}

// 5. Composite
float w = uHybridBlendWeight;  // 0=cascade only, 1=correction only
vec3 finalIndirect = mix(indirectColor, correction, w);
```

**Why direct lighting at the hit, not cascade lookup**: if we read the cascade atlas at the hit point, we get cascade's already-biased radiance. That defeats the purpose. Using fresh direct lighting at the hit point gives **a pure one-bounce-from-truth correction** that the cascade should agree with eventually.

### 2.2 What this captures

A single stochastic raymarch per pixel computes: `albedo × directlight_at_random_hit`. Over many frames (EMA), the per-pixel mean converges to:

```
correction_eq = albedo × E[directlight_at_hit | cosine-PDF over hemisphere]
              = albedo × ∫ (direct(hit) × cos / π) dω
              = (albedo/π) × ∫ direct(hit) × cos dω
              = single-bounce indirect (EXACT integral, not RC's approximation)
```

This is **exactly PT's bounce 1 contribution** — no discretization, no probe interpolation, no cascade chain dilution. Just per-pixel exact MC.

### 2.3 What this does NOT capture

- **Multi-bounce (bounces 2+)**: a single raymarch only captures one bounce. To get bounce 2, the raymarch's hit point would need to also have indirect — which we'd read from the cascade or PT. Reading from cascade re-introduces its bias; PT recursion is expensive.
  - **Mitigation**: the cascade already has 1-bounce indirect baked in. Hybrid replaces RC's bounce-1 with exact bounce-1; the cascade's bounce-2+ (via MB) still contributes proportionally to `(1-w)`. So hybrid + MB = exact bounce-1 + cascade's lossy bounce-2+.
- **Specular / glossy**: pure diffuse only (consistent with PT and cascade scope).

### 2.4 Compositing — REWRITTEN per critic-05 H1+H2

**Critic-05 H1 caught**: cascade's bake already stores `hit_albedo × (direct + ambient)` per probe bin. Hybrid's correction computes EXACTLY THE SAME PRODUCT at the per-pixel hit. Both are estimators of "bounce-1 indirect at this pixel" — different sampling methods, same expected value. A naive `mix(cascade, correction, w)` formula double-counts bounce-1 at weight `(1-w) × cascade_b1_estimate + w × correction_b1_estimate`.

**Critic-05 H2 caught**: `mix()` interpolates between two estimates as if they were alternatives. But they're both estimators of THE SAME QUANTITY. Linear interpolation is statistically wrong — optimal is inverse-variance weighting, but we don't have variance estimates available cheaply.

**Decomposition (the right way to think about this)**:
- `cascade_indirect ≈ lossy_bounce_1 + lossy_bounce_2+` (with MB feedback active)
- `correction ≈ exact_bounce_1` (per-pixel MC)

**v1 composition: pure replacement of bounce 1 by exact correction**:
```glsl
// Default w = 1.0: bounce 1 entirely from correction; cascade's bounce-2+ is NOT added.
// Tradeoff: we lose cascade's MB contribution (~7-10% extra brightness on Cornell).
// Most users will accept this for the exact bounce-1 quality.
vec3 finalIndirect = correction;
// User can dial w < 1 to mix in some cascade (which adds bounce-2+ but also
// double-counts bounce-1; net effect depends on cascade's bounce-1 accuracy).
finalIndirect = mix(correction, cascadeIndirect, max(0.0, 1.0 - uHybridBlendWeight));
// Note: this is interpolating from "pure correction" toward "pure cascade",
// which is the OPPOSITE of rev 1's mix(). At w=1: pure correction (exact b1).
// At w<1: bias toward cascade (lossy b1 + b2+).
```

**v2 future**: estimate cascade_bounce_2plus separately:
- Run cascade twice (once with MB, once without), pixel-difference = bounce-2+ contribution
- Add that to correction: `finalIndirect = correction + cascade_bounce_2plus`
- No double-count, full multi-bounce coverage, ~2× cascade cost

**v1 ships with `w = 1.0` default** (pure correction). Documents the multi-bounce loss as known.

## 3. Performance estimate

Per pixel per frame:
- 1 ray, ~30-100 steps (SDF sphere-march, similar to existing raymarchSDF)
- 1 shadow ray at hit, ~30-50 steps
- ~10-20 ALU ops for direction sampling + composition
- ~150 SDF lookups + ~30 misc ops per pixel

At 1080p × 1 ray = 2M pixels × 150 lookups = 300M lookups/frame.
GPU 3D-texture rate ~5-10 GTex/s → **~30-60 ms/frame additional**.

That's not free. At 720p: ~14-27 ms. Still significant.

**Mitigations**:
- Half-resolution correction (~½ pixel count = ~½ cost) + bilinear upsample for display
- Tile-based dispatch (1/4 pixels per frame, full converges over 4 frames) — same as PT
- Skip raymarch entirely when cascade indirect is "high enough" (heuristic: skip if cascade indirect > threshold) — saves per-pixel work in already-bright regions

Realistic v1 target: ~10-20 ms/frame additional at 720p with half-resolution + EMA averaging.

## 4. Implementation

### 4.1 New shader pass (or inline in raymarch.frag)

Two options:
- **A (simple)**: inline the correction raymarch directly in `raymarch.frag`'s main path
- **B (modular)**: separate compute shader `hybrid_correction.comp` writes a half-res correction texture, raymarch.frag samples it

Recommendation: **A for v1** (simpler, no FBO management). Move to B for v2 if perf demands.

### 4.2 New uniforms

```glsl
uniform int   uHybridCorrection;     // 0 off, 1 on
uniform float uHybridBlendWeight;    // 0..1; default 1.0 (pure correction) per critic-05 H1
uniform float uHybridEMAAlpha;       // 0..1; temporal smoothing; default 0.1
uniform float uHybridMaxDist;        // max raymarch distance; default = length(uGridSize) per critic-05 L1
uniform uint  uHybridFrameSeed;      // per-frame RNG seed
uniform sampler2D uHybridAccum;      // RGBA32F half-res accumulator (parallels ptAccumTexture pattern per L2)
uniform int   uHybridAccumValid;     // 0 if hybridAccumTexture not yet allocated
```

### 4.2b RNG hashing function (per critic-05 M1)

Port from PT shader / MB feedback path:
```glsl
uint hybridHash(vec2 fragCoord, uint frameSeed) {
    uint x = floatBitsToUint(fragCoord.x);
    uint y = floatBitsToUint(fragCoord.y);
    uint h = x * 1973u ^ y * 9277u ^ frameSeed * 26699u;
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
```

Same pattern as Mode 7's PCG RNG and Phase MB's mbHash. Decorrelates adjacent pixels via large coprime multipliers; frameSeed varies per frame for temporal accumulation.

### 4.2c Helper duplication contract (per critic-05 M2)

The following helpers must be DUPLICATED into raymarch.frag (no GLSL #include):
- `cosineSample(N, vec2)` — from `pt_reference.comp` / `radiance_3d.comp` (MB v2)
- `genTB(N, T, B)` — same source
- PCG RNG state machine (`mbHash`, `mbPcg`) — same source
- `traceSDF` + `Hit` struct — must MATCH `pt_reference.comp`'s version exactly (camera-ray-from-outside-bbox case where `intersectBox` advances; this is critical for hit pixels at scene corners)

**Contract comment to include in raymarch.frag**:
```
// Hybrid correction helpers DUPLICATED from pt_reference.comp + radiance_3d.comp.
// Any change to traceSDF / cosineSample in those files MUST be mirrored here.
// "Silent divergence" between PT and hybrid would make hybrid's claim to be
// "PT-bounce-1-equivalent" subtly false. See critic-15 W8 / critic-02 W8.
```

### 4.2d Invalidation triggers (per critic-05 M3)

Reset `hybridAccumTexture` to zero AND reset `hybridSampleCount = 0` when ANY of:
- `hybridDirty == true` (set by setters or first dispatch)
- Camera position OR target changes > threshold (1e-4 length delta — same as PT)
- Scene reload / OBJ swap
- Light position / direction / intensity change
- `uHybridBlendWeight` change
- `uHybridEMAAlpha` change
- Any cascade settings that change `sampleSDF` / `traceSDF` behavior (rare)

Same `markHybridDirty()` setter pattern PT uses. Camera-move uses threshold-only (no time debounce; consistent with PT — critic-04 M2's rationale).

### 4.3 Per-pixel correction texture

`hybridAccumTexture` (RGBA32F, half-viewport size).
Allocation/clear pattern mirrors `ptAccumTexture` (Phase 7).

EMA blending similar to PT: `merged = mix(prev, sample, ema_alpha)`.
Reset on camera move / scene change / hybrid toggle.

### 4.4 C++ side

- New state members in `Demo3D`: `useHybrid`, `hybridBlendWeight`, `hybridEMAAlpha`, `hybridMaxDist`, `hybridAccumTexture`
- Setters trigger accumulator reset (`hybridDirty = true`)
- Camera-change invalidation (same pattern as PT)
- Uniform binding in `raymarchPass`
- GUI controls: checkbox + 2 sliders (blend weight, EMA alpha)

### 4.5 Sampling consistency with PT

The hybrid correction uses:
- Same cosine sampling as PT
- Same `traceSDF` (port from `pt_reference.comp` or share via duplication contract)
- Same direct lighting formula
- Same shadow trace

This makes hybrid correction a PT-bounce-1-equivalent. Expected:
- Mode 19 with hybrid ON should go nearly white (cascade_GI + correction ≈ PT_GI)

## 5. Sequencing (~5-7 days; rev 2 per critic-05 cross-cutting)

### Day 1 — v0.5 prototype (single-frame, no temporal accum)

- Port helpers (cosineSample, PCG RNG, traceSDF) into raymarch.frag with duplication contract per §4.2c
- Add inline correction raymarch in main path
- Add 5 uniforms; C++ binding; CLI flag
- Build + verify: hybrid ON visibly changes GI (likely noisy without temporal smoothing yet — that's expected)

### Day 2 — Half-resolution accumulator + temporal EMA

- Allocate `hybridAccumTexture` RGBA32F half-res (parallel to ptAccumTexture pattern)
- Compute shader pass that writes correction to accumulator with EMA blend
- Display reads accumulator (bilinear upsample to full res)
- Bind in `raymarchPass`

### Day 3 — Invalidation + GUI

- `hybridDirty` flag + setters + camera-delta check (per §4.2d)
- GUI: checkbox + 2 sliders (blend weight, EMA alpha) in Hierarchy & Merge tab
- Reset accumulator button
- Status display: sample count + estimated convergence time

### Day 4 — Mode 19 verification + composition tuning

- Visual A/B: cascade only / hybrid / PT side-by-side
- Tune `uHybridBlendWeight` empirically — find sweet spot for Mode 19 going white
- Verify the "default w=1.0 pure correction" hypothesis holds in practice
- Document the multi-bounce-loss tradeoff

### Day 5 — Perf optimization

- Profile: how much is the inline raymarch costing?
- Add "skip if cascade indirect > threshold" heuristic (avoids redundant work in already-bright regions)
- Optional: separate compute pass for the correction (better perf control than inline)
- Wall-clock target: ≤ 20 ms additional at 720p with half-res

### Days 6-7 — Buffer for surprises + sponza testing

Reserved per critic pattern — PT-bounce-1 prediction is mathematical; implementation surprises are inevitable. Test on cornell-orig AND sponza-master.

## 6. Validation

### Primary metric: Mode 19 visual response

With hybrid ON:
- **Default Cornell + directional light**: dominant blue should reduce significantly
- **cornell-orig + directional light**: 397k blue pixels should drop to ~50k or less (target: 87% reduction)

### Per-region: identify regions where hybrid DOESN'T help

Hybrid is a 1-bounce correction. If a region's gap is dominated by MULTI-BOUNCE (e.g., deep alcove with all-indirect lighting), hybrid won't fix it. Those regions are honest "RC + hybrid bounce-1 still misses multi-bounce in dense scenes" — separate workstream.

### Cost measurement

Wall-clock: hybrid OFF baseline vs hybrid ON. Target: < 30 ms/frame additional at 720p.

## 7. Risks

- **Read-write hazard**: if the correction raymarch reads cascade-affected textures (uSDF, uAlbedo), no issue (read-only). If it reads `hybridAccumTexture` for EMA, that's its own texture (single-bind ok).
- **Stochastic noise**: temporal EMA at alpha=0.1 takes ~10 frames to smooth a per-frame stochastic sample. During interactive use, may show slight per-frame shimmer until convergence.
- **EMA history rejection**: like PT, hybrid needs camera-move invalidation. Same patterns apply.
- **Cost spike**: 30-60 ms/frame at 1080p worst case. Half-res + EMA gets us to ~10-20 ms. Tile-based saves more. Acceptable for an opt-in feature.

## 8. Open questions

1. **Where to source the direct lighting at hit**: 
   - Option A: compute fresh in shader (current plan)
   - Option B: sample cascade atlas at hit (cheaper but re-introduces cascade bias)
   - Recommendation: A for v1 — get the exact bounce-1 contribution.

2. **Blend weight default**: 0.7 (mostly correction, some cascade for multi-bounce) vs 1.0 (pure correction, no cascade multi-bounce). 
   - 1.0 loses cascade's multi-bounce-via-MB (~7% on Cornell)
   - 0.7 keeps it but dilutes correction
   - Recommendation: 0.7 default; user can dial to 1.0 for pure exact-bounce-1.

3. **Sampling rate**: 1 ray per pixel per frame vs N rays per frame. 
   - 1 ray = cheapest, noisiest
   - More rays = smoother per-frame, higher cost
   - Recommendation: 1 ray default; user can boost.

4. **Should hybrid be IN raymarch.frag or a separate compute pass**: 
   - Inline = simpler
   - Separate compute = better perf control, can run at different rate than display
   - Recommendation: A for v1, B as v2 perf optimization.

## 9. Success criteria (rev 2)

### Hard requirements
- ☐ Build clean
- ☐ Default OFF preserves bit-exact pre-hybrid behavior (cascade modes 0/14/15/16/17/18/19 unchanged)
- ☐ Hybrid ON at default w=1.0 visibly closes Mode 19's blue gap (cornell-orig directional: ≥70% blue pixel reduction)
- ☐ Cost ≤ 30 ms/frame at 720p with half-res accumulator
- ☐ Temporal stability: no visible per-frame flicker after ~10 frames EMA convergence
- ☐ GUI checkbox + 2 sliders functional (blend weight, EMA alpha)
- ☐ Doc + critic + reply + cerebrum entry

### Honest framing (per critic-05 H3/H4)
- ☐ Doc explicitly notes hybrid is DISPLAY-PATH ONLY (atlas, Phase 3 evaluation, bake-leak metric all unaffected)
- ☐ Doc explicitly notes default w=1.0 LOSES cascade's MB multi-bounce contribution (~7-10% on white-walled scenes); acceptable trade for exact bounce-1
- ☐ Doc honestly states use case: "PT-quality per-pixel GI in interactive viewers" (not "deprecates cascade work")

## 10. What this unlocks downstream

If hybrid works as designed:
- **Mode 19 becomes a tuning knob, not a diagnosis** — user can dial cascade+hybrid to match PT
- **Investigation of cascade integration losses becomes optional** — we have a cheaper way to plug the gap
- **Phase 3 / Phase MB become smaller-leverage features** — hybrid does the heavy lifting for the per-pixel gap
- **Real-time PT-quality GI becomes plausible** — hybrid gives "PT bounce 1 in real-time" + cascade for bounces 2+
- **Future work shifts to**: better multi-bounce in cascade (since hybrid handles bounce 1); specular GI; dynamic scenes

This is the path from "RC is a structural approximation with quality ceiling" to "RC is a fast baseline + hybrid correction for exact per-pixel results." Cost is the per-pixel raymarch — acceptable for offline-style validation, possibly acceptable for interactive at 720p with half-res.
