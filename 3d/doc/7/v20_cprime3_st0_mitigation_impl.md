# v2.0 (h.c)''' ST=0 mitigation validation across scenes — soft generalization

**Status:** Composite verdict `MIXED_HOLD_DEFAULT_ST1` per pre-committed
band, BUT both signals (cascade/PT ratio AND RMSE vs PT-GI) move in the
ST=0 direction on BOTH scenes — the ratio gain just falls under the
+5pt absolute threshold the analyzer used. Interpretation: the (h.c)'
alcove finding (ST=0 raises cam-ratio) directionally generalizes to
simpler Cornell scenes, but the magnitude is smaller (≈+4pt vs the
+12-21pt alcove deltas). Recommendation: do NOT flip the global default,
but surface `--use-spatial-trilinear=0` as an opt-in quality flag with
the soft generalization documented.
**Date:** 2026-05-24
**Predecessors:** [v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md),
[v20_cprime2_downstream_knobs_impl.md](v20_cprime2_downstream_knobs_impl.md)

## 1. Hypothesis & test design

(h.c)' on `cornell-orig-alcove` found `--use-spatial-trilinear=0`
improves cascade/PT ratio at cam0 (+21.2%) and cam2 (+8.7%) but widens
the cam2/cam0 spread (symmetrizer pattern). (h.c)'' confirmed every
downstream knob exhibits the same symmetrizer pattern, locking-in
asymmetry source to bake-side per-direction-bin atlas content.

**Mitigation question** (independent of asymmetry diagnostics): given
ST=0 raises both cams' absolute ratios on alcove, does this absolute
quality gain generalize to scenes WITHOUT alcove geometry? If yes,
flip the default; if no, hold default and surface as opt-in flag.

**Test:** Capture default `cornell` and `cornell-orig` scenes (no alcove
geometry, auto-fit camera) at ST=0 vs ST=1, MB-OFF b=2 M0. 4 cells
total. Compute per-scene `delta_ratio = ratio_st0 - ratio_st1` AND
`delta_rmse = rmse_st0 - rmse_st1` — both should agree on direction for
a clean verdict.

**Pre-committed verdict bands:**
- Two-signal: `ratio_better = delta_ratio >= +0.05`, `rmse_better = delta_rmse <= -0.001`
- `ST0_IMPROVES_QUALITY`: both better
- `ST_NEUTRAL`: neither better, ratio delta < 0.05
- `ST1_BETTER`: ratio delta <= -0.05 OR rmse delta > +0.005
- `ST_MIXED`: ratio better but RMSE worse, or vice versa, or signs disagree

Composite: `RECOMMEND_FLIP_DEFAULT_TO_ST0` only if BOTH scenes
`ST0_IMPROVES_QUALITY`; otherwise hold default.

## 2. Results

### Per-cell ratios + RMSE

| scene | ST | integPT | integCasc | casc/PT | rmse | n_valid |
|---|---:|--------:|----------:|--------:|-----:|--------:|
| cornell_default | 1 | 22151.96 | 6661.77 | 0.3007 | 0.1262 | 162336 |
| cornell_default | 0 | 22151.96 | 7578.94 | **0.3421** | **0.1211** | 162336 |
| cornell_orig    | 1 | 13731.56 | 3780.93 | 0.2753 | 0.0845 | 163566 |
| cornell_orig    | 0 | 13731.56 | 4319.31 | **0.3146** | **0.0821** | 163566 |

### Per-scene ST=0 vs ST=1 delta

| scene | ratio_st1 | ratio_st0 | Δratio | rmse_st1 | rmse_st0 | Δrmse | verdict |
|---|--------:|--------:|--------:|--------:|--------:|--------:|---|
| cornell_default | 0.3007 | 0.3421 | **+0.0414** | 0.1262 | 0.1211 | **−0.0051** | `ST_MIXED` |
| cornell_orig    | 0.2753 | 0.3146 | **+0.0392** | 0.0845 | 0.0821 | **−0.0024** | `ST_MIXED` |

**Composite:** `MIXED_HOLD_DEFAULT_ST1` per pre-committed band.

## 3. Re-interpretation — soft generalization is real

The `ST_MIXED` label is mechanically correct per the band, but
misleading. Both scenes show:

- **Ratio** moves toward 1.0 (better) by +13.8% (cornell_default) and
  +14.2% (cornell_orig) in relative terms
- **RMSE** moves down (better) by 4.0% and 2.8% in relative terms
- **Both signals agree on direction** on BOTH scenes

The ST_MIXED verdict triggered ONLY because the ratio delta is just
under the +5pt absolute threshold the analyzer used. With a tighter
threshold (e.g., +3pt) BOTH scenes would land `ST0_IMPROVES_QUALITY`.

This is a soft generalization of the alcove finding: ST=0 mildly
improves integration quality on simpler scenes too, but the magnitude
shrinks (+12-21pt on alcove → +4pt on Cornell-default). The alcove
scene has more spatial probe-content variance, which amplifies the
spatial-trilinear penalty.

### What this means for the mitigation flag

- **Hold default at ST=1**: yes — the magnitude on simple scenes is too
  small to justify a default flip, and ST=1 still acts as a symmetrizer
  on the alcove scene (closes cam2/cam0 spread by 0.067).
- **Surface ST=0 as opt-in quality flag**: yes — for scenes with high
  spatial probe-content variance (alcoves, occluded corners), ST=0 can
  deliver double-digit ratio improvements. The CLI flag is already
  wired; document it.
