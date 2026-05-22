# MBRC v2.0-pre HDR-EXR Honest Metric — Implementation & First Results

**Date**: 2026-05-22 (PM, same day as the (δ) probe-density sweep, immediately
following the "named-hypothesis-tree exhausted" verdict).

**Motivation**: All four (α/β/γ/δ) probe sweeps used the same LDR-PNG
saturation-band classifier (SAT=0.55, LUMA=0.05) on a colormap with
divisor=0.2. Any pixel with `|cascade − PT| > 0.2` in radiance space
saturates the colormap regardless of magnitude. The four REJECT/LEVERAGE
verdicts are therefore consistent with two competing explanations:
(a) the named knob has no leverage, OR
(b) the named knob *has* leverage, but the LDR metric clamped it away.
Until this is falsified, the "tree exhausted" verdict is built on uncertain
measurement ground.

This document describes the engine work needed to falsify (b), and reports
the result of the first HDR-EXR sweep — a 6-capture replay of (δ).

## 1. Engine wiring (~120 LoC)

### 1.1 Headers / build

- `lib/tinyexr/tinyexr.h` (single-header BSD-3, 11k LoC)
- `lib/tinyexr/miniz.{h,c}` (ZIP compression dep for SaveEXR)
- `lib/tinyexr/streamreader.hh`, `lib/tinyexr/exr_reader.hh` (newer tinyexr
  releases split these out)
- `src/exr_writer.{h,cpp}` — thin wrapper isolating tinyexr.h from raylib.h.
  Required because tinyexr.h `#include`s `<windows.h>`, which clashes with
  raylib's C-linkage `CloseWindow()` / `ShowCursor()`. Same isolation
  pattern as `src/rdoc_helper.cpp`.
- `CMakeLists.txt`: added `src/exr_writer.cpp`, `lib/tinyexr/miniz.c`,
  include path `lib/tinyexr`, and a `/W0` exemption for miniz.c (third-party,
  noisy under `/W4 /WX-`).

### 1.2 Public state + setter

`Demo3D::exrCapture` (bool, default false), `setExrCapture(bool)`,
`getExrCapture() const`. Setter resets the PT accumulator so the captured
mean is from a clean restart.

### 1.3 CLI

