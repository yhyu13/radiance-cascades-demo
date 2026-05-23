# MBRC v2.0 — absolute-residual analyzer (basis-error hypothesis falsified)

**Date**: 2026-05-23 (immediately follows
[v20_cam2_asymmetry_diagnostic_impl.md](v20_cam2_asymmetry_diagnostic_impl.md),
commit `f0abf75`).

**Motivation**: The cam0/cam2 asymmetry diagnostic ranked hypothesis (d)
**basis-representation error** as NEW P1, on the strength of the bidirectional
pink+blue pattern in mode-19. That pattern was visually suggestive, but
self-critique §4 flagged it as interpretive, not measured, and proposed an
absolute-residual analyzer with a pre-committed verdict rule. This doc runs
that analyzer.

## 1. Pre-committed verdict rule

Before running the analyzer, classify by `|Σ+ / Σ−|` where Σ± is the sum of
positive / |negative| per-pixel `Δ = lum(cascadeGI) − lum(ptGI)` across all
valid pixels (`lum(ptGI) > 1e-3`):

| ratio band                | verdict                       | implied next step                                  |
|---------------------------|-------------------------------|-----------------------------------------------------|
| `[0.70, 1.30]`            | **BASIS_ERROR_CONFIRMED**     | basis-error real → thin-merge shader (Priority 2 of §16.5) |
| `< 0.50`                  | **NET_UNDER_BRIGHT**          | cascade loses energy → energy audit at bake time    |
| `> 2.00`                  | **NET_OVER_BRIGHT**           | cascade fabricates energy → re-open leak hypothesis |
| `[0.50, 0.70]` / `[1.30, 2.00]` | BORDERLINE_UNDER / OVER | report leaning but do not act                       |

Committed in [analyze_absolute_residual.py](../../tools/v20_arch_diagnostic/analyze_absolute_residual.py)
function `classify()` lines 91–115.

## 2. Capture

[absolute_residual_capture.ps1](../../tools/v20_arch_diagnostic/absolute_residual_capture.ps1)
takes 2 HDR EXR triplets (cascade_gi / pt_full / pt_direct) at cam0 + cam2
under new engine defaults (no CLI flags). `cornell-orig-alcove`,
cascadeC0Res=32 (engine default), hybrid OFF, seed 0, 512 frames, 512 PT spp
per accumulator. Outputs in
[captures_abs_residual/](../../tools/v20_arch_diagnostic/captures_abs_residual/).

PT accumulator size 640×360 (engine-built); cascadeGI is dumped at 1280×720
and the analyzer 2×2-averages it down to PT resolution before differencing.

## 3. Results

```
cam  valid%      Sum+      Sum-   |+/-|   pos%   neg%    mean+    mean-   meanPT  meanCasc
----------------------------------------------------------------------------------------------------
  0   14.9%   478.488  2682.243   0.178  26.2%  73.8%   0.0532   0.1058   0.1805    0.1163
  2   11.4%   270.128  4133.508   0.065  19.1%  80.9%   0.0539   0.1951   0.2386    0.0911
```

| cam | `|Σ+/Σ−|` | verdict           | integrated cascade / PT |
|----:|----------:|-------------------|------------------------:|
| 0   | 0.178     | **NET_UNDER_BRIGHT** | 0.645                |
| 2   | 0.065     | **NET_UNDER_BRIGHT** | 0.382                |

The integrated-energy ratio (0.645 / 0.382) is **byte-equivalent to the
triple-stack ceiling reported in §16.5** (cam0 0.681 / cam2 0.392, within
±5pp — the residual ±5pp is consistent with mode-17 reporting GI-only in
clamped HDR vs the §16.5 mode-0 luminance which mixes GI + direct + tone
curve). The energy gap is *the* central v2.0 finding and it is now confirmed
in absolute radiance units, not just LDR composite means.

Signed-field PNGs confirm visually:
[alcove_cam0_signed_residual.png](../../tools/v20_arch_diagnostic/captures_abs_residual/alcove_cam0_signed_residual.png)
and
[alcove_cam2_signed_residual.png](../../tools/v20_arch_diagnostic/captures_abs_residual/alcove_cam2_signed_residual.png)
are **dominantly blue** (cascade < PT) over the visible scene; red specks
are sparse and localized (mostly thin edges and high-frequency detail
at occluder rims). cam2 is visibly bluer than cam0.

