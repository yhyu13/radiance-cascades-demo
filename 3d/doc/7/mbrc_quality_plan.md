# MBRC Quality Plan — closing the gap to hybrid GI without per-pixel SDF tracing

**Author:** Claude — 2026-05-20
**Status:** brainstorm / plan, awaiting user steering
**Predecessor docs:** [hybrid_rc_pixel_correction_impl.md](hybrid_rc_pixel_correction_impl.md), [hybrid_v12_validation_phase8_impl.md](hybrid_v12_validation_phase8_impl.md), [multi_bounce_temporal_impl.md](multi_bounce_temporal_impl.md), [pt_reference_impl.md](pt_reference_impl.md)
**Reference:** [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl), [shader_toy/Image.glsl](../../shader_toy/Image.glsl), [shader_toy/Common.glsl](../../shader_toy/Common.glsl)

## 1. Motivation

Hybrid GI (PT correction at half-res = ~1/4 spp screen-space ray tracing, see [hybrid_rc_pixel_correction_impl.md](hybrid_rc_pixel_correction_impl.md)) closed the visible quality gap between cascade-only MBRC and the PT reference (mode 16) — but at a cost: ~3-7 ms/frame for the correction + blur + merge passes on a 1280×720 viewport. v1.3 (DI cone + roughness MIS) did not move the needle (variance sweep §10.4 = TIE), so any further hybrid gains require either ~true~ light-position NEE (v1.3.2) or more samples — both push cost up, not down.

This doc asks the inverse question: **can MBRC alone reach hybrid-comparable quality, freeing the budget hybrid currently spends?** The user's diagnosis points at two specific failure modes of the current cascade pipeline:

1. **Long-distance GI lacks spatial resolution** — at C2/C3, probe spacing in non-co-located mode (32³/4 → 8³ probes covering 4m volume) is 0.5 m. A bright distant surface (red wall 3m behind camera) gets trilinearly smeared across 8 sparse probes; high-frequency directional contrast at the receiver becomes a low-frequency blob.
2. **Short-distance GI lacks radial (angular) resolution** — at C0 with default `dirRes=4`, each probe stores 16 octahedral bins ≈ 25° angular cells. Fine angular detail in near-field bounces (a narrow bright streak, a 10° caustic) gets averaged into a wide bin.

Hybrid GI sidesteps both because the per-pixel SDF trace gives **pixel-precise spatial origin** AND **fresh per-frame angular sampling**. Without that trace, MBRC has three levers: better consume-time reconstruction, better bake-side direction/probe budget allocation, and architectural changes that bypass the spatial probe grid for the parts the grid handles worst.

## 2. Current MBRC architecture (recap, with the limits)

