# M1 Stage 9 Plan - MB-Gain Sponza Sensitivity Ladder

**Date:** 2026-05-27.
**Predecessor:** [v3_m1_stage8_source_energy_ab_impl.md](v3_m1_stage8_source_energy_ab_impl.md) (verdict `MB_FEEDBACK_DOMINANT`).
**Goal:** find an MB-gain value (or per-scene MB-gain policy) that clears the |p95(ratio-1)| ≤ 0.50 retirement gate on Sponza **without** regressing Cornell or the implicit |p95| baseline at gain=1.0.

## Why this stage exists

Stage 8 proved multi-bounce temporal feedback is the dominant Sponza failure source and showed a near-linear gain relationship on the 3-point ladder {0.0, 0.5, 1.0}:

| Sponza | `mb_gain` | `ratio_self` | `\|p95\|` |
|---|---:|---:|---:|
|  | 0.00 | 0.969 | 0.272 |
|  | 0.50 | 1.613 | 0.889 |
|  | 1.00 | 4.715 | 4.528 |

The 0.0→0.5 step moves `|p95|` by ~0.62, the 0.5→1.0 step by ~3.64. So the relationship is monotonic but **strongly non-linear**: the gain interval `[0, 0.5]` is comparatively safe; the interval `[0.5, 1.0]` is where the over-drive lives. The crossing through `|p95| = 0.50` therefore lies somewhere in `[0.25, 0.5]` (probably closer to 0.5).

Cornell at gain=1.0 (baseline) sits at `|p95|=0.86`, ratio_self=0.49 — already under-bright, so reducing gain will push Cornell further down (Stage 8 confirmed: Cornell mb_off → ratio_self=0.21, `|p95|`=0.91). So a single global gain is unlikely to satisfy both. The ladder will quantify the trade.

## Plan

### 1. Sponza ladder

Sponza, cam 0, N=2048, render-mode 17, hybrid OFF, all other flags identical to Stage 7/8 baseline. Vary only `--multi-bounce-gain`.

Gain values: **{0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.75, 1.00}**.

Eight points. Three are already captured (0.0 = `mb_off`, 0.5 = `mb_gain_half`, 1.0 = `baseline` reused from Stage 7). Five new captures.

`mb_off` (gain=0.0) is achieved by `--use-multi-bounce=0`. To keep one knob — and because Stage 8 showed that gain=0.0 via the gain slider is equivalent to MB=0 for ratio purposes — the new variants will all use `--use-multi-bounce=1 --multi-bounce-gain=X`. Stage 8's `mb_off` row can be reused since `gain=0.0` × `mb=1` should equal `mb=0`. (Pre-flight verification: SC1 below.)

### 2. Cornell sanity grid

Cornell, cam 0, N=2048, same flags. Vary `--multi-bounce-gain` over **{0.25, 0.50, 0.75, 1.00}**. Four points. Gain=1.0 already captured (Cornell baseline from Stage 8). Three new.

This is the trade quantification: at the gain where Sponza clears the gate, what does Cornell `|p95|` look like?

### 3. Capture cost

11 new captures total (5 Sponza + 3 Cornell + 3 sanity recaptures to confirm gain=0.0 ≡ mb=0). Actually 5 Sponza + 3 Cornell = 8 new at ~3 min ≈ 24 min sequential. Reuse Stage 8 captures where possible.

### 4. Analyze

`tools/v3_m1_mb_gain_ladder/analyze_mb_gain.py`. Reuse Stage 8's `analyze_source_energy.py` infrastructure (read_exr, mask, screen metrics). New output: per-scene gain → `{ratio_self, bad_pct, |p95|}`. Plot-friendly JSON.

Decision algorithm:

