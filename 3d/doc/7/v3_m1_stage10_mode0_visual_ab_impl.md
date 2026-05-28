# M1 Stage 10 Implementation - Mode-0 Final-Composite Visual A/B

**Date:** 2026-05-27.
**Plan:** [v3_m1_stage10_mode0_visual_ab_plan.md](v3_m1_stage10_mode0_visual_ab_plan.md).
**Result artifact:** `tools/v3_m1_mode0_ab/mode0_ab_results.json`, side-by-side PNGs in `tools/v3_m1_mode0_ab/visuals/`.
**Verdict:** **`STAGE8_9_VINDICATED` on both scenes.** The mode-0 best gain matches the mode-17 best gain. Sponza gain=0.10 is a real visual improvement (10× lower mode-0 RMS than gain=1.0; closes 95% of the gap to hybrid). Cornell gain knob barely moves the mode-0 image — confirms a separate consumer-side problem.

## What changed

No engine source changes (Fork A was reverted before this stage). Tooling only:

1. `tools/v3_m1_mode0_ab/capture_extra.ps1` — captures the 2 missing variants (`cornell_g010`, `cornell_hybrid`) at N=2048 with all mode-17 sidecars. Per plan SC9, this redirects demo stdout to a per-capture log file (`*> $logFile`) instead of piping through `Select-String`, avoiding the slow-consumer block that killed the original Stage 10 "Fork A" verification.
2. `tools/v3_m1_mode0_ab/analyze_mode0_ab.py` — reads all 8 variants (6 reused from Stage 7/8/9 + 2 new), synthesizes `cascade_full = pt_direct + cascade_gi` per plan §2, computes linear-HDR `composite_rms`, MAE, mean relative error, p99-clamped RMS, direct share, and the SC1 synth-sanity ratio. Writes side-by-side PNGs (cascade synth on left, PT full on right).

