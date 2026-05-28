# M1 Stage 10 Plan - Mode-0 Final-Composite Visual A/B

**Date:** 2026-05-27.
**Predecessor:** [v3_m1_stage9_mb_gain_ladder_impl.md](v3_m1_stage9_mb_gain_ladder_impl.md).
**Replaces:** the original Stage 10 "Fork A per-scene MB-gain preset" plan (reverted as premature — measurement-first principle).

**Goal:** test whether Stage 8/9 MB-gain attribution measures a real visual-quality problem or a measurement artifact. Compare **mode-0 final composite** (direct + indirect) against PT ground truth at multiple MB gains and against hybrid-ON, on Sponza AND Cornell.

## Why this re-evaluation exists

Stage 9 declared Sponza gain=0.10 "best" by minimizing `|p95(ratio-1)|` on the GI-only term. Running the verification produced a near-black Sponza mode-17 screenshot. The PNG matched the Stage 9 gain=0.10 PNG pixel-for-pixel — so the rendering was correct, but the user's reaction ("pure dark scene") surfaces a real concern:

**Mode-17 amplifies tiny GI differences by stripping direct light.** Sponza PT_GI is intrinsically dim (single weak directional source, no ambient/sky term). Matching dim-with-dim gives `ratio_self=1.04`, which is a great `|p95|` score but a *visually meaningless* fix — the actual rendered image is essentially black with or without the gain change.

The honest question for the pivot: **does any MB-gain change move the mode-0 final image meaningfully toward the hybrid-ON reference?** If not, Stage 8/9's attribution is solving a measurement artifact and the entire gain ladder is the wrong knob.

## Plan

### 1. Variants

All N=2048, render-mode 17 (mode 17 emits the same `cascade_gi/pt_full/pt_direct` EXRs the bake produces under mode-0 — the only difference is what the *screen* displays; the bake and the EXRs are identical), cam 0.

| Tag                | Scene   | `--multi-bounce-gain` | `--use-hybrid` | Source                                |
|--------------------|---------|---:|---:|---|
| `sponza_g010`      | sponza  | 0.10 | 0 | reuse Stage 9 capture                  |
| `sponza_g050`      | sponza  | 0.50 | 0 | reuse Stage 8 (`mb_gain_half`)         |
| `sponza_g100`      | sponza  | 1.00 | 0 | reuse Stage 7 baseline                 |
| `sponza_hybrid`    | sponza  | 1.00 | 1 | reuse Stage 8 (`hybrid_on`)            |
| `cornell_g010`     | cornell | 0.10 | 0 | **new capture**                        |
| `cornell_g050`     | cornell | 0.50 | 0 | reuse Stage 9 capture                  |
| `cornell_g100`     | cornell | 1.00 | 0 | reuse Stage 8 (`cornell_baseline`)     |
| `cornell_hybrid`   | cornell | 1.00 | 1 | **new capture**                        |

Two new captures only (~6 min total).

### 2. Metric — mode-0 final-composite distance from PT

`cascade_gi.exr` is the GI-only term. `pt_full.exr` is the PT ground-truth full composite. `pt_direct.exr` is the PT ground-truth direct-only.

Since the engine and PT share the same direct-light shading kernel (both do shadow-ray + Lambertian, identical light position, identical surface response), the cascade's full composite is **synthesized** as:

```
cascade_full ≈ pt_direct + cascade_gi
```

This is the same composition the mode-0 display path performs at runtime (direct + indirect). It is NOT the tone-mapped PNG — it is the linear HDR composite, comparable directly against `pt_full`.

Per-variant metrics computed on linear-HDR pixels, valid mask `(pt_full > 0.05 luma) & (gbuffer.a > 0)`:

- `composite_rms` = RMS(cascade_full, pt_full) in linear luma — the headline number.
- `composite_mae` = MAE(cascade_full, pt_full).
- `composite_mean_relative_error` = mean(|cascade_full - pt_full| / max(pt_full, eps)).
- `composite_ssim` (optional, if scikit-image available) on luma channel.
- `direct_share` = mean(pt_direct.luma) / mean(pt_full.luma) — context: how much does direct dominate the final image?

**Headline read:** the variant whose `composite_rms` is closest to `*_hybrid`'s `composite_rms` is the best target. Hybrid is the empirical retirement reference.

### 3. Decision rule

For each scene:

1. Compute `composite_rms` for each gain variant and for the hybrid_on variant.
2. The **hybrid_on RMS** is the empirical floor (best achievable composite-distance under the current bake).
3. Define `excess_rms_over_hybrid[gain] = composite_rms[gain] - composite_rms[hybrid_on]`.
4. A gain is "**competitive with hybrid**" if `excess_rms_over_hybrid[gain] ≤ 0.05 × composite_rms[hybrid_on]` (within 5%).
5. **Verdicts:**
   - `STAGE8_9_VINDICATED` — the gain that minimizes mode-17 `|p95|` (Stage 9 Sponza g=0.10) also minimizes mode-0 `composite_rms` for that scene.
   - `MODE17_ARTIFACT` — the mode-17 best gain is NOT competitive on mode-0; the gain that wins mode-0 is different.
   - `FLAT_GAIN_ROOM` — all gains within 5% of hybrid on mode-0; the gain knob doesn't meaningfully move the final image at all.