- **Do NOT promote as automatic**: no auto-detection heuristic for "when
  is this scene alcove-like enough to benefit." The soft generalization
  means small scenes get a 2-5pt RMSE improvement but lose a
  symmetrizer; net is roughly a wash without per-scene profiling.

## 4. Cumulative state — v2.0 hypothesis tree at h-stage close

| stage | conclusion | hypothesis ruled out / locked in |
|---|---|---|
| (h.b)   | `BLEND_ZONE_NOT_THE_BUG` | smoothstep S-curve compression |
| (h.c)   | `CAM2_PROBE_COVERAGE_NEUTRAL` (mean) | probe-cell boundary aliasing at the mean |
| (h.c)'  | `SPATIAL_TRILINEAR_WIDENS_SPREAD` (symmetrizer) | spatial weighting chain as asymmetry source |
| (h.c)'' | `DOWNSTREAM_PATH_LOCKED_IN_INNOCENT` | useDirectionalMerge + useDirBilinear |
| (h.c)''' | `MIXED_HOLD_DEFAULT_ST1` (soft gen) | mitigation-flag flip across scenes |

**Bake-side framing now locked-in by elimination across 4 independent
downstream diagnostics.** Next direct test: P2 per-pixel dominant-bin
viz shader to localize WHICH atlas direction bins cam0 vs cam2 sample
dominantly.

## 5. Self-critique

**Strengths:**
- Two-signal verdict (ratio + RMSE) caught the soft-generalization
  pattern that a single-signal band would have classified as either
  `NEUTRAL` (ratio under threshold) or `ST0_IMPROVES_QUALITY` (RMSE
  improved). The four-way MIXED/IMPROVE/NEUTRAL/WORSE matrix correctly
  surfaced "both signals agree but magnitude is small."
- Cheap-first: 4 cells, 0.8 min capture, <1 min analyzer. Reused all
  existing scene assets and shaders.
- Auto-fit camera removes the cam0/cam2 spread confound from the
  measurement — this test is about ABSOLUTE quality on simple scenes,
  not about cross-cam asymmetry, and the auto-fit eliminates that axis
  cleanly.

**Weaknesses:**
- **Pre-committed +5pt ratio threshold was too coarse** for soft-gen
  detection. The relative ratio improvement is +14% on both scenes
  (substantial in relative terms), but the absolute delta is +0.04
  because the baseline is only 0.30. Should have set the threshold in
  RELATIVE terms (e.g., +10% relative) instead of absolute pt. Future
  tests with low-baseline ratios should pre-commit relative-magnitude
  bands. DNR candidate.
- **Only 2 scenes tested.** A third scene (e.g., cornell-orig-alcove
  itself with auto-fit camera) would have anchored the alcove vs
  non-alcove comparison directly. Skipped for cheap-first, but if the
  user re-opens this question, that's the next data point.
- **No per-camera coverage on these scenes.** Tested at auto-fit only.
  If a future user complains about ST=0 quality regression on a
  specific viewpoint, we lack the per-cam data to confirm or deny.
- **The "soft generalization" finding is interpretively interesting but
  has no immediate code action.** The mitigation flag stays opt-in, the
  default stays at ST=1, the bake-side P2 work proceeds as planned.
  This phase produced a documentation deliverable, not a code change.
- **Did not test ST=0 on a scene that's DEFINITELY alcove-free** (e.g.,
  pure open box). Both Cornell scenes have corner geometry that
  partially mimics alcove probe-occlusion patterns. A worst-case
  bias-toward-ST=0 test would use a featureless sphere room.

## 6. What this resolves about the broader v2.0 effort

Originally the v2.0 motivation centered on the cam0/cam2 cascade-vs-PT
spread on the alcove scene. The four-stage falsification chain
((h.b)+(h.c)+(h.c)'+(h.c)'') established that the spread is bake-side
sourced, not downstream-amplified. This phase (h.c)''' resolves the
adjacent question of whether the spatial-trilinear knob discovered
during (h.c)' has broader value — answer: weak yes, document as opt-in,
do not flip default.

The h-stage is now closed. Three concrete artifacts remain:
1. **P2 per-pixel dominant-bin viz** — direct bake-side measurement
2. **Opt-in `--use-spatial-trilinear=0` flag documentation** — minor
3. **Defer cam0/cam2 spread fix** until P2 localizes the bin asymmetry

## 7. Recommended next step

P2 (per-pixel dominant-direction-bin viz shader, ~1h). The bake-side
framing is now 4-way locked-in by elimination; P2 converts that
inferential lock-in to direct measurement by surfacing WHICH bins cam0
and cam2 sample dominantly. Once P2 confirms a specific bin-range
asymmetry, targeted bake-side fixes (e.g., bin-coverage hardening,
direction-aware probe placement) become well-scoped.

## 8. Artifacts

- Capture script: [tools/v20_arch_diagnostic/h8_st0_mitigation_capture.ps1](../../tools/v20_arch_diagnostic/h8_st0_mitigation_capture.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h8_st0_mitigation.py](../../tools/v20_arch_diagnostic/analyze_h8_st0_mitigation.py)
- Captures: `tools/v20_arch_diagnostic/captures_h8_st0_mitigation/` (4 PNGs + 12 EXRs)
- Results JSON: `tools/v20_arch_diagnostic/h8_st0_mitigation_results.json`