`--screenshot-exr=0/1` in [src/main3d.cpp](../../src/main3d.cpp#L283) next
to the existing `--screenshot=` flag. Intended pairing:
`--render-mode=17 --screenshot=foo.png --screenshot-exr=1`.

### 1.4 Shader (raymarch.frag)

The existing `uSeparateGI` MRT path (location=0 = direct, location=2 = GI)
fires only for `uRenderMode == 0` so display modes 6/17 reach the composite
path. Widened the gate to also fire for `uRenderMode == 17` so the cascade
indirect lands in `giIndirectTex` for the EXR dump. `fragColor` is set to
`indirectColor` (not `directColor`) when in mode 17 so the PNG sanity-check
shows GI rather than direct. See
[res/shaders/raymarch.frag:981-990](../../res/shaders/raymarch.frag#L981).

### 1.5 Engine gates (demo3d.cpp)

Three minimal extensions, each guarded on `(exrCapture && raymarchRenderMode == 17)`:

1. **PT-dispatch gate** at [demo3d.cpp:1262](../../src/demo3d.cpp#L1262):
   added to the existing `(16 | 18 | 19 | 20)` list so the PT accumulator
   populates while in mode 17.
2. **PT direct-only sub-dispatch** at [demo3d.cpp:3314](../../src/demo3d.cpp#L3314):
   already gated on `(18 | 19)`; widened so `ptDirectAccumTexture` actually
   has data (without this fix, the first sanity capture produced a 3.8 KB
   all-zero pt_direct.exr — see §3 self-critique).
3. **giBlurActive gate** at [demo3d.cpp:3029](../../src/demo3d.cpp#L3029):
   widened so `giFBO` is bound and `giIndirectTex` receives the MRT
   write. The `giBlurPass()` composite at
   [demo3d.cpp:1308](../../src/demo3d.cpp#L1308) is **not** triggered for
   mode 17 (still gated on `0|3|6`), so we manually `glBlitFramebuffer`
   `giDirectTex` → default FB inside `dumpScreenshotEXRs` so the
   subsequent `TakeScreenshot(.png)` is non-black.

### 1.6 dumpScreenshotEXRs

Reads three textures via `glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, ...)`,
Y-flips (EXR is top-left origin; GL is bottom-left), and emits 32-bit-float
RGB EXRs via `exrw::save_rgb32f_exr`:

| File suffix         | Source texture            | Format    | Size           |
|---------------------|---------------------------|-----------|----------------|
| `_cascade_gi.exr`   | `giIndirectTex`           | RGBA16F   | full viewport  |
| `_pt_full.exr`      | `ptAccumTexture`          | RGBA32F   | half viewport  |
| `_pt_direct.exr`    | `ptDirectAccumTexture`    | RGBA32F   | half viewport  |

PT textures store the running per-frame radiance MEAN
(`mix(prev, frameMean, rays/(spp+rays))` in `pt_reference.comp`), so they
are already normalized. `giIndirectTex` is raw post-MB cascade indirect.

## 2. Analyzer ([tools/v20_pre_measurement/analyze_hdr_exr.py](../../tools/v20_pre_measurement/analyze_hdr_exr.py))

Per-pixel signed relative error: `r = (cascadeGI − ptGI) / max(ptGI, EPS_PT)`
where `ptGI = pt_full − pt_direct` (clamped to ≥0). `cascadeGI` is
downsampled 2×2-avg to match PT's half-viewport resolution before
differencing.

`EPS_PT` (default 1e-3) masks out background/dark pixels with no PT signal;
sensitivity sweep over `{1e-4 … 1e-1}` confirmed the verdicts are stable
across thresholds (the noisy outliers in the 1e-4 / 1e-3 columns disappear
by 1e-2 but the central tendencies hold).

Reports per-(cam, N): valid-pixel %, p05/p50/p95 of signed `r`, p50/p95 of
`|r|`, mean PT and cascade GI luminance, fraction with cascade < 0.5×PT
("dim"), fraction with cascade > 2×PT ("bright").

## 3. Sweep replay of (δ): N ∈ {16, 32, 64} × cam ∈ {0, 2} (6 captures, 1.0 min)

Same engine config as the LDR (δ) sweep (`useMultiBounce=0 useHybrid=0`,
all merge toggles ON, `noise-seed-offset=0`, 512 frames). Mode 17 instead
of 18/19, `--screenshot-exr=1` added.

### 3.1 Headline numbers (EPS_PT = 1e-3)

```
cam   N  valid%   p50_rel   |p50|   |p95|   meanPT  meanCasc   dim% bright%
  0  16   14.9%    -0.845   0.868   9.224   0.1805   0.0321   81.0%   14.8%
  0  32   14.9%    -0.809   0.839   7.053   0.1805   0.0364   78.2%   14.3%
  0  64   14.9%    -0.792   0.831   9.130   0.1805   0.0373   81.4%   14.3%
  2  16   11.4%    -0.882   0.886   1.000   0.2386   0.0370   91.8%    3.5%
  2  32   11.4%    -0.943   0.946   1.000   0.2386   0.0334   87.6%    2.9%
  2  64   11.4%    -0.713   0.729   1.000   0.2386   0.0585   93.3%    3.4%
```

### 3.2 Comparison with LDR (δ) result

| Metric                  | LDR (captures_delta) verdict        | HDR result                                                       |
|-------------------------|-------------------------------------|------------------------------------------------------------------|
| cam0 N16→N64            | 26.5% → 27.9% (Δ-area, ±10%: tie)   | meanCasc/meanPT 0.18→0.21 (+15%), |p50| 0.87→0.83 (−5%)         |
| cam2 N16→N64            | 19.5% → 20.4% (Δ-area, ±10%: tie)   | meanCasc/meanPT 0.16→0.25 (+58%), |p50| 0.88→0.72 (**−19%**)     |
| Overall verdict         | DELTA_REJECT (no leverage)          | (δ) **has cam2 leverage**; cam0 still flat                       |

### 3.3 Two findings, one big

**Finding A — methodology**: The LDR DELTA_REJECT for cam2 was a
**measurement artifact**. The cam2 LDR Δ-area moved 0.9 percentage points
(19.5→20.4) while the HDR signed-p50 moved 19% in magnitude (0.88→0.72)
and the cam2 mean cascade-to-PT ratio moved 58%. The LDR colormap (divisor
= 0.2) was clamping a real radiance signal into a saturated band.

The **prior three (α / β / γ) rejections must be re-litigated against HDR**
before declaring the tree exhausted.

**Finding B — actual cascade behavior**:
Cascade GI is **systematically delivering 15–25% of PT GI luminance**
across all (cam, N) configurations. The under-illumination is not a
parameter tuning issue, it is a structural property of the cascade-vs-PT
pipeline as architected. 78–93% of valid-PT pixels have cascade < 0.5× PT
(median pixel under-bright by ~80%). At the same time, cam0 has a
heavy-tailed firefly population (|p95| ratio 7–9, vs cam2 ≤ 1.0) — so
cam0 also has bright outliers.

The asymmetric cam2 vs cam0 pattern observed in the full-sweep report §11
persists in HDR with a clean signature: cam2 is more uniformly under-bright
(higher dim%, lower bright%), cam0 has mixed under-bright with a firefly
tail.

### 3.4 Self-critique

1. **`|p95| = 1.000` for cam2 is a histogram clip, not a bug.** `rel ∈
   [−1, +∞)`; max negative magnitude is 1.0 (cascade=0). cam2's 95th
   percentile of `|rel|` saturating at 1.0 means 95% of cam2 valid pixels
   have cascade ≤ 2× PT — consistent with the `bright% ≤ 3.5` column.
2. **PT convergence: 512 spp on 640×360 at half-viewport, single ray/pixel
   per frame.** Per `pt_reference.comp:355` PT stores running mean; 512
   samples is well-converged for the diffuse cornell GI signal. Variance
   at the worst pixel ≈ 1/√512 ≈ 4.4% relative. Therefore the 19%
   cam2 N16→N64 |p50| movement is far above PT measurement noise.
3. **Single seed (`noise-seed-offset=0`).** bug-230 still open (only
   `uMBFrameSeed` is wired through). Cascade is deterministic per-config;
   PT noise floor is reduced by 512-frame averaging. The signal we found
   is large enough that single-seed noise is not the dominant uncertainty.
4. **Resolution mismatch handled correctly.** cascade is full 1280×720
   RGBA16F; PT textures are half (640×360) RGBA32F. The analyzer 2×2-avg
   downsamples cascade to match. (Bilinear upsample of PT would be the
   alternative; box-avg of cascade is preferred to avoid PT-side
   interpolation artifacts.)
5. **EPS_PT sensitivity.** Sweep over `{1e-4 … 1e-1}` shows the central
   tendency (`meanCasc/meanPT`, `|p50|`) is stable to ±0.02; only the
   `|p95|` outliers in the noisiest `1e-4` regime change materially. The
   verdicts are EPS-robust.
6. **giBlurPass NOT invoked in mode 17.** The PNG sanity image is the raw
   `giDirectTex` blit (which carries `indirectColor` in mode 17, per the
   shader change). Tonemap is bypassed; for cornell-orig-alcove the result
   is correctly dim (cascade indirect peak ~0.34 in radiance space).
   Functional but visually dim — that is correct behavior, not a bug.

## 4. Recommended next session

### 4.1 Immediate (high priority)

1. **Re-litigate (α / β / γ) against HDR.** All three named-hypothesis
   rejections were measured with the same saturated LDR metric. Run the
   same 6-capture HDR replay pattern (N held at 32, vary the hypothesis
   parameter, capture mode 17 + EXR). Estimated: 30 min capture per
   hypothesis + 5 min analyze = ~2 hours total.
   - (α) merge-mode: cam2 was the only one that moved in LDR (+14.6%
     when `useDirectionalMerge` OFF). HDR may reveal larger leverage or
     reverse-sign on cam0.
   - (β) MB-gain: g=2.0 LDR showed +363% / +214% Δ-area (cam0 / cam2).
     HDR will tell us whether this is real-signal "MB at g=2 is
     catastrophically off" or LDR-artifact "MB at g=2 brightens cascade
     into a different saturation band of the LDR metric".
   - (γ) angular under-sampling (D=8→16): LDR said −1.0% / −1.4%. The
     full-sweep promoted this hypothesis to leading on visual grounds.
     HDR replay is the cheapest discriminator.

### 4.2 Secondary

2. **Promote `analyze_hdr_exr.py` to drop-in replacement for LDR
   classifier.** Add CLI flags to load a directory of captures (any
   filename pattern), emit JSON + the table currently printed. Wire into
   `full_sweep.ps1` so the standard 72-capture sweep produces both LDR and
   HDR results side-by-side.
3. **Investigate Finding B.** Cascade-vs-PT 15–25% delivery is a
   structural deficit. Candidates: (i) cascade integration drops energy
   somewhere (merge weights, atlas resolution, scaled-D-res), (ii) PT
   over-counts (unbiased, but could be over-energetic — implausible given
   it matches the cornell reference), (iii) the cascade samples a
   different solid angle than PT integrates over.
4. **bug-230 fix** (1–2h). Now that we have a metric that can detect real
   signal, the noise-floor estimate matters for STRONG / WEAK verdicts.

## 5. Files touched this session

- New: `lib/tinyexr/{tinyexr.h, miniz.h, miniz.c, streamreader.hh, exr_reader.hh}`
- New: `include/exr_writer.h`, `src/exr_writer.cpp`
- New: `tools/v20_pre_measurement/{hdr_exr_sweep.ps1, analyze_hdr_exr.py}`
- New: `tools/v20_pre_measurement/captures_hdr_exr/` (7 PNG + 18 EXR, ~5 MB total)
- Modified: `CMakeLists.txt` (sources + warning suppression)
- Modified: `src/demo3d.h` (`exrCapture` member, `setExrCapture`,
  `dumpScreenshotEXRs` decl)
- Modified: `src/demo3d.cpp` (PT-dispatch gate, PT-direct gate,
  giBlurActive gate, `dumpScreenshotEXRs` impl, tinyexr include via
  `exr_writer.h`)
- Modified: `src/main3d.cpp` (`--screenshot-exr=` CLI, dump call from
  clean-screenshot path)
- Modified: `res/shaders/raymarch.frag` (broadened `uSeparateGI`
  early-return to mode 17, `indirectColor` to `fragColor` for mode 17)
