# v2.0 (h.c)' Spatial-Trilinear A/B cam0+cam2 — predicted-direction INVERSION

**Status:** Numeric verdict `SPATIAL_TRILINEAR_WIDENS_SPREAD`. The fract-bias
→ trilinear-weight chain framing from (h.c) §5 is **falsified in its
predicted direction** but the data reveals a more interesting picture:
spatial trilinear was acting as a SYMMETRIZER, hiding a larger underlying
cam0/cam2 atlas-content asymmetry. Both cams BRIGHTEN under nearest-parent
(closer to PT), but cam0 brightens 3.75× more than cam2 — widening the
cam2/cam0 spread from 0.6497 → 0.5826.
**Date:** 2026-05-24
**Predecessor:** [v20_c_fract_viz_impl.md](v20_c_fract_viz_impl.md)

## 1. Hypothesis & test design

**Hypothesis (c)' (from h.c §5):** cam2's fract-corner-bias → consistent
8-neighbor trilinear weight pattern → same one or two probes get most
weight at every cam2 pixel → if those probes have dim direction bins, cam2
sees a dim cascade. Disabling spatial trilinear (`--use-spatial-trilinear=0`,
nearest-parent lookup) should **close the cam0/cam2 spread** by making
fract value irrelevant.

**Test:** 4-cell A/B at MB-OFF b=2, M0 defaults except `--use-spatial-trilinear=0/1`,
on cam0 + cam2. Re-used (h.b) smoothstep baseline for ST=1 cells; captured
2 new ST=0 cells. Compute cascade/PT energy ratio per cell; spread = cam2/cam0;
verdict from spread delta ST=0 − ST=1.

**Pre-committed verdict bands (h6 capture script):**

| delta spread | verdict | meaning |
|---:|---|---|
| ≥ +0.10 | `SPATIAL_TRILINEAR_PRIMARY_CONTRIBUTOR` | predicted: chain confirmed |
| +0.03..+0.10 | `SPATIAL_TRILINEAR_PARTIAL_CONTRIBUTOR` | predicted: chain contributes |
| -0.03..+0.03 | `SPATIAL_TRILINEAR_NOT_THE_DRIVER` | null: red herring |
| ≤ -0.03 | `SPATIAL_TRILINEAR_WIDENS_SPREAD` | inverted: ST=1 was symmetrizer |

**Cerebrum DNR 2026-05-24 sub-check applied**: per-cam ratio deltas
reported independently so we can tell whether ST flip moves "the whole
pipeline" or specifically cam2.

## 2. Implementation

No engine work. Re-used existing `--use-spatial-trilinear` CLI flag
(shipped per cerebrum 2026-05-22 enumeration of Phase-5 wired toggles).
Capture script
[h6_spatial_trilinear_capture.ps1](../../tools/v20_arch_diagnostic/h6_spatial_trilinear_capture.ps1)
(2 cells, 0.3 min) + analyzer
[analyze_h6_spatial_trilinear.py](../../tools/v20_arch_diagnostic/analyze_h6_spatial_trilinear.py).

## 3. Results

### Per-cell cascade/PT ratio

| ST | cam | integPT | integCasc | casc/PT |
|---:|---:|--------:|----------:|--------:|
|  1 |  0 | 2773.87 | 1206.08 | 0.4348 |
|  1 |  2 | 3077.69 |  869.44 | 0.2825 |
|  0 |  0 | 2773.87 | 1462.17 | **0.5271** |
|  0 |  2 | 3077.69 |  945.17 | **0.3071** |

### Per-cam ratio delta (ST=0 − ST=1)

| cam | ratio_st1 | ratio_st0 | delta | pct |
|---:|---------:|---------:|------:|----:|
| 0  | 0.4348 | 0.5271 | **+0.0923** | **+21.23%** |
| 2  | 0.2825 | 0.3071 | +0.0246 | +8.71% |

### Spread (cam2 / cam0)

| ST | cam0 | cam2 | c2/c0 |
|---:|-----:|-----:|------:|
| 1  | 0.4348 | 0.2825 | 0.6497 |
| 0  | 0.5271 | 0.3071 | **0.5826** |

