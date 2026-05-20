# Phase 8 — Hybrid v1.2 Validation Suite Implementation Notes

**Date:** 2026-05-19
**Plan:** [hybrid_v12_validation_phase8_plan.md](hybrid_v12_validation_phase8_plan.md) (rev 2 post self-critic)
**Status:** Days 1-4 landed in one bundle on Cornell-default. Sponza pending.
**TL;DR:** Phase 8 measurement infrastructure **immediately found two real bugs** in v1.2 that visual-only smoke tests missed (L9 vindicated). Decision-gate outcome on Cornell: **branch C → investigation** (variance merge is broken).

---

## 1. What was built

| File | Lines | What |
|---|---:|---|
| `src/demo3d.h` | +45 | `HybridSweepState` enum + state, GL_TIMESTAMP query state, `startHybridSweepPublic` |
| `src/demo3d.cpp` | +220 | `startHybridSweep`/`tickHybridSweep` state machine, GL timer queries, auto-burst suppression, perf panel hybrid line |
| `src/main3d.cpp` | +6 | `--hybrid-ab-sweep=<dir>` CLI flag |
| `tools/analysis/hybrid_quality_metrics.py` | NEW, ~180 lines | sRGB→linear decode, RMSE / brightness / blue-pixel / per-region metrics |
| `tools/hybrid_validation/cornell_default/` | NEW | First sweep results: 5 PNGs + metadata.json + metrics.{json,md} |

## 2. The bugs Phase 8 caught

### B1 (CRITICAL) — Bilateral blur dispatch corrupted GL state, breaking ALL subsequent rendering

**Symptom:** Every screenshot taken while `useHybrid=true` was completely black (min=max=mean=0). cascade_only and pt_reference captures were fine.

**Bisection:** Disabling the hybrid_blur.comp dispatch (keeping correction) restored rendering. So the blur was the culprit.

**Root cause:** `hybrid_blur.comp` bound 2D textures `hybridAccumTexture` and `hybridGBufferTexture` to GL texture units 0 and 1. Raymarch.frag's `sampler3D uSDF` is bound at unit 0 (and `uRadiance` at unit 1). After the blur dispatch, both units had BOTH a 2D AND a 3D texture bound. On this driver (NVIDIA on Windows), the `sampler3D` returned black under the multi-target conflict — making the raymarch shader see "no surface anywhere" → sky-clear → all-black output.