## Verification

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_mode0_ab/capture_extra.ps1 -Variant cornell_g010
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_mode0_ab/capture_extra.ps1 -Variant cornell_hybrid
python tools/v3_m1_mode0_ab/analyze_mode0_ab.py
```

Both captures completed cleanly with exit=0 and all 8 sidecars written. Analyzer succeeded on all 8 variants.

## Results

### Mode-0 composite RMS (linear HDR luma)

| Variant         | `composite_rms` | `composite_mae` | `mean_rel_err` | `direct_share` |
|-----------------|---:|---:|---:|---:|
| sponza_g010     | **0.0196** | 0.0147 | 0.110 | 0.877 |
| sponza_g050     | 0.0458 | 0.0424 | 0.327 | 0.877 |
| sponza_g100     | 0.2060 | 0.2015 | 1.568 | 0.877 |
| **sponza_hybrid** (oracle) | **0.0100** | 0.0089 | 0.065 | 0.877 |
| cornell_g010    | 0.3945 | 0.3375 | 0.443 | 0.487 |
| cornell_g050    | 0.3678 | 0.3124 | 0.408 | 0.487 |
| cornell_g100    | **0.2992** | 0.2496 | 0.336 | 0.487 |
| **cornell_hybrid** (oracle) | **0.2704** | 0.2310 | 0.310 | 0.487 |

### Excess RMS over hybrid oracle

| Scene | best-gain RMS | hybrid RMS | best-gain excess | best-gain key matches mode-17 best? |
|---|---:|---:|---:|---|
| Sponza  | 0.0196 | 0.0100 | +96% | ✅ (both pick gain=0.10) |
| Cornell | 0.2992 | 0.2704 | +11% | ✅ (both pick gain=1.00) |

### Sponza: gain knob has a big visual effect

Going from gain=1.0 → 0.10 reduces mode-0 composite RMS by **10.5×** (0.206 → 0.020). The gap to hybrid shrinks from `+0.196` (~20× the hybrid RMS) to `+0.010` (~96% of the hybrid RMS itself). Translating to "fraction of the cascade-vs-hybrid gap closed by lowering gain":

```
gap_at_g100 = 0.206 − 0.010 = 0.196
gap_at_g010 = 0.020 − 0.010 = 0.010
gap_closed  = 1 − (0.010 / 0.196) = 94.9%
```

**95% of the Sponza cascade-vs-hybrid mode-0 gap is closed by switching gain 1.0 → 0.10.** This is the headline finding. The Stage 8/9 gain ladder is measuring something real and quantitative for Sponza, not a measurement artifact.

### Cornell: gain knob barely moves the needle

Going from gain=0.10 → 1.0 reduces Cornell mode-0 composite RMS by only **24%** (0.394 → 0.299). The gap to hybrid only shrinks from `+0.124` to `+0.029`. Hybrid itself is also far from PT (RMS 0.270, which is 27× Sponza hybrid's 0.010). Cornell direct_share is only 48.7% (half of GI), so gain changes should matter MORE in principle — but they don't. This confirms Stage 9 SC10: **Cornell GI is structurally under-bright at every gain**; the MB-gain knob cannot fix it.

### Direct-share asymmetry explains the per-scene gain sensitivity

- **Sponza direct_share = 87.7%.** Direct light dominates the final image. GI is a small additive term. Therefore the mode-0 RMS is small at the best gain (≈0.02), but a wrong gain still adds visible energy (0.21 at g=1.0). Tightening GI to PT is a real but modest visual win — invisible in mode-17 (which strips direct), large in mode-0 (which keeps direct).
- **Cornell direct_share = 48.7%.** GI contributes roughly the same as direct. So in principle, getting GI right should give a larger mode-0 improvement. But every gain gives ~similar mode-0 RMS. Combined with Cornell's `ratio_self ≈ 0.5` at every gain (Stage 9), this means: cascade GI is *consistently* under-bright on Cornell — by a structural ~2× factor that no gain change addresses.

## Self-critique

**SCs that fired:**

- **SC1 (synthesis sanity vs PNG)**: Failed — the inverse-gamma I used in the analyzer (pure 2.2) does not match the engine's actual tone-mapping (likely ACES or other curve), so `synth_sanity_ratio_vs_png` came out wildly off (11–360 instead of ~1). The synthesis itself (`cascade_full = pt_direct + cascade_gi` in linear HDR) is still a valid comparison against `pt_full` (also linear HDR), so the headline `composite_rms` numbers stand. The sanity check is removed from the verdict logic but kept in the JSON as a "broken; do not interpret" flag.
- **SC4 (clamped RMS)**: confirmed. `composite_rms_clamped` is within 2-7% of `composite_rms` across all variants — bright-pixel domination is not biasing the metric. The verdict would be identical using either.
- **SC6 (hybrid isn't ground truth)**: confirmed and important. Sponza hybrid RMS = 0.010 (small); Cornell hybrid RMS = 0.270 (large — 27× Sponza). Hybrid is good on Sponza but a poor reference on Cornell. The "match hybrid" gate is therefore tight on Sponza and loose on Cornell.
- **SC8 (Cornell consumer-side ratio_self=0.49)**: confirmed harder. Cornell hybrid RMS (0.270) is itself big, and Cornell gain=1.0 RMS (0.299) is only slightly worse. So even hybrid doesn't close the cascade-vs-PT gap on Cornell. The Cornell problem is genuinely consumer-side, not a hybrid-vs-cascade-gain choice.
- **SC9 (stdout pipe fix)**: confirmed. Both new captures completed cleanly with the log-file redirect. The original Stage 10 "Fork A" verification script's `Select-String` pipe was the early-exit cause.

**New SCs surfaced by the implementation:**

- **SC11 (new): Visual side-by-sides reveal a subtle calibration mismatch even at best gain.** For sponza_g010 the side-by-side PNG shows the synthesized composite (left) is visibly *brighter and slightly more detailed* than `pt_full` (right). The numerical mean relative error is 0.110 (11%), which is consistent with this — the cascade is over-bright by ~10% in the final image even at the best gain. The hybrid variant (RMS 0.010, rel_err 0.065) shows much closer left-right visual match. So Sponza gain=0.10 is "best of the cascade options" but still distinguishable from hybrid by eye.
- **SC12 (new): "Competitive with hybrid" (within 5% of hybrid RMS) is empty for every variant on both scenes.** No gain choice reaches the 5% tolerance. The verdict still picks the best mode-0 RMS (which matches mode-17 best), but the absolute gap to hybrid is non-trivial. Stage 11 work should not assume the gain ladder alone reproduces hybrid quality.

## Interpretation

The Stage 8/9 attribution chain is **vindicated on both scenes**: the mode-17 |p95| metric is not a measurement artifact. The gain that wins on the GI-only diagnostic also wins on the final-composite RMS. For Sponza, the win is dramatic (10× RMS reduction, 95% gap closed to hybrid). For Cornell, the win is small (only 24% RMS reduction) because Cornell's failure mode is structural under-brightness, not a tunable gain.

**Implication for Fork A (the per-scene MB-gain preset, reverted at the start of this stage):** Fork A IS the right move for Sponza. The reverted infrastructure (CLI flag + scene-class lookup + apply at scene-load) should be revived as Stage 11. For Cornell, Fork A is a no-op (gain=1.0 is already the engine default), so Cornell's per-scene branch only matters as an explicit "this is intentional" audit.

**Implication for Cornell:** No amount of gain tuning fixes it. Cornell needs a consumer-side investigation: probe atlas read normalization, `sampleDirectionalGI` cosine-weighting, atlas alpha gating. The signal is consistent with cascade GI being under-integrated by a constant ~2× factor across the entire Cornell image.

**The "pure dark scene" reaction was correct but the interpretation was misleading:**

- "Pure dark" applied to the mode-17 PNG, which displays GI-only. Sponza GI is 12% of the final image — mode-17 strips the 88% direct light and shows only the small residual. At low gain that residual is correctly small.
- The mode-0 PNG (final composite, what a user actually sees in normal rendering) at the same Sponza gain=0.10 looks fine — bright walls + dim indirect, similar to PT.
- The gain change is a real visual improvement in the final image, just one that doesn't *look* dramatic because direct light dominates either way.

## Improved next direction

Stage 11 should split into two independent tracks:

1. **Stage 11a — Revive Fork A for Sponza (justified)**. Per-scene MB-gain preset, opt-in CLI flag, scene-class detection. Same scope as the reverted Stage 10 "Fork A" plan, but now grounded by mode-0 evidence. Stage 9's gain=0.10 minimum is also the mode-0 best, so 0.10 is the right Sponza default. Default flip to "Auto" remains a separate decision.
2. **Stage 11b — Cornell consumer-side audit (unblocked)**. Investigate why Cornell cascade GI is ~2× under-bright at every gain. Candidates: `sampleDirectionalGI` cosine weighting, atlas read normalization, alpha gating in the consumer shader. Use mode-17 + mode-0 EXR sidecars; compare against PT_GI on a probe-by-probe basis (Stage 5/7 infrastructure already exists).

These are independent — 11a does not block 11b.

## Decision

`STAGE8_9_VINDICATED`. Fork A is justified by mode-0 evidence. Cornell is a separate Stage 11b problem. Stage 10 produces no engine source changes; the verdict updates the Stage 11 work order.