- 4 cascades: C0 (finest spatial, fewest directions) → C3 (sparsest spatial, most directions)
- Non-co-located default: Ci uses (32 >> i)³ probes → 32³, 16³, 8³, 4³
- Direction encoding: octahedral, D² bins/probe with default D=4 (16 bins) at every cascade unless `useScaledDirRes=true` (C0=D2, C1=D4, C2=D8, C3=D16 = 4/16/64/256 bins)
- Bake rays: `baseRaysPerProbe × 2^i` per probe at Ci → 8/16/32/64 with default
- Ray intervals: `[intervalStart_i, intervalEnd_i)`, geometrically doubling: C0 → [0, 0.125m], C1 → [0.125, 0.25], C2 → [0.25, 0.5], C3 → [0.5, ∞) (numbers at default `baseInterval = 0.125m`)
- Consume: [raymarch.frag:429 sampleDirectionalGI](../../res/shaders/raymarch.frag#L429) does 8-tap trilinear over surrounding C0 probes, each ending in [raymarch.frag:395 sampleProbeDir](../../res/shaders/raymarch.frag#L395) which loops D² bins, accumulating `irrad += a.rgb * wcos * a.a` (cosine + α-gate)
- MB v2 (multi-bounce temporal, [multi_bounce_temporal_impl.md](multi_bounce_temporal_impl.md)): cascade bake reads its own previous-frame atlas at the hit point, cosine-weighted hemisphere integration, +3.5% brightness gain
- Hybrid v1.2.4 (PT correction): orthogonal layer riding on cascade for high-frequency detail; we are now asking how to retire it

**Quality gaps the architecture *cannot* close without changes:**
- Long-distance GI is **spatially limited by the C3 probe grid** (8³ probes, 0.5m spacing). Trilinear interp between sparse probes is a low-pass spatial filter.
- Short-distance GI is **angularly limited by the C0 direction count** (16 bins/probe at default). Per-bin sum is a low-pass angular filter.
- Both compose: short-distance receiver looking at long-distance bright source loses BOTH spatial AND angular detail (sparse probes contributing low-bin-count signal).

## 3. The lever taxonomy

Three tiers by cost. Within each tier, ideas are independent; pick a subset for a v2.0 cluster.

### Tier A — better reconstruction at consume time (no bake change)

**A1. Per-pixel BRDF-weighted bin selection (cos^k or top-K).** Currently bins are weighted by `wcos = max(0, dot(binDir, N))` — linear cosine. Replace with `pow(wcos, k)` for k=2..4 (tighter Phong-like reconstruction lobe) or only accumulate the top-K bins by `wcos`. Effect: grazing bins (often noisy from probe positions slightly off-surface) contribute less; near-front bins dominate, sharpening apparent angular detail at the receiver. Cost: 0 extra fetches; ~5 GLSL lines in [sampleProbeDir](../../res/shaders/raymarch.frag#L395). Risk: cos^k is biased away from energy conservation; must re-normalize by `Σ pow(wcos, k)` rather than `Σ wcos·α`.

**A2. Bicubic / Catmull-Rom spatial blend at distant cascades.** Replace the 8-tap trilinear in [sampleDirectionalGI](../../res/shaders/raymarch.frag#L429) with a 32-tap Catmull-Rom (or smoothstep-weighted bicubic). Apply ONLY at C2/C3 (where probe spacing is large enough that high-order interpolation matters); C0/C1 stay trilinear. Effect: smooth high-frequency far-field GI without banding. Cost: 4× probe fetches at the distant cascade; ~20 GLSL lines. Risk: bicubic kernels with negative weights can create ringing on hard radiance discontinuities — clip negatives if so.

**A3. SH2 directional projection at bake time (closed-form diffuse irradiance).** Bake side: alongside the D² bin atlas, project each probe's bins onto 2nd-order spherical harmonics (9 vec3 coefficients = 27 floats). Consume side: at primary hit, reconstruct directional irradiance via the **Ramamoorthi & Hanrahan 2001 closed-form diffuse SH formula** (`E(n) = Σ c_l A_l Y_l^m(n)` with A_0=π, A_1=2π/3, A_2=π/4). Effect: **for diffuse-only consumption, SH2 IS the appropriate basis** — the diffuse BRDF is a low-pass filter that erases everything above l=2, so SH2 reconstruction is equivalent to infinite bins (not "25 bins"; literally infinite for diffuse). This is the cleanest answer to "near-field radial blur" provided we are consuming diffuse. Cost: bake-side projection ~30 GLSL lines + a sibling 3D RGBA16F texture array (9 SH coeffs × volume = ~36 MB at C0 default); consume-side ~20 GLSL lines, closed-form, **no loop over bins**. Risk: SH2 cannot reproduce sharp angular features (sun ray, caustic) — those need the bin atlas. Mitigation in B1.

**A4. Variance-aware cascade weighting.** Mode 15 (TemporalOscillation, [raymarch.frag:412](../../res/shaders/raymarch.frag#L412)) already computes per-bin `4·α·(1-α)` oscillation signal. Use the AVERAGED per-probe oscillation to bias trilinear toward the upper cascade where C0 is unstable. Effect: reduces flicker in near-field where C0's coarse directions pick up jitter. Cost: ~10 GLSL lines; uses existing data. Risk: low risk; mostly a render-time mod.

**A5. 4-bin angular bilinear at consume.** Currently per-bin texelFetch picks the nearest of D² bins for the receiver normal. Replace with 4-bin bilinear between the 4 surrounding octahedral bins (analogous to what 5f bilinear does for upper-cascade reads at bake). Effect: removes directional banding that masquerades as "low radial resolution." Cost: 4× direction fetches per probe at consume; ~15 GLSL lines.

### Tier B — modest bake/memory cost

**B1. SH2 + per-bin RESIDUAL encoding.** Best of both worlds: store SH2 (smooth, low-pass) AND keep the D² bin atlas as the residual (atlas value MINUS SH reconstruction at bin direction). At consume, irradiance = `SH_diffuse(n) + Σ residual_bin × wcos × α`. Smooth SH gives the irradiance baseline, residual carries the high-frequency angular detail. Effect: high-quality diffuse anywhere (SH2 closed-form), high-frequency angular detail preserved (residual atlas). Cost: SH bake + atlas bake (both already present); ~50 GLSL lines + ~36 MB SH array. Risk: residual encoding doubles per-bin floating-point precision needs because the residual can be negative — RGBA16F is fine, RGBA8 won't work.

**B2. Invert `useScaledDirRes`: C0 = D16 (256 bins), C3 = D2 (4 bins).** Current `useScaledDirRes=true` puts MORE bins at coarser cascades on the theory that distant GI needs angular detail (small far light = narrow angular extent at receiver). The user's complaint says the opposite is the actual problem in *their* scenes — near-field detail is what's blurred. Inverting redistributes the total bin budget to where the user feels the loss. Cost: 1 line (the cascade dir-res schedule). Risk: this regresses the original-design scene category (large open scenes with small distant lights). Add a runtime toggle, not a default change.

**B3. Per-pixel mini-cone reconstruction at primary hit.** At each primary hit, instead of reading 8-trilinear × D²-bin = up to 256 atlas fetches, do this:
  1. Compute the dominant bin direction for the surface normal (octahedral encode of `N`).
  2. Sample 4 neighboring bins along the cosine-weighted lobe (importance-weighted: sample more bins where `wcos` is high).
  3. Trilinearly blend ONLY those 4 selected bins, weighted by `wcos × α`.
Effect: cheaper per pixel than full-D² loop, but with a SHARPER angular reconstruction than current uniform-weighted sum. Cost: ~30 GLSL lines; same fetch count as current at default D=4 but with smarter sample placement. Risk: at high D (e.g. D=16 from B2) the saving is real (256→4 fetches/probe); at D=4 it's wash. Best paired with B2.

**B4. Depth/normal-aware probe trilinear (reject occluded contributors).** Use the GBuffer normal at pixel; reject probe-corner contributions whose interpolated normal disagrees by >45° OR whose linear depth disagrees by >2× probe-cell-size. Renormalize remaining weights. Effect: kills probe-corner leak from far-side probes contributing to near-side shadowed pixels (the *spatial* version of the bake-side α-gate). Cost: ~15 GLSL lines; uses GBuffer that already exists ([raymarch.frag fragGBuffer](../../res/shaders/raymarch.frag)). Risk: when too many probe corners are rejected, the remaining few dominate and produce blocky artifacts; need a smoothstep rejection, not binary.

### Tier C — architectural changes

**C1. World-probe cubemap for distant GI (ShaderToy-inspired).** Bake a single low-res cubemap (64×64×6 = 24K texels) once per N frames (4-8) by tracing rays from scene center (or a few anchor points if center isn't representative). At raymarch primary hit, look up `worldProbeCubemap(direction)` for the distant-GI contribution; replace cascade C3 contribution for any pixel where `cellDistance(C3 probes) > someThreshold`. Effect: **distant GI gains infinite angular resolution** at the receiver (the cubemap is direction-keyed), at the cost of LOSING parallax (the cubemap is one-point-sampled). For scenes where distant GI is "the rest of the room from far away," parallax loss is invisible. Cost: bake cubemap once per N frames (~1 ms one-shot), single cubemap fetch at raymarch (~0.05 ms). Memory: 24K × RGBA16F × 6 = 1.2 MB. Risk: large open scenes (Sponza) need multiple anchor points or fail; per-anchor selection is its own problem.

**C2. Screen-space probe placement (first-hit anchored probes).** Place an irradiance probe at each pixel's primary surface hit; populate via 1-2 cone-aimed cascade taps + temporal accumulation. ReSTIR-style spatial reuse across adjacent pixels with depth/normal compatibility. Effect: per-pixel spatial resolution (no probe grid limit), per-pixel angular reconstruction. Cost: closer to hybrid PT pass cost; needs a per-pixel probe accumulator + ReSTIR pass (~2-3 ms estimated). Risk: full ReSTIR is a research-level commitment; cheaper variants (no spatial reuse, just temporal) may not move the needle.

**C3. Probe-as-VPL stochastic sampling at primary hit.** Treat each cascade probe as a Virtual Point Light with directional emission profile (its stored bins). At primary hit, stochastically pick 4-8 probes (weighted by `1/dist² × max(0, dot(N, dir_to_probe))`); for each, evaluate `probe_radiance_in_direction(reverse) × geometric_term`. Sum contributions. Effect: **receiver gains infinite spatial resolution** (probes are points, not interpolated). Angular resolution at receiver is bounded by `num_picks × D²`. Cost: per-pixel 4-8 probe atlas reads + per-probe direction lookup (~0.5-1 ms estimated). Risk: classic VPL singularities at `dist → 0` need clamping; energy conservation needs MIS or stochastic-uniform weighting.

**C4. Temporal angular accumulation (per-pixel, 1 bin/frame).** Each frame, every pixel samples ONE new cascade bin direction (rotated via blue noise / Halton across frames). EMA accumulate per pixel in screen-space irradiance buffer. Over N frames the accumulator hits every bin with proper weighting. Effect: per-pixel reconstruction at temporal cost only; converges to the "loop over all D² bins" answer in ~D² frames. Cost: screen-space irradiance buffer (RGBA16F × viewport = ~5 MB at 1080p) + 1 atlas fetch per pixel per frame. Risk: convergence-after-camera-move flicker; needs reprojection + history-rejection like TAA.

## 4. ShaderToy patterns we already have vs. what we don't

Quick audit of shader_toy/CubeA.glsl (and the identical Image.glsl) against current MBRC:

| ShaderToy technique | MBRC equivalent | Notes |
|---|---|---|
| Cubemap probe storage | 3D atlas (RGBA16F volume × directional res) | We chose volumetric for arbitrary probe placement; ShaderToy's cube approach is what C1 above proposes for distant GI |
| Cascade ray intervals (`tInterval = probeSize * 2 / 64`) | `intervalStart_i / intervalEnd_i` | Equivalent geometric doubling |
| Last cascade rays to infinity (`if cascade>4.5: tInterval = 10000`) | `intervalEnd[N-1]` set to volume diagonal | Equivalent; we could push further (true infinity ray for distant cubemap) |
| Smoothstep merge with `l = 1 - clamp(rayHit.t - interpMinDist)/interpMaxInterval` | We have similar `blendFraction` smoothstep at merge | Equivalent |
| Bounce light from cubemap at hit | MB v2 reads C0 atlas at hit | Equivalent (different storage); ShaderToy benefits from cubemap's lower-overhead lookup |
| Sun visibility ray from bake hit | We have direct-lighting at bake hit | Equivalent |
| WeightedSample bake-side visibility | Phase 3 v3 (`useWeightedSample`) | Equivalent; we found it's the wrong primitive for volumetric probes — see [visibility_phase3_impl.md](../6/claude_plan/visibility_phase3_impl.md) |
| 4-tap bilinear merge | Phase 5f bilinear | Equivalent |
| **No** SH or residual coding | A3/B1 above | ShaderToy doesn't help here — pure bins everywhere |
| **No** depth/normal-aware probe blending | B4 above | ShaderToy is wall-attached so depth is trivial; we have volumetric probes that need this |
| **No** world cubemap for distant GI | C1 above | ShaderToy *IS* a cubemap throughout; this is the "free lunch" we should harvest for the distant-GI case |

The key takeaway: ShaderToy's choice of cubemap storage gives them C1 *for free* — they don't need to "add" a world probe because the whole structure IS one (per probe, anyway). The cubemap layout is what makes their distant-GI quality believable at low cost.

## 5. Recommended v2.0 cluster (my honest ranking)

Picking the *cheapest credible wins* that don't overlap and address both named gaps:

| Rank | Item | Addresses | Cost | Why |
|---|---|---|---|---|
| 1 | **A3 SH2 projection + diffuse closed form** | short-distance radial | 1 day | Principled, textbook, no per-pixel runtime cost (single SH eval per pixel) |
| 2 | **A2 bicubic at C2/C3** | long-distance spatial | 4 h | Direct fix for distant-probe sparsity; surgical, optional toggle |
| 3 | **C1 world-probe cubemap** | long-distance spatial | 2 days | ShaderToy-proven; gives infinite angular at receiver for distant GI |
| 4 | **A4 variance-aware cascade weighting** | flicker / instability | 2 h | Uses existing mode-15 signal; defensive, low risk |
| 5 | **B4 depth-normal probe trilinear** | leak / shadow correctness | 4 h | Cleans up trilinear's blind spots; helps everywhere |
| Deferred | B1 (SH + residual) | both | 2 days | Wait until A3 alone is measured |
| Deferred | B3 (mini-cone) | short-distance radial | 1 day | Only worthwhile after B2 raises D; without B2 it's wash |
| Deferred | B2 (invert `useScaledDirRes`) | short-distance radial | 1 h | Cheap but risky; do as A/B toggle only |
| Deferred | C2 (screen-space probes) | both | 3+ days | Research commitment; effectively re-implements hybrid PT |
| Deferred | C3 (probe-as-VPL) | long-distance spatial | 2 days | VPL singularities are real; needs careful MIS |
| Deferred | C4 (temporal angular) | radial | 2 days | Convergence-after-move flicker; needs full TAA reprojection |

**Suggested phase ordering:**

- **Phase 9 v2.0a (≈ 1.5 days):** A3 (SH2) + A4 (variance-aware) + A2 (bicubic distant)
- **Phase 9 v2.0b (≈ 2 days, separate):** C1 (world cubemap)
- **Phase 9 v2.0c (≈ 0.5 day):** B4 (depth-normal trilinear)

Each phase ships with: (a) opt-in toggle, (b) variance/RMSE sweep against PT reference, (c) per-pixel diff heatmap vs the v1.3 hybrid baseline. **Acceptance criterion** for "MBRC retires hybrid": v2.0a+b+c on cornell-orig-alcove achieves RMSE within 5% of the hybrid v1.2.4 result against the same PT reference, at <20% the GPU time of hybrid.

## 6. Self-critique

(Per the established convention in [hybrid_v12_validation_phase8_impl.md §10.3](hybrid_v12_validation_phase8_impl.md), V/G/F classification.)

### Validated by current implementation

- **V1.** The current cascade trilinear is genuinely a low-pass filter (8-tap with `(1-fx)(1-fy)(1-fz)`-style weights collapse high-frequency probe-value variation into smooth output). User's diagnosis of long-distance spatial blur matches the math.
- **V2.** D=4 at C0 with 16 bins = 22.5° per bin assuming uniform octahedral. User's diagnosis of short-distance radial blur matches the math.
- **V3.** Hybrid v1.2 RMSE on cornell-default was 0.083 → 0.047 (44% reduction). Cascade alone cannot match this without changing one of: spatial reconstruction, angular reconstruction, or probe density.

### Gaps in this plan

- **G1.** No actual measurement of the two named quality gaps. The user's claim is qualitative; I have NOT produced an A/B image comparing far-field RC GI with far-field hybrid GI to confirm the spatial-blur diagnosis, nor a near-field comparison for the radial-blur diagnosis. **Without these, the priorities in §5 are guesses.** This is the *exact* failure mode the variance sweep §10.4 corrected for v1.3 — we should not repeat the bug-212 trap of building a feature without first measuring what we're trying to fix.
- **G2.** SH2-for-diffuse claim is *technically* correct (Ramamoorthi 2001 closed form), but it relies on assuming our shader's consume pass is *only* doing diffuse — which it currently is. If the user later adds glossy reflections (mentioned as a v1.3 motivation that didn't pan out), SH2 silently regresses for glossy and we'd need to fall back to bin atlas. The plan does not specify when/how the fallback triggers.
- **G3.** "Bicubic 4× cost at C2/C3" claim is unverified — at C3 with 4³ probes, 32 taps means we're reading ~half the entire cascade per pixel. Need to measure the actual cost on the GPU, not just count taps.
- **G4.** **C1 world cubemap assumes one anchor point.** Sponza is named as a target scene later in the project and would catastrophically violate that assumption (long corridor, no representative "scene center"). The plan dodges this by claiming "for scenes where distant GI is the rest of the room from far away" — that's circular reasoning. Either the cubemap needs multiple anchors (which is its own probe placement problem) or it's a cornell-only fix.
- **G5.** No estimate of how SH2 baking interacts with `useMultiBounce` (MB v2). MB v2 reads back the previous-frame atlas; if we add an SH array we'd need to SH-project at bake time AND read SH at MB-bake time, doubling the work. Not blocking, but unspecified.
- **G6.** The "retire hybrid" framing in §5 is premature. Hybrid solves more than spatial/radial blur — it also gives variance-correct unbiased PT signal that can validate cascade against ground truth (mode 16 wouldn't exist without the PT compute path that hybrid shares with). Retiring hybrid loses the validation infrastructure too.

### Flaws

- **F1.** A1 (cos^k bin weighting) is **biased**. `cos^k` for k>1 is NOT energy-conserving; the irradiance integral `∫ L(ω) cos(θ) dω` requires linear cosine, not cos^k. Using k>1 systematically darkens diffuse surfaces (less bin contribution → less irradiance). Listed as Tier A "low cost" but is actually a correctness regression masquerading as quality improvement. **Should be removed from the plan or relabeled "perceptual sharpening, energy non-conserving."**
- **F2.** B2 (invert `useScaledDirRes`) is listed as "1 hour" but the inversion to D16 at C0 means C0 atlas grows from 32³×16 to 32³×256 = 16× memory (~270 MB at default, vs ~17 MB currently). Cost claim is wildly wrong; this is a multi-day effort because the atlas allocation, layout, and the bake's inner loop all assume the schedule that lets the cascade fit in VRAM.
- **F3.** B3 (mini-cone) was described as "4 bins out of D² weighted by wcos" — but at D=4 (default), D²=16, so "pick 4 of 16" is more expensive (extra logic) than just doing all 16. Only worthwhile at high D — and high D only happens with B2 (which is broken per F2). The B3-after-B2 ordering in §5 is internally consistent BUT premised on the broken B2; without B2, B3 is dead.
- **F4.** C1 cubemap cost estimate of "1 ms one-shot" assumes 64×64×6 trace cost. At 6 faces × 64² = 24K rays, with our SDF-trace cost per ray (~5 µs), that's 120 ms. Real cost is 100× my estimate. Cubemap is plausible at much lower res (16×16×6 = 1500 rays = ~7 ms one-shot) or by tracing into the cascade rather than the SDF.
- **F5.** §5's "acceptance criterion: RMSE within 5% of hybrid at <20% the GPU time" is a numeric target with no validation path. We don't know what 5% looks like perceptually (see [hybrid_v12_validation_phase8_impl.md §9 B3](hybrid_v12_validation_phase8_impl.md): RMSE != perceptual quality). And "20% the GPU time" is unsourced — hybrid is 3-7 ms, so 20% = 0.6-1.4 ms — none of the v2.0 items have been timed to confirm they fit that envelope.

### Ranked improvement plan

Following the same convention as §10.3 of the hybrid impl doc.

| # | Improvement | Effort | Payoff | Depends on |
|---|---|---:|---|---|
| 1 | **Before any v2.0 implementation, capture a far-field A/B (cascade-only vs hybrid)** at cornell-orig-alcove using the existing sweep harness with `--use-hybrid=0/1` flag toggle. Per-pixel diff heatmap. **Measure first, plan second.** | 1 h | Validates or refutes §1 diagnosis; prevents bug-212-style A/B-unfalsifiable shipping | — |
| 2 | Same A/B but for near-field radial detail: use a scene with fine angular features (small bright window, narrow caustic) and compare cascade-only against hybrid on a per-pixel angle-aware metric (HDR-SSIM on the GI-only channel, mode 19). | 1 h | Validates §1's radial-blur claim | #1 |
| 3 | **Drop A1 cos^k from the plan.** Replace with "**A1' top-K bin selection by `wcos·α` with linear-cosine renormalization**" — pick the K bins with highest `wcos·α`, weight by `wcos·α`, normalize by `Σ wcos·α`. Energy-conserving, perceptually similar to cos^k, no bias. | doc | Fixes F1 | — |
| 4 | **Cost-verify B2** before promising it as Tier B: compute exact atlas-memory delta for D16 at C0, identify whether it fits in VRAM budget, identify all bake-side loop changes. | 2 h | Fixes F2; either re-classify as Tier C (multi-day) or kill it | — |
| 5 | **Cost-verify C1** with a 16×16×6 cubemap prototype (skipping the SDF trace, just sampling into C3's atlas). Time one bake. Compare quality vs C3 trilinear. | 4 h | Fixes F4; gates whether C1 is in v2.0b at all | — |
| 6 | **Define a SH2-vs-bin fallback policy:** for materials with `roughness < 0.7` (any glossy hint from MTL), the consume path uses bin atlas; for diffuse, SH2 closed form. Document in A3 spec before implementation. | 1 h | Fixes G2 | — |
| 7 | **Defer "retire hybrid" framing** until v2.0a+b+c are measured. Reframe acceptance criterion as: "v2.0 cluster produces a v1.3-comparable image at strictly lower GPU cost." Hybrid remains the validation oracle either way. | doc | Fixes G6 | — |
| 8 | **Drop the named GPU-time envelope from §5 acceptance criterion.** Replace with: "v2.0 cluster's per-frame cost MUST be measured and reported alongside the RMSE/perceptual metric; the user decides the trade-off." | doc | Fixes F5 | — |
| 9 | **Add a multi-anchor extension to C1** as part of the C1 plan: if the scene's bounding-box max dimension exceeds 2× the cubemap's effective parallax-tolerable radius, fall back to N anchor cubemaps with nearest-anchor lookup (or trilinear blend across anchors). Note this turns C1 from "free distant GI" back into "another probe grid placement problem." | doc | Fixes G4 | — |
| 10 | **Spec the SH-MB interaction** in A3: at MB-bake time, the per-bin read becomes "SH eval at bin direction + residual lookup" (if B1) or just "SH eval at hemi-sample direction" (if A3 alone). Document the extra `texelFetch` count. | 30 min | Fixes G5 | — |

## 7. Improved plan (post-critique)

Applying improvements #1-#10 from §6 inverts the order: **measure before building.** The revised v2.0 roadmap is:

**Phase 9 v2.0-pre (1 day):** Measurement phase. Capture A/B at two scenes (cornell-orig-alcove for short-range, sponza or open-cornell for long-range) with cascade-only vs hybrid-on, both against PT reference. Per-pixel HDR-SSIM and signed-luminance-delta heatmaps. **Conclude with a 1-page measurement report** listing the actual quality deltas the §1 diagnosis predicts, and identifying which scenes/regions show the largest gap. This is the data that justifies (or kills) each Tier item below.

**Phase 9 v2.0a (1 day, after pre):**
- A3 SH2 + closed-form diffuse, gated on `roughness ≥ 0.7` (item 6)
- A4 variance-aware cascade weighting
- A2 bicubic at C2/C3 (with measurement-derived priority — if §v2.0-pre shows C2 is fine and C3 is the actual problem, only apply at C3)
- A1' (top-K linear-cosine, NOT cos^k per item 3)

**Phase 9 v2.0b (2 days, if §v2.0-pre confirms distant-GI gap):**
- C1 world cubemap, after C1 prototype cost-verification (item 5)
- Multi-anchor extension (item 9) only if the prototype confirms parallax tolerance is too narrow

**Phase 9 v2.0c (4 h, defensive):**
- B4 depth-normal probe trilinear

**Killed or deferred:**
- A1 (cos^k) — replaced by A1' (item 3)
- B2 (invert dir-res) — cost-verify first per item 4; likely re-classify as Tier C
- B3 (mini-cone) — dead unless B2 lands (item 3 in original ranking is now coupled)
- C2 (screen-space probes) — too close to re-implementing hybrid (item 7)
- C3 (probe-as-VPL) — VPL singularities unresolved
- C4 (temporal angular) — convergence-after-move flicker unresolved

**Revised acceptance criterion (item 8):** v2.0 cluster ships with measured per-frame cost AND PT-RMSE AND PT-HDR-SSIM, presented as a 3-way table (cascade-only baseline vs cascade + v2.0 vs hybrid). User decides whether v2.0 alone is enough to retire hybrid, or whether v2.0 + reduced-cost hybrid (e.g. quarter-res correction instead of half-res) is the best Pareto point. Hybrid remains the oracle.

## 8. Files this would touch (estimate)

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | A3: SH2 projection at probe write; A4: oscillation already computed; B4: nothing (consume-side) |
| `res/shaders/raymarch.frag` | A2: bicubic; A3: SH2 eval + closed-form; A4: variance weighting; A1': top-K loop; B4: depth/normal-aware trilinear |
| `res/shaders/inject_radiance.comp` | A3 only if probe init writes SH |
| `src/demo3d.cpp` | New SH3D texture (RGBA16F × 7 = 9 vec3 SH coeffs packed); GUI for each toggle |
| `src/demo3d.h` | State + 5-10 setters |
| `src/main3d.cpp` | CLI flags for each toggle (per established convention) |
| `tools/hybrid_validation/v20_*` | New sweep dirs per phase |
| `tools/analysis/mbrc_v20_quality_plot.py` | New analysis script (extend v13 NEE plot template) |
| `doc/7/mbrc_quality_plan.md` | This doc |
| `doc/7/mbrc_quality_impl.md` | NEW per-phase impl doc |

## 9. Open questions for user — ANSWERED 2026-05-20

User decisions locked in:

1. **Hybrid retirement is the goal** — because hybrid is noisy (firefly/temporal residual even after v1.3.1). Hybrid code stays in the tree as fallback / regression reference, but the v2.0 quality target is "MBRC alone good enough that hybrid can be off by default." Implication: cubemap C1 should aim for full distant-GI replacement, not just hot-spot patching.
2. **Scene priority: cornell-orig-alcove only** for v2.0-pre measurement and v2.0a/b/c development. Sponza validation deferred to a separate v2.1 sweep after cornell is signed off. (Smaller test bed = faster iteration, controlled lighting = cleaner RMSE attribution.)
3. **Memory budget: unbounded.** Trade memory for quality/performance. This rules IN: B2 (inverted useScaledDirRes, 16× memory), generous cubemap atlases for C1, SH2+residual for A3+B1. Rules OUT only what would be slower per frame, not what would be bigger.
4. **Quality target: as close to unbiased PT reference as possible.** Mode 16 (PT reference) is the ground truth, NOT hybrid. Implication: **F1 (biased cos^k sharpening) is RULED OUT** — energy conservation matters because PT is the gold standard. SH2 (linear, closed-form, unbiased) is preferred over cos^k. Also rules out anything that introduces structured bias for sharpness; variance reduction via more samples / better reconstruction is OK.
5. **v2.0-pre measurement report is a separate, signed-off deliverable** before any v2.0a code. The measurement report is the next concrete artifact this plan produces; it tells us whether the §1 gap diagnosis is correct, and if so, which lever (A3 SH2 vs B2 angular vs C1 cubemap) actually closes the dominant error.

### What this re-derives

- **v2.0-pre stays as planned** (§7) — produces the measurement report.
- **v2.0a SH2 + bicubic + variance-weight cluster** is still the recommended first implementation phase, contingent on what v2.0-pre measures. Rationale: cheapest, energy-conserving (per #4), unblocked by #3.
- **v2.0b cubemap C1** now upgraded from "conditional" to "likely" given #1 (hybrid retirement) — because C1 is the only Tier-C lever that adds distant-GI quality without growing per-frame ray budget.
- **B2 inverted useScaledDirRes** newly viable (was ruled out on memory grounds in original Tier B). 16× angular bins at C0 = 256 bins/probe near-field, 4 bins far-field. Add to v2.0a as B2 alongside A3 — or stage as v2.0a-second-half if SH2 alone already closes near-field radial gap.
- **C4 temporal angular accumulation** still secondary; convergence latency (question 4) is moot once C1 + B2 + A3 already bring quality to PT-near, because temporal accumulation is then a polish lever not a quality necessity.

### Next concrete deliverable

**v2.0-pre measurement plan + report.** Separate doc (`doc/7/mbrc_v20_pre_measurement_plan.md`) that scopes:
- What to instrument (per-cascade RMSE decomposition, angular-vs-spatial error split, far-vs-near pixel partition).
- What artifacts to produce (heatmaps, RMSE tables, per-cascade contribution percentages).
- Sign-off criteria for "diagnosis confirmed" vs "diagnosis wrong, replan."
- Estimated effort: 1 day, no shipping code changes — only diagnostic render modes + analysis script.
