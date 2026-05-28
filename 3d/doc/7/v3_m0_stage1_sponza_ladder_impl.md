# M0 Stage 1 Sponza Ladder Implementation

**Date:** 2026-05-27.
**Plan:** `doc/7/v3_m0_stage1_sponza_ladder_plan.md`.
**Status:** Complete. Sponza sign-off N is 2048, verdict `CONVERGED`.

## Implemented

| Item | Result |
|------|--------|
| `tools/v3_baseline/analyze_baselines.py` | Added EXR analyzer for Sponza cascade/PT metrics and PT convergence deltas. |
| Sponza cascade-OFF ladder | Captured N=128, 256, 512, 1024, 2048 with PNG + `cascade_gi`/`pt_full`/`pt_direct` EXRs. |
| Sponza hybrid-ON capture | Captured N=2048 with PNG + three EXR sidecars. |
| `tools/v3_baseline/sponza_default_metrics.json` | Generated cascade-OFF metrics + convergence verdict. |
| `tools/v3_baseline/sponza_hybon_metrics.json` | Generated hybrid-ON metrics at N=2048. |
| `tools/v3_baseline/baseline_lock.json` | Rebuilt with Sponza N=2048, verdict `CONVERGED`, file hashes, and Sponza metrics. |

## Commands Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/sponza_capture.ps1 -UseHybrid 0
python tools/v3_baseline/analyze_baselines.py --scene=sponza --hybrid=0 --out tools/v3_baseline/sponza_default_metrics.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/sponza_capture.ps1 -UseHybrid 1 -FrameList 2048
python tools/v3_baseline/analyze_baselines.py --scene=sponza --hybrid=1 --n 2048 --out tools/v3_baseline/sponza_hybon_metrics.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/build_baseline_lock.ps1 -SponzaN 2048 -SponzaVerdict CONVERGED
```

## Sponza PT Convergence

PT indirect mean is computed from `max(pt_full - pt_direct, 0)`.

| Interval | PT mean delta |
|----------|---------------|
| 128 -> 256 | 0.0203 |
| 256 -> 512 | 0.0088 |
| 512 -> 1024 | 0.0268 |
| 1024 -> 2048 | 0.0238 |

Verdict: **CONVERGED**, because the 1024 -> 2048 delta is 2.38%, below the 5% threshold.

## Metrics at N=2048

| Capture | ratio_self | abs_p95 | dim_pct | bright_pct | pt_indirect_mean | valid |
|---------|------------|---------|---------|------------|------------------|-------|
| Sponza cascade-OFF | 4.7148 | 4.5279 | 0.0 | 100.0 | 0.05888 | 693 |
| Sponza hybrid-ON | 0.8315 | 0.3090 | 6.64 | 0.0 | 0.05888 | 693 |

Interpretation: the default Sponza cascade-GI baseline is severely bright relative to PT indirect in the valid mask. Hybrid-ON pulls the mode-17 GI much closer to PT for this camera, but this is a baseline observation, not a new algorithmic conclusion.

## Lock State

`baseline_lock.json` now reports:

- `cornell_cam0_cascade_off.status = complete`
- `cornell_cam0_hybrid_on.status = complete`
- `sponza_cam0_cascade_off.status = complete`
- `sponza_cam0_hybrid_on.status = complete`
- `sign_off.sponza_N = 2048`
- `sign_off.sponza_convergence_verdict = CONVERGED`

## Self-Critique and Improvements

- **SC1: The first lock-builder pass treated Sponza metrics as out-of-band.** Improvement: patched `build_baseline_lock.ps1` to attach Sponza analyzer rows directly to the relevant capture entries.
- **SC2: The analyzer's valid mask is narrow on Sponza at high N.** At N=2048, valid count is 693 pixels. This is enough for a lock anchor, but M1 should not overgeneralize the Sponza verdict as whole-scene quality. Improvement: document valid count in metrics and lock.
- **SC3: Cascade-OFF Sponza is extremely bright.** This could be a real scene/camera baseline or a mask/scale sensitivity. Improvement: keep it as baseline evidence and require M1 deltas to compare against the locked artifact, not against expectations from Cornell.
- **SC4: Hybrid-ON uses the same PT sidecars as cascade-OFF at N=2048.** The PT means match exactly in the analyzer output, which is expected for the same camera/seed/sample count, but it also means hybrid assessment is only about `cascade_gi` output. Improvement: note this explicitly in the metrics table.
- **SC5: No N=4096 escalation.** Because 2048 converged under the threshold, no escalation is needed. Improvement: `CONVERGED` is recorded rather than silently skipping the contingency ladder.

## Handoff

M0 Stage 1 is now complete enough for M1 Stage 0 planning and Cornell/Sponza locked comparisons. The next phase should be M1 Stage 0: per-delta implementation docs for the #3 + #6 bundle first, then #4 comparative formulation work, using `baseline_lock.json` as the comparison anchor.
