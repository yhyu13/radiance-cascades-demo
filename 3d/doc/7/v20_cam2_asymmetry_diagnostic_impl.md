# MBRC v2.0 — cam0/cam2 asymmetry diagnostic (first measurement under new defaults)

**Date**: 2026-05-23 (immediately follow-on to v2.0-pre closeout, commit `4875124`).

**Motivation**: [mbrc_v20_pre_measurement_report.md §16.5](mbrc_v20_pre_measurement_report.md)
priority-1 hand-off: localize where the cam2 residual lives under the new
engine defaults (M4_iso_nearest + MB g=1.0, commit `d64ea17`). v2.0-pre's
[alpha_m4_deepdive_impl.md §4](alpha_m4_deepdive_impl.md) found triple-stack
ceiling cam0=0.681 / cam2=0.392 — cam0 gap 0.32, cam2 gap 0.61. Asymmetry
persists at every stack level; not a tunable axis among the four tested.
Three candidate causes flagged: (a) bake-side leak (mode 14), (b) smoothstep
blend zone math, (c) probe-grid view angle / camera-projection.

This sweep tests (a) and (c) at once with 8 captures (~2 min).

## 1. Sweep matrix

[asymmetry_diagnostic_sweep.ps1](../../tools/v20_arch_diagnostic/asymmetry_diagnostic_sweep.ps1)
captures cam ∈ {0, 2} × mode ∈ {0 composite, 14 leak-suspect, 18 cascade-vs-PT
total delta, 19 cascade_GI-vs-PT_GI delta}, all under new engine defaults
(no config CLI flags), hybrid OFF, seed 0, 512 frames. `cornell-orig-alcove`
scene, cameras from [cameras.json](../../tools/v20_pre_measurement/cameras.json).

[captures_asymmetry/](../../tools/v20_arch_diagnostic/captures_asymmetry/) —
8 PNGs.

## 2. Findings

### 2.1. Mode 0 composite (camera framing context)

- **cam0** ([alcove_cam0_m0_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam0_m0_newdefault.png)) —
  front-center, looking INTO the alcove. Viewport mostly fills with main-room
  interior + partition columns + back wall straight ahead.
- **cam2** ([alcove_cam2_m0_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam2_m0_newdefault.png)) —
  front-left-elevated, looking at the alcove from above. Viewport shows more
  of the partition's alcove-side faces and the back-wall area; less of the
  main-room flat walls.

Key takeaway: cam2 samples a *different mix* of scene-space surfaces, with
more alcove-side and less main-room than cam0.

### 2.2. Mode 14 leak-suspect (HYPOTHESIS (a) BAKE-LEAK REJECTED)

- cam0 ([alcove_cam0_m14_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam0_m14_newdefault.png))
  and cam2 ([alcove_cam2_m14_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam2_m14_newdefault.png))
  look essentially identical: solid bright red across the entire visible
  scene, sparse green specks (clean pixels).
- Red = atlas radiance present in α=0 (occluded) bins → bake-side leak
  candidate. The default `leakHeatmapDivisor=0.5` may also be over-sensitive
  catching too much, but the cross-cam comparison is what matters here.

**If leak were the asymmetry driver, cam2 would show *more* leak than cam0.
It does not.** Both show the same rampant level. Leak rejected as the
dominant cause of the +0.29 gap difference.

### 2.3. Mode 18 vs Mode 19 (direct light is correct)

Comparing per cam:
- cam0 m18 ([alcove_cam0_m18_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam0_m18_newdefault.png))
  vs cam0 m19 ([alcove_cam0_m19_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam0_m19_newdefault.png))
  — essentially identical heatmaps.
- cam2 m18 ([alcove_cam2_m18_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam2_m18_newdefault.png))
  vs cam2 m19 ([alcove_cam2_m19_newdefault.png](../../tools/v20_arch_diagnostic/captures_asymmetry/alcove_cam2_m19_newdefault.png))
  — essentially identical.

Mode 18 = total delta (direct + indirect). Mode 19 = indirect only.
m18 ≈ m19 → **the cascade-vs-PT delta is entirely in the indirect/GI term**.
Cascade's direct light contribution agrees with PT at both cams. No
architectural attention needed on the direct path.

### 2.4. Mode 19 spatial structure (the actual finding)

Both cams show a **bidirectional** residual:

- **Blue (cascade < PT, under-bright)** — back walls of the alcove area;
  surfaces that receive indirect light only via paths through the partition
  opening.
- **Pink (cascade > PT, over-bright)** — main-room walls and ceiling;
  surfaces that receive indirect light from short bounce paths.

The scene-space residual is the same shape at both cameras. The asymmetric
*mean* (cam0 0.32 gap, cam2 0.61 gap) emerges from cam2's viewport
containing a larger pixel fraction of under-bright alcove-side surfaces.

This is a **camera-projection / surface-mix effect**, not a per-cam
architectural deficit. Stated differently: in scene-space the cascade is
making the same mistake at both cams; cam2 just averages over more of the
"badly-served" half.

### 2.5. What the bidirectional pattern means

Pink + blue from one shader pass is the classic signature of a **basis-
representation error**, not an energy-conservation error:

- Energy-conservation error would show one-sided bias (either uniformly
  under- or uniformly over-bright globally).
- Basis-representation error shows over-bright where the basis can represent
  the radiance (smearing peaks into floor) and under-bright where the basis
  cannot (occluded surfaces whose only light comes through narrow apertures).
- Cascade's per-probe radiance representation uses D² (D=8 default = 64)
  direction bins per probe, integrated via spherical cap. A wall-to-wall
  bounce path that PT resolves as a sharp directional spike gets averaged
  across multiple bins of the receiving probe → peak dropped → under-bright
  on the receiving wall. The averaged radiance is then redistributed across
  the *other* direction bins on subsequent merges → over-bright everywhere
  else.
