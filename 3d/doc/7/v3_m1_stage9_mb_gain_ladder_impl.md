# M1 Stage 9 Implementation - MB-Gain Sponza Sensitivity Ladder

**Date:** 2026-05-27.
**Plan:** [v3_m1_stage9_mb_gain_ladder_plan.md](v3_m1_stage9_mb_gain_ladder_plan.md).
**Result artifact:** `tools/v3_m1_mb_gain_ladder/mb_gain_ladder_results.json`.
**Verdict:** `FORK_PER_SCENE_OR_PROBE` — no single global MB gain satisfies both Sponza and Cornell.

## What changed

No shader / engine source changes. Tooling only.

1. `tools/v3_m1_mb_gain_ladder/capture_gain.ps1` — parameterized `(Scene, Gain)` capture using the gain-slider path consistently (`--use-multi-bounce=1 --multi-bounce-gain=$Gain`, per SC1).
2. `tools/v3_m1_mb_gain_ladder/run_ladder.ps1` — sequential runner: 5 new Sponza gains {0.00, 0.10, 0.20, 0.30, 0.40} + 3 new Cornell gains {0.25, 0.50, 0.75}. Reuses Stage 7 baseline (gain=1.0) and Stage 8 `mb_gain_half` (gain=0.5) on Sponza, and Stage 8 Cornell baseline (gain=1.0).
3. `tools/v3_m1_mb_gain_ladder/analyze_mb_gain.py` — reads ladder + reused points, computes screen-level `{ratio_self, bad_pct, |p95|, valid, diag_rgb_max}`, linear-interp crossing for `|p95|=0.50`, and applies the §4 Cornell tolerance band to pick the fork.

