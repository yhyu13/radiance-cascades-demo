# MBRC v2.0-pre HDR Re-Litigation of (α / β / γ) — Implementation & Results

**Date**: 2026-05-22 (PM, same-session follow-on to
`doc/7/hdr_exr_metric_impl.md`).

**Motivation**: The (δ) HDR replay proved that the LDR-PNG saturation-band
classifier (colormap divisor=0.2) was clamping real radiance signal — a
+0.9pp LDR Δ-area movement on cam2 corresponded to a 19% |p50| HDR movement
and a 58% ratio movement. Per [hdr_exr_metric_impl.md §4.1](hdr_exr_metric_impl.md#L191),
the prior three named-hypothesis REJECTions ((α) merge-mode, (β) MB-gain,
(γ) angular-bin) were all measured with the same saturated metric and must
be re-litigated against HDR before declaring the tree exhausted.

## 1. Harness ([tools/v20_pre_measurement/hdr_relitigate_sweep.ps1](../../tools/v20_pre_measurement/hdr_relitigate_sweep.ps1))

26 captures total (5.7 min, single noise seed):
- **(α)** 5 merge-mode arms × 2 cams = 10 captures
  - M0_baseline (dirMerge=1, dirBilinear=1, spatialTrilinear=1)
  - M1_no_bilin (dirMerge=1, dirBilinear=0, spatialTrilinear=1)
  - M2_iso_merge (dirMerge=0, dirBilinear=1, spatialTrilinear=1)
  - M3_no_spatialtri (dirMerge=1, dirBilinear=1, spatialTrilinear=0)
  - M4_iso_nearest (dirMerge=0, dirBilinear=0, spatialTrilinear=1)
- **(β)** 5 gains × 2 cams = 10 captures
  - g ∈ {0.5, 1.0, 1.5, 2.0, 3.0}, with `--use-multi-bounce=1`
  - Baseline arm = g=1.0 (engine default once MB is on)
- **(γ)** 3 uniform-D × 2 cams = 6 captures
  - D ∈ {4, 8, 16}, with `--cascade-scaled-dir-res=0`
  - Baseline arm = D=8 (matches LDR sweep anchor)

All captures: cornell-orig-alcove, hybrid OFF, mode 17 + `--screenshot-exr=1`,
seed 0, 512 frames, frames=512.

## 2. Pre-committed re-litigation rule

Per-arm, vs same-cam baseline. The rule is **deliberately strict** — if LDR
was hiding sub-tonemap signal, HDR must show >20% movement on at least one
of the two HDR metrics on at least one cam.

| max(|Δratio|%, |Δ|p50||%) on either cam | Verdict |
|----------------------------------------|---------|
| ≥ 20%                                  | HDR_LEVERAGE |
| 10% – 20%                              | HDR_MIXED    |
| ≤ 10% (both cams, both metrics)        | HDR_TIE      |

Where `ratio = meanCasc / meanPT` over valid pixels (PT GI ≥ 1e-3).

## 3. Headline numbers

```
arm                  cam   ratio    |p50|    |p95|   dim%  bright%
=== (alpha) baseline (LDR called: ALPHA_LEVERAGE_WRONG_DIR, mostly tie) ===
M0_baseline            0   0.201    0.839    7.053  78.2   14.3
M0_baseline            2   0.140    0.946    1.000  87.6    2.9
M1_no_bilin            0   0.208    0.828    5.540  78.0   14.2   TIE
M1_no_bilin            2   0.141    0.949    1.000  87.6    2.9   TIE
M2_iso_merge           0   0.260    0.781    8.190  78.5   15.3   LEVERAGE (+29%)
M2_iso_merge           2   0.153    0.938    1.000  87.2    3.3   LEVERAGE
M3_no_spatialtri       0   0.243    0.791    7.511  79.1   13.8   LEVERAGE (+21%)
M3_no_spatialtri       2   0.152    0.938    1.000  88.0    2.8   LEVERAGE
M4_iso_nearest         0   0.309    0.742    8.717  77.3   15.4   LEVERAGE (+53%)
M4_iso_nearest         2   0.165    0.929    1.000  86.6    3.4   LEVERAGE

=== (beta) baseline g=1.0 (LDR called: BETA_LEVERAGE_NOT_CURE) ===
g050                   0   0.276    0.763    9.682  75.5   15.0   LEVERAGE (-42% / +30%)
g050                   2   0.177    0.935    1.000  83.6    3.2
g100 (baseline MB ON)  0   0.474    0.586   17.132  56.8   15.9
g100                   2   0.288    0.901    1.000  74.2    3.7
g150                   0   2.112    1.014   86.946  18.3   50.7   LEVERAGE runaway
g150                   2   1.284    0.755    5.645  54.6   26.9
g200                   0  65.734   44.460 6669.730   9.9   89.9   catastrophic
g200                   2  40.506   13.548  231.563   8.1   91.8
g300                   0 107.537   85.689 13287.877  9.9   90.1   blow-up
g300                   2  58.126   19.380  308.383   8.1   91.8

=== (gamma) baseline D=8 (LDR called: GAMMA_REJECT) ===
d04                    0   0.161    0.868    6.673  82.2   13.9   LEVERAGE WRONG DIR (-20%)
d04                    2   0.104    0.947    1.000  92.6    3.0
d08 (baseline)         0   0.200    0.838    7.138  78.6   14.3
d08                    2   0.138    0.944    1.000  87.8    2.9
d16                    0   0.220    0.831    7.754  77.1   14.4   TIE (+9.5%)
d16                    2   0.150    0.945    1.000  86.3    3.0   TIE (+8.5%)
```

(Full JSON in [tools/v20_pre_measurement/hdr_relitigate_results.json](../../tools/v20_pre_measurement/hdr_relitigate_results.json).)

## 4. Per-hypothesis verdicts

### 4.1 (α) merge-mode — **LDR VERDICT REVERSED**

| arm                  | LDR verdict                          | HDR verdict        |
|----------------------|--------------------------------------|--------------------|
| M1_no_bilin          | minor; cam2 −0.8% Δ-area             | **HDR_TIE**        |
| M2_iso_merge         | cam2 +14.6% Δ-area (LEVERAGE_WRONG)  | **HDR_LEVERAGE** (ratio +29% cam0) |
| M3_no_spatialtri     | minor; cam2 ~unchanged               | **HDR_LEVERAGE** (ratio +21% cam0) |
| M4_iso_nearest       | cam2 +19.6% Δ-area (LEVERAGE_WRONG)  | **HDR_LEVERAGE** (ratio +53% cam0, +18% cam2) |

The LDR sweep concluded "merge weighting is doing useful work; OFF makes
things WORSE." HDR shows the opposite: **disabling the directional merge
brightens cascade significantly toward PT** (cam0 ratio 0.20 → 0.31 with
M4, a 53% improvement). The LDR colormap was saturating the cascade-dim
delta into a fixed band, so when the merge change shifted Δ-area into a
different saturation cell ("more red pixels above 0.2"), LDR read it as
"OFF made things worse." HDR's continuous ratio shows what actually
happened: the smart-merge features were attenuating cascade radiance.

**The new working hypothesis** (call it (ε), per [delta_probe_density_sweep_impl.md §6.C6](delta_probe_density_sweep_impl.md#L228)):
the directional-merge weighting and the spatial-trilinear blend are
*subtracting* cascade energy via per-direction-bin upper-cascade fetch
geometry — likely the same "WeightedSample-like" attenuation that v1.3
trace-side gating was designed to NOT do. Concrete next steps section 6.

### 4.2 (β) MB-gain — **LDR VERDICT CONFIRMED (LEVERAGE NOT CURE), magnitude larger**

| gain | cam0 ratio | cam2 ratio | LDR Δ-area (cam0/cam2) | HDR verdict |
|------|------------|------------|------------------------|-------------|
| 0.5  | 0.276      | 0.177      | (not LDR-tested)       | LEVERAGE −42% ratio |
| 1.0  | 0.474      | 0.288      | baseline               | (baseline) |
| 1.5  | 2.112      | 1.284      | +60%/+74% (LDR)        | LEVERAGE +346% ratio (overshoot) |
| 2.0  | 65.7       | 40.5       | +363%/+214% (LDR)      | LEVERAGE +13,770% (catastrophic) |
| 3.0  | 107.5      | 58.1       | (not LDR-tested)       | LEVERAGE +22,591% (runaway) |

HDR confirms the LDR finding: g>1.0 is catastrophic feedback runaway, not
a useful correction. **NEW HDR observation**: g=1.0 MB ON brings cam0 ratio
from 0.201 (MB OFF, from §4.1) → 0.474, a **+136% brightness movement** —
about 2× as bright. The original Phase-MB plan predicted "7-22% brightness
gain"; the measurement at the time reported "+3.5% at gain=1.0". The +3.5%
number was the LDR Δ-area movement; HDR shows the actual radiance-space
change was +136% — over an order of magnitude larger.

This means **MB feedback at g=1.0 closes about half the cascade-vs-PT
brightness gap** for cam0 (0.20→0.47 toward 1.00). It does not eliminate
the residual but it is the largest single tunable effect we have measured.

### 4.3 (γ) angular under-sampling — **LDR VERDICT CONFIRMED (REJECT)**

| D | cam0 ratio | cam2 ratio | LDR Δ-area (cam0/cam2) | HDR verdict |
|---|------------|------------|------------------------|-------------|
| 4 | 0.161      | 0.104      | (not LDR-tested)       | LEVERAGE WRONG DIR (−20%/−25%) |
| 8 | 0.200      | 0.138      | baseline               | (baseline) |
| 16| 0.220      | 0.150      | −1.0%/−1.4%            | HDR_TIE (+9.5%/+8.5%) |

D=4 makes cascade dimmer (as expected; less angular resolution = more
energy missed). D=16 produces only ~9% ratio improvement — borderline TIE
under the strict 10% rule. **(γ) remains rejected**, and the HDR ratio
moves *less* than (α) M4_iso_nearest (+53% cam0). Doubling angular
resolution buys substantially less than the merge-mode discovery.

## 5. Cross-hypothesis takeaway

Of the four pre-HDR REJECT/LEVERAGE_NOT_CURE verdicts:

| Hypothesis | LDR call          | HDR call                        | Status            |
|------------|-------------------|---------------------------------|-------------------|
| (γ)        | REJECT            | TIE (barely; +9% ratio)         | LDR_CONFIRMED     |
| (β)        | LEVERAGE_NOT_CURE | LEVERAGE_NOT_CURE (×100 larger) | LDR_CONFIRMED     |
| (α)        | LEVERAGE_WRONG_DIR| **LEVERAGE_RIGHT_DIR (+53%)**   | **LDR_REVERSED**  |
| (δ)        | REJECT            | LEVERAGE (cam2 −19% |p50|)      | **LDR_REVERSED**  |

**Two of four** LDR rejections were measurement artifacts. The
named-hypothesis tree is therefore **NOT** exhausted — (α) and (δ)
re-open with HDR leverage signal, and the (α) finding *points at a
specific architectural change* (disable or rework the directional-merge
weighting), which is a much more concrete path than the (β) "tuning won't
help" or (γ) "no leverage at all" outcomes.

## 6. Recommended next session

### 6.1 Immediate (highest priority)

**Investigate (α) M4_iso_nearest in detail.** This is the single largest
cascade-vs-PT brightness movement found via knob-tuning (+53% cam0 ratio,
+18% cam2 ratio). It needs:

1. **Visual A/B**: capture mode 17 PNG side-by-side at cam0+cam2 for M0
   vs M4. Does the M4 image LOOK closer to PT, or is it brighter-but-
   wrong (e.g. flat-shaded, no spatial detail)?
2. **Pair with MB ON at g=1.0**: the M4 finding was measured at MB OFF.
   At MB ON g=1.0, cam0 ratio is 0.474 (vs M4-MB-OFF 0.309). Does
   M4 + MB stack? Quick capture: 4 cells = {M0,M4} × {MB off, MB g=1.0}
   on cam0.
3. **Read [radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656)**
   to understand what the OFF arms are actually disabling. If M4 disables
   energy attenuation, write the change up as a known cascade-bake bug
   (and consider shipping `useDirectionalMerge=0` as the new engine default).
4. **D=16 × M4 stacking** test: does D=16 angular resolution amplify the
   M4 merge-mode improvement? At 6 captures total.

Estimated: ~1h capture + ~1h analyze/visual + ~2-3h code if M4 becomes
the new default. Total: half a session.

### 6.2 Secondary

- **(δ) full sweep**: the original (δ) HDR replay was at hybrid OFF /
  merge OFF defaults; rerun with M4_iso_nearest as the merge config to
  see if probe-density leverage is amplified.
- **(α) M4 + (β) g=0.5 combination**: HDR showed g=0.5 actually
  *reduces* MB feedback strength. M4 + low MB might be a sweet spot
  (M4 alone is already +53%, MB g=0.5 is +37% over MB OFF). Cheap to test.
- **Re-litigate the "structural 15-25% delivery" finding** from
  [hdr_exr_metric_impl.md §3.3](hdr_exr_metric_impl.md#L141) — was it
  really structural, or was it the merge-mode attenuation also? If
  M4 + best-other-tunables gets ratio above 0.5 on cam0, the structural
  floor was misattributed.

## 7. Self-critique

1. **`|p95|=1.000` for cam2 across all (α) and (γ) arms** is the same
   histogram clip explained in [hdr_exr_metric_impl.md §3.4](hdr_exr_metric_impl.md#L161)
   (`r∈[−1,+∞)`, max negative magnitude = 1.0). Not a bug; it means cam2
   is so uniformly under-bright that the 95th percentile of |r| saturates
   at the negative-side floor. Confirmed by `bright%≤3.4%` column across
   those arms.

2. **(β) g≥2.0 HDR ratios are astronomical** (65–107×PT). This is the MB
   feedback loop blowing up: each frame's bake samples the previous frame's
   atlas, multiplies by `albedo × gain`, writes back. With `gain=2` the
   eigenvalue of the bake operator on lit surfaces exceeds 1, and 512
   frames of feedback grow geometrically. This is exactly the same finding
   the LDR sweep reported as "+363% Δ-area" — HDR just lets us read the
   actual magnitude (≈14,000% ratio movement on cam2). Not a bug in the
   metric; a known property of the system at g≥1.5.

3. **(α) baseline ratio cam0=0.201** matches the (δ) cam0 N32 ratio
   (0.202 from `hdr_exr_results.json` rows section). Confirms the
   re-litigation harness is reproducing the prior measurement and the
   M4 +53% is a real shift in the same metric, not a config drift.

4. **EPS_PT sensitivity not re-tested for the new analyzer.** The
   `analyze_hdr_exr.py` sweep over {1e-4 ... 1e-1} showed verdicts
   were stable to ±0.02 in the central tendencies. The same code path
   computes `ratio` and `|p50|` here; the 26-capture replay uses the
   same EPS_PT=1e-3 default. The HDR_LEVERAGE thresholds (20%) are far
   above the ±2% noise floor observed in the (δ) replay, so the
   verdicts are EPS-robust without re-sweeping.

5. **Single seed (`noise-seed-offset=0`).** bug-230 still open. The HDR
   signal magnitudes here (29-53% for (α), 9% for (γ), 100s of % for
   (β) g≥1.5) are all far above the ~4.4% PT variance bound at 512 spp.
   bug-230 fix would tighten WEAK/TIE band confidence but does not
   change the LEVERAGE verdicts.

6. **MB ON requires bug-234 force-rebake.** Verified the
   `measurementCamera>=0 && useMultiBounce` gate at
   [demo3d.cpp:995](../../src/demo3d.cpp#L995) is mode-agnostic, so the
   (β) HDR captures actually use MB feedback (vs the silent-MB-OFF
   bug-234 trap). Console logs confirm `[MB] cascade-bake feedback
   ACTIVE` on every (β) capture.

## 8. Files touched this session

- New: [tools/v20_pre_measurement/hdr_relitigate_sweep.ps1](../../tools/v20_pre_measurement/hdr_relitigate_sweep.ps1) (26-capture harness)
- New: [tools/v20_pre_measurement/analyze_hdr_relitigate.py](../../tools/v20_pre_measurement/analyze_hdr_relitigate.py) (per-axis baseline-compare analyzer + per-arm verdict)
- New: `tools/v20_pre_measurement/captures_hdr_{alpha,beta,gamma}/` (26 PNG + 78 EXR ≈ 25 MB)
- New: [tools/v20_pre_measurement/hdr_relitigate_results.json](../../tools/v20_pre_measurement/hdr_relitigate_results.json)
- New: this doc

No engine code touched. All discoveries are pure measurement findings
on the v2.0-pre engine + HDR-EXR metric shipped earlier this session.