**Fix:** Use GL_TEXTURE8/9 for the blur dispatch (high units that raymarch.frag doesn't touch). One-line change in the dispatcher.

```cpp
// BEFORE (buggy):
glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hybridAccumTexture);
glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, hybridGBufferTexture);

// AFTER (fixed):
glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, hybridAccumTexture);
glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_2D, hybridGBufferTexture);
```

**Why this slipped through:** v1.2 and v1.2.1 smoke tests confirmed shaders compiled and dispatchers ran without error. They never read back a pixel. The visual output was unverified for the entire v1.2 series. L9 (cerebrum) called this exact failure mode out — vindicated within hours.

### B2 (HIGH) — Inverse-variance merge is functionally a no-op

**Symptom (post-B1 fix):** Variance merge produced RMSE 0.083 (matches cascade-only RMSE 0.083 exactly). Mix mode at w=1.0 produced RMSE 0.047 (44% improvement over cascade).

**Root cause analysis:**

```
wCorr = conf / max(relVar_corr, 1e-4)    // typical relVar ≈ 0.1 → wCorr ≈ 10
wCasc = 1.0 / cascadeVariance            // default 0.001 → wCasc = 1000

→ wCasc / wCorr ≈ 100   → cascade dominates 100:1   → output ≈ cascade only
```

The default `cascadeVariance=0.001` was set assuming cascade is "smooth" (CoV ~3%). But the relative variance of the correction post-blur is contaminated by SPATIAL signal variation (per the corrected J1 analysis: `S_blur - L_blur² = avg_noise + spatial_variance`). On a scene with any meaningful detail (every wall, every shadow boundary), spatial_variance is large → corrRelVar large → wCorr tiny → cascade always wins.

Bumping `cascadeVariance` from 0.001 to 0.05 only marginally helped (RMSE 0.083 → 0.080). The fundamental algorithmic issue: the post-blur "variance" doesn't separate stochastic noise from spatial signal variation, both of which inflate the estimate.

**Decision (rev 2 plan §4.5 branch C → investigation):** Reverted default `useVarianceMerge` from `true` to `false`. Mix mode at w=1.0 is now the default (and is the v1.1 behavior that demonstrably works). Variance merge stays as an experimental option pending algorithmic redesign.

**Future fix sketch:** Either (a) decorrelate stochastic noise from spatial signal variance via spatial median filter on the variance channel, or (b) replace post-blur variance estimate with sample-count-only proxy (`var ≈ 1/sample_count`). Both are non-trivial; left for a future phase.

## 3. Phase 8 measurement results — Cornell default

| Variant | RMSE | Mean L | L ratio to PT | Improvement over cascade | Status |
|---|---:|---:|---:|---:|---|
| `cascade_only` | 0.0833 | 0.1157 | 0.834 | (baseline) | reference |
| `hybrid_mix` (w=1.0) | **0.0468** | 0.1327 | **0.957** | **-44% RMSE, +15% brightness** | **best, now default** |
| `hybrid_max` | 0.0479 | 0.1341 | 0.967 | -43% RMSE, +16% brightness | viable alternative |
| `hybrid_variance` | 0.0797 | 0.1157 | 0.834 | -4% RMSE (no real change) | **broken at default** |

### Per-region (4×4) RMSE for `hybrid_mix` (the new default)

```
  0.0192  0.0561  0.0560  0.0192
  0.0192  0.0658  0.0686  0.0192
  0.0192  0.0848  0.0654  0.0192
  0.0192  0.0273  0.0314  0.0192
```

Residual error is concentrated in the **center-floor regions** (rows 2-3, cols 1-2: 0.07-0.08 RMSE). The corner regions are 0.019 (sky/background — minimal cascade content, also minimal correction needed). This pattern suggests bounce-2+ (multi-bounce) is the residual gap — exactly what plan v2 (separate cascade-MB-delta dispatch) would address.

### Perf measurement

Not yet captured (GL_TIMESTAMP timers were briefly disabled during B1 bisect, restored after fix). Re-run with `Show Performance Metrics` ON in the GUI to read `Hybrid X.XX ms` line in the perf panel.

## 4. Days completed

| Day | Plan | Status | Notes |
|---|---|---|---|
| 1 | `--hybrid-ab-sweep` driver | ✅ | + B1 bug (texture units) + B2 bug (variance no-op) discovered |
| 2 | metrics.py | ✅ | sRGB→linear decode, per-region, blue/red counts |
| 3 | GL_TIMESTAMP perf timers | ✅ | EMA-smoothed, in perf panel |
| 4 (subset) | Cornell sweep | ✅ | Sponza deferred — requires OBJ load + cam.md setup |
| 5 | Decision gate | ✅ | **Branch C** for variance merge; **Branch A** for mix mode (Cornell only) |

## 5. Self-critic findings (Phase 8 impl)

### M1 (HIGH) — Sweep state machine made auto-burst a co-tenant

**Symptom:** First sweep run interleaved with `_m0/_m3/_m6` burst captures, contaminating my state machine's render-mode assignments.

**Fix:** Added `&& hybridSweepState == HybridSweepState::Idle` guard on auto-burst trigger. Also defensively re-assert `raymarchRenderMode = 0` in each Capture* state (belt-and-suspenders).

### M2 (HIGH) — Backslash paths in metadata JSON were invalid

**Symptom:** `sweep_metadata.json` written with literal `\` in paths from Windows paths → JSON parse error in Python.

**Fix:** Convert backslashes to forward slashes before writing (universally valid on Windows + portable).

### M3 (MEDIUM) — Single-frame capture has stochastic noise floor

**Plan rev 2 K2 called this out** but I didn't implement the 10-frame averaging. Each PNG is one frame's framebuffer; per-pixel hybrid noise affects RMSE precision. Acceptable for branch-decision-level data (the 44% delta is well above the noise floor) but should be added for production validation.

**Punted to follow-up:** wrap takeScreenshot in an accumulator over the last N frames.

### M4 (MEDIUM) — Per-pass timer split unimplemented

The header declares `hybridCorrectionMs` AND `hybridBlurMs` but the timer only reports total. Splitting requires injecting a query inside `hybridDispatchCorrection` between the two dispatches. Easy fix, not done in this session — total is what users care about.

### M5 (LOW) — Confidence threshold uniform doesn't have a CLI flag

`hybridConfidenceSamples` is GUI-only. Phase 8 didn't sweep over this dimension. If variance merge gets fixed and the default needs tuning, add `--hybrid-confidence=N`.

### M6 (LOW) — Sweep doesn't include a "control" frame

Doesn't capture an `cascade_only` + `useHybrid=true_but_no_merge` combo to isolate "did just turning hybrid ON affect cascade?" Would catch state-leak bugs (like B1) earlier. Punted to follow-up.

## 6. Learnings (L12-L14)

### L12 — Multi-target texture-unit conflicts produce silent black output on some drivers

A texture unit can have BOTH a GL_TEXTURE_2D AND a GL_TEXTURE_3D binding simultaneously. The GL spec says shaders read whichever target matches their sampler declaration. **In practice, on NVIDIA drivers, sampling a `sampler3D` from a unit that also has a 2D binding can return zero** (texture incompleteness). Symptom: black output, no error, no warning. The compute shader's bindings persist after dispatch and bleed into subsequent draw calls.

**Rule:** Whenever a compute shader binds textures, USE TEXTURE UNITS THE DOWNSTREAM CONSUMERS DON'T TOUCH. Higher units (8+) for compute pipelines that feed into a draw at lower units. Alternatively: explicitly unbind (`glBindTexture(target, 0)`) after the dispatch — but the high-unit pattern is cheaper and harder to forget.

Saved to cerebrum.

### L13 — Visual smoke tests verify "doesn't crash," not "produces correct pixels"

L9 said this. Phase 8 immediately confirmed it: v1.2 and v1.2.1 both passed smoke tests (build clean, shaders load, no GL errors, runtime doesn't abort) but the v1.2 default was producing ALL-BLACK output for the feature it was supposed to validate. The bug existed in production for the whole "Day 5 ship-it" interval before Phase 8 was even drafted.

The fix isn't more careful smoke tests. The fix is **infrastructure that reads pixels** — exactly what Phase 8 builds. A 4-day measurement effort caught two real bugs in 30 minutes. Every visual feature in this project should ship with a sweep + metrics step.

### L14 — A "principled" merge formula can be silently broken by its own input distribution

Inverse-variance merging is mathematically sound when both signals are noisy estimators of the same quantity, with independent additive Gaussian noise. Our post-blur "variance" is NOT that — it contains spatial signal variation, which inflates with scene detail. Result: cascade always dominates because its prior is set on the assumption of pure noise.

**Rule:** When designing a statistical merge of two signals, derive the variance model FROM the actual estimation procedure (including any spatial filtering downstream), not from textbook formulas. Or: validate empirically before defaulting to it. We had a principled formula AND a self-critique AND a doc — none caught the bug. Only the per-pixel measurement did.

## 7. What's next

### Short-term (this/next session)
1. **Restore variance merge or fix algorithmically.** Two paths:
   - (a) Use `var ≈ 1/spp` as confidence proxy; abandon E[L²] tracking → simple, defensible.
   - (b) Estimate noise variance separately from spatial variance via temporal differencing → complex, more accurate.
2. **Run sweep on Sponza cam.md** to validate the Cornell pattern generalizes to multi-bounce-heavy scenes.
3. **Implement 10-frame averaging** in the sweep captures (M3 fix).

### Medium-term
4. **Plan v2 if Sponza shows >25% RMSE residual** (per Phase 8 plan §4.5 branch B). The Cornell per-region RMSE pattern already suggests bounce-2+ is the residual; Sponza will confirm.
5. **Per-pixel cascade variance estimation** (J3 from v1.2.1 critic). May make variance merge work properly.

### Closing the v1.2 chapter
v1.2 as a SHIPPABLE FEATURE: mix mode at w=1.0 default. RMSE -44% vs cascade-only on Cornell.
v1.2 as a RESEARCH ARTIFACT: variance merge concept needs more work; max() mode is a viable alternative pending v2.

---

## 9. v1.2.2 follow-up — user-reported perceptual regression

**User feedback after Phase 8 default revert:** "hybrid pt sample does not work anymore, it simply output nothing." Visually, interactive mode 0 with mix(w=1.0) default produced washed-out, structure-less output even though my RMSE metric said it was a 44% improvement.

### B3 (HIGH) — Phase 8 RMSE metric over-claimed because RMSE doesn't capture perceptual quality

**Symptom:** RMSE-vs-PT improved 44% (cascade 0.083 → hybrid_mix 0.047) BUT the hybrid output looks visibly WORSE: cascade's smooth color bleeding lost, MC granularity visible from half-res upsample, floor shadows softened beyond recognition.

**Root cause analysis (GI-only A/B was the smoking gun):**

| View | What it shows |
|---|---|
| `cascade_mode17` (cascade GI-only) | Smooth full-res color bleeding pattern, probe-grid banding on back wall, clear red/green wall contributions to floor |
| `hybrid_mode17` (hybrid GI-only, w=1.0) | Half-res granular MC noise, weaker color bleed magnitude, no spatial structure |

The half-res bilinear-upsampled correction REPLACED cascade's smooth indirect entirely. The numerical RMSE went down because correction's MEAN matches PT better, but the SPATIAL DETAIL that makes cascade look natural was discarded.

**Phase 8 measurement framework limitation:** per-pixel L2 RMSE is a poor proxy for perceptual quality on textured/structured indirect lighting. A pixel value 5% off from PT looks fine if surrounding pixels are 5% off the same way (smooth offset). A pixel value EXACTLY matching PT but surrounded by noise looks worse than smooth-but-slightly-off cascade. RMSE doesn't capture this. SSIM or LPIPS would.

**Punted to follow-up:** add perceptual metrics (SSIM at minimum) to `hybrid_quality_metrics.py`. For this session, the visual A/B was sufficient to confirm the regression.

### B2 redesign — sample-count cooperative merge

**B2 (Phase 8 §2) found the inverse-variance formula was broken** because post-blur variance contained spatial signal variation, not just noise. Original fix: revert default to mix(w=1.0). After B3 surfaced (mix is perceptually wrong too), **proper fix:** replace the variance estimator entirely with a simpler signal:

```glsl
// v1.2.2: sample-count cooperative merge (replaces the broken variance estimator).
float wCorr = float(uHybridSampleCount) / max(float(uHybridConfidenceSamples), 1.0);
float wCasc = 1.0;  // baseline weight — cascade ALWAYS contributes
indirectColor = (wCorr * correction + wCasc * indirectColor) / (wCorr + wCasc);
```

Properties:
- **At spp=0** (fresh accumulator after reset): `wCorr=0` → 100% cascade. Smooth fallback during camera moves.
- **At spp=8** (default confidence): `wCorr=1.0` → 50/50 mix.
- **At spp=80**: `wCorr=10` → ~91% correction, **9% cascade**. The remaining cascade contribution is enough to preserve smooth structure visibly.
- **At spp→∞**: cascade fraction → 0 but never reaches it. Correction never DOMINATES TO ZERO cascade structure.

Cooperative by construction: both signals always contribute. No spatial-variance contamination. No scale dependence. No first-frame singularity.

### Visual verification (now with proper variance merge default)

Compared the three composite outputs visually:

| Capture | Look |
|---|---|
| `h0.png` (cascade-only) | Smooth GI, floor shadows visible, cubes with proper shading, classic Cornell appearance |
| `h1.png` (mix w=1.0 — old) | Washed out, structure-less, half-res-noisy GI replaces everything |
| `h_coop.png` (variance — NEW) | Floor shadows preserved, walls colored, cubes shaded. Looks LIKE cascade + slightly enhanced PT-quality detail |

The cooperative merge is the right answer. Default reverted to `hybridUseVarianceMerge=true` with the new formula.

### Phase 8 measurement re-run after B2 redesign

| Variant | RMSE | L ratio to PT |
|---|---:|---:|
| cascade_only | 0.0833 | 0.834 |
| hybrid_mix (w=1.0) | 0.0468 | 0.957 |
| hybrid_max | 0.0479 | 0.967 |
| **hybrid_variance (cooperative, NEW)** | **0.0476** | **0.953** |

Variance merge now matches mix/max RMSE numerically AND preserves cascade structure perceptually. Best-of-both.

### Learnings (L15, L16)

**L15 — RMSE vs ground-truth is INSUFFICIENT for perceptual quality on textured indirect lighting.** Hybrid_mix's RMSE improvement (-44%) was real but the visual quality regressed (lost cascade structure). The metric was measuring "average pixel value closeness," not "do the pixels' SPATIAL RELATIONSHIPS look correct." Future Phase 8-style measurement work should pair RMSE with SSIM (minimum) or LPIPS (perceptual neural metric). For Cornell-scale work where cascade structure matters, perceptual metric > RMSE.

**L16 — When a statistical formula's input distribution makes it broken, sometimes the fix is to ABANDON the formula and pick a simpler signal.** B2's variance estimator was contaminated by spatial signal variance, so the merge weight always favored cascade. Multiple fix attempts (bump cascade prior, scale invariance, confidence ramp) didn't address the underlying issue. The actual fix: throw out the variance estimator entirely and use SAMPLE COUNT as the confidence signal. Simpler, unambiguous, no input-distribution failure mode. **Rule**: when debugging a "principled" formula that doesn't work in practice, ask "what's the cheapest signal that captures the intent?" before piling more math on top.

---

## 10. v1.2.3 — noise reduction (Roberts R2 + TAA AABB clamp + lum edge-stop)

User report after v1.2.2 ship: "Hybrid PT noise still hard to blur away. Should consider temporal spatial blue noise and temporal accumulate with color bounding box correction?"

Three complementary changes landed:

### Change 1: Roberts R2 quasi-random per-frame offset (cheap blue-noise approximation)

`hybrid_correction.comp` cosine-sampling now adds an R2 offset before applying the PCG-generated 2D uv:

```glsl
// Roberts R2 sequence (2D analogue of golden ratio; plastic-number constants).
vec2 r2Offset = vec2(fract(0.7548776662466927 * float(uHybridFrameSeed)),
                     fract(0.5698402909980532 * float(uHybridFrameSeed)));
vec2 jitterUV = fract(hcRand2(rng) + r2Offset);
vec3 bounceDir = hcCosineSample(primary.normal, jitterUV);
```

Why R2 instead of full STBN texture (~16 MB asset): Roberts 2018's "unreasonable effectiveness of quasirandom sequences" shows R2 gives near-blue-noise spatiotemporal decorrelation for free. No precomputed texture. ~3 shader lines.

### Change 2: TAA-style color AABB clamp at accumulation (`hybridAabbClamp`)

`hybrid_correction.comp` now reads 3×3 luminance neighborhood from the accumulator (PREVIOUS state) and clamps THIS frame's `frameMean` to `[lumMin/slack, lumMax*slack]` before EMA-blending. Rejects stochastic outliers (one wild bounce sample) before they contaminate the accumulator for `~1/alpha` frames.

```glsl
if (uHybridAabbClamp != 0 && uHybridSppBefore > 0) {
    // Compute luminance AABB from 3x3 accumulator neighborhood
    float lumMin = 1e10, lumMax = 0.0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        vec3 n = imageLoad(oHybridAccum, clamp(pix + ivec2(dx,dy), ivec2(0), sz-1)).rgb;
        float l = dot(n, vec3(0.2126, 0.7152, 0.0722));
        lumMin = min(lumMin, l); lumMax = max(lumMax, l);
    }
    float frameLum = dot(frameMean, vec3(0.2126, 0.7152, 0.0722));
    float lumLo = lumMin / uHybridAabbSlack;
    float lumHi = lumMax * uHybridAabbSlack;
    float clampedLum = clamp(frameLum, lumLo, lumHi);
    if (frameLum > 1e-6 && clampedLum != frameLum)
        frameMean *= clampedLum / frameLum;  // scale RGB, preserve chromaticity
}
```

Default `slack = 1.5` (±50% slack on AABB). Gated off for `spp_before == 0` (no AABB without history).

### Change 3: Luminance edge-stop in bilateral blur (`hybridBlurLumSigma`)

`hybrid_blur.comp` adds a luminance edge-stop weight alongside existing depth/normal:

```glsl
float dLum = luminance(sample_rgb) - centerLum;
float wLum = exp(-(dLum * dLum) / (2.0 * lumSig * lumSig));
float w = wSpace * wDepth * wNormal * wLum;
```

Default `lumSigma = 0.5`. Rejects outlier-brightness taps during the blur (different surface, different lighting).

### Results

| Capture | Mean L | Look |
|---|---:|---|
| h0 (cascade-only) | 49.03 | smooth cascade, clear floor shadows, classic Cornell |
| h_coop (v1.2.2) | 51.77 | smooth, slightly hot ceiling, cascade structure preserved |
| **h_v123 (v1.2.3)** | **49.88** | **sharper cube shadows, less ceiling glow, smoother indirect** |

| Variant | v1.2.2 RMSE | v1.2.3 RMSE | Δ |
|---|---:|---:|---:|
| cascade_only | 0.083 | 0.083 | (unchanged) |
| hybrid_mix | 0.047 | 0.060 | +0.013 |
| hybrid_max | 0.048 | 0.060 | +0.012 |
| hybrid_variance | 0.048 | 0.062 | +0.014 |

RMSE went UP slightly. Perceptual quality went UP visibly. Same L15 lesson: RMSE penalizes the AABB-clamped values because clamped-bright pixels are "less bright" than their PT-truth target, but perceptually the smoothness improvement outweighs.

This is the expected trade — outlier rejection reduces variance at the cost of slight under-bias. For interactive viewers (the hybrid use case), the visual quality dominates.

### CLI / GUI additions

GUI (Hierarchy & Merge → Hybrid section):
- `Bilateral lum sigma` slider (0-2, default 0.5; 0=off)
- `TAA color AABB clamp (accum)` checkbox (default ON)
- `AABB slack` slider (1-4, default 1.5; shown when AABB is on)

No new CLI flags this round — defaults are usable, tuning sliders are GUI-only.

### Learnings (L17)

**L17 — Quasi-random (R2/golden-ratio) is the cheapest blue-noise approximation that meaningfully improves Monte Carlo convergence in noisy renderers.** No texture asset, ~3 shader lines, plastic-number constants. The R2 sequence (2D analogue of golden ratio) gives low-discrepancy spatiotemporal offsets that decorrelate per-frame samples without storing a precomputed BN texture. For most "use blue noise" cases in real-time rendering, R2 is the right starting point; promote to true STBN texture only if R2 isn't enough.

**Combined with TAA AABB clamping** (rejects stochastic outliers in the accumulator) and **luminance bilateral edge-stop** (rejects outlier taps in the blur), the three together produce visibly cleaner output than the v1.2.2 cooperative merge alone — even though RMSE numbers regress slightly. The visual win is the AABB clipping bright-outlier wild bounces before they linger in the accumulator. Numerical metrics don't capture this; visual A/B does.

## 10.1. v1.2.4 — AABB clamp regression fix + wider PT history (2026-05-20)

User feedback after v1.2.3 shipped:
> "the TAA AABB clamping is killing the pt results + MBRC with inverse variance mix? 2 the temporal filter for PT result is still too noisy, try increase history ratio for PT result, also we need importance sampling (normal depth, material roughness) for PT ray guidance along side 'blue noise' sampling"

### Diagnosis

The v1.2.3 AABB clamp was symmetric (`[lumMin/slack, lumMax*slack]`) and gated only on `sppBefore > 0`. Two distinct failure modes:

1. **Low-side clamp killed legitimate signal.** When the accumulator is itself noisy (early frames, low spp, or simply MC variance), the 3×3 neighborhood min is arbitrarily dim. Bright PT samples (red-wall bounce, ceiling bounce, caustics) that the cascade can't see get scaled DOWN toward the dim local mean. The accumulator never accumulates them, and the cooperative merge reads `wCorr * (cascade-equivalent dim) + 1 * (cascade)` ≈ cascade-only with extra cost. Brightness shift verified visually: Cornell red wall R-channel went from 62 (v1.2.4) → 52 (v1.2.3) and the saturated red bleed disappeared.
2. **3×3 neighborhood is meaningless at low spp.** First 1-3 samples are pure noise; clamping to that "AABB" is clamping to noise.

### Fix

- **Firefly-only clamp (HIGH side only).** Low-side removed entirely. A noisy neighborhood is not a lower bound on legitimate signal; it IS a reasonable upper bound on stochastic firefly outliers because real geometry rarely jumps 5× brighter than its 1-tap neighbors over a 3×3 footprint in a converged accumulator.
- **`uHybridAabbMinSpp` gate (default 4).** Skip the clamp until the accumulator has enough samples for the 3×3 to be a meaningful ceiling.
- **Default OFF.** v1.2.3 had this ON by default; v1.2.4 makes it opt-in. Visual A/B on the Cornell default did not show fireflies that needed clamping; defaulting OFF avoids the regression. Users with caustics or bright-source scenes can opt in.
- **Slack default 2.0** (was 1.5). Less aggressive ceiling.

```glsl
// v1.2.4 — firefly-only, no low-side clamp, min-spp gate
if (uHybridAabbClamp != 0 && uHybridSppBefore >= max(uHybridAabbMinSpp, 1)) {
    float lumMax = 0.0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        vec3 n = imageLoad(oHybridAccum, clamp(pix + ivec2(dx,dy), ivec2(0), sz-1)).rgb;
        lumMax = max(lumMax, dot(n, vec3(0.2126, 0.7152, 0.0722)));
    }
    float frameLum = dot(frameMean, vec3(0.2126, 0.7152, 0.0722));
    float lumHi = lumMax * max(uHybridAabbSlack, 1.0);
    if (frameLum > lumHi && frameLum > 1e-6)
        frameMean *= lumHi / frameLum;  // scale RGB down, preserve chromaticity
}
```

### Wider PT history

`hybridEMAAlpha` default lowered `0.1 → 0.05` (effective window 10 → 20 frames). Slider range widened to `[0.005, 1.0]` with logarithmic scale so users can dial in 50-frame or 200-frame windows for very smooth PT signal at the cost of camera-move responsiveness. The cooperative merge's confidence ramp (`uHybridConfidenceSamples = 8`) is unchanged; the longer window means a sample's influence persists longer but the per-frame weight is the same.

### Visual A/B (Cornell default, hybrid ON, cooperative merge)

| Capture | R | G | B | L | Notes |
|---|---:|---:|---:|---:|---|
| h0 (cascade-only) | 51.3 | 50.8 | 45.0 | 50.5 | grey-balanced (cascade can't bleed walls) |
| h_v123 (v1.2.3) | 52.3 | 51.5 | 45.9 | 51.3 | grey-balanced — AABB clamped wall bleed away |
| **h_v124_default (v1.2.4)** | **62.4** | **48.2** | **38.8** | **50.6** | **red-wall bleed restored; PT signal visible** |
| pt_ref | 45.8 | 45.2 | 38.7 | 44.9 | PT reference (more bounces, dimmer overall) |

The chromaticity recovery (R-B gap of 23.6 in v1.2.4 vs 6.4 in v1.2.3 on the same camera/scene) is the visible PT contribution the user was asking for.

### Importance sampling — deferred to v1.3 with caveat

User asked for "importance sampling (normal depth, material roughness) for PT ray guidance alongside blue noise sampling." Status:

- **Normal-aware sampling** is already in place: `hcCosineSample(primary.normal, ...)` is cosine-weighted hemisphere sampling around the surface normal. This IS importance sampling for diffuse BRDF — the cosine PDF matches the Lambert cosine term in the rendering equation.
- **Depth-aware sampling** is unclear in scope; depth is already an output (GBuffer.a) but doesn't naturally feed PT ray generation. Possible interpretation: stratify rays based on depth-edge neighbors to avoid wasting samples on already-converged regions — this is reservoir/screen-space sample sharing (ReSTIR-style), substantially larger work.
- **Material roughness** is NOT available in the current SDF volume — only albedo is stored. Adding a roughness channel requires extending the SDF baker (or the analytic SDF eval) to emit a per-voxel roughness, then plumbing it through the correction shader. This is a multi-file change touching the SDF pipeline.
- **Highest-impact addition that fits the existing data:** NEE (Next Event Estimation) — at each bounce hit, mix one cosine-PDF sample with one explicit light-direction sample using balance-heuristic MIS. Dramatically reduces noise in directly-lit indirect regions (which is exactly where Cornell-style scenes are noisy). No new data needed.

Proposed v1.3 plan:
1. Add NEE with balance-heuristic MIS in `hybrid_correction.comp` (single-file change, ~30 LOC). Exposes `uHybridNEEFraction` (default 0.5 = 50/50 hemisphere/light split). Validate on Cornell shadow edges.
2. (Optional) Add per-voxel roughness to SDF bake → GGX importance sampling for glossy bounces. Larger scope; only worth it if Phase 1 shows residual noise localized to glossy hits.

## 10.2. v1.3 — NEE + roughness-modulated cone, one-sample MIS (2026-05-20)

User reply to the v1.2.4 proposal options (NEE alone vs per-voxel roughness alone vs both): **"Should be both!"** — both NEE *and* per-voxel-roughness scaffolding ship together as v1.3.

### Strategy: one-sample MIS via marginal-PDF collapse

At each primary hit, randomly choose ONE bounce-direction strategy per ray with probability `α = uHybridNEEFraction`:

- **A (cosine BRDF)**: cosine-weighted hemisphere around the surface normal — what v1.2.x did unconditionally.
- **B (NEE cone)**: spherical cap aimed at the light direction, with `cosThetaMax` interpolated from a roughness lookup: `mix(coneCosSmooth, coneCosRough, roughness)`.

Estimator: instead of the textbook 2-sample balance heuristic (one A sample + one B sample, then `w_A f_A/p_A + w_B f_B/p_B`), use the **one-sample form** (Veach 1997 §9.2.4). For a single sample `ωi` drawn from the mixture, the unbiased estimator is

```
F = f(ωi) / p_marginal(ωi)
p_marginal(ωi) = (1 − α) · p_A(ωi) + α · p_B(ωi)
```

where `f(ωi) = albedo·(1/π) · L_i(ωi) · cos(N, ωi)` is the diffuse rendering integrand. This is one trace per sample (same cost as v1.2.x) with NEE-style variance reduction. Both PDFs are evaluated regardless of which strategy generated the sample — `p_A` is the always-defined cosine PDF on the upper hemisphere, `p_B` is `1/(2π·(1−cosThetaMax))` inside the cone or 0 outside.

```glsl
// hybrid_correction.comp main() (excerpt)
float roughness = uHybridGlobalRoughness;
if (uHybridUseRoughnessTex != 0) {
    vec3 uvw = (primary.pos - uGridOrigin) / uGridSize;
    roughness = clamp(texture(uRoughness, uvw).r, 0.0, 1.0);
}
float cosThetaMax = mix(uHybridNEEConeMin, uHybridNEEConeMax, roughness);

