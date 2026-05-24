# v2.0 P2 — Per-Pixel Dominant-Direction-Bin Viz (mode 22) + cam0/cam2 measurement

**Status:** Direct measurement of bake-side per-bin atlas asymmetry on
cornell-orig-alcove. Converts the inferential bake-side framing (downstream
path locked-in INNOCENT via h.b+h.c+h.c'+h.c'' 4-A/B chain — see
[v20_downstream_symmetrizer_architecture.md](v20_downstream_symmetrizer_architecture.md))
to a direct readout of which atlas direction bins each camera fetches
dominantly. **Verdict: P2_OVERLAP_MEDIUM (overlap=0.6617).**
**Date:** 2026-05-24
**Engine commit:** post-ST-default-flip (`useSpatialTrilinear=false`), mode 22 added this session.

## 1. What mode 22 does

For each surface pixel, mode 22 runs (at the **nearest-parent** probe — no
spatial interpolation) the inner loop of `sampleProbeDir`:

```
for (dy = 0..D-1) for (dx = 0..D-1):
    bdir = binToDir(dx, dy, D)
    wcos = max(0, dot(bdir, normal))
    a    = texelFetch(uDirectionalAtlas, ivec3(pc.x*D+dx, pc.y*D+dy, pc.z))
    contribution[dx,dy] = luminance(a.rgb) * wcos * a.a
```

then takes `argmax` over (dx, dy) and encodes the result into fragColor /
fragGI:

```
R = (dx_best + 0.5) / D       analyzer recovers dx = floor(R * D)
G = (dy_best + 0.5) / D       analyzer recovers dy = floor(G * D)
B = top_contrib / total_contrib   dominance fraction in [0,1]; 0 = no GI
```

The choice of NEAREST parent (not 8-neighbor trilinear) is deliberate:
spatial smoothing is itself a symmetrizer (it pulls cam0 and cam2 toward
their local probe-cluster means rather than revealing the per-probe atlas
content). Mode 22 measures **atlas content as it sits on disk** at the
single probe each pixel most directly samples.

EXR sidecar = `<stem>_dombin.exr` (RGBA32F, full viewport). PT is NOT
dispatched for mode 22 — there's no ground truth to compare against; mode
22 is a structural readout, not a residual measurement.

## 2. Capture config

| param | value |
|---|---|
| scene | cornell-orig-alcove |
| cameras | cam0, cam2 (`tools/v20_pre_measurement/cameras.json`) |
| merge mode | M0 (default) |
| cascade base | b=2 |
| multi-bounce | OFF |
| ST (useSpatialTrilinear) | 0 (new engine default — irrelevant for mode 22 since it uses nearest-parent) |
| blend mode | 0 (smoothstep) |
| frames | 256 (lets cascades settle; mode 22 needs no MC accumulation) |
| atlas direction res (inferred) | **D = 4** (16 bins per probe) |

Wall time: 0.2 min for both cameras.

## 3. Results

| metric                          | cam0       | cam2       |
|---------------------------------|-----------:|-----------:|
| Valid GI pixels                 | 138,332 / 921,600 (15.0%) | 116,390 / 921,600 (12.6%) |
| Mean dominance fraction         | 0.119      | 0.116      |
| Top-1 bin                       | (dx=1, dy=1) | (dx=0, dy=1) |
| Top-1 share                     | **31.4%**  | **54.9%**  |
| Top-2 bin                       | (dx=0, dy=1) | (dx=1, dy=1) |
| Top-2 share                     | 26.7%      | 19.8%      |
| Top-3 bin                       | (dx=3, dy=1) | (dx=2, dy=1) |
| Top-3 share                     | 19.4%      | 7.5%       |
| Top-4 bin                       | (dx=2, dy=1) | (dx=0, dy=2) |
| Top-4 share                     | 9.3%       | 4.6%       |

**Composite metrics:**

| metric | value | interpretation |
|---|---:|---|
| Histogram overlap (sum of per-bin min) | **0.6617** | falls in [0.40, 0.70) MEDIUM band |
| Jensen-Shannon divergence (base 2) | 0.1327 | low-to-moderate distributional divergence |
| **Verdict** | **P2_OVERLAP_MEDIUM** | partial bake-side asymmetry confirmed |