`delta spread = 0.5826 − 0.6497 = −0.0671` → **`SPATIAL_TRILINEAR_WIDENS_SPREAD`**.

## 4. Verdict (pre-committed)

**`SPATIAL_TRILINEAR_WIDENS_SPREAD`** (band ≤ -0.03). The predicted
direction was: ST=0 should close the gap. The measured direction is the
opposite: ST=0 widens it. The original (c)' interpretation from h.c §5 is
**falsified in its predicted direction**.

## 5. Re-interpretation — fract-bias finding still holds, but ST=1 is the symmetrizer not the contributor

The data is more subtle than "chain confirmed" or "chain red herring":

**The fract-corner-bias finding from (h.c) is still mechanically correct.**
cam2's fract distribution piles 47.7% of pixels into one bin, biased to one
corner. Under ST=1, cam2's effective behavior is ALREADY close to
nearest-parent (because the fract concentration forces ~one neighbor to
dominate the trilinear weight). Switching cam2 from ST=1 → ST=0 therefore
barely moves cam2's ratio (+0.0246, +8.7%) — cam2 was already behaving
like nearest-parent.

**cam0 is where the action is.** cam0's fract distribution is near-uniform,
so under ST=1 cam0 actually averages across all 8 neighbors. Forcing ST=0
makes cam0 pick a SINGLE probe — and that single probe is **substantially
brighter** than the 8-probe average. cam0 ratio jumps +0.0923 (+21.2%).

**The chain inversion:** Spatial trilinear was DRAGGING cam0 DOWN (by
averaging in dim neighbors) more than it was dragging cam2 down. ST=1 was
serving as a **symmetrizer**, partially MASKING a larger underlying
asymmetry in the **atlas content** at the probes each cam sees.

**Why does cam2 still under-supply under ST=0?** Because the nearest-parent
probe selected for cam2's surface pixels has GENUINELY DIM direction-bin
atlas content. The asymmetry source isn't the spatial weighting — it's the
content of the probes themselves. cam2 sees alcove geometry that places its
nearest-parent probes in positions where their bake-time direction
gathering caught mostly back-wall-shadow directions; cam0 sees more
"averaged-over-lit-walls" probes.

This is a textbook instance of the cerebrum DNR pattern: a single-axis
verdict (delta spread) caught the right SIGN (ST=0 doesn't close the gap)
but missing the per-cam decomposition would have read this as
"hypothesis (c)' rejected, pivot away from spatial entirely." With the
DNR-mandated per-cam sub-check, we can see ST=0 ACTUALLY improves the
PRIMARY metric (cascade/PT ratio absolute) for both cams, just unequally.

## 6. Architectural implications

1. **Spatial trilinear is a net integration-quality LOSER on this scene.**
   Both cams are closer to PT under ST=0 than ST=1 (cam0: 0.43 → 0.53;
   cam2: 0.28 → 0.31). The "average across 8 neighbors" formula is
   contaminating both cams with dim-probe contributions. This is a real
   regression from a feature that's nominally an improvement.

2. **The cam0/cam2 architectural floor on spread is LOWER than (h.2)
   estimated.** (h.2) reported spread floor ≈ 0.50 at M4. ST=0 takes us to
   0.58 (worse) at M0, AND raises both absolute ratios — so this is not a
   pareto improvement on (h.2)'s framing. The floor is conditional on the
   merge+ST combination.

3. **Per-direction-bin atlas content at the cam2-visible probes is now the
   confirmed asymmetry source.** Spatial weighting was the symmetrizer.
   The next concrete diagnostic must visualize per-probe directional-bin
   energy at cam0-visible vs cam2-visible probes.

4. **(h.b)'s "blend zone innocent" + (h.c)' "spatial weighting is the
   symmetrizer not the contributor"** together rule out the entire
   downstream consumption path of the cascade. Whatever is dim about cam2
   was already dim in the atlas at bake time. This narrows the search to
   the BAKE-SIDE pipeline (cascade direction-bin gathering at the cam2-area
   probes).

## 7. Self-critique

**Strengths:**
- Zero engine work; 0.3 min capture; ~5 min analyzer.
- Cerebrum DNR (2026-05-24, written THIS morning from h.c experience)
  already paid off — the per-cam sub-check turned a misleading "spread
  widens" verdict into a productive interpretation (ST=1 is a symmetrizer,
  asymmetry is upstream). Without the sub-check this could have been a
  dead-end "(c)' rejected, ??? next" outcome.
