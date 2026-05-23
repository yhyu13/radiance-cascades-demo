# MBRC v2.0 — (h.2) merge-variant asymmetry sweep at MB-OFF b=2

**Date**: 2026-05-23 (immediately follows
[v20_h_source_disambig_impl.md](v20_h_source_disambig_impl.md), commit
`54b2196`).

**Motivation**: (h) disambig isolated a 2× cam0/cam2 spread in the
single-bounce merge at MB-OFF b=2 (cam0=0.67 / cam2=0.33). The
disambig doc §6 recommended a 3-min sweep across merge variants
(M0/M2/M4) to test whether one variant closes, preserves, or widens
the spread — and whether the alpha_m4_deepdive engine-default flip to
M4 (measured at MB-ON only) was a brightness-vs-symmetry trade.

## 1. The experiment

[h2_merge_asymmetry_capture.ps1](../../tools/v20_arch_diagnostic/h2_merge_asymmetry_capture.ps1)
captured 4 new cells (M0 + M2, each on cam0+cam2) at MB-OFF b=2.
M4 reused from `captures_h_disambig/`. 4 captures, 0.7 min.

Merge configs:
- **M0_baseline**: `dm=1 db=1 st=1` (engine PRE-v2.0-pre default; all directional features ON)
- **M2_iso_merge**: `dm=0 db=1 st=1` (no directional merge; hardware bilinear ON)
- **M4_iso_nearest**: `dm=0 db=0 st=1` (engine NEW default; nearest-fetch isotropic)

## 2. Results

| merge | cam0 ratio | cam2 ratio | cam2/cam0 spread |
|-------|-----------:|-----------:|------------------:|
| M0_baseline    | 0.4348 | 0.2825 | **0.6497** |
| M2_iso_merge   | 0.5630 | 0.3079 | 0.5470 |
| M4_iso_nearest | **0.6686** | 0.3327 | **0.4976** |

cascade/PT energy ratio at MB-OFF b=2 across merge variants.

### 2.1. The brightness–symmetry trade is monotonic

Moving M0 → M2 → M4:
- **cam0 brightens monotonically**: 0.43 → 0.56 → 0.67 (+54% from M0 to M4).
- **cam2 brightens marginally**: 0.28 → 0.31 → 0.33 (+18%).
- **Spread WIDENS monotonically**: 0.65 → 0.55 → 0.50.

M4 maximizes cam0 brightness at the cost of asymmetry. M0 minimizes
the spread but at the cost of overall dimness. M2 is the middle point.

### 2.2. Pre-committed verdict → MIXED (rule bands too tight)

The verdict-classifier expected one of three outcomes:
- All in [0.45, 0.55] → MERGE_NOT_THE_SOURCE (spread invariant).
- One in [0.80, 1.20], others [0.40, 0.55] → MERGE_VARIANT_SYMMETRIZES.
- All < 0.6 with one brightening cam0 → MERGE_BRIGHTENS_BUT_PRESERVES_SPREAD.

Actual spreads {0.65, 0.55, 0.50} straddle the [0.45, 0.55] band
boundary (M0 = 0.65 is above; M2 = 0.55 is at the boundary; M4 = 0.50
is at the lower bound). Verdict landed MIXED but the spirit is clear:
**MERGE_BRIGHTENS_BUT_PRESERVES_SPREAD** is the correct interpretation
once the lower bound is relaxed from 0.45 to 0.40 — all three
variants are below the symmetry threshold (< 0.70), and the differences
between variants are dominated by cam0 movement, not cam2.

## 3. Architectural implication

The 2× cam0/cam2 asymmetry is **fundamental to the merge architecture**,
not specific to any merge variant. None of M0/M2/M4 brings cascade
above 0.33 on cam2 at single-bounce. Moving merge variants is a
brightness lever for cam0 but barely touches cam2.

**The alpha_m4_deepdive engine-default flip to M4 was a brightness
trade, not a symmetry win.** Measured at MB-ON only, M4 maximized cam0
brightness and the +16.8% super-additive M4×MB interaction; the present
sweep at MB-OFF reveals M4 also has the *worst* cam0/cam2 spread of
the three variants. The flip is not wrong (cam0 brightness gain is
real and survives MB-ON), but the engine-default narrative should
acknowledge: M4 maximizes cam0 brightness at MB-ON; the cam0/cam2
asymmetry is monotonically worse moving from M0 to M4; cam2 barely
improves (+18% absolute at MB-OFF; the cam0/cam2 spread on the
default-flip scene was always architectural, not tunable via merge
selection).

The cam2 stuckness at ~0.33 across all 3 merge variants is the
sharpest "merge is not the bottleneck for cam2" signal we've measured.
For cam2 progress, the next investigations must target *upstream of
the merge*:

1. **(b) smoothstep blend-zone math** at [radiance_3d.comp:771](../../res/shaders/radiance_3d.comp#L771).
   The blend factor `l` is applied identically across merge variants;
   it can't be diagnosed by switching merge formulas. Needs a new
   shader `uUseSmoothstepBlend` uniform (~30 min eng work).
2. **(c) probe-grid coverage of cam2 viewport.** cam2 sees the alcove
   geometry; if those surfaces fall at probe-cell *boundaries* where
   ALL merge variants under-weight them, the asymmetry is in probe
   placement, not the merge formula. Diagnostic: render mode that
   visualizes per-pixel probe-cell `fract()` coordinate on cam0 vs
   cam2 viewports — render mode 8 already does this. A capture
   compare would tell us whether cam2 oversamples probe boundaries.
3. **Atlas content asymmetry.** Maybe the atlas at cam2-visible probe
   locations contains less radiance than at cam0-visible probes — the
   bake-time light injection / RC bake itself is dimmer in the alcove
   region. Could be measured by rendering atlas-direct (mode 6
   directional atlas) at both cams and comparing integrated luminance.

## 4. Updated hypothesis space

| Hypothesis | Pre-h.2 sweep | Post-h.2 sweep |
|------------|---------------|-----------------|
| (a) bake-side leak | REJECTED | unchanged |
| (b) smoothstep blend zone | P3 | **promoted to P1** — explicit next target, needs ~30 min shader work |
| (c) camera-projection / surface-mix | P2 | **promoted to P1 (parallel)** — cheap diagnostic via existing mode 8 (probe-cell `fract()` viz); can run before any code work |
| (d) basis-representation error | FALSIFIED | unchanged |
| (e) thin-merge shader | P5 | unchanged |
| (f) bake-time energy loss | FALSIFIED | unchanged |
| (g) multi-bounce delivery gap | P3 | unchanged (MB compensates) |
| (h.1) MB feedback amplifier | confirmed-stacked | unchanged |
| (h.2) first-bounce merge asymmetry | NEW P1 | **REFINED**: the asymmetry is invariant across merge variants → merge formula is NOT the asymmetry source. The asymmetry exists upstream of merge (atlas content / probe placement / smoothstep blend). **Demoted to P3** as a tuning lever; (b) and (c) take over as the architectural-source candidates. |

## 5. Recommended next step

**Run mode-8 probe-cell `fract()` viz on cam0 vs cam2 viewports** (zero
engine work, ~1 min capture, ~5 min visual A/B). Render mode 8 already
exists at [raymarch.frag](../../res/shaders/raymarch.frag). If cam2's
viewport visibly oversamples probe-cell boundaries (where the
smoothstep blend zone math applies), (b) and (c) are the same
hypothesis and the next step is the smoothstep toggle. If cam0 and
cam2 look uniform in probe-cell fract coverage, the asymmetry source
is in the atlas content itself (bake-side), and we need atlas-direct
mode-6 comparison to localize it.

Cost: 1 min capture (cam0+cam2 at mode 8) + visual A/B. Disambiguates
(b)+(c) source candidates before any shader work.

## 6. Self-critique

- **Pre-committed band bounds were too tight.** [0.45, 0.55] band for
  MERGE_NOT_THE_SOURCE missed M0 = 0.65, which is qualitatively in the
  "spread invariant" class but quantitatively above the band. Future
  classifier-rule bands should be derived from prior measurement
  variance (e.g. from the bounce-ladder spread of 0.30–0.65 across
  bounce counts), not chosen ahead of any data.
- **The sweep didn't include extreme variants.** M3 (`dm=1 db=1 st=0`,
  no spatial trilinear) and M5 (hypothetical `dm=0 db=0 st=0`,
  fully nearest in both space and direction) would have provided more
  data points on the brightness-vs-symmetry curve. The 3 captured
  variants suggest a monotonic trade; testing M3/M5 would confirm
  whether it's actually monotonic vs U-shaped.
- **PT_b=2 reference variance.** Per ladder §7.2, PT_b=2 at 512 spp
  may have hard-pixel noise that biases low-magnitude ratios.
  cam2 ratios at 0.28–0.33 are in the low-magnitude regime; the +18%
  range across M0→M4 (0.28→0.33) could be partially noise. Worth
  re-running cam2 cells at 1024 spp before drawing the "cam2 is
  un-tunable via merge" conclusion as load-bearing.
- **MB-ON sweep across the same merge variants is missing.** The
  alpha_m4_deepdive data has M0+MBon and M4+MBon at b=8; it does NOT
  have M2+MBon at b=2 or the apples-to-apples MB-OFF vs MB-ON
  factorial at b=2 across 3 variants. A 12-capture sweep (M0/M2/M4 ×
  MB-OFF/MB-ON × cam0/cam2 at b=2, ~3 min) would let us measure
  whether MB's brightness multiplier varies across merge variants —
  which would tell us whether the M4+MB super-additivity is a
  merge-specific phenomenon or a general property of M0/M2/M4.

## 7. Artefacts

- Capture script: [tools/v20_arch_diagnostic/h2_merge_asymmetry_capture.ps1](../../tools/v20_arch_diagnostic/h2_merge_asymmetry_capture.ps1)
- New captures (8 files): [tools/v20_arch_diagnostic/captures_h2_merge/](../../tools/v20_arch_diagnostic/captures_h2_merge/)
- Reused M4 captures: [tools/v20_arch_diagnostic/captures_h_disambig/alcove_cam{0,2}_b2_mboff_m17_*](../../tools/v20_arch_diagnostic/captures_h_disambig/)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h2_merge_asymmetry.py](../../tools/v20_arch_diagnostic/analyze_h2_merge_asymmetry.py)
- Results JSON: [tools/v20_arch_diagnostic/h2_merge_asymmetry_results.json](../../tools/v20_arch_diagnostic/h2_merge_asymmetry_results.json)
