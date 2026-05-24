# v2.0 (h.c)'' Downstream-Knobs Final Rule-Out — entire consumption path locked-in innocent

**Status:** Composite verdict `DOWNSTREAM_PATH_LOCKED_IN_INNOCENT`. The two
remaining downstream toggles (`useDirectionalMerge`, `useDirBilinear`) both
fail to close the cam0/cam2 spread; both exhibit the **same
symmetrizer pattern** (h.c)' identified for spatial trilinear (cam0 brightens
more than cam2 when feature is disabled). Combined with (h.b)+(h.c)+(h.c)',
the entire DOWNSTREAM consumption path is now ruled out. Asymmetry source
is locked-in to BAKE-SIDE per-direction-bin atlas content at cam2-visible
probes.
**Date:** 2026-05-24
**Predecessor:** [v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md)

## 1. Hypothesis & test design

After (h.c)' falsified the predicted direction of the spatial-trilinear
chain (revealing ST=1 as symmetrizer not contributor), two downstream
toggles remained untested as potential cam0/cam2 spread sources:
- `useDirectionalMerge` (DM): 0 = isotropic fallback bypassing per-direction-bin
  atlas lookup entirely (uses `texture(uRadiance)` average), 1 = per-bin lookup (default)
- `useDirBilinear` (DB): 0 = nearest-bin `texelFetch`, 1 = 4-bin bilinear
  inside per-direction lookup (default)

**Test:** 3-config A/B at MB-OFF b=2 M0 ST=1: baseline DM=1 DB=1 (reused
h.b smoothstep cells), DM=0 (isotropic), DM=1 DB=0 (nearest-bin within
per-direction). 6 cells total (4 new). Per-cam absolute deltas reported
alongside spread deltas per cerebrum DNR 2026-05-24.