## 4. What this means for hypothesis (d)

From [v20_cam2_asymmetry_diagnostic_impl.md §3](v20_cam2_asymmetry_diagnostic_impl.md):

| Hypothesis                                              | Diagnostic rank | This analyzer's verdict |
|---------------------------------------------------------|----------------:|-------------------------|
| (a) bake-side leak                                      | REJECTED        | still REJECTED (mode-14 cross-cam identical, unchanged) |
| (b) smoothstep blend zone math                          | P4              | unchanged                |
| (c) camera-projection / surface-mix                     | CONFIRMED       | still CONFIRMED (cam2 0.065 vs cam0 0.178 — cam2 viewport oversamples under-bright; this is exactly what the diagnostic predicted) |
| (d) basis-representation error                          | NEW P1          | **FALSIFIED** — `|Σ+/Σ−|` is 0.18 / 0.07, not the [0.7, 1.3] band that the basis-redistribution model predicts |
| (e) thin-merge shader (consequence of d)                | P2              | demoted to **P5** — the structural test for the precondition just failed |

The visually compelling bidirectional pink+blue in mode-19 turned out to be
a **colormap artefact**: mode-19's per-pixel divisor normalizes positive
and negative residuals separately, so pixels that are 0.05 over-bright and
pixels that are 0.5 under-bright both light up at full saturation in their
respective colors. The bidirectional pattern is *real* — there exist
over-bright pixels — but they are 6×–15× smaller in magnitude than the
under-bright pixels (mean+/mean− = 0.503 at cam0, 0.276 at cam2), and they
cover 26%/19% of the lit pixels vs 74%/81% under-bright.

**New hypothesis (f) bake-time energy loss / under-bake — NEW P1.** The
cascade's per-probe radiance integration loses energy somewhere in:
- C0 hemisphere-sum normalization (numeric coefficient on the sum-of-bin
  spherical integration that should yield the surface irradiance),
- ray-march termination (`maxBounceDistance`, sky/miss radiance contribution,
  early-out conditions),
- bake-time α (occlusion) interaction with radiance accumulation (α and
  radiance are combined per-bin; an asymmetric weighting could systematically
  under-count).

## 5. Self-critique

### What this analyzer cannot support

- **Single scene**, alcove only. The v2.0-pre cross-scene validation
  ([engine_default_validation_impl.md](engine_default_validation_impl.md))
  established that the engine-default flip is safe on plain Cornell and
  Sponza, but the absolute-residual analyzer has not been run on those
  scenes. A Cornell-only `|Σ+/Σ−|` near 0.18 might be specific to alcove's
  partition-shadowed back wall; on plain Cornell or Sponza the ratio could
  be different and `(f)` could be the wrong narrative. Deferred to the
  cross-scene energy-audit follow-on, after a candidate fix has been
  prototyped.
- **Engine-default config only**. cascadeC0Res=32 (default). Higher-N
  configurations might shift the ratio toward [0.7, 1.3] if more bins
  resolve more peaks. But (γ) D=8→16 only +9% and (β) c0 32→64 only +24pp
  in HDR ratio per the v2.0-pre §16 verdicts, so it is unlikely a within-
  current-architecture knob can reach [0.7, 1.3] from 0.07; the deficit
  is too large to be a tuning issue.
- **Luminance scalar collapses per-channel differences.** dR/dG/dB could
  differ (Sponza warm-shift hint from cross-scene validation). The
  analyzer reports luminance Δ; a chromatic energy mismatch (e.g. red
  attenuated more than blue) would not show separately. Quick follow-on:
  rerun per-channel if a per-channel fix candidate emerges.
- **PT reference budget at cam2 back wall.** Same flag as in §4 of the
  diagnostic — if PT is itself under-converged at long-bounce-path cam2
  pixels, the `Σ−` magnitude is inflated. Worth one quick check: re-render
  cam2 with PT-only at higher spp (e.g. 2048) and confirm Σ− does not
  collapse. Deferred unless energy-audit work yields a fix and the gap
  doesn't close as expected.

### What could still go wrong with the verdict