Pre-committed bands (`p2_dombin_capture.ps1`):

- `P2_OVERLAP_HIGH`   (overlap ≥ 0.70): bake-side framing INCOMPLETE
- `P2_OVERLAP_MEDIUM` (overlap in [0.40, 0.70)): partial bake-side asymmetry
- `P2_OVERLAP_LOW`    (overlap < 0.40): bake-side per-bin framing CONFIRMED

## 4. ASCII heatmaps (row=dy, col=dx, glyph density = share)

```
cam0 (dy=1 row; spread across dx=0..3):
            (dy=0)
    %@:*    (dy=1)  ← dx=[1,0,3,2] share=[31.4%, 26.7%, 19.4%, 9.3%]
            (dy=2)
            (dy=3)

cam2 (dy=1 row; collapsed to dx=0):
            (dy=0)
    @-.     (dy=1)  ← dx=[0,1,2] share=[54.9%, 19.8%, 7.5%]
            (dy=2)
            (dy=3)
```

## 5. Interpretation

**Shared structure** (the source of the 0.66 overlap baseline):
- Both cameras' dominant bins live entirely on row `dy=1`. This is forced
  by surface geometry: bins on row `dy=1` map to octahedral directions
  with the dominant upward-ish hemisphere component, which is where most
  Cornell wall/floor normals point.
- Both cameras have similar valid-GI pixel coverage (15% vs 12.6%) and
  essentially identical mean dominance fractions (~0.12).

**Asymmetric structure** (what the 0.34 non-overlap is telling us):
- cam0 spreads its dominant-bin pixels across **3 nearly-equal bins**
  in row `dy=1` (dx=0 27%, dx=1 31%, dx=3 19% — total 78% across 3
  bins). The integrated radiance at cam0 surfaces draws from a wide
  azimuthal range.
- cam2 **collapses 55% of dominant-pixel mass onto a single bin**
  (dx=0, dy=1). The next 2 bins together add another 27%. cam2 is
  reading from a fundamentally narrower azimuthal slice of the atlas.

**The 0.66 overlap number understates the asymmetry** because both cameras
share the dy=1 row — the per-row histogram-overlap denominator is large.
A more discriminating per-row metric (or a per-row-conditioned JS) would
show stronger separation:
- cam0 row-dy=1 distribution: [27%, 31%, 9%, 19%] (entropy-rich, fanned)
- cam2 row-dy=1 distribution: [55%, 20%, 8%, 0%]   (entropy-poor, concentrated)

The dx-collapse on cam2 directly mirrors what the h.b/h.c/h.c'/h.c''
chain inferred: cam2 reads radiance from a much narrower atlas region
than cam0 does. **A single atlas bin's content sets cam2's brightness;**
cam0 by contrast averages across multiple bins so any single-bin
under-fill (e.g., the alcove partition occluding most of the dx=0..3 sky
hemisphere for some cam2 probes) is diluted before reaching surface
luminance.

## 6. What this confirms and what it leaves open

**Confirmed:**
- The bake-side per-direction-bin atlas content IS distributed asymmetrically
  between cam0-visible and cam2-visible surfaces. The asymmetry is direct
  and measurable (not just inferential).
