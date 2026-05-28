# M1 Stage 1 Implementation - Delta #3/#6 Matrix Verdict

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage1_delta36_matrix_plan.md`.  
**Result artifact:** `tools/v3_m1_delta36/matrix_results.json`.  
**Verdict:** do not continue with the current #3/#6 ShaderToy-gap closure as a promoted path.

## What Was Implemented

1. Added `tools/v3_m1_delta36/analyze_matrix.py`.
   - Reads Cornell and Sponza mode-17 EXR sidecars.
   - Computes cascade GI vs PT indirect metrics using the same luminance-ratio method as M0.
   - Compares candidates against both:
     - locked M0 cascade-OFF baselines from `tools/v3_baseline/baseline_lock.json`;
     - the same-run matrix baseline to avoid over-reading baseline drift.
   - Emits `tools/v3_m1_delta36/matrix_results.json`.

2. Ran the full N=2048 matrix.
   - Cornell command:
     - `powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_delta36/capture_matrix.ps1 -Scene cornell -N 2048`
   - Sponza command:
     - `powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_delta36/capture_matrix.ps1 -Scene sponza -N 2048`
   - Each scene produced 4 conditions:
     - `baseline`
     - `delta3`
     - `delta6`
     - `both`
   - Each condition produced PNG + cascade GI EXR + PT full EXR + PT direct EXR.

3. Fixed one analyzer robustness issue.
   - `baseline_lock.json` is written with a UTF-8 BOM on this machine.
   - The analyzer now reads it with `utf-8-sig`.

## Primary Results

Primary verdict uses same-run matrix baseline comparisons. M0 comparisons remain in the JSON as a drift check.

| Condition | Cornell vs matrix baseline | Sponza vs matrix baseline | Combined |
|---|---:|---:|---:|
| `delta3` | DEAD | MISSING (`empty_mask`) | DEAD |
| `delta6` | DEAD | MARGINAL | DEAD |
| `both` | DEAD | DEAD | DEAD |

Key Cornell facts:

| Condition | ratio_self | abs_p95 | dim_pct | bright_pct | valid |
|---|---:|---:|---:|---:|---:|
| baseline | 0.4845 | 0.8892 | 86.70 | 3.77 | 37866 |
| delta3 | 0.0418 | 0.9965 | 100.00 | 0.00 | 5625 |
| delta6 | 0.3992 | 0.8985 | 90.04 | 1.66 | 37856 |
| both | 0.1617 | 0.9967 | 97.75 | 0.51 | 11446 |

Cornell interpretation:

- `delta3` nearly kills indirect energy.
- `delta6` is less destructive than `delta3`, but still moves ratio farther from 1.0, slightly worsens `abs_p95`, and increases dim pixels.
- `both` inherits the same under-energy failure.

Key Sponza facts:

| Condition | ratio_self | abs_p95 | dim_pct | bright_pct | valid |
|---|---:|---:|---:|---:|---:|
| baseline | 4.7148 | 4.5279 | 0.00 | 100.00 | 693 |
| delta3 | empty_mask | n/a | n/a | n/a | 0 |
| delta6 | 2.9583 | 2.4701 | 0.00 | 100.00 | 693 |
| both | 0.1537 | 0.9736 | 100.00 | 0.00 | 256 |

Sponza interpretation:

- `delta6` reduces over-bright magnitude but every valid pixel is still over-bright.
- `both` overcorrects into complete dim failure.
- `delta3` standalone falls out of the current valid mask, which is not a usable success signal.

## Self-Critique

1. **The original plan relied too much on M0-only comparison.**
   - Cornell matrix baseline drifted materially from the M0 lock: ratio 0.6499 in M0 vs 0.4845 in the matrix run, and dim pixels rose from 28.60% to 86.70%.
   - Improvement made: analyzer now reports same-run matrix-baseline comparisons as the primary candidate verdict.

2. **The Sponza mask is narrow.**
   - Sponza still has only 693 valid baseline pixels at this camera/mask threshold.
   - This is acceptable for vetoing extreme behavior, but not enough to tune final constants.

3. **The current `delta3` implementation may be too literal or too late in the pipeline.**
   - Replacing scalar attenuation with `WeightedSample.rgb` strongly suppresses energy in both scenes.
   - That suggests the missing ShaderToy behavior is not simply "use the weighted RGB result here"; the surrounding probe-to-world mapping and weighting contract are likely different.

4. **`delta6` has a useful clue but is not a fix.**
   - In Sponza, geometric cone widening moves the ratio from 4.7148 to 2.9583 and drops `abs_p95` by 45.45%.
   - It fails the combined verdict because Cornell gets worse and Sponza remains 100% over-bright.

## Improved Direction

Do not revert the EXR capture, measurement, baseline, or A/B switch infrastructure. Keep those.

Do not promote the current #3/#6 algorithm changes. Leave them as diagnostic toggles only, or remove them after the next candidate is selected.

Next phase should shift away from copying individual ShaderToy code fragments and instead isolate the radiance mismatch by transport contract:

1. Lock a minimal per-cascade probe audit image/table:
   - sampled upper-cascade energy;
   - receiver cascade energy before merge;
   - cascade interval/cone footprint;
   - visibility hit/miss contribution.

2. Add one candidate at a time around the largest observable contract mismatch:
   - either probe footprint/interval normalization;
   - or merge-energy normalization;
   - or atlas/world position mapping.

3. Use Sponza over-bright as the veto scene and Cornell under-energy as the regression scene.

## Final Decision

Proceed with a better direction, not with the current #3/#6 closure attempt.

The useful work from this phase is the measurement harness and the negative result: #3/#6 do not explain the production mismatch in a way that survives both Cornell and Sponza.
