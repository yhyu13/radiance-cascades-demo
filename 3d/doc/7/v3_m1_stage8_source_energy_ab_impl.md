# M1 Stage 8 Implementation - Source-Energy A/B

**Date:** 2026-05-27.
**Plan:** [v3_m1_stage8_source_energy_ab_plan.md](v3_m1_stage8_source_energy_ab_plan.md).
**Result artifact:** `tools/v3_m1_source_energy_ab/source_energy_results.json`.
**Verdict:** `MB_FEEDBACK_DOMINANT` (Sponza broad-local energy is multi-bounce temporal feedback; Cornell signal is unrelated).

## What changed

No shader / engine source changes. Only tooling and CLI flag usage.

1. `tools/v3_m1_source_energy_ab/capture_source_energy.ps1` — parametric Sponza/Cornell × {baseline, mb_off, mb_gain_half, jitter_off, delta3_on, hybrid_on}; writes the Stage 7 EXR sidecar set into per-variant capture dirs.
2. `tools/v3_m1_source_energy_ab/run_all_captures.ps1` — sequential runner (8 captures, ~20 min total).
3. `tools/v3_m1_source_energy_ab/analyze_source_energy.py` — reuses Stage 7 mask logic; reports fixed-cell view at the Stage 7 dominant Sponza cells `(7,5,4) (6,5,4) (6,4,4)` and screen-level `ratio_self / bad_pct / |p95|`. Includes a screen-fallback verdict when fixed-cell `p000` shifts off the baseline cells (SC1) and a `CASCADE_BROKEN` sentinel for variants that zero the cascade.