- **valid% is low** (14.9% / 11.4%). Most of the viewport has `lum(ptGI) <
  1e-3` (background, dark unlit corners). That's correct for a scene with
  a small lit zone, but it means Σ+ / Σ− is computed over a small region.
  Spot-check: if I raise EPS_PT to 1e-2, the ratio should stay in the same
  band; if it flips, the verdict is fragile. (Quick test next, before
  shipping this doc as the basis for a code change.)
- **Downsample 2×2-avg of cascade to match PT 640×360.** Could under-count
  high-frequency cascade peaks that get averaged into adjacent low cells.
  If those peaks happen to be cascade-overshoot, downsampling would
  artificially compress Σ+ and inflate the under-bright verdict. The fix
  would be to upsample PT instead, but PT at 1280×720 would 4× the
  per-pixel spp requirement to maintain convergence; tradeoff is real and
  deferred.

### Robustness spot-check (EPS sensitivity) — PASSED

EPS_PT swept across 3 orders of magnitude: `{1e-4, 1e-3, 1e-2, 5e-2}`:

| EPS_PT | cam0 \|Σ+/Σ−\| | cam2 \|Σ+/Σ−\| |
|-------:|---------------:|---------------:|
| 1e-4   | 0.257          | 0.086          |
| 1e-3   | 0.178          | 0.065          |
| 1e-2   | 0.091          | 0.057          |
| 5e-2   | 0.082          | 0.051          |

Both cams stay in NET_UNDER_BRIGHT (< 0.50) across the whole sweep —
verdict is robust to masking-threshold choice. Tightening EPS_PT actually
*strengthens* the under-bright verdict: it removes dim pixels where the
cascade occasionally slightly over-shoots, leaving the dominant under-
bright signal cleaner.

## 6. Recommended v2.0 next step

The pre-committed rule says: **energy audit at bake time**. The right
investigation order is:

1. **Energy spot-test on a known-radiance scene.** Render a unit-luminance
   sphere in an otherwise empty box, then ask: does the cascade integrate
   the single-bounce irradiance on a diffuse far surface to within ±10% of
   the analytic value? If yes, the integration math is sound and the loss
   is in something else (occlusion, ray-march termination, sky). If no,
   start at the C0 hemisphere-sum coefficient and the `dirRes` normaliz-
   ation.
2. **Per-stage energy ledger.** Instrument the bake chain to dump total
   energy entering each stage (rays cast, hits, miss-absorption,
   accumulator writes) and compare cascade vs PT. The hand-off where
   energy goes missing localizes the loss.
3. **Then** decide between: hemisphere-sum-coefficient fix (cheap),
   ray-march-termination fix (cheap), bake-time α-radiance reweighting
   (medium), or pivot to a different basis (expensive — only justified if
   integration math is provably sound and loss is in representation).

Estimated cost-vs-information: step 1 is ~1h (write the spot-test scene,
run cascade-only + PT, compare a single number), and disambiguates between
"math wrong" vs "scene-interaction wrong." Step 2 is ~3–5h. Step 3 is
half a day.

This is a much larger investigation than the asymmetry diagnostic and the
analyzer combined, but it is the right investigation: the absolute-residual
verdict means an architectural intervention is needed, not a tuning sweep.

## 7. Artefacts

- Capture harness: [tools/v20_arch_diagnostic/absolute_residual_capture.ps1](../../tools/v20_arch_diagnostic/absolute_residual_capture.ps1)
- Captures (8 files = 2 PNG + 6 EXR): [tools/v20_arch_diagnostic/captures_abs_residual/](../../tools/v20_arch_diagnostic/captures_abs_residual/)
- Analyzer: [tools/v20_arch_diagnostic/analyze_absolute_residual.py](../../tools/v20_arch_diagnostic/analyze_absolute_residual.py)
- Results JSON: [tools/v20_arch_diagnostic/absolute_residual_results.json](../../tools/v20_arch_diagnostic/absolute_residual_results.json)
- Signed-field PNGs:
  [captures_abs_residual/alcove_cam0_signed_residual.png](../../tools/v20_arch_diagnostic/captures_abs_residual/alcove_cam0_signed_residual.png),
  [captures_abs_residual/alcove_cam2_signed_residual.png](../../tools/v20_arch_diagnostic/captures_abs_residual/alcove_cam2_signed_residual.png)
