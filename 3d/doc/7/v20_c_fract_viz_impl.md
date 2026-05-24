# v2.0 (h.c) Probe-Cell fract(pg) Viz — cam0 vs cam2 distribution A/B

**Status:** Numeric verdict `CAM2_PROBE_COVERAGE_NEUTRAL`; shape-of-distribution
asymmetry observed and promoted to a new focusing hypothesis. Hypothesis (c)
"probe-cell boundary aliasing" **REJECTED**, but cam2 has a fract-corner-bias
distinct from cam0 that suggests **directional-bin sampling correlation** as
the live next-step.
**Date:** 2026-05-24
**Predecessor:** [v20_b_smoothstep_toggle_impl.md](v20_b_smoothstep_toggle_impl.md)

## 1. Hypothesis & test design

**Hypothesis (c):** cam2 systematically samples more probe-cell boundaries
than cam0; the trilinear weight aliasing this causes contributes to cam2's
under-supplied cascade/PT ratio.

**Test:** Capture render mode 8 (per-pixel `fract(probeGridCoord)` RGB) at
the same engine config used in (h.b) — M0 defaults, MB OFF, b=2 — on cam0
and cam2. Compute per-pixel
`d = max(|fract.x - 0.5|, |fract.y - 0.5|, |fract.z - 0.5|)` (distance from
probe-cell center, clamped in [0, 0.5]); compare mean and distribution shape
across cams.

Surface mask: sky pixels are pure black because mode 8 returns early
at `if (uRenderMode == 8)` only after the SDF surface check; sky rays leave
fragColor at the default clear color. Filter `R+G+B > 10/255` to isolate
surface-hit pixels.

## 2. Implementation