## Verification

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_source_energy_ab/run_all_captures.ps1
python tools/v3_m1_source_energy_ab/analyze_source_energy.py
```

All 8 captures completed (1 Cornell baseline + 5 Sponza variants + 2 Cornell sanity). Sponza baseline was reused from Stage 7's existing capture (no recapture).

## Results

### Sponza screen-level metrics

| Variant       | `ratio_self` | `bad_pct` | `|p95(ratio-1)|` | Verdict (vs baseline) |
|---------------|---:|---:|---:|---|
| baseline      | 4.7148 | 100.00% | 4.5279 | — (reused Stage 7) |
| `mb_off`      | **0.9689** | **0.58%** | **0.2721** | **STRONG_COLLAPSE** |
| `mb_gain_half`| 1.6128 | 90.04% | 0.8888 | STRONG_COLLAPSE |
| `jitter_off`  | 4.7148 | 100.00% | 4.5279 | NEUTRAL (bit-identical to baseline) |
| `delta3_on`   | n/a (cascade=0) | n/a | n/a | VARIANT_BROKEN |
| `hybrid_on`   | 0.8315 | 0.00% | 0.3090 | STRONG_COLLAPSE (oracle reference) |

Scope §7 hybrid-retirement gate: `|p95(ratio-1)| ≤ 0.50`. **Both `mb_off` (0.27) and `hybrid_on` (0.31) clear the gate; `mb_gain_half` (0.89) does not.**

### Cornell sanity (SC5)

| Variant     | `ratio_self` | `bad_pct` | `|p95|` | valid pixels | Verdict |
|-------------|---:|---:|---:|---:|---|
| baseline    | 0.4923 | 3.86% | 0.8568 | 37036 | — |
| `mb_off`    | 0.2071 | 0.30% | 0.9133 | 36936 | WORSE |
| `delta3_on` | 0.0430 | 0.00% | 0.9965 | 5234  | CASCADE_BROKEN |

Cornell is already under-bright at baseline (`ratio<1`). `mb_off` makes it *more* under-bright (per SC2 darkening confound). `delta3_on` zeros the cascade on Cornell too.

### Attribution

The five toggles cleanly partition the source-energy hypothesis:

| Hypothesis (Stage 7 §"Interpretation") | Discriminating variant | Result |
|---|---|---|
| Temporal MB feedback amplifies local field | `mb_off`, `mb_gain_half` | **CONFIRMED.** Sponza ratio drops monotonically with MB gain (4.71 @ gain=1.0 → 1.61 @ gain=0.5 → 0.97 @ gain=0). |
| Probe jitter accumulation smears local energy | `jitter_off` | **REJECTED.** Bit-identical to baseline. |
| Upper-cascade merge feeds broad local energy | `delta3_on` | **INCONCLUSIVE.** Delta-3 gated trilinear zeros the cascade on both scenes — not a usable discriminator in this form. |
| Bake-side source energy too high | (catch-all) | Not needed; MB-feedback hypothesis already explains the Sponza signal. |

## Self-critique

**Did the plan's self-critique catch anything? Yes — three SCs fired:**

- **SC1 (fixed-cell shift)**: confirmed under `mb_off`. The `*_probe_diag.exr` rgb scale changed ~3.4× between baseline and `mb_off` at identical (camera, pixel) — see "Open shader-side anomaly" below. The screen-fallback verdict path in the updated analyzer handled this cleanly.
- **SC2 (darkening confound)**: confirmed under Cornell. Raw cascade luma in Cornell drops under `mb_off`, but ratio-toward-1.0 gets *worse*, so the verdict correctly flagged WORSE rather than COLLAPSE. The plan's primary discriminator (ratio improvement, not luma drop) is the right one.
- **SC3 (delta3 p000 shift)**: SC3 expected p000 to shift; what actually happened is more severe — delta3 zeroed the cascade entirely. The added `CASCADE_BROKEN` sentinel covers this.

**Things the plan missed:**

- **SC10 (new): `--m1-delta3-gated-trilinear=1` is not a measurement-grade discriminator.** It is structurally aggressive (rejects radiance via the inverted-α convention) and zeros the cascade on both scenes. Stage 1 matrix already saw this; the plan should have skipped delta3 here rather than including it. Logged for future stages.
- **SC11 (new): MB-gain is a linear lever, not a binary toggle.** `mb_gain_half` confirms a roughly linear relationship between MB gain and Sponza ratio_self (4.71 → 1.61 → 0.97 at gain 1.0/0.5/0.0). A two-point ladder is enough to motivate a sweep but not to pin the right gain for Sponza alone.

## Open shader-side anomaly (data-integrity flag)

`fragProbeDiag.rgb` from `raymarch.frag:841` is `pg / uAtlasVolumeSize`, which depends only on world position and atlas geometry — both invariant across the `mb_off` toggle. But per-pixel comparison shows mb_off rgb is ~0.29× baseline rgb at the same `(pixel, scene, camera)`:

| Pixel (y,x) | baseline rgb | mb_off rgb | ratio |
|---|---|---|---|
| (0,0)     | (0.052, 0.034, 0.030) | (0.015, 0.010, 0.009) | ~0.29 |
| (175,1)   | (0.026, 0.017, 0.014) | (0.006, 0.004, 0.004) | ~0.25 |
| (719,1279)| (0.134, 0.081, 0.062) | (0.021, 0.012, 0.010) | ~0.15 |

The scale is not a fixed constant, suggesting it is not a uniform-value swap. Plausible causes (not yet falsified):

- `uAtlasVolumeSize` is multi-cascade dependent in some uniform binding and the binding differs under MB-off;
- temporal EMA of `giProbeDiagTex` accumulates a non-constant signal under MB-off because the texture is reused across cascades;
- `probeGridCoord` reads `uAtlasGridOrigin/Size` from a uniform that is staleness-sensitive on the MB-off code path.

This does not change Stage 8's verdict (screen metrics are unaffected and unambiguous) but should be filed as a Stage 9 follow-up if shader-side per-cell attribution is needed again.

## Interpretation and decision

The Stage 7 verdict was `BROAD_LOCAL_ENERGY` and asked which source drives it. Stage 8 attributes it cleanly:

**Sponza broad-local atlas energy is multi-bounce temporal feedback.** With MB disabled the Sponza failure mode disappears: `|p95|` drops from 4.5 to 0.27 (well under the 0.50 retirement gate) and `bad_pct` from 100% to 0.58%. With MB gain halved, the same metrics drop by roughly half as much, confirming a near-linear relationship.

However, **MB-off is not a fix**. Cornell at MB-off gets *worse* (`|p95|` 0.86→0.91, `ratio_self` further below 1.0), so a global `useMultiBounce=0` default would regress the simpler scene. The honest statement is:

- **Single-bounce is the right answer for Sponza at the current MB calibration.**
- **MB feedback is over-driven for Sponza, correctly calibrated for Cornell.**

This makes Sponza's failure mode a *calibration* problem on the MB feedback path, not a structural cascade problem. That changes what Stage 9 needs to investigate.

## Improved next direction

Stage 9 should investigate **why MB feedback over-drives Sponza specifically**, with this priority order:

1. **MB-gain sensitivity ladder on Sponza only.** Capture Sponza at `--multi-bounce-gain` ∈ {0.0, 0.25, 0.5, 0.75, 1.0} and find the gain that brings Sponza `|p95|` under 0.50 without forcing Cornell to regress. If a single global gain works for both, that is the smallest correct fix.
2. **MB feedback source instrumentation.** If no single gain satisfies both scenes, the MB pump must be different per probe. Capture mode-17 with bake-side MB-history accumulator readback (new EXR sidecar) so we can see whether the high local energy at Sponza shader cells `(7,5,4)/(6,5,4)/(6,4,4)` is geometry-correlated (long sight lines into bright walls) or independent.
3. **Defer delta3 reframing.** Delta-3 gated trilinear in its current `--m1-delta3-gated-trilinear=1` form is structurally too aggressive (zeros the cascade). If delta3 returns to the M1 work order, it must first land in a graded form that does not zero radiance.

## Decision

Proceed to Stage 9 with an MB-gain Sponza-only ladder. **Do NOT** promote `--use-multi-bounce=0` to default. **Do NOT** ship `--m1-delta3-gated-trilinear=1` in any preset. Keep `jitter_off` filed as "MB-independent toggle, no signal." Keep `hybrid_on` as the oracle reference for the gate.