### 4. Side-by-side visual comparison

For each scene, tone-map (sRGB gamma 2.2, no exposure) `cascade_full` and `pt_full` to 8-bit PNG and emit an HTML/markdown table showing:
- the rendered PNG (mode-17 — already on disk),
- the synthesized mode-0 composite PNG (new),
- the PT reference PNG (new),
- per-variant numbers.

This is the visual A/B the user can eyeball.

## Self-critique and improvements

### SC1 — `pt_direct + cascade_gi` may not match what mode-0 actually displays

If the engine's mode-0 path differs from PT's direct-light kernel in any subtle way (e.g. different shadow ray bias, different ambient term, color-space mismatch), the synthesized composite is wrong. **Improvement:** also capture an actual mode-0 screenshot per variant (the PNG) and compute a sanity ratio `mean(synthesized_composite) / mean(linear-extracted-from-mode-0-PNG)` — if not within 5% the synthesis assumption is broken and the variant block is invalid. Cost: 8 extra screenshots, ~negligible time once the captures themselves are running.

### SC2 — Cornell hybrid_on at gain=1.0 doesn't exist yet

Stage 8 captured Cornell hybrid_on baseline at gain=1.0 only on Sponza. For Cornell, only `cornell_baseline` (hybrid=0) was captured. **Improvement:** add `cornell_hybrid` (`--use-hybrid=1 --multi-bounce-gain=1.0`) to the new-capture set (already in §1).

### SC3 — Cornell at gain=0.10 not captured yet

Cornell ladder only went down to gain=0.25. Need cornell_g010 for apples-to-apples comparison with Sponza. **Improvement:** add `cornell_g010` to new-capture set (already in §1).

### SC4 — `composite_rms` may be dominated by tone-mapped highlights

Linear-HDR RMS over-weights bright outliers. **Improvement:** also report a luminance-clamped version `composite_rms_clamped` where both `cascade_full` and `pt_full` are clamped at the 99th-percentile luma before RMS. The verdict uses the unclamped RMS as primary but the clamped version as cross-check; if they disagree on best-variant the impl doc must surface that.

### SC5 — SSIM may not match human perception in HDR

scikit-image SSIM assumes 8-bit normalized inputs. **Improvement:** if SSIM is reported, apply it to the tone-mapped 8-bit version (sRGB gamma'd), and label as "perceptual proxy" not a quantitative truth.

### SC6 — Hybrid isn't necessarily ground truth

Hybrid is the current production fallback that the v3 pivot aims to retire. Its `composite_rms` against PT is itself a real number, not zero. If hybrid is also far from PT (e.g. excess of 0.20 RMS), then "match hybrid" is a lower bar than "match PT". **Improvement:** also report `composite_rms[hybrid_on] / mean(pt_full.luma)` as a normalized number so the reader can see whether hybrid itself is close to PT or just less wrong than cascade.

### SC7 — Mode-0 PNG vs mode-17 PNG may show different visual stories

Mode-17 Sponza is "pure dark" because GI-only is dim. Mode-0 Sponza will be bright (direct light dominates). The visual table makes this contrast explicit. **Improvement:** the impl doc should explicitly note that "pure dark" in mode-17 is a display artifact, not a rendering problem — and that the mode-0 PNG is the actually-shipped final composite a user sees.

### SC8 — Cornell ratio_self=0.49 problem is still out of scope

Stage 9 SC10 surfaced that Cornell GI is under-bright by ~2× regardless of MB gain. That's a consumer-side normalization concern. This stage **does not** investigate it. **Improvement:** if Cornell mode-0 `composite_rms` is large even at hybrid_on, file a separate Stage 11 ticket for the consumer-side audit.

### SC9 — The 2 new captures need fresh script invocation; previous script had a stdout-pipe issue

The Stage 10 "Fork A" verification script piped `& $exe @args | Select-String ... | ForEach-Object` and starved the demo's stdout, killing it early. **Improvement:** new capture script redirects demo output to a per-capture log file directly (`> $logFile 2>&1`), no Select-String pipe. Manual N=2048 runs confirmed this works.

### SC10 — Don't ship any default change in Stage 10

Stage 10 is a measurement stage only. No CLI flags, no code changes, no preset infrastructure. Pure observation + analysis. If the verdict is `STAGE8_9_VINDICATED`, a future stage can revive the Fork A preset idea on firmer ground.

## Acceptance

- 2 new captures: `cornell_g010` and `cornell_hybrid` at N=2048 with all mode-17 EXR sidecars.
- `tools/v3_m1_mode0_ab/mode0_ab_results.json` records per-variant `{composite_rms, composite_mae, composite_mean_relative_error, direct_share, excess_rms_over_hybrid}` and SC1 synthesis-sanity ratio.
- 16 side-by-side PNGs (cascade_full + pt_full, per variant) in `tools/v3_m1_mode0_ab/visuals/`.
- Impl doc records the verdict (`STAGE8_9_VINDICATED` / `MODE17_ARTIFACT` / `FLAT_GAIN_ROOM`) and the implication for whether Fork A (now reverted) should be revived.

## Out of scope

- Any engine source change.
- Cornell consumer-side under-brightness investigation.
- Visualizing per-region (walls/floor/window) breakdown beyond the global metric.
- Reviving Fork A preset infrastructure (decided by verdict).
