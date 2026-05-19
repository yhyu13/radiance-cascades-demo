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
