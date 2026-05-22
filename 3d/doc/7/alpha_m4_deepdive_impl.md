# MBRC v2.0-pre (α) M4_iso_nearest Deep-Dive — Stacking with MB & D=16

**Date**: 2026-05-22 (PM, follow-on to
[hdr_relitigation_impl.md](hdr_relitigation_impl.md)).

**Motivation**: The HDR re-litigation found M4_iso_nearest (`useDirectionalMerge=0,
useDirBilinear=0, useSpatialTrilinear=1`) is the single largest cascade-vs-PT
brightness lever measured (+53% cam0 ratio, +18% cam2). Per
[hdr_relitigation_impl.md §6.1](hdr_relitigation_impl.md#L168), the deep-dive
plan was: (1) visual A/B M0 vs M4, (2) stack M4 with MB g=1.0, (3) read the
shader to confirm what M4 disables, (4) stack M4 with D=16. All four checks
performed in this session.

## 1. Shader semantics — what each merge-mode disables

[radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656) has a 3-way branch:

| Mode | Path | Cost | Direction-aware? | Spatial blend? |
|------|------|------|------------------|----------------|
| M0 (dirMerge=1, spatialTri=1, scaled-dir-res=1) | `sampleUpperDirTrilinear` | 8 corners × D² atlas fetches | YES (cosine-weighted hemisphere integration per corner via `sampleUpperDir`) | YES (8-corner trilinear) |
| M2 (dirMerge=0, dirBilinear=1) | `texture(uUpperCascade, uvwProbe).rgb` | 1 hardware-bilinear sample | NO | hardware bilinear of raw atlas RGB |
| M3 (dirMerge=1, spatialTri=0) | `sampleUpperDir(upperProbePos, ...)` | D² atlas fetches | YES | NO (single probe) |
| M4 (dirMerge=0, dirBilinear=0) | `texelFetch(uUpperCascade, upperProbePos, 0).rgb` | 1 nearest fetch | NO | NO |

**Key insight**: M0 and M3 both route through `sampleUpperDir` ([radiance_3d.comp:206](../../res/shaders/radiance_3d.comp#L206)),
which loops over D² upper-cascade direction bins and accumulates
cosine-weighted contributions. **This per-direction-bin cosine-weighted
integration is a hemisphere average at merge time** — it spreads peak radiance
across many directions per merge step, scaling like `Σ_bins (cos θ · rad_bin) / Σ cos θ`.
For a sharply directional radiance distribution (lit wall in one bin, dark
in others) this attenuates the peak severely. M2 and M4 both skip this
integration; M4 reads a single texel directly, M2 hardware-bilinear-blends
texels without direction awareness.

The +53% cam0 brightening at M4 vs M0 is consistent with M4 bypassing this
hemisphere averaging at every cascade merge step. **This is hypothesis (ε)
from [hdr_relitigation_impl.md §4.1](hdr_relitigation_impl.md#L106) made
concrete**: the directional merge weighting is *energy-suppressing* at every
upper-cascade fetch, not just at the WeightedSample-gated branch.

## 2. Visual A/B M0 vs M4 (existing PNGs from HDR sweep)

Mode 17 (cascade-GI only, no direct lighting → most of frame is black; visible
region is what cascade integrated). cam0 + cam2 compared
between [captures_hdr_alpha/cam{0,2}_M0_baseline_m17.png](../../tools/v20_pre_measurement/captures_hdr_alpha/)
and [cam{0,2}_M4_iso_nearest_m17.png](../../tools/v20_pre_measurement/captures_hdr_alpha/):

- **cam0 M4**: back-alcove wall noticeably brighter (visible grey vs near-black
  in M0); pillars retain structure; slight loss of directional shading per pillar
  (consistent with bin-blind read).
- **cam2 M4**: **strong color bleed** present in M4 but absent in M0 — red
  wall's red is reaching the partition wall, green wall is now visibly green.
  This is real GI content the smart-merge was attenuating. Cost: visible
  probe-grid voxel-aliasing artifact (the "polkadot" moiré on the partition
  back wall) because spatial interpolation across probes is gone.

**Verdict**: M4 is NOT brighter-but-wrong (e.g. flat-shaded ambient flood). It
is brighter *with real GI content* — color bleed, indirect bounce light. The
tradeoff is "real GI but visible probe-grid stair-step" vs "smooth but
dim and color-bleed-suppressed."

## 3. Stacking harness ([tools/v20_pre_measurement/alpha_m4_stack_sweep.ps1](../../tools/v20_pre_measurement/alpha_m4_stack_sweep.ps1))

8 NEW captures (~3.7 min) producing a complete 2×2×2 stacking grid (8 cells × 2 cams = 16)
when combined with 8 baseline cells reused from prior sweeps:

| factor | values |
|--------|--------|
| merge  | M0 (baseline), M4 (iso_nearest) |
| MB     | OFF, ON at g=1.0 |
| D-cfg  | D8 scaled-dir-res (engine default), D16 uniform |

Baseline cells reused from `captures_hdr_{alpha,beta,gamma}/` (no recapture).
NEW cells:
- S1: M4 + MB ON g=1.0 + D8-scaled (M4 × MB pair-stack)
- S2: M0 + MB ON g=1.0 + D16-uniform (MB + D16 control)
- S3: M4 + MB OFF + D16-uniform (M4 × D16 pair-stack)
- S4: M4 + MB ON g=1.0 + D16-uniform (triple-stack ceiling)

## 4. Headline numbers — 2×2×2 stacking grid

```
                                cam0   cam2     cam0   cam2
                                ratio  ratio    |p95|  |p95|
M0 + MB OFF + D8scaled  (baseline) 0.201 0.140   7.05   1.00
M0 + MB OFF + D16uniform           0.220 0.150   7.75   1.00
M0 + MB ON  + D8scaled             0.474 0.288  17.13   1.00
M0 + MB ON  + D16uniform           0.505 0.302  18.08   1.00
M4 + MB OFF + D8scaled             0.309 0.165   8.72   1.00
M4 + MB OFF + D16uniform           0.320 0.169  10.14   1.00
M4 + MB ON  + D8scaled             0.645 0.382  22.48   1.15
M4 + MB ON  + D16uniform (TRIPLE)  0.681 0.392  25.66   1.31
```

(Full per-cell JSON: [tools/v20_pre_measurement/m4_stack_results.json](../../tools/v20_pre_measurement/m4_stack_results.json).)

### 4.1 Main effects (mean ratio averaged over the other two factors)

| factor | cam0 OFF | cam0 ON | Δ | cam2 OFF | cam2 ON | Δ |
|--------|----------|---------|---|----------|---------|---|
| merge (M0→M4)  | 0.350 | 0.488 | **+0.138** | 0.220 | 0.277 | +0.057 |
| MB (OFF→ON)    | 0.262 | 0.576 | **+0.314** | 0.156 | 0.341 | +0.185 |
| D (D8→D16)     | 0.407 | 0.431 | +0.024 | 0.244 | 0.253 | +0.009 |

**MB is the largest single lever, ≈2× the M4 lever; D=16 is ≈6× weaker than M4 on cam0** (consistent
with prior γ rejection). The cam2 lever sizes are roughly half of cam0's across all three factors — the
cam0/cam2 asymmetry is *uniform* in absolute units across the stacking grid.

### 4.2 Additivity check (M4 × MB on D8)

| cam | base | Δ(M4) | Δ(MB) | Δ(both) | sum(M4+MB) | non-linear |
|-----|------|-------|-------|---------|-----------|-----------|
| 0   | 0.201 | +0.107 | +0.272 | +0.443 | +0.379 | **+0.064 (+16.8% super-additive)** |
| 2   | 0.140 | +0.025 | +0.148 | +0.242 | +0.174 | **+0.068 (+39.4% super-additive)** |

**M4 and MB stack super-additively on both cams.** The combined brightening
exceeds the sum of the singletons. Interpretation: removing the merge-time
hemisphere attenuator (M4) exposes more atlas radiance for MB's temporal
feedback loop to bounce → MB equilibrium is higher than it would be against
the M0-attenuated baseline. The two knobs hit different parts of the same
missing-energy budget; combining them recovers more than each independently.

### 4.3 Triple-stack ceiling — does cascade approach PT?

| cam | triple ratio | gap from 1.0 | verdict |
|-----|--------------|--------------|---------|
| 0   | 0.681        | 0.319        | RESIDUAL_GAP_PRESENT |
| 2   | 0.392        | 0.608        | RESIDUAL_GAP_PRESENT |

**Even with all three biggest knobs maxed, cam0 is at 68% of PT GI and cam2 at
39%.** The triple-stack does NOT close the gap. There is a residual cascade-vs-PT
shortfall that is not addressable by these three tunable axes.

### 4.4 Firefly-tail growth

|p95| (firefly indicator) tracks brightness stacking on cam0: 7.05 (base) → 8.72 (M4 only) →
17.13 (MB only) → 22.48 (M4+MB) → 25.66 (triple). MB is the dominant
firefly-amplifier. On cam2, |p95| stays at the histogram-clip floor of 1.000 until
the triple-stack, where it breaks to 1.15 (M4+MBon+D8) and 1.31 (triple) — the
first cam2 cell in the entire MBRC v2.0-pre program to escape the negative-side
floor.

## 5. Visual triple-stack PNG check

[cam0_S4_M4_MBon_D16unif_m17.png](../../tools/v20_pre_measurement/captures_hdr_m4stack/cam0_S4_M4_MBon_D16unif_m17.png):
fully recognizable Cornell box — red wall reads red, green wall reads green,
back wall lit, pillars structured, ceiling light source visible. Some
firefly speckle on the back wall (consistent with |p95|=25.6) but **no
catastrophic blowout** — orders of magnitude below the (β) g≥2.0 runaways.

[cam2_S4_M4_MBon_D16unif_m17.png](../../tools/v20_pre_measurement/captures_hdr_m4stack/cam2_S4_M4_MBon_D16unif_m17.png):
clean Cornell box with vivid color walls and visible light source on the
ceiling. Voxel-grid moiré on the partition wall is the M4 cost (no spatial
interp). Picture quality wise this is the best cascade-GI-only image
the engine has produced for cam2.

## 6. Per-hypothesis verdicts

| Hypothesis | Prediction | Result | Verdict |
|------------|------------|--------|---------|
| H1 (M4 stacks with MB additively) | d_both ≈ d_M4 + d_MB | super-additive +17%/+39% | **CONFIRMED, stronger than predicted** |
| H2 (triple-stack saturates near PT) | ratio → 1.0 | 0.68/0.39 — residual gap | **REJECTED** |
| H3 (M4 destabilizes MB feedback eigenvalue) | ratio > 1.0 overshoot | 0.68 max, stable | **REJECTED** |
| H4 (D=16 is near-neutral with M4) | Δ ≤ ±5% | +0.011 / +0.004 ratio | **CONFIRMED (TIE)** |

## 7. Cross-finding takeaway

**The "structural 15-25% delivery" finding from [hdr_exr_metric_impl.md §3.3](hdr_exr_metric_impl.md#L141)
was misattributed.** That section concluded cascade GI delivers 15-25% of PT
GI as a *structural property*. M4+MBon shows cam0 at 65% with both axes
re-tuned and cam2 at 38% — about **45 percentage points on cam0 and 24 pp on cam2**
were attributable to the M0 merge-mode attenuation + missing MB feedback, not
to a structural floor. The actual structural floor lies somewhere ≤ (1.0 − 0.68) = 32%
on cam0 and ≤ (1.0 − 0.39) = 61% on cam2.

**Cam0 vs cam2 asymmetry persists at every stack level.** Triple-stack
cam0=0.681, cam2=0.392 — gap 0.29 wide, similar to the cam0/cam2 gap at the
M0 baseline (0.201 vs 0.140 = 0.06 gap, scales with brightness). The
asymmetry from [mbrc_v20_pre_measurement_report.md §11](mbrc_v20_pre_measurement_report.md)
**is not tunable by these three axes**. Whatever drives the residual cam2
deficit is a 5th hypothesis the named-tree has not yet enumerated.

**Engine-default recommendation**: ship `useDirectionalMerge=0` (or expose as
default-off) **and** `useMultiBounce=1` at g=1.0. The combined cam0 ratio
movement is +0.443 (0.201 → 0.645) — roughly 3× brighter cascade for nearly free
(MB has the ~1ms/frame stochastic-sample cost already; M4 is *cheaper* than M0).
Cost: visible probe-grid voxel-aliasing artifact. If the artifact is unacceptable,
the M2 path (dirBilinear, hardware-bilinear) gives most of M4's benefit
without the moiré at the cost of ~half the M4 lever (M2 cam0 = +29% vs M4 +53%
per the original re-litigation §3 table).

## 8. Recommended next session

### 8.1 Immediate (post-this-doc)

1. **Read [radiance_3d.comp:206 `sampleUpperDir`](../../res/shaders/radiance_3d.comp#L206)**
   in detail to confirm the per-bin cosine-weighting math is what's
   suppressing energy. Once confirmed, design a "thin merge" — keep the
   directional-bin awareness (so different rayDirs see different upper
   bins) but skip the cosine-weighted hemisphere sum. The hypothesis is
   that the right operation is "fetch the upper bin that best matches
   the current rayDir" (nearest-bin or bilinear-bin), not "hemisphere-average
   over D² upper bins."
2. **Pair M2 (dirBilinear, smooth) with MB g=1.0** to see if the
   smooth alternative captures most of the M4 lever without the
   voxel-grid artifact. 2 captures.
3. **Investigate the residual cam2 deficit (60% gap at triple-stack).**
   Likely candidates: (i) probe-grid-vs-view-angle aliasing (cam2's
   shallower angle hits more inter-probe transitions); (ii) smoothstep
   blend-zone math [radiance_3d.comp:771-775](../../res/shaders/radiance_3d.comp#L771)
   under-weights cam2's contribution. Mode-14 leak-suspect heatmap at
   cam2 with M4+MB could localize.

### 8.2 Engine-default decision (write-up + commit)

Strong case for shipping `useDirectionalMerge=0` + `useMultiBounce=1` at
g=1.0 as the new MBRC v2.0-pre engine defaults. Before doing so:
- Sponza visual A/B at cam.md — the M4 voxel-grid moiré will be more visible
  on Sponza's larger surfaces.
- Plain Cornell (no alcove) visual A/B — light source is on the ceiling not
  in an alcove, different probe-density-vs-radiance distribution.
- Decision: ship as default or as recommended-config (CLI/GUI preset)?

## 9. Self-critique

1. **|p95| firefly growth is real and unhandled.** Triple-stack cam0 |p95|=25.7
   means 5% of valid pixels have cascade > 25× PT — these are bright
   isolated probes feeding back through MB into themselves. Mitigation
   would be the existing v1.2.4 firefly-HIGH-only clamp pattern from
   [[feedback_asymmetric_filters]]; but that's a hybrid-pipeline clamp, not
   present in the cascade-bake feedback path. If we ship M4+MB as default,
   the cascade-bake feedback gets its own firefly clamp request (~30 min,
   1 shader uniform + 1 line in cascade-bake feedback shader).

2. **D=16 uniform is not a full sweep.** The D=16 cells in this grid use
   `cascade-scaled-dir-res=0 --cascade-dir-res=16` (uniform across all
   cascades). The (γ) HDR re-litigation already showed D=16 main effect is
   small (+9.5%/+8.5% cam0/cam2); this grid confirms that effect doesn't
   amplify with M4 (Δ=+0.011 cam0, +0.004 cam2 at MB-OFF). D=24, D=32 not
   tested — but the γ verdict and this stacking confirm the angular-resolution
   axis is exhausted for the current architecture.

3. **MB g=0.5 not tested in this grid.** The β sweep showed g=0.5 reduces
   MB feedback strength vs g=1.0. Adding M4+MBg=0.5+D8 would clarify whether
   the firefly tail (|p95|=22.5) at g=1.0 is acceptable or whether g=0.5 +
   M4 sits in a better noise/brightness trade-off. 2 captures, cheap follow-on.

4. **Single noise seed (bug-230 still open).** All 8 stacking cells used
   `--noise-seed-offset=0`. The ratio differences here (+0.443 cam0 between
   base and triple) are >> the 4.4% PT-variance bound — verdicts robust.
   But the |p95| firefly tail comparisons (25.7 vs 22.5) are within the
   range where seed-variance could shift the verdict; firefly-mitigation
   comparisons should re-run with bug-230 fixed first.

5. **PNG visual A/B is mode 17 (cascade-GI only).** Final compositor output
   (mode 0) adds direct lighting which dominates the brightness budget;
   the M4+MB stack's contribution may visually disappear under direct
   lighting. **Before shipping engine defaults**: capture mode 0 (full
   composite) at cam0+cam2 with M0+MBoff vs triple-stack and confirm the
   composited image is also better, not just the GI-only channel. 2 captures.

6. **Cam2 still under-bright by 61% at triple-stack** — points to a
   structural cam-geometry issue (probably probe-density at cam2's
   view distance or grid-vs-view-angle aliasing) that no merge / MB /
   bin-count tuning can address. The next hypothesis to design against is
   this asymmetric residual, not further single-axis tuning.

## 10. Files touched this session

- New: [tools/v20_pre_measurement/alpha_m4_stack_sweep.ps1](../../tools/v20_pre_measurement/alpha_m4_stack_sweep.ps1) (8-capture stacking harness)
- New: [tools/v20_pre_measurement/analyze_m4_stack.py](../../tools/v20_pre_measurement/analyze_m4_stack.py) (2×2×2 grid analyzer + main-effects + additivity + triple-stack verdict)
- New: `tools/v20_pre_measurement/captures_hdr_m4stack/` (8 PNG + 24 EXR ≈ 10 MB)
- New: [tools/v20_pre_measurement/m4_stack_results.json](../../tools/v20_pre_measurement/m4_stack_results.json)
- New: this doc

No engine code touched. Pure measurement on the v2.0-pre engine + HDR-EXR metric.