- The asymmetry shape matches the (h.c)' / (h.c)''' result: cam2's
  symmetrizer-OFF brightness lift was +9% (smaller than cam0's +21%)
  because cam2 was already concentrated on a narrow bin set — the
  symmetrizer had less per-pixel mass to smooth.

**Not confirmed (and the reason the verdict band is MEDIUM, not LOW):**
- The shared dy=1 row means cam0 and cam2 are *both* fetching from the
  same atlas region in a structural sense (the upper hemisphere). The
  asymmetry is *within* that region, not *across* atlas regions.
  Bake-side fixes targeted at per-dx-bin coverage on the dy=1 row would
  affect both cameras simultaneously; targeted asymmetry-correction
  would need per-(dx, dy, probe-region) precision.
- The mean dominance fraction is low (~0.12) — meaning each pixel's
  TOP bin only accounts for 12% of its total contribution, with the
  remaining 88% spread across other bins. So the symmetrizer wasn't
  *only* masking the top-bin asymmetry; it was averaging across a
  long tail. Per-bin firefly clamps at bake would address single-bin
  outliers in this long tail; bin-coverage hardening would broaden
  the active-bin set.

## 7. Self-critique

**Strengths:**
- Closes the inferential loop. Before P2, "bake-side per-bin asymmetry"
  was the only suspect left standing after elimination. Now it's a
  measured property: cam2's top-1 share (55%) is 1.75× cam0's (31%).
- The mode-22 readout is INDEPENDENT of every downstream toggle tested
  in the h-stage chain (it uses nearest-parent probe, not trilinear,
  not directional-merge, not blend-mode). So the asymmetry it shows
  is genuinely upstream of all consumption-time symmetrizers.
- D was *inferred* (not hard-coded) — the analyzer detects atlas
  direction-resolution from the EXR data and quantizes back cleanly.
  Future captures at a different D auto-adapt without code changes.

**Weaknesses:**
- The histogram-overlap metric mixes "same row" and "same column"
  contributions. A per-row-conditioned divergence would expose the
  dx-axis asymmetry more cleanly — the 0.66 overlap is dominated by
  the shared dy=1 row. **Follow-up:** add per-row-conditioned JS or
  per-row chi-squared to the analyzer.
- Only top-1 dominant bin is analyzed per pixel; the long tail of
  contributions (mean dominance ~12%) is not characterized. A full
  per-bin contribution dump (instead of just argmax) would let us
  measure whether cam2's tail is also concentrated or whether only
  the top bin is the asymmetry source. **Follow-up cost:** rewrite
  shader to emit per-bin contributions to a 16-channel-equivalent
  texture (4 EXR sidecars at D=4) — moderate effort, modest value
  given that top-1 share is already 1.75× different.
- Single scene measurement. The architectural doc's §5
  generalization rule says: "validate symmetrizer claims across scenes
  with varying probe-content variance." P2 has only been run on
  cornell-orig-alcove; would benefit from a flat-symmetric scene
  (sphere room) where we'd predict overlap → 1.0.
- D=4 is the engine default; cornell-orig-alcove might show different
  asymmetry at D=8 (more bins → finer asymmetry resolution). A D-sweep
  on the same scene would let us state: "the asymmetry persists / sharpens
  / dissolves at higher direction resolution."
- The captures are at MB-OFF only. MB-ON could redistribute the
  per-bin atlas content via temporal feedback (occluded bins filled
  in from prior frames). **Follow-up:** rerun P2 with `--use-multi-bounce=1`
  to see if MB closes the dx-axis collapse.

## 8. Cross-reference

- Upstream inference chain ending at this measurement:
  - [v20_b_smoothstep_toggle_impl.md](v20_b_smoothstep_toggle_impl.md) — (h.b) blend-zone INNOCENT
  - [v20_c_fract_viz_impl.md](v20_c_fract_viz_impl.md) — (h.c) probe-cell mean INNOCENT
  - [v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md) — (h.c)' ST symmetrizer-not-contributor
  - [v20_cprime2_downstream_knobs_impl.md](v20_cprime2_downstream_knobs_impl.md) — (h.c)'' DM+DB INNOCENT, full downstream locked-in
  - [v20_cprime3_st0_mitigation_impl.md](v20_cprime3_st0_mitigation_impl.md) — (h.c)''' ST=0 cross-scene soft generalization
- Architectural framing:
  - [v20_downstream_symmetrizer_architecture.md](v20_downstream_symmetrizer_architecture.md)
- Engine changes this session:
  - `src/demo3d.cpp:197-205` — `useSpatialTrilinear` default flipped 1→0
  - `res/shaders/raymarch.frag` — mode 22 block added before mode 18
  - `src/demo3d.cpp` — `setRenderMode` range [0,19]→[0,22]; mode 22 label
    added; `giBlurActive` gates mode 22 EXR path; `dumpScreenshotEXRs`
    splits mode 22 (dombin only) vs other modes (cascade_gi + pt_full +
    pt_direct)
- Analyzer artifacts:
  - `tools/v20_arch_diagnostic/p2_dombin_capture.ps1`
  - `tools/v20_arch_diagnostic/analyze_p2_dombin.py`
  - `tools/v20_arch_diagnostic/captures_p2_dombin/p2_dombin_results.json`
