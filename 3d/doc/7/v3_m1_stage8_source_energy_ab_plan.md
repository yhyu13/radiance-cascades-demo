# M1 Stage 8 Plan - Source-Energy A/B at Sponza Dominant Shader Cells

**Date:** 2026-05-27.
**Predecessor:** [v3_m1_stage7_shader_contrib_impl.md](v3_m1_stage7_shader_contrib_impl.md) (verdict `BROAD_LOCAL_ENERGY`).
**Goal:** decide which energy source is responsible for the broad-local atlas energy at Sponza shader-side cells `(7,5,4)`, `(6,5,4)`, `(6,4,4)` — multi-bounce feedback, probe-jitter accumulation, or upper-cascade merge — by toggling each suspect independently and re-measuring at those exact cells.

## Why this stage exists

Stage 7 ruled out hot-bin and hot-probe explanations. The remaining honest options are:

1. Temporal multi-bounce gain (`--use-multi-bounce`, `--multi-bounce-gain`) pumps the local atlas.
2. Probe-jitter accumulation (`--use-probe-jitter`) spatially smears bake energy into adjacent probes.
3. Upper-cascade merge feeds broad energy from C1+ into C0 (gated by Delta #3 — `--m1-delta3-gated-trilinear`).
4. None of the above — bake-side source energy is too high for those probes regardless of consume path.

This stage discriminates 1–3 with the existing CLI toggles. Outcome 4 is the rejection signal (no variant collapses raw luma at the dominant cells).

## Plan

### 1. Variants

All Sponza, cam 0, N=2048, render-mode 17, hybrid OFF (matches Stage 7). Single toggle change per variant against baseline so attribution stays clean.

| Tag | Change vs baseline | Hypothesis under test |
|---|---|---|
| `baseline` | unchanged Stage 7 config | reference |
| `mb_off` | `--use-multi-bounce=0` | MB feedback is the source |
| `mb_gain_half` | `--multi-bounce-gain=0.5` | MB linear-gain attribution |
| `jitter_off` | `--use-probe-jitter=0` | jitter-accumulation source |
| `delta3_on` | `--m1-delta3-gated-trilinear=1` | upper-cascade merge bleed |
| `hybrid_on` | `--use-hybrid=1` | oracle reference (what retirement target looks like) |

`hybrid_on` is not an A/B variant; it is the upper-bound oracle (what we are trying to match).

### 2. Capture

Reuse the existing Stage 7 EXR/JSON sidecar surface. Each variant produces a complete sidecar set so the existing Stage 7 analyzer code path can be reused.

Per variant outputs into `tools/v3_m1_source_energy_ab/captures_sponza_<tag>/`:

- `<tag>.png`
- `<tag>_cascade_gi.exr`
- `<tag>_pt_full.exr`
- `<tag>_pt_direct.exr`
- `<tag>_gbuffer.exr`
- `<tag>_probe_diag.exr`
- `<tag>_probe_contrib.exr`
- `<tag>_probe_bin.exr`
- `<tag>_probe_stats.json`

### 3. Analyze

`tools/v3_m1_source_energy_ab/analyze_source_energy.py` reuses `tools/v3_m1_local_sampling/analyze_local.py` for EXR reads and Stage 7's `valid` + `bad` mask logic.

Two views, both pivoted on the same shader-side cells:

**(a) Fixed-cell view (primary).** Take the baseline's top 3 dominant shader cells `(7,5,4)`, `(6,5,4)`, `(6,4,4)`. For each variant, restrict to pixels whose `probe_diag` p000 maps to one of those three cells, and report:

- count of valid pixels at the cell;
- mean cascade-GI luma;
- mean PT-GI luma (`pt_full - pt_direct`, lower-clamped);
- mean ratio `cascade/PT`;
- delta vs baseline for cascade luma and ratio.

**(b) Screen view (context).** For each variant, report:

- `ratio_self = mean(cascade_GI / PT_GI)` on valid pixels;
- `bad_pct = % valid pixels with ratio > 1.3`;
- `|p95|` of `ratio - 1` (the locked hybrid-retirement criterion from scope §7).

### 4. Decision rule

Each variant is judged on **fixed-cell raw cascade luma** vs baseline:

- `STRONG_COLLAPSE` if mean cascade luma at all three cells drops by ≥40% vs baseline.
- `PARTIAL_COLLAPSE` if mean cascade luma at all three cells drops by 15–40%.
- `NEUTRAL` if mean cascade luma changes by <15% in either direction.
- `WORSE` if mean cascade luma rises by ≥15%.

Attribution rule for the source of broad local energy:

- `mb_off` STRONG → temporal MB feedback is the dominant source. Next: instrument MB feedback gain at the cell and trim.
- `jitter_off` STRONG → jitter accumulation is the dominant source. Next: bound jitter offset for Sponza.
- `delta3_on` STRONG → upper-cascade merge is the dominant source. Next: promote Delta #3 (per-corner gated trilinear).
- All variants NEUTRAL → no consume-time toggle reduces local energy. Conclusion: bake-side source energy is the cause; next stage should instrument bake-time radiance at the implicated probes (not consume side).

Tie-break when more than one variant is STRONG: pick the variant whose **screen** `bad_pct` and `|p95|` also drop the most, since dominant-cell collapse with no screen improvement could be a topology shift (different cells become dominant).

### 5. Comparison against oracle

`hybrid_on` provides the upper bound at the same cells. Any variant whose fixed-cell cascade luma overshoots `hybrid_on` is over-correcting (must not be promoted as a fix). Any whose value matches or is between baseline and `hybrid_on` is a candidate.

## Self-critique and improvements

### SC1 — Fixed cells may not be dominant under every variant

A variant that re-distributes energy spatially (most likely `delta3_on`) could move dominance off `(7,5,4)`, `(6,5,4)`, `(6,4,4)` so the per-cell readout looks artificially good while another cell now over-bright. **Improvement:** also report each variant's own top-3 dominant cells beside the baseline-fixed cells, and flag any variant whose own top-cell cascade luma exceeds baseline's max top-cell cascade luma. Verdict tie-break in §4 already uses screen `bad_pct` for this — make that mandatory, not optional.

### SC2 — Single-bounce darkening confound

Disabling MB makes the entire image darker by construction (the indirect bounce is the second light). A "collapse" at the cell could just be the global darkening, not a targeted fix. **Improvement:** primary readout is `ratio = cascade / PT` at the cell, not raw cascade luma. A correct fix should drop the ratio toward 1.0; pure darkening drops both numerator and PT denominator proportionally and the ratio barely moves.

### SC3 — Variants are not independent in pipeline

`--m1-delta3-gated-trilinear=1` lands an inverted-α path; if Delta #3 also alters which probes are sampled, the per-pixel `probe_diag` p000 itself shifts. **Improvement:** acknowledge this in the impl doc, treat `delta3_on` per-cell numbers as "advisory" not authoritative, and lean on the screen-level `ratio_self` and `bad_pct` for the verdict when p000 shifts >5% of bad pixels off the baseline cells.

### SC4 — Capture cost

6 variants × Sponza N=2048 ≈ ~3 min per variant on the current hardware (Stage 7 capture took similar). Total ≈ 18–20 min — acceptable, no parallelization needed.

### SC5 — Cornell as a sanity control

Cornell has been NEUTRAL on every Stage 0–7 variant after #1+#2 landed. Adding Cornell here would only verify that Sponza variant deltas don't break Cornell. **Improvement:** run only `mb_off` and `delta3_on` on Cornell as a sanity gate; skip the others to save capture time. If Cornell `ratio_self` regresses by >5% under either, surface as a blocker.

### SC6 — `--multi-bounce-gain=0.5` is a continuous lever, not a discrete toggle

If `mb_off` STRONG and `mb_gain_half` PARTIAL, the gain lever is monotonic and the next stage is a gain sweep — that is a useful finding but not in the current verdict table. **Improvement:** record the implied gain ladder result in the impl doc as a follow-up, without trying to fit it into the §4 verdict bands.

### SC7 — Hybrid as oracle may bias interpretation

Hybrid uses the correction pass; over-fitting to match hybrid at the cell would just be reintroducing hybrid. **Improvement:** only use `hybrid_on` to bound "over-correction"; do NOT use it as the success target. Success target stays PT_GI.

### SC8 — N=2048 may not be enough for the smaller variants

If `--use-multi-bounce=0` removes a slow-converging term, the variant might converge faster, and using the same N may bias the comparison. **Improvement:** N=2048 is already the locked sign-off N per scope §7 and is well past the convergence elbow for both MB-on and MB-off in prior Phase 8 measurements; keeping N=2048 fixed and not lowering it for any variant keeps the comparison apples-to-apples.

### SC9 — The shader struct field rename (`sample` → `ps`) from Stage 7

Stage 7 hit a GLSL reserved-word collision. Stage 8 doesn't touch shaders, only flags. **Improvement:** note in the impl doc that no shader changes are required so that build risk is zero.

## Acceptance

- 6 capture scripts run cleanly (5 Sponza + 1 Cornell sanity per SC5: `mb_off`, `mb_gain_half`, `jitter_off`, `delta3_on`, `hybrid_on`, `cornell_mb_off`, `cornell_delta3_on` — actually 5 Sponza variants + baseline reuse + 2 Cornell sanity = 7 captures total; baseline reuses Stage 7's existing Sponza capture).
- `tools/v3_m1_source_energy_ab/source_energy_results.json` records per-variant per-cell metrics and the §4 verdict per variant.
- Impl doc records the attribution verdict and the chosen next stage.

## Out of scope

- Bake-side source-energy instrumentation. That is the followup if all consume-time variants are NEUTRAL.
- A multi-bounce gain sweep. Recorded as a follow-up only if `mb_off` STRONG and `mb_gain_half` PARTIAL.
- Promoting any of the variants to default. This stage only attributes; promotion is a separate decision that needs screen-level `|p95|` to clear the scope §7 retirement band.