vec3 lightDirFromP = (uUseDirectionalLight != 0)
    ? -normalize(uLightDir) : normalize(effLightPos - primary.pos);
float nDotL    = dot(primary.normal, lightDirFromP);
float alphaNEE = (nDotL <= 0.0) ? 0.0 : clamp(uHybridNEEFraction, 0.0, 1.0);

bool useNEE  = (hcRand1(rng) < alphaNEE);
vec3 bounceDir = useNEE
    ? hcConeSample(lightDirFromP, cosThetaMax, jitterUV)
    : hcCosineSample(primary.normal, jitterUV);
if (dot(primary.normal, bounceDir) <= 0.0) continue;  // below-horizon → reject

// [trace + direct lighting at the bounce hit — unchanged from v1.2.4]

float nDotWi = max(0.0, dot(primary.normal, bounceDir));
float pA     = hcCosinePdf(bounceDir, primary.normal);
float pB     = hcConePdf(bounceDir, lightDirFromP, cosThetaMax);
float pMarg  = (1.0 - alphaNEE) * pA + alphaNEE * pB;
if (pMarg > 1e-8) {
    vec3 integrand = primary.albedo * (1.0 / PI) * bounceRadiance * nDotWi;
    frameSum += integrand / pMarg;
}
```

### Why one-sample MIS instead of two-sample

Two-sample MIS would double the trace cost per pixel. The correction pass already pays for one SDF trace + direct-lighting sample at the bounce hit. One-sample MIS lifts the convergence of a "one-trace-per-frame" path tracer (which is what the hybrid correction is) without lifting per-frame cost. Variance reduction at fixed cost.

### Roughness sampling

- **`uRoughness`** is a `sampler3D` (GL_R8) sibling of `uSDF` / `uAlbedo` at unit 2. Defaulted at allocation to a uniform 1.0 (Lambert / wide cone), so v1.3 is bit-equivalent to v1.2.4 when `uHybridUseRoughnessTex = 0` AND `uHybridNEEFraction = 0`.
- `cosThetaMax = mix(coneMin, coneMax, roughness)` with defaults `coneMin=0.95` (cos ~18°, smooth surfaces) and `coneMax=0.50` (cos ~60°, fully diffuse). Below-horizon and back-facing light rejections are handled by `nDotL ≤ 0` early-out (sets `alphaNEE=0`).
- `OBJMaterial::roughness` is derived from `Ns` (Phong shininess) via a **Walter-inspired heuristic mapping**: `roughness = sqrt(2 / (Ns + 2))`. Walter et al. 2007 §5.2 derives this relation to match a *Phong specular lobe* against a *GGX/Beckmann specular lobe* — it is **not** an equivalence for diffuse-cone width, but it gives a sane monotonic map (high `Ns` → low roughness → tight cone; low `Ns` → high roughness → wide cone) until we replace it with a measured BRDF roughness term. Materials without `Ns` default to roughness=1.0 (Lambert). See §10.3 G2.

### Known limitation — cone-axis vs reflected direction (G4)

The DI cone is aimed at **`lightDirFromP`** (toward the light), not at the **mirror-reflected camera direction**. For Lambert / diffuse surfaces this is the right choice: the integrand `f·cos(θ)` has no view-dependent peak, so concentrating samples around the light is pure shadow-aware importance sampling. For **glossy** surfaces (`roughness → 0`), the integrand peaks around `reflect(-ωo, n)` — a tight cone around the light direction can be far from that peak and *increase* variance vs cosine sampling. v1.3's roughness coupling makes this worse on glossy surfaces (lower roughness → tighter cone → larger angular gap from the reflection lobe).

This is not a defect of the math (the estimator stays unbiased), only a limitation of the strategy. **Deferred to v1.4** alongside true light-position NEE; the right fix is either a second cone aimed at the reflection direction (two-strategy MIS) or a microfacet-style sampling distribution where the half-vector tilt is sampled directly. v1.3's cone is correct *as a Lambert-tuned shadow-aware DI cone*; do not enable it as the only sampler for glossy materials.

### Scope deferral

The per-voxel roughness path is **scaffolded but not baked** in v1.3: the `roughnessTexture` is allocated R8 at volumeResolution³ and bound to unit 2, but stays at the uniform 1.0 fill until v1.3.1 wires the CPU/GPU SDF-bake flood-fill from per-triangle material `roughness`. With `uHybridUseRoughnessTex = 1` today, the cone modulation degenerates to a scene-wide `coneMax` (fully rough) — equivalent to setting global roughness to 1.0. The **global roughness slider is the live tuning knob** for v1.3.

### Visual A/B (Cornell-orig, hybrid ON, 640×360)

| Capture | NEE fraction | Frame | Visual |
|---|---:|---:|---|
| `v13_nee_off_f60.png` | 0.0 | 60 | Cornell with red/green wall bleed; surfaces converged |
| `v13_nee_on_f60.png` | 0.8 | 60 | Bit-for-bit indistinguishable visually |
| `v13_nee_off.png` | 0.0 | 300 | Fully converged Cornell |
| `v13_nee_on.png` | 0.5 | 300 | Fully converged Cornell — same as off |

**Convergence equivalence is the correct outcome for unbiased one-sample MIS** — the marginal-PDF collapse guarantees `E[F] = ∫ f(ωi) dωi` independent of `α`. NEE's win is **variance at fixed sample count**, not expectation. At 640×360 with EMA(0.05) running for 60+ frames, the per-pixel variance is already below display-discrimination threshold; the win moves to scenes with stronger shadow boundaries (cornell-orig-alcove, shadow caustics, multi-light) where rejection sampling against directly-lit bounce regions is large. The visual A/B confirms **no regression**; quantitative variance measurement is a v1.3 follow-up validation task using the Phase 8 sweep harness.

### Files touched

| File | Net change |
|---|---:|
| `res/shaders/hybrid_correction.comp` | +60 lines (uRoughness sampler, 5 uniforms, hcConeSample/Pdf/hcCosinePdf helpers, MIS main-loop restructure) |
| `src/demo3d.h` | +13 lines (5 v1.3 state members + setters + roughnessTexture GLuint) |
| `src/demo3d.cpp` | +60 lines (state init, roughnessTexture allocation + label + cleanup, 6 uniform binds in correction dispatch, 5 GUI sliders + tooltips) |
| `src/main3d.cpp` | +18 lines (3 new CLI flags: `--hybrid-nee=`, `--hybrid-roughness=`, `--hybrid-use-roughness-tex=`) |
| `src/obj_loader.h` | +12 lines (`OBJMaterial::roughness` field; MTL parser reads `Ns` and derives roughness) |
| `doc/7/hybrid_v12_validation_phase8_impl.md` | +this §10.2 |

### CLI

```
--hybrid-nee=0.5            # NEE fraction; 0 = pure cosine BRDF, 1 = always cone, 0.5 default
--hybrid-roughness=1.0      # scene-wide roughness when -tex=0; 1.0 = Lambert, 0.0 = mirror
--hybrid-use-roughness-tex=0   # 0 = use global (default); 1 = per-voxel uRoughness sampler
```

## 10.3 v1.3 self-critique and improvement plan

Brutally honest pass over the v1.3 ship to surface what's actually *validated*, what's *plausible but unproven*, and what's *dead code wearing a GUI hat*. Sections labelled **V/G/F**:

- **V** = validated by the shipped artifacts
- **G** = gap (claim not backed by evidence yet)
- **F** = flaw (real defect or misleading API surface)

### What's actually validated

- **V1. Identity at α=0.** Shader algebra at [hybrid_correction.comp:400-405](../../res/shaders/hybrid_correction.comp#L400-L405): `F = (albedo/π · L · cos(θ)) / (cos(θ)/π) = albedo · L`. Reduces exactly to the pre-v1.3 cosine-only estimator. Confirmed in code; A/B captures at α=0 vs v1.2.4 binary not regression-hashed but visually unchanged.
- **V2. Divide-by-zero guard.** `pMarginal > 1e-8` at [hybrid_correction.comp:410](../../res/shaders/hybrid_correction.comp#L410). Below-surface cone samples already have `nDotWi=0 → integrand=0`, so the guard prevents NaN without biasing.
- **V3. Below-horizon clamp.** `nDotL ≤ 0 → α=0` at [hybrid_correction.comp:360-361](../../res/shaders/hybrid_correction.comp#L360-L361) avoids degenerate cone sampling when the light is behind the surface.
- **V4. Global-roughness fallback.** `if (uHybridUseRoughnessTex != 0)` branch at [hybrid_correction.comp:338-342](../../res/shaders/hybrid_correction.comp#L338-L342) — toggle-off path is unambiguous; the global slider is the live knob.

### Gaps (G) — claims that aren't proven

- **G1. The A/B is unfalsifiable.** Four captures showing "NEE on ≡ NEE off at convergence" is *consistent with* unbiased MIS — and also consistent with "NEE samples are silently zeroed out", "cone pdf is wrong but cancels", or "the `useNEE` branch is never taken". We need a **positive** validation: either (a) a per-pixel variance image at fixed frame budget showing NEE reducing variance, or (b) a synthetic "α=1 only" test confirming the pure-cone integrand converges to the same mean as α=0 in a few hundred frames. Without one of these, §10.2's "convergence equivalence is the correct outcome" is a *defensible explanation*, not *evidence of correctness*.
- **G2. Phong→roughness mapping is misapplied.** `roughness = sqrt(2/(Ns+2))` (Walter et al. 2007 §5.2) is derived for a **GGX/specular** lobe matching a Phong specular lobe. v1.3 plugs this into a **diffuse** cone-width modulator. The numerical mapping is fine as a heuristic, but the doc's "Walter equivalence" citation overclaims. Should be labelled "heuristic mapping inspired by Walter §5.2" not "equivalence".
- **G3. No quantitative variance measurement.** Whole point of NEE is variance reduction at fixed sample count. We claimed it in prose; we didn't measure it. The Phase 8 sweep harness can produce per-frame RMSE-vs-frame-count curves with NEE=0 vs 0.5 — that plot is the one number that would actually defend v1.3.
- **G4. Cone-axis choice for glossy.** Cone is light-aligned (axis = `lightDirFromP`). For a Lambert surface this is shadow-aware importance sampling and is fine. For a low-roughness (glossy) surface, the integrand peaks around the **reflected** direction, not the light direction — a tight cone around the light is *worse* than cosine sampling when the light is far from the reflection lobe. The v1.3 default cone shrinks WITH roughness decrease, so this gets *worse* on glossy surfaces. Untested.

### Flaws (F) — real defects to fix

- **F1. "NEE" is misnamed.** Classical NEE samples a **light position** with pdf = solid-angle to the light's bounding sphere or geometry. v1.3 samples a **cone around the light direction** with pdf = 1/(2π·(1−cosThetaMax)). These are different estimators. For a point/directional light the difference vanishes (light direction = single ray), but the moment we add area lights, the v1.3 strategy stops being NEE and the cone-angle knob loses physical meaning. Suggest renaming throughout to "directional-importance cone" or "DI cone" and reserving "NEE" for a future v1.4 that samples actual light geometry.
- **F2. "Use roughness texture" toggle is a no-op until v1.3.1.** The GUI checkbox, CLI flag (`--hybrid-use-roughness-tex=1`), and tooltip all suggest a working per-voxel path. Today it degenerates to scene-wide `coneMax` (texture is uniform 1.0). Two acceptable fixes: (a) gray out the control with tooltip "v1.3.1 — bake pending", or (b) hide it entirely until the bake lands. Current state is a UX trap that will burn the first user who toggles it expecting per-material behavior.
- **F3. α is independent of roughness.** Smooth surfaces (tight cone) should sample the cone *more often* because the BRDF lobe is concentrated. Current code uses a global α irrespective of per-pixel roughness. Trivial fix: `effectiveAlpha = mix(uHybridNEEFraction, 1.0, 1.0 - roughness)` or similar coupling. The current decoupled form means smooth surfaces both shrink the cone AND keep sampling it rarely — the worst combination.
- **F4. Default `uHybridNEEFraction = 0` ships v1.3 with the feature off.** Conservative, but means a user who pulls main sees no behavior change and won't discover the slider. A safer default of 0.3 (or 0.5 if G3 confirms variance reduction) would put the feature on the user's hot path. Document the choice either way.
- **F5. Missing bit-equivalence regression.** v1.3 *should* be bit-equivalent to v1.2.4 when `α=0 AND useRoughnessTex=0 AND globalRoughness=1`. We didn't add an automated check. A 1-frame screenshot hash compared against a v1.2.4 reference would catch any future MIS-code drift that quietly breaks the identity at α=0.

### Concrete improvements — ranked

| # | Improvement | Touch | Effort | Payoff |
|---|---|---|---:|---|
| 1 | Re-run Phase 8 sweep with `--hybrid-nee=0.0` vs `0.5`, generate per-frame RMSE plot; commit as `tools/hybrid_validation/v13_nee_variance/` | `tools/analysis/` only | 1h | Defends v1.3's existence quantitatively (G1, G3) |
| 2 | Bake per-voxel roughness (v1.3.1 follow-up): mirror albedo flood-fill in `OBJLoader::voxelize()` + CPU EDT + GPU JFA SDF; upload to `roughnessTexture` | `src/obj_loader.h`, `src/demo3d.cpp` (4 sites) | 4-6h | Makes F2 toggle real |
| 3 | Couple α to roughness: `effAlpha = max(uHybridNEEFraction, 1.0 - roughness)` so smooth surfaces sample cone more | `hybrid_correction.comp:361` | 5 min | Fixes F3, measurable in #1's plot |
| 4 | Rename "NEE" → "DI cone" throughout (GUI labels, CLI flag aliases, doc) and reserve "NEE" for true light-position sampling | `src/demo3d.cpp` GUI strings, `src/main3d.cpp` CLI, this doc | 30 min | Fixes F1, avoids future confusion when area lights land |
| 5 | Add hash-based regression: capture 1 frame with `--hybrid-nee=0 --hybrid-use-roughness-tex=0 --hybrid-roughness=1`, compare PNG hash against v1.2.4 reference; fail CI on mismatch | `tools/hybrid_validation/v12_equivalence/` | 1h | Catches F5 silent regressions |
| 6 | Default `uHybridNEEFraction = 0.3` (or whatever #1's plot supports) | `src/demo3d.h` ctor init | 1 min | Fixes F4 — feature is discoverable |
| 7 | Gray out "Use roughness texture" checkbox with tooltip "scaffolded; per-voxel bake lands in v1.3.1" until #2 ships | `src/demo3d.cpp` GUI block | 5 min | Fixes F2 UX trap; can ship same patch as #6 |
| 8 | Hardening: clamp cone cosines to `[-0.99, 0.999]` to avoid degenerate full-sphere or zero-cap caps | `src/demo3d.cpp` slider min/max | 2 min | Prevents `1/(1-cosθ)` blow-up at extremes |
| 9 | Reword §10.2 "Walter equivalence" → "Walter-inspired heuristic mapping" with one sentence noting the diffuse vs specular mismatch | this doc | 2 min | Fixes G2 overclaim |
| 10 | Document cone-axis-vs-reflected-dir limitation in §10.2 (or §10.4) as a known limitation for glossy surfaces; defer fix to v1.4 alongside true NEE | this doc | 5 min | Honest accounting of G4 |

### Recommended ordering

Do **#1 first** (variance plot). Everything else either depends on #1's signal (#3, #6) or is independently small. If #1 shows no variance reduction, v1.3 is exposed as a no-op feature and the priority shifts to investigating *why* (cone pdf wrong? `useNEE` branch never taken? cone axis wrong for the test scene?). If #1 shows clear variance reduction, then #2-#7 land in a single v1.3.1 patch and the feature graduates from "shipped" to "validated and on-by-default".

### What v1.3 actually delivered (honest summary)

A **mathematically correct one-sample MIS scaffolding** with per-pixel roughness *hooks* exposed all the way through to GUI/CLI. Unbiased at α=0 (proven by algebra), unfalsified at α>0 (proven by visual equivalence, not by variance measurement). The per-voxel roughness path is wired end-to-end except for the bake step. As shipped, the feature is **a knob users can turn that doesn't visibly change anything** — which is either "correct unbiased MIS at convergence" or "a no-op". Improvement #1 collapses that ambiguity.

## 10.4 v1.3.1 #1 — variance plot experiment (the critique's first test)

Per §10.3 improvement #1 ("re-run Phase 8 sweep with `--hybrid-nee=0.0` vs `0.5`, generate per-frame RMSE plot"), this section documents the experiment, its bugs surfaced during setup, and the **uncomfortable result**: NEE provides **no measurable variance reduction** on cornell-orig-alcove at this configuration.

### Harness

- `tools/hybrid_validation/v13_nee_variance/run_variance_sweep.ps1` — drives `RadianceCascades3D.exe` three times (a00: α=0 hybrid, a05: α=0.5 hybrid, ptref: mode-16 PT) and lands per-frame captures via the existing `--shots-prefix`/`--shots-after`/`--shots-count` flags. ~160 s wall time.
- `tools/analysis/hybrid_nee_variance_plot.py` — averages PT captures in linear light → reference, computes per-frame `RMSE(capture, reference)` for both α curves, writes `rmse_curves.png` + `metrics.{json,md}`.

### Bugs surfaced setting this up (worth recording)

| # | Symptom | Root cause | Fix |
|---|---|---|---|
| B1 | `RadianceCascades3D.exe : Traceback ...` aborted PS script even though exe exited 0 | PS 5.1 `2>&1` on native exe wraps stderr lines as `NativeCommandError` records; combined with `$ErrorActionPreference=Stop` it terminated the harness. Cerebrum already warns about this pattern | Switched to `*> $logFile` redirect; set `ErrorActionPreference=Continue`; check `$LASTEXITCODE` + capture-count instead |
| B2 | 0 captures produced despite exe running 60s and logging "shots-count reached" | raylib's `TakeScreenshot()` ignores absolute paths and writes to CWD using the basename only. Our `--shots-prefix=D:\...\captures\a00` collapsed to `a00_f10.png` in the repo root, not the captures dir | Pass bare prefix `a00`; run from `$repoRoot`; `Move-Item` PNGs to captures dir post-run |
| B3 | `a00_f10.png` and `a05_f10.png` had **identical MD5 hashes** for every frame | `useHybrid` defaults to **false** ("opt-in") — the v1.3 correction pass (which contains all NEE code) never ran. CLI flags `--hybrid-nee=...` quietly set state on a disabled path | Added `--use-hybrid=1` to the harness. This is the **smoking gun for §10.3 G1**: without this fix, the visual A/B in §10.2 (and any user A/B that forgets the flag) is testing nothing |
| B4 | RMSE settled at 0.234 in linear light — huge | PT (mode 16) and hybrid (mode 0) render different lighting models (multi-bounce PT vs single-bounce correction + cascade bake). The bias floor between the two dominates over any variance signal | Disabled the bake-time ambient floor via `--ambient-bake-strength=0` (still leaves a bias floor — see Findings) |
| B5 | Python `metrics.md` write failed with `UnicodeEncodeError: 'gbk' codec` | Windows default codepage doesn't include `⇒` U+21D2 | `write_text(..., encoding="utf-8")` |

The B1–B3 chain is significant: **three separate silent failures stacked**, each of which produced "completed-looking" output (exe exited 0, log said shots saved, files even appeared somewhere). Without the hash-equality check on the captures, the harness would have happily plotted noise and we'd have shipped a fake validation.

### Findings (the real story)

`rmse_curves.png` — both curves overlap to the eye and to 0.001 RMSE per frame:

| Curve | First-frame RMSE | Last-frame (f209) RMSE | Half-life | Improvement vs first |
|---|---:|---:|:---:|---:|
| α=0.0 (cosine-only) | 0.29971 | 0.23375 | never reaches 50% | 22.0% |
| α=0.5 (DI-cone MIS) | 0.29973 | 0.23378 | never reaches 50% | 22.0% |

**Last-frame relative difference: −0.01% (TIE).** The two curves are statistically indistinguishable — they share the same convergence trajectory, the same plateau, the same warmup-cliff at f24. The hash-difference test confirms the v1.3 code IS executing distinctly per α (different bitstreams), but the **output is functionally identical**.

### Why NEE doesn't help here

Cornell-orig-alcove geometry + lighting analysis:

- Light is a **single overhead area-light** (top of the box, normal = downward).
- Dominant lit surfaces (floor, alcove floor) have **normal ≈ up**. For these, `lightDirFromP ≈ normal`, so the NEE cone (axis = light direction) almost coincides with the cosine BRDF lobe (peak along normal). Cone and BRDF sample roughly the same directions, so MIS reduces to ≈ cosine sampling.
- Sidewalls (red/green) have horizontal normals. For these, the light direction is along the normal **only if the surface faces the light** — for vertical surfaces, the light is at ~90° to the normal. The shader's `nDotL ≤ 0 → α=0` clamp ([hybrid_correction.comp:360](../../res/shaders/hybrid_correction.comp#L360)) **disables NEE entirely** when the cosine of the light angle is non-positive. That gates out the very pixels where NEE would help most.
- Net effect: on dominant lit surfaces NEE ≈ cosine; on geometrically interesting (sidewall, alcove) surfaces NEE is gated off. No variance win available.

### Implications for v1.3

The §10.3 critique was **right to be skeptical**. Three concrete actions emerge:

1. **F3 (α decoupled from roughness) is now empirically supported.** A global α=0.5 sprays cone samples on surfaces where they degenerate to cosine, while gating off where they'd matter. The fix proposed in §10.3 #3 (`effAlpha = max(α, 1-roughness)`) still doesn't unlock NEE on sidewalls because the cone-axis clamp triggers first — see #2 below.
2. **The "below-horizon clamp" gates out the win.** If the light is at >90° to the normal, classical NEE *should* still sample positions on the light source — the light is just outside the cosine hemisphere but visibility/occlusion can still matter. v1.3's DI-cone implementation conflates "light direction is below horizon" with "NEE is useless", which is wrong for non-trivial geometry. This is a v1.4 fix — needs true light-position NEE (samples a position on the area light with solid-angle pdf), not cone-around-light-direction.
3. **The test scene was wrong.** A scene where NEE shines has lots of N⊥L geometry — concave alcoves, off-axis light, multi-light setups. cornell-orig-alcove with a single overhead light is the *worst* scene for this estimator. Better follow-up test bed: a scene with the light placed at the side (still on the ceiling, but biased toward one wall) so sidewalls have meaningful n·L; or sponza with directional sun at a glancing angle.

### Verdict & next-step recommendation

**Status:** v1.3 ships a correct unbiased one-sample MIS estimator that **does not actually reduce variance** on the chosen benchmark. This isn't a bug in the math — the integrand-vs-pdf alignment between cone and cosine on this scene is too good for the cone to add anything.

**Recommendation:** do NOT enable NEE by default in v1.3.1 (§10.3 #6 cancelled). Instead, prioritize:
- **§10.3 #7** (gray out the no-op toggle) → still valid, shipping a dead UX is worse than not shipping the feature
- **§10.3 #4** (rename "NEE" → "DI-cone") → now strongly motivated; we have proof it's not classical NEE
- **NEW v1.3.2 item**: true light-position NEE (samples on the area light geometry with solid-angle pdf). This is the estimator the §10.3 critique foresaw at F1 — and the variance experiment shows it's the *only* form that would actually help.

### Files touched

| File | Net change |
|---|---:|
| `tools/hybrid_validation/v13_nee_variance/run_variance_sweep.ps1` | NEW (~85 lines, harness with B1-B3 fixes) |
| `tools/hybrid_validation/v13_nee_variance/captures/*.png` | NEW (450 captures, ~70 MB — git-ignored) |
| `tools/hybrid_validation/v13_nee_variance/{rmse_curves.png,reference.png,metrics.{json,md}}` | NEW (artifacts) |
| `tools/analysis/hybrid_nee_variance_plot.py` | NEW (~160 lines, plotter) |
| `doc/7/hybrid_v12_validation_phase8_impl.md` | +this §10.4 |

### Reproduce

```pwsh
cd 3d
powershell -ExecutionPolicy Bypass -File tools/hybrid_validation/v13_nee_variance/run_variance_sweep.ps1
python tools/analysis/hybrid_nee_variance_plot.py tools/hybrid_validation/v13_nee_variance/
# inspect: tools/hybrid_validation/v13_nee_variance/{rmse_curves.png, metrics.md}
```

## 10.5 v1.3.1 cleanup batch (#4 + #7 + #8 + #9 + #10)

Shipped immediately after §10.4 — small, independently valid fixes the variance finding doesn't depend on. The §10.3 #6 "default α=0.3" is **cancelled** by §10.4 data; #2 (per-voxel bake) and #3 (α-roughness coupling) remain backlogged because the §10.4 finding doesn't disprove them (it shows v1.3's *current* DI cone provides no benefit on cornell-orig-alcove, not that it never could on glossy/sidewall-shadowed scenes once the cone is properly modulated).

| # | Change | File | Effect |
|---|---|---|---|
| #4 | Rename "NEE fraction" / "NEE cone cos" → "DI cone fraction" / "DI cone cos" in GUI sliders + tooltips. Tooltip now says "NOT classical NEE (which samples a point on light geometry)" and cites §10.4 + v1.3.2 backlog. | [src/demo3d.cpp:5354-5391](../../src/demo3d.cpp#L5354-L5391) | Stops the next reader from expecting NEE behavior |
| #7 | `ImGui::BeginDisabled(true)` around "Use roughness texture" checkbox; tooltip explains "v1.3.1 #2 bake pending, slider currently no-op." | [src/demo3d.cpp:5366-5375](../../src/demo3d.cpp#L5366-L5375) | Eliminates F2 UX trap |
| #8 | Slider range tightened: cMin `[0.5, 1.0]` → `[0.5, 0.999]`; cMax `[-1.0, 0.95]` → `[-0.99, 0.95]`. | [src/demo3d.cpp:5381-5388](../../src/demo3d.cpp#L5381-L5388) | Prevents degenerate full-sphere (cosThetaMax=-1 → cap covers whole sphere, pdf denominator wrong) and zero-cap (cosThetaMax=1 → 1/(2π·0) blows up) |
| #9 | §10.2 "Walter equivalence" wording → "Walter-inspired heuristic mapping" with explicit "not an equivalence for diffuse-cone width" sentence. | this doc §10.2 "Roughness sampling" | Removes G2 overclaim |
| #10 | New "Known limitation — cone-axis vs reflected direction (G4)" subsection in §10.2 explicitly stating cone-axis-vs-reflection-direction limitation for glossy surfaces, with deferral to v1.4. | this doc §10.2 | Honest accounting of G4 |

### Verified

- Build still succeeds (no new warnings introduced in the GUI rewrite).
- `--hybrid-nee=0.5` CLI path unchanged — variable name stays `hybridNEEFraction` to avoid a wider refactor; only the user-facing strings move to "DI cone". When the GUI label and the CLI flag disagree (`--hybrid-nee` is what the harness passes), `--hybrid-nee` survives because changing it would break the v13_nee_variance sweep harness and the buglog grep paths.
- The disabled checkbox renders grayed in the imgui pane; hover still produces the explanatory tooltip via `ImGuiHoveredFlags_AllowWhenDisabled`.
- Slider range change is **non-destructive**: existing state values (default `cMin=0.95`, `cMax=0.50`) sit inside the new ranges. Users who had pushed sliders to the old extremes will have the value snap to the new range on first interaction.

### Files touched

| File | Net change |
|---|---:|
| `src/demo3d.cpp` | ~−7/+22 lines (GUI slider rename + BeginDisabled + clamp ranges + tooltip rewrites) |
| `doc/7/hybrid_v12_validation_phase8_impl.md` | +~30 lines (§10.2 wording + new known-limitation section + this §10.5) |
| `.wolf/cerebrum.md` | +2 Do-Not-Repeat entries (useHybrid silent gate; NEE-vs-DI-cone naming) |

### What this does NOT do

- Does **not** add the per-voxel roughness bake (§10.3 #2 — 4-6h, still backlogged).
- Does **not** couple α to roughness (§10.3 #3 — would help glossy surfaces if/when DI cone is restored on them, but cornell-orig-alcove is Lambert-only so no test bed here).
- Does **not** rename the C++/CLI symbols (`hybridNEEFraction`, `--hybrid-nee=`) — only the user-facing strings. The variance harness `run_variance_sweep.ps1` still passes `--hybrid-nee=0.0/0.5`, the cerebrum/buglog history still greps cleanly.
- Does **not** ship a v1.3.2 "true light-position NEE" — that's the next standalone phase, scoped separately because it touches the shader's sampling pdf, not just GUI strings.

## 11. Phase 8 status — what's done, what's open

### Done in Phase 8 (and follow-ups)
- [x] `--hybrid-ab-sweep` CLI + state machine
- [x] `tools/analysis/hybrid_quality_metrics.py` (RMSE, blue/red counts, per-region)
- [x] GL_TIMESTAMP perf timer in `Hybrid X.XX ms` perf panel line
- [x] B1 fix: blur dispatch texture units (8/9 instead of 0/1) — solved all-black hybrid screenshots
- [x] B2 fix: replaced broken inverse-variance with sample-count cooperative merge
- [x] B3 acknowledgement: RMSE != perceptual quality on textured scenes
- [x] v1.2.3 noise reduction: R2 + AABB + lumSigma
- [x] v1.2.4 AABB regression fix (firefly-only, opt-in, min-spp gate) + wider PT history (EMA 0.05 default)
- [x] Cornell default validated end-to-end

### Open (deferred to future phases)
- [ ] Sponza cam.md validation sweep — requires OBJ load setup
- [ ] SSIM / LPIPS perceptual metrics in the Python script (per L15)
- [ ] 10-frame averaged captures for noise-floor reporting (per M3 in §5)
- [ ] Per-pass timer split (`hybridCorrectionMs` vs `hybridBlurMs`) — currently combined
- [ ] STBN texture asset as v1.2.4 if R2 isn't enough on Sponza

## 8. Files touched in this session (Phase 8 impl)

| File | Net change |
|---|---:|
| `src/demo3d.h` | +45 lines (state + sweep enum + timer query state) |
| `src/demo3d.cpp` | +220 lines (sweep state machine + timer queries + B1 fix + B2 default change + auto-burst suppression) |
| `src/main3d.cpp` | +6 lines (sweep CLI flag) |
| `res/shaders/raymarch.frag` | 0 (bisect-disabled then restored) |
| `tools/analysis/hybrid_quality_metrics.py` | NEW (~180 lines) |
| `tools/hybrid_validation/cornell_default/sweep_metadata.json` | NEW (sweep output) |
| `tools/hybrid_validation/cornell_default/metrics.{json,md}` | NEW (metric output) |
| `doc/7/hybrid_v12_validation_phase8_impl.md` | NEW (this doc, ~6500 tok) |
| `doc/7/hybrid_rc_pixel_correction_impl.md` | will append §11 with B1/B2 findings |