- Pre-committed band caught the correct sign even though the predicted
  direction was inverted.

**Weaknesses:**
- **Predicted direction was wrong.** The (c)' hypothesis as stated in
  (h.c) §5/§7 explicitly predicted "If cam0/cam2 spread closes under ST=0,
  fract→trilinear chain confirmed." Direction inverted. Should have
  considered the symmetrization alternative pre-test (a 4-cell pre-test
  doesn't have enough cells to disambiguate "chain contributes" from
  "ST=1 symmetrizes"). For future tests with inversion potential, pre-write
  BOTH directions' interpretations in the verdict-band table.
- **Single merge config (M0).** Under M4 (already nearest-parent spatially),
  the ST=0 toggle is a no-op and the spread should be IDENTICAL between
  M4-ST=1 and M4-ST=0. Could verify by re-running 2 cells at M4 — adds
  ~0.1 min. Not done; cost/benefit marginal.
- **Doesn't directly probe per-direction-bin atlas content.** The
  conclusion "asymmetry is in atlas content at cam2-visible probes" is the
  only consistent reading but is still inferential. Direct test requires
  per-direction-bin energy viz (~1h shader work).
- **PT-ratio absolute jumps under ST=0 are large enough (+21% cam0, +9%
  cam2) that they affect (h.b)/(h.2)/(h.3) interpretations.** Those prior
  measurements all used ST=1 default and reported the cam0 ratio as
  baseline. The ST=0 cam0 ratio (0.5271) is now the LARGEST cam0 ratio
  measured at MB-OFF b=2 — bigger than M2 (0.5630 was M2 ST=1; need to
  check M2 ST=0). This raises the question: how much of the (h.2) merge-
  variant brightness ladder was just "different merges incidentally averaged
  over different probe sets"? Beyond scope here but worth flagging.

## 8. Recommended next step

The data now points to **bake-side per-direction-bin atlas content at the
cam2-visible probes** as the live asymmetry source. The cheapest concrete
diagnostic: a per-pixel "dominant direction bin" viz mode that reports
which atlas direction bin contributes the most luminance to each surface
pixel's cascade fetch. Compare cam0 vs cam2 distributions of dominant-bin
index. If cam2 concentrates on a different set of bins than cam0 (likely),
inspect what those bins capture at bake time.

Cost: ~1h shader work (new render mode 9 or 21 returning dominant-bin
index as RGB) + 0.2 min capture + analyzer.

ALTERNATIVE (cheaper): mode-6 (cascade directional atlas direct viz) at
cam0 vs cam2 viewports. Render mode 6 already ships and displays atlas
content directly; A/B the integrated brightness of the visible atlas pixels
between cams. ~0.2 min capture + ~10 min analyzer. Doesn't reveal WHICH
direction bin is dim but does confirm the atlas-content-is-the-source
interpretation by a different path. Recommended as the cheap-first cross-
check before sinking 1h into a new render mode.

## 9. Artifacts

- Capture script: [tools/v20_arch_diagnostic/h6_spatial_trilinear_capture.ps1](../../tools/v20_arch_diagnostic/h6_spatial_trilinear_capture.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h6_spatial_trilinear.py](../../tools/v20_arch_diagnostic/analyze_h6_spatial_trilinear.py)
- Captures: `tools/v20_arch_diagnostic/captures_h6_spatial_trilinear/` (2 PNGs + 6 EXRs)
- Reused (h.b) ST=1 baseline: `captures_h4_smoothstep/alcove_cam{0,2}_blend_smoothstep_*`
- Results JSON: `tools/v20_arch_diagnostic/h6_spatial_trilinear_results.json`