Mode 8 already exists in the shipped engine at
[raymarch.frag:652-658](../../res/shaders/raymarch.frag#L652). No engine
work needed. Capture script
[h5_fract_capture.ps1](../../tools/v20_arch_diagnostic/h5_fract_capture.ps1)
(2 cells, 0.1 min) + analyzer
[analyze_h5_fract.py](../../tools/v20_arch_diagnostic/analyze_h5_fract.py).

## 3. Results

### Per-cam summary (M0 defaults, MB OFF, b=2)
| cam | shape     | surf% | mean_d | p25 | p50 | p75 | p95 |
|----:|-----------|------:|-------:|----:|----:|----:|----:|
|   0 | 720×1280  | 18.62% | 0.3475 | 0.2765 | 0.3745 | 0.4333 | 0.4882 |
|   2 | 720×1280  | 30.95% | 0.3778 | 0.3745 | 0.4020 | 0.4176 | 0.4843 |

`delta mean_d = cam2 - cam0 = +0.0303` → inside [−0.05, +0.05]
NEUTRAL band.

### Mean fract per axis (RGB = probe-grid X/Y/Z)
| cam | mean_fx | mean_fy | mean_fz | distance from (0.5,0.5,0.5) |
|----:|--------:|--------:|--------:|----------------------------:|
|   0 | 0.5308 | 0.4951 | 0.5048 | 0.0308 |
|   2 | 0.4510 | 0.3811 | 0.3935 | **0.1189** |

cam0 averages essentially at cell center (0.03 distance, well within one
fract step). cam2 averages **3.9× farther from cell center** and biased
specifically toward LOW fract on Y and Z axes — i.e., a specific corner
of the probe cell.

### Histogram of d=max(|fract - 0.5|) (% of surface pixels)
| bin range       | cam0%  | cam2%  | ratio cam2/cam0 |
|-----------------|-------:|-------:|----------------:|
| [0.000, 0.050)  | 0.00   | 0.00   | —      |
| [0.050, 0.100)  | 1.01   | 0.48   | 0.48× |
| [0.100, 0.150)  | 5.74   | 3.11   | 0.54× |
| [0.150, 0.200)  | 6.76   | 3.33   | 0.49× |
| [0.200, 0.250)  | 7.58   | 3.97   | 0.52× |
| [0.250, 0.300)  | 8.50   | 4.36   | 0.51× |
| [0.300, 0.350)  | 11.60  | 6.02   | 0.52× |
| [0.350, 0.400)  | 20.63  | 17.06  | 0.83× |
| **[0.400, 0.450)** | **19.56** | **47.73** | **2.44×** |
| [0.450, 0.500)  | 18.63  | 13.94  | 0.75× |

The cam2 distribution is bimodal-flat below 0.40 (~52% spread thinly
across 7 bins) and then **piles 47.73% of pixels into a single 0.05-wide
bin** at [0.40, 0.45). cam0 is much more uniform across [0.30, 0.50).

## 4. Verdict (pre-committed)

**`CAM2_PROBE_COVERAGE_NEUTRAL`** — delta mean_d = +0.0303 lands in the
[−0.05, +0.05] NEUTRAL band. Hypothesis (c) as framed (cam2 oversamples
**boundaries**, where d is large) is **rejected at the mean-distance level**.

**However**, the verdict band is a 1D summary that misses the
distribution-shape asymmetry that IS present:

1. cam2 mean-fract is 3.9× farther from cell-center than cam0's, biased to
   a specific corner (low Y + low Z).
2. cam2 piles 2.44× more pixels into [0.40, 0.45) than cam0 does.

This is another instance of the cerebrum DNR pattern: **single-axis verdict
bands miss two-dimensional signals.** The right next-step is informed by
the shape asymmetry, not the numeric verdict.

## 5. Architectural implication

The fract-corner-bias is geometrically meaningful: cam2's view sees probe
cells through a foreshortened projection that aligns most surface samples
to a similar fract value within each cell. This is consistent with cam2
being an elevated near-side view of the alcove — the alcove floor and
back wall present at consistent angles to the probe grid, so the fract
calculation `pos_in_atlas_grid - 0.5` converges on a similar partial
position relative to the probe lattice.

**Why this matters for cascade/PT under-supply**: the cascade lookup at a
surface pixel does:
1. spatial trilinear over 8 neighbor probes (weighted by `fract`)
2. per-direction-bin atlas fetch within each probe

If cam2 consistently picks the SAME fract-corner of the probe cell, the
trilinear weights are also consistent: the same one or two probes get most
weight at every cam2 surface pixel. If those weighted-heavily probes
happen to have systematically dim direction bins (because their probe
location relative to the alcove geometry happens to capture only
back-wall-shadow directions), cam2 sees a dim cascade.

cam0 by contrast has a near-uniform fract distribution → the trilinear
average integrates across all 8 neighbor probes per cell over the
viewport, washing out per-probe directional asymmetry.

**This is a focusing hypothesis for the live (c)' branch: per-direction-bin
sampling correlation, NOT spatial probe-cell boundary aliasing.**

## 6. Self-critique

**Strengths:**
- Zero engine work (mode 8 already shipped). 0.1 min capture, ~10 min
  analyzer + interpretation.
- Clean rejection of the original spatial-boundary hypothesis at the
  pre-committed band.
- Surface mask defensible (sky is pure black by construction; threshold
  trivial).

**Weaknesses:**
- **Pre-committed band caught the wrong feature.** Yet another instance
  of the cerebrum §216/§217 pattern: a 1D summary statistic missing a 2D
  distribution-shape signal. Should have included a shape-asymmetry sub-test
  (e.g., entropy of fract distribution, or KL-divergence between cam0 and
  cam2 fract distributions) in addition to mean-distance.
- **Resolution mismatch.** Both images at 720×1280 (good) but `surface_pct`
  differs (cam0 18.62%, cam2 30.95%) — cam2 has 66% more surface pixels.
  Doesn't bias the per-pixel mean_d, but does mean cam2's distribution has
  better statistics — if anything, makes the cam2 pile-up more reliable.
- **Doesn't probe the bin-level asymmetry directly.** The interpretation
  (cam2's fract bias → consistent trilinear weights → consistent
  upper-cascade-bin sampling → dim cam2 if those bins are dim) is plausible
  but not directly tested. Next step would need a mode that visualizes
  WHICH direction bin gets the most weight per pixel, then compare cam0 vs
  cam2 distributions of that "dominant bin index."
- **Only one merge config.** Tested at M0 (full trilinear + dir-bilinear).
  Under M4 (`spatial-trilinear=0` nearest-parent), the fract distribution
  wouldn't matter — only the single nearest-parent probe contributes.
  Could verify the "fract drives the asymmetry" interpretation by re-running
  the (h.b)-style cascade/PT ratio at M0 vs M4 vs SPATIAL-TRILINEAR-OFF only,
  and checking whether the cam0/cam2 spread closes under nearest-parent
  spatial lookup.

## 7. Recommended next step

The fract-corner-bias finding suggests a concrete (c)' sub-test:
**A/B `--use-spatial-trilinear=0` vs default at cam2**. If the cam0/cam2
spread closes under nearest-parent spatial lookup (which makes the fract
value irrelevant), then the fract-bias → trilinear-weight chain is
confirmed as a contributor. If it doesn't close, the asymmetry is in the
per-direction-bin sampling layer, not the spatial layer.

Cost: 4 cells (cam0+cam2 × spatial-trilinear=0/1) × 1 capture cycle
= ~0.7 min. Re-use the (h.b) infrastructure with `--use-spatial-trilinear=`
varied. Estimated ~15 min total.

After that, if still unresolved, design a per-pixel "dominant direction
bin" viz mode (~1h shader + 5 min capture).

## 8. Artifacts

- Capture script: [tools/v20_arch_diagnostic/h5_fract_capture.ps1](../../tools/v20_arch_diagnostic/h5_fract_capture.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h5_fract.py](../../tools/v20_arch_diagnostic/analyze_h5_fract.py)
- Captures: `tools/v20_arch_diagnostic/captures_h5_fract/` (2 PNGs)
- Results JSON: `tools/v20_arch_diagnostic/h5_fract_results.json`