## Verification

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_mb_gain_ladder/run_ladder.ps1
python tools/v3_m1_mb_gain_ladder/analyze_mb_gain.py
```

8 captures completed in ~24 minutes. Total measured points: 7 Sponza gains + 4 Cornell gains.

## Results

### Sponza gain ladder

| `gain` | `ratio_self` | `bad_pct` | `\|p95\|` | source |
|---:|---:|---:|---:|---|
| 0.00 | 0.969 | 0.58%  | **0.272** | stage9 |
| 0.10 | 1.040 | 1.73%  | **0.253** | stage9 |
| 0.20 | 1.144 | 16.02% | **0.351** | stage9 |
| 0.30 | 1.268 | 47.91% | **0.493** | stage9 |
| 0.40 | 1.418 | 73.16% | 0.667 | stage9 |
| 0.50 | 1.613 | 90.04% | 0.889 | stage8 |
| 1.00 | 4.715 | 100.00% | 4.528 | stage7 |

Scope §7 retirement gate `|p95| ≤ 0.50` is cleared at every gain in **[0.00, 0.30]**. Linear-interp crossing through `|p95|=0.50` ≈ **gain 0.304**.

**Non-monotonic finding (Plan SC2):** `gain=0.10` (|p95|=0.253) is BETTER than `gain=0.00` (|p95|=0.272) AND has `ratio_self=1.040` — essentially exactly 1.0. The ladder has a shallow minimum around gain ≈ 0.10, not at gain=0.00. This was not predicted by the plan's linear extrapolation.

### Cornell sanity ladder (SC4 darkening confound)

| `gain` | `ratio_self` | `bad_pct` | `\|p95\|` | source |
|---:|---:|---:|---:|---|
| 0.25 | 0.237 | 0.46% | 0.899 | stage9 |
| 0.50 | 0.286 | 0.92% | 0.880 | stage9 |
| 0.75 | 0.361 | 1.59% | 0.857 | stage9 |
| 1.00 | 0.492 | 3.86% | 0.857 | stage8 |

Cornell `|p95|` is **nearly flat** across the gain range (0.857 → 0.899; max rise +0.042). Cornell `ratio_self` drops monotonically and substantially (0.492 → 0.237; max drop -0.255). Cornell becomes more under-bright at lower gain, as predicted by Stage 8 SC2.

### Cornell tolerance check (Plan §4 step 2, refined by SC4)

| `gain` | `\|p95\|` rise vs g=1.0 | `ratio_self` drop vs g=1.0 | inside `[≤0.10, ≤0.10]` band? |
|---:|---:|---:|:---:|
| 0.25 | +0.042 | +0.255 | ❌ (ratio drop) |
| 0.50 | +0.023 | +0.206 | ❌ (ratio drop) |
| 0.75 | -0.000 | +0.132 | ❌ (ratio drop) |

Every Cornell gain below 1.0 violates the ratio-drop tolerance.

### Fork decision

At the Sponza crossing gain (≈ 0.30), Cornell `ratio_self` is roughly 0.27 (interp between 0.24@0.25 and 0.29@0.50) — a drop of ~0.22 from the gain=1.0 baseline (0.49). That is 2.2× the tolerance threshold of 0.10.

**Verdict: `FORK_PER_SCENE_OR_PROBE`.** No single global MB gain clears Sponza's `|p95|≤0.50` gate without significantly worsening Cornell brightness.

## Data-integrity finding (Plan SC8 confirmed)

`fragProbeDiag.rgb` from `raymarch.frag:841` — supposedly the geometric `pg/uAtlasVolumeSize` per pixel — has its maximum value increase **monotonically with MB gain** on Sponza:

| `gain` | `diag_rgb_max` |
|---:|---:|
| 0.00 | 0.159 |
| 0.10 | 0.162 |
| 0.20 | 0.169 |
| 0.30 | 0.177 |
| 0.40 | 0.187 |
| 0.50 | 0.200 |
| 1.00 | 0.391 |

This is **not** a code-path-specific anomaly (Stage 8's hypothesis) — it is a continuous function of MB gain. The most likely explanation is that `giProbeDiagTex` participates in the temporal EMA pipeline and the EMA path multiplies by `multi_bounce_gain` or composes with the MB feedback buffer, rather than treating `fragProbeDiag` as a pure per-frame overwrite. The geometric `pg/uAtlasVolumeSize` value is independent of gain by construction; only a pipeline-level interaction can produce a monotonic gain dependence.

This does not affect Stage 9's verdict (it relies on screen-level `cascade_gi` / `pt_gi` metrics, not on diag.rgb scaling). Filed as a separate shader-pipeline issue for **Stage 11 candidate**: trace `giProbeDiagTex` through temporal_blend.comp / inject_radiance.comp / radiance_3d.comp to find where MB gain leaks into the diagnostic texture.

## Self-critique

**SCs from the plan that fired:**

- **SC1 (gain=0 ≡ mb_off equivalence)**: verified true. Stage 9 gain=0.00 |p95|=0.272 vs Stage 8 mb_off |p95|=0.272 — identical to 4 decimals. The gain-slider path is the right primary path for the ladder.
- **SC2 (ladder spacing might miss the crossing)**: NOT NEEDED. The crossing falls cleanly between gain=0.30 (|p95|=0.493) and gain=0.40 (|p95|=0.667), a gain interval of 0.10. Estimated crossing at 0.304. No adaptive second pass needed.
- **SC4 (Cornell darkening confound)**: confirmed and decisive. Cornell |p95| is flat but ratio_self drops by ~50% across the gain range. Without the refined SC4 criterion (decoupled |p95| rise from ratio drop), the analyzer would have falsely declared FORK_GLOBAL_GAIN.
- **SC8 (diag.rgb monotonic in gain)**: confirmed — see "Data-integrity finding" above. The Stage 8 anomaly is real and gain-dependent.

**Things I missed in the plan (new SCs):**

- **SC9 (new): Non-monotonic Sponza minimum at gain=0.10.** The plan assumed monotonic behavior from the 3-point Stage 8 data. The 8-point ladder reveals a shallow minimum at gain ≈ 0.10 where `ratio_self` is essentially exactly 1.0 (1.040). This means the "best Sponza" gain is NOT zero — there is a real first-order signal in single-bounce-plus-a-bit-of-MB. For Sponza specifically, gain=0.10 is the engine-default candidate, not gain=0.00.
- **SC10 (new): Cornell ratio_self at gain=1.0 is itself the unfixed problem.** The baseline Cornell ratio_self of 0.49 (half of PT) is an under-brightness problem unrelated to MB gain — every Cornell gain value sits below 0.50. Cornell GI fundamentals (probe contribution, normalization) are likely off by ~2x. This is out of Stage 9 scope but **must** be acknowledged: any per-scene gain policy for Cornell that picks gain=1.0 will still leave Cornell at ratio_self=0.49. A future Cornell-specific stage needs to look at the *consumer-side* path, not MB gain.
- **SC11 (new): The shallow Sponza minimum at gain=0.10 should be searched more finely.** If 0.10 beats 0.00 by 7% on |p95|, gain ∈ {0.05, 0.07, 0.13, 0.15} might do even better. Not in Stage 9 scope (the §4 acceptance only asks for the crossing band) but a Stage 10 follow-up.

## Interpretation

The Stage 8 attribution (MB feedback over-drives Sponza) is fully confirmed. The new insight is the **scene asymmetry**:

- Sponza's MB feedback at default gain=1.0 is over-driven by ~14× (need gain ≤ 0.30 to clear the gate; best at gain=0.10).
- Cornell's MB feedback at default gain=1.0 is *itself* under-driven by ~2× (ratio_self only 0.49). Halving MB gain on Cornell just makes the existing under-brightness worse.

These are not compatible failure modes for a single global lever. The honest statement: **MB feedback gain at the current implementation is calibrated for neither scene correctly. Sponza is over-pumped by a large multiplier; Cornell is under-pumped by a factor of 2 regardless of gain.** The MB gain knob cannot reconcile them.

## Improved next direction

Stage 10 must pursue Fork A (per-scene gain) as the minimum patch and treat Fork B (probe-local gain) as the principled fix:

1. **Stage 10 (Fork A immediate)**: ship a `--mb-gain-sponza-class` vs `--mb-gain-cornell-class` per-scene preset. For Sponza-class scenes default to gain=0.10. For Cornell-class scenes keep gain=1.0 (no improvement on ratio_self, but no regression either). This is config-only, zero shader risk.
2. **Stage 11 (Fork B principled)**: probe-local MB gain attenuation. Hypothesis: Sponza probes that sit near long open sight lines into bright walls accumulate too much MB energy; Cornell probes do not. A probe-local SDF distance or local-cone-aperture term could attenuate gain at high-leverage probes. Requires shader work in `radiance_3d.comp` MB feedback path.
3. **Stage 11 (data-integrity)**: separately fix `fragProbeDiag.rgb` gain leakage so per-cell attribution becomes possible again (Stage 6/7-style diagnostics).
4. **Out of scope here**: Cornell `ratio_self=0.49` under-brightness is a consumer-side problem, not an MB problem. File as a separate Cornell-only stage.

## Decision

Promote `FORK_PER_SCENE_OR_PROBE`. Stage 10 begins with Fork A (per-scene preset, gain=0.10 for Sponza, gain=1.0 for Cornell). **Do NOT** ship a global `--multi-bounce-gain=0.10` default — it would regress Cornell ratio_self by 0.26 (from 0.49 to 0.24). **Do NOT** treat this as a Cornell fix. Track Cornell under-brightness as a separate quality concern.