1. **Compute Sponza-acceptable band:** smallest gain interval whose endpoints bracket the |p95|=0.50 line (linear interp between bracket endpoints gives the estimated crossing gain).
2. **Compute Cornell-tolerable band:** largest gain at which Cornell `|p95|` rises by ≤10% vs gain=1.0 baseline (Cornell's existing 0.86 → 0.95 is the worst we tolerate).
3. **Intersection test:** if Sponza-acceptable max ≤ Cornell-tolerable min, a single global gain exists. Promote that gain.
4. **No intersection:** no single global gain works. Pivot recommendation in §5.

### 5. If no single gain works

The honest read is that MB gain must be **scene-dependent** or **probe-local**. Three forks for Stage 10:

- **Fork A (cheapest):** per-scene preset (`gain=1.0` for cornell-class scenes, gain=Sponza-fit-value for sponza-class). Requires only a config knob, no shader work.
- **Fork B (principled):** probe-local gain attenuation based on local geometry density / SDF distance. Requires shader-side per-probe weighting.
- **Fork C (last resort):** if neither A nor B closes the gap, MB feedback as a whole may need redesign (energy normalization or history-validation).

This stage **does not** decide the fork — that's the Stage 10 plan based on the data points produced here.

## Self-critique and improvements

### SC1 — `gain=0.0` may not equal `--use-multi-bounce=0`

Stage 8 used `--use-multi-bounce=0` (which presumably skips the MB code path entirely) for `mb_off`, and `--use-multi-bounce=1 --multi-bounce-gain=0.5` for `mb_gain_half`. The two paths might differ in side-effects (history clear, temporal seed). **Improvement:** add a `gain=0.0_via_gain` capture (`--use-multi-bounce=1 --multi-bounce-gain=0.0`) and verify it matches `mb_off` to <1% on `|p95|`. If not, prefer the gain-slider path for the entire ladder and recapture gain=0.0 / 0.5 (relegate the Stage 8 numbers to advisory).

### SC2 — Ladder spacing may miss the |p95|=0.50 crossing

The Stage 8 jump from 0.5 → 1.0 in `|p95|` is 0.89 → 4.53. If the curve is steeply non-linear, the crossing through 0.50 could be at gain ≈ 0.45 — very close to the existing 0.5 point but not exact. **Improvement:** plan an adaptive second pass: after the first capture sweep, if the crossing falls inside a gain interval wider than 0.10, capture two intermediate points to bracket it within 0.05 (gain resolution 0.05 is sufficient for a slider default).

### SC3 — N=2048 sign-off N may under-converge low-gain MB

MB feedback at low gain converges faster (less feedback => less iteration needed). At high gain it might still be converging at N=2048. The Stage 8 mb_off showed ratio_self=0.969 — extremely close to 1.0 — which suggests the system **is** well-converged at the low-gain end. **Improvement:** keep N=2048 fixed across the ladder for apples-to-apples comparison; record convergence trend as a follow-up (frame-by-frame `|p95|` time-series is out of scope for Stage 9).

### SC4 — Cornell under-bright at baseline is a separate problem

Cornell at gain=1.0 already has `|p95|=0.86` and `ratio_self=0.49` (under-bright). Reducing MB gain will only make it more under-bright. So the "Cornell-tolerable band" criterion in §4 step 2 is too generous — Cornell is *already* outside the 0.50 retirement gate. **Improvement:** rephrase the Cornell criterion: a tolerable gain is one where Cornell `|p95|` does not *worsen* by more than 10%, AND where Cornell `ratio_self` does not drop by more than 0.10 below the gain=1.0 baseline. This decouples "gain doesn't make Cornell worse" from "gain doesn't fix Cornell" — the latter is out of scope for Stage 9.

### SC5 — Gain change might affect Cornell direct-shadowing path

Stage 8 saw Cornell `valid` pixel count drop from 37036 (baseline) to 36936 (mb_off): only 100 fewer pixels (~0.3%). So the masking is stable. **Improvement:** still report `valid` per gain to surface any unexpected mask drift.

### SC6 — The 11-point capture cost is large

5 Sponza + 3 Cornell + 3 SC1 sanity = 11 captures × ~3 min ≈ 33 min. Acceptable but high. **Improvement:** if SC1 sanity (gain=0.0 via gain slider) matches `mb_off` within 1% on the first capture, skip the rest of the SC1 sanity recaptures (cuts 2 captures, ~6 min).

### SC7 — Don't promote the chosen gain in Stage 9

This stage measures the trade and recommends a fork. Promoting `--multi-bounce-gain=X` to default is a separate Stage 10 implementation choice. **Improvement:** explicit "out of scope" note: Stage 9 produces data + fork recommendation; it does not edit any defaults.

### SC8 — The shader-side `fragProbeDiag.rgb` scaling anomaly from Stage 8 is still open

Stage 8 flagged that `fragProbeDiag.rgb` (supposed to be geometric) scales ~0.29× under MB-off. The gain ladder will exercise the same code path at intermediate gain values. **Improvement:** capture and dump the diag rgb max per gain to see if the scaling is a continuous function of gain. If yes, that's a real shader bug worth fixing independently (a Stage 11 candidate). If gain=0.0 alone is anomalous, it's a code-path-specific bug. Either way, surface it as a data integrity note; don't let it block the Stage 9 verdict.

## Acceptance

- `tools/v3_m1_mb_gain_ladder/mb_gain_ladder_results.json` records 8 Sponza points + 4 Cornell points with `{ratio_self, bad_pct, |p95|, valid}`.
- Linear-interp estimate of the Sponza gain that puts `|p95|=0.50`.
- Cornell `|p95|` at that estimated Sponza-acceptable gain.
- Fork recommendation (A/B/C) based on whether Cornell stays inside its tolerance.

## Out of scope

- Promoting any gain to default (Stage 10).
- Implementing per-scene or per-probe MB gain (Stage 10+).
- Fixing the `fragProbeDiag.rgb` scaling anomaly (separate Stage 11 candidate).
- Investigating Cornell under-brightness at baseline (separate quality concern).