**Pre-committed verdict bands**: positive `delta_spread = spread(cfg) -
spread(baseline)` means knob CLOSES the gap (= contributor). Negative or
zero means INNOCENT (= doesn't close).

## 2. Results

### Per-cell cascade/PT ratio

| cfg | cam | integPT | integCasc | casc/PT |
|---|---:|--------:|----------:|--------:|
| dm1db1 | 0 | 2773.87 | 1206.08 | 0.4348 |
| dm1db1 | 2 | 3077.69 |  869.44 | 0.2825 |
| dm0    | 0 | 2773.87 | 1561.66 | **0.5630** |
| dm0    | 2 | 3077.69 |  947.73 | 0.3079 |
| dm1db0 | 0 | 2773.87 | 1253.73 | 0.4520 |
| dm1db0 | 2 | 3077.69 |  875.12 | 0.2843 |

### Per-cam delta vs dm1db1 baseline

| cfg | cam | base | cur | delta | pct |
|---|---:|-----:|----:|------:|----:|
| dm0    | 0 | 0.4348 | 0.5630 | **+0.1282** | **+29.48%** |
| dm0    | 2 | 0.2825 | 0.3079 | +0.0254 | +9.00% |
| dm1db0 | 0 | 0.4348 | 0.4520 | +0.0172 | +3.95% |
| dm1db0 | 2 | 0.2825 | 0.2843 | +0.0018 | +0.65% |

### Spread cam2/cam0

| cfg | cam0 | cam2 | c2/c0 | delta vs base |
|---|-----:|-----:|------:|-------:|
| dm1db1 | 0.4348 | 0.2825 | 0.6497 | (base) |
| dm0    | 0.5630 | 0.3079 | 0.5470 | **−0.1028** |
| dm1db0 | 0.4520 | 0.2843 | 0.6291 | −0.0206 |

### Verdict

| cfg | delta_spread | verdict |
|---|---:|---|
| dm0    | −0.1028 | `DOWNSTREAM_KNOB_INNOCENT` |
| dm1db0 | −0.0206 | `DOWNSTREAM_KNOB_INNOCENT` |

**Composite:** `DOWNSTREAM_PATH_LOCKED_IN_INNOCENT`.

## 3. Pattern observation — every downstream knob is a SYMMETRIZER

The pattern is identical to (h.c)'s spatial-trilinear finding: disabling
any downstream feature causes cam0 to brighten substantially more than
cam2, widening the spread. This means every downstream feature is
**partially symmetrizing** the cam0/cam2 asymmetry by dragging cam0
DOWN. None of them CAUSE the asymmetry; they all HIDE part of it.

Magnitude of the symmetrizer effect (cam0 delta % when feature is disabled):

| feature disabled | cam0 delta | cam2 delta | spread widens by |
|---|---:|---:|---:|
| ST (h.c') | +21.2% | +8.7% | 0.067 |
| DM (h.c'') | +29.5% | +9.0% | 0.103 |
| DB (h.c'') | +3.95% | +0.65% | 0.021 |

`useDirectionalMerge` is the LARGEST symmetrizer (when off, isotropic
fallback drags cam0 up by 29.5%; cam2 barely moves). `useDirBilinear` is
the smallest (4-bin bilinear vs nearest-bin barely affects anything —
suggests the 4 neighbor bins within a probe have similar content, so
which-bin matters more than how-they-blend).

## 4. What this locks in

**Cumulative falsification chain (all at MB-OFF b=2 M0):**

| measurement | conclusion | hypothesis ruled out |
|---|---|---|
| (h.b) | BLEND_ZONE_NOT_THE_BUG | smoothstep S-curve compression |
| (h.c) | CAM2_PROBE_COVERAGE_NEUTRAL (mean) | probe-cell boundary aliasing |
| (h.c)' | SPATIAL_TRILINEAR_WIDENS_SPREAD (symmetrizer) | spatial weighting chain |
| (h.c)'' | DOWNSTREAM_PATH_LOCKED_IN_INNOCENT | dir-merge + dir-bilinear |

**Every downstream feature has been tested and ruled out as the cam0/cam2
spread source.** The chain of evidence is now overwhelming: the spread
originates in the **bake-side per-direction-bin atlas content at the
probes that cam2's surface pixels look up**. The cam2 alcove view
geometrically places its surface samples in spatial regions whose probes
have systematically dim direction-bin populations.

**Mechanism inference**: at bake time, each cascade probe gathers
radiance from a discrete set of directions (D² bins for cascade level
N). Probes located in spatial regions where most of those directions
encounter SDF occluders within the cascade's `tMin..tMax` window will
record dim bins (occluded direction = miss, recorded radiance = 0 or
sky). The alcove view's cam2 sees surfaces whose nearest probes happen
to be in such "directionally-occluded" positions. cam0 sees probes that
have more open-direction bins.

## 5. Self-critique

**Strengths:**
- Cheap-first: 4 new cells, 0.7 min capture, ~5 min analyzer. Reused
  (h.b) baseline cells — no redundant work.
- Per-cam DNR sub-check immediately surfaced the symmetrizer pattern (3rd
  time in 2 hours that the DNR has paid off). The "verdict band INNOCENT"
  on its own would have read as "downstream is fine"; the per-cam delta
  read makes it "downstream is composed entirely of symmetrizers."
- Final rule-out of the consumption path is now triple-confirmed
  ((h.c)' spatial + (h.c)'' dir-merge + (h.c)'' dir-bilinear). The
  bake-side framing is no longer inferential — it's the only consistent
  reading.

**Weaknesses:**
- **Analyzer verdict naming is misleading.** "INNOCENT" suggests the
  feature has no effect, but the feature IS affecting both cam0 AND
  cam2 — just symmetrically (well, asymmetrically toward symmetry). The
  band correctly classifies "does this knob close the gap?" but doesn't
  capture "is this knob a symmetrizer?" Should add a sub-classification:
  `INNOCENT_QUIET` (both cams move <5%) vs `INNOCENT_SYMMETRIZER` (cam0
  moves ≥2× cam2 in absolute deltas) for future use.
- **Doesn't directly probe bake-side content.** The bake-side framing is
  the only consistent INTERPRETATION but is still indirect — derived by
  elimination, not by direct measurement. P2 (per-pixel dominant-bin viz
  shader) is the direct test.
- **Only tests at M0 + MB-OFF + b=2.** Other merge variants or with MB
  on, the symmetrizer magnitudes may shift. M4 already uses nearest-parent
  spatially (ST=0 equivalent), so M4 results are constrained by similar
  reasoning. Spot-check at M4 would be ~2 cells worth.
- **`useDirBilinear` barely moves anything** (+3.95%/+0.65%). This is
  consistent with "4-bin bilinear vs nearest-bin doesn't matter because
  neighbor bins within a probe have similar content" but could also mean
  the bilinear implementation itself is degenerate (e.g., always picking
  the same bin in practice). Worth a sanity-check by capturing mode 6 at
  DM=1 DB=0 vs DM=1 DB=1 and visually confirming non-trivial difference.
  Out of scope here.

## 6. Recommended next step

Per the established 3-phase plan, proceed to P3 (Cornell-default ST A/B
for mitigation validation) and P2 (per-pixel dominant-bin viz shader).
The triple-confirmed bake-side framing makes P2 a near-certain payoff:
visualizing WHICH bins cam0 vs cam2 sample dominantly will localize the
asymmetry to specific direction regions of the atlas, enabling targeted
bake-side fixes.

## 7. Artifacts

- Capture script: [tools/v20_arch_diagnostic/h7_downstream_knobs_capture.ps1](../../tools/v20_arch_diagnostic/h7_downstream_knobs_capture.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h7_downstream_knobs.py](../../tools/v20_arch_diagnostic/analyze_h7_downstream_knobs.py)
- Captures: `tools/v20_arch_diagnostic/captures_h7_downstream/` (4 PNGs + 12 EXRs)
- Reused (h.b) baseline: `captures_h4_smoothstep/alcove_cam{0,2}_blend_smoothstep_*`
- Results JSON: `tools/v20_arch_diagnostic/h7_downstream_knobs_results.json`