- This explanation also matches the (γ) sweep verdict: D=8→16 only +9% HDR
  ratio. Doubling the bins twice would help, but the basis is still finite
  and the spike is still impossible to resolve exactly.

## 3. Hypotheses now ranked

| Hypothesis                                              | Pre-sweep rank | Post-sweep rank      | Reason                                                                                    |
|---------------------------------------------------------|---------------:|----------------------|-------------------------------------------------------------------------------------------|
| (a) bake-side leak (mode 14)                            |          P1    | **REJECTED**         | cam0/cam2 mode-14 essentially identical; leak doesn't track the asymmetric residual.      |
| (b) smoothstep blend zone math                          |          P2    | Demoted to P4        | Bidirectional pattern is across whole geometry, not localized at cascade boundaries.      |
| (c) camera-projection / scene-space-asymmetric residual |          P3    | **CONFIRMED**        | Same scene-space delta pattern at both cams; cam2 viewport oversamples the under-bright half. |
| (d) basis-representation error (per-probe D² bins)      | not on list    | **NEW P1**           | Pink+blue from one pass is basis-error signature; matches (γ) sweep's borderline TIE.     |
| (e) thin-merge shader (drop hemisphere sum, keep bin)   |    P2 (§16.5)  | Promoted to P2       | Targets the merge-time consequence of (d); reasonable next architectural test.            |

## 4. Self-critique

### What this 8-capture sweep cannot support

- **Quantitative pixel-by-pixel co-localization** — visual A/B is suggestive
  but I did not compute pixel-overlap statistics between mode 14 and mode 19
  heatmaps. The "leak rejected" verdict is justified by the absent
  cross-cam difference, not by pixel-correlation math.
- **Other scenes** — alcove-only. Plain Cornell or Sponza may have different
  asymmetry character. Deferred: cross-scene was already validated for the
  *engine default flip* (§16.1); the v2.0 asymmetry investigation can stay
  alcove-focused until an architectural intervention candidate emerges.
- **The "basis-representation error" hypothesis is interpretive, not
  measured.** The bidirectional pink+blue pattern is consistent with
  basis-error but other explanations also fit (e.g. the cascade's spatial
  trilinear merge could over-shoot some bilinear corners and under-shoot
  others). Stronger test: an absolute-residual analyzer (`cascade_GI -
  PT_GI` as a signed scalar field in HDR) that sums positive vs negative
  contributions over the viewport — if |positive| ≈ |negative|, the basis-
  error case is much stronger.

### What could still go wrong with this read

- **Leak heatmap divisor=0.5 may be over-sensitive** — both cams saturate to
  red; if a smaller divisor (more sensitive) found cam0/cam2 differences in
  the *quantitative* leak level, the "REJECTED" verdict would weaken. Quick
  follow-on: 4 captures at `--leak-heatmap-divisor=0.1`. Deferred unless
  someone pushes back on the rejection.
- **PT reference budget might be insufficient at cam2's back-wall pixels**
  — cam2's blue back-wall region is exactly where PT has the hardest
  convergence (long bounce paths through the partition opening). If PT
  itself is under-bright due to insufficient spp at those pixels, the "blue =
  cascade under-bright" verdict could partially invert. The PT cache
  ([mbrc_v20_pre_measurement_impl.md §2.5](mbrc_v20_pre_measurement_impl.md))
  was built with 4096 spp which should be sufficient, but worth re-checking
  the pt_convergence.csv specifically for cam2 back-wall pixels.

## 5. Recommended v2.0 next step

The single highest-information next step is **build an absolute-residual
analyzer over the existing HDR EXR captures** and run it on cam0 + cam2
under the new defaults. Specifically:

1. Capture mode 17 HDR EXRs at cam0 + cam2 under new defaults (2 captures,
   ~30s — the HDR pipeline already exists,
   [hdr_exr_metric_impl.md](hdr_exr_metric_impl.md)).
2. Extend or fork [analyze_hdr_exr.py](../../tools/v20_pre_measurement/analyze_hdr_exr.py)
   to compute and dump:
   - Per-pixel signed `Δ = cascade_GI − PT_GI` (HDR scalar field).
   - Sum of positive Δ vs sum of negative Δ across the viewport.
   - Mean |Δ| over positive-Δ region vs over negative-Δ region.
   - A 2-color PNG (red/blue) of the signed scalar field, like mode 19
     but in absolute radiance units instead of colormap-divisor units.
3. Verdict rule (pre-committed before running):
   - **|Σ+| ≈ |Σ−|** (within 30%) → basis-representation error confirmed.
     Next architectural step: thin-merge shader variant (Priority 2 of
     §16.5).
   - **|Σ+| ≪ |Σ−|** → cascade is net-under-bright; the apparent over-
     bright pink is local re-distribution but the system loses energy.
     Next: energy audit at bake time.
   - **|Σ+| ≫ |Σ−|** → cascade leaks energy from nowhere. Re-open the
     leak hypothesis.

This is ~1h of analyzer work + 30s capture + 15 min analysis. It produces a
**measured** verdict on hypothesis (d) before any shader work begins.

Estimated cost-vs-information: 1.5h for a verdict that determines whether
the next architectural intervention is thin-merge (Priority 2) or something
else entirely. Better than jumping straight into shader code.

## 6. Artefacts

- Sweep harness: [tools/v20_arch_diagnostic/asymmetry_diagnostic_sweep.ps1](../../tools/v20_arch_diagnostic/asymmetry_diagnostic_sweep.ps1)
- Captures (8 PNGs): [tools/v20_arch_diagnostic/captures_asymmetry/](../../tools/v20_arch_diagnostic/captures_asymmetry/)
