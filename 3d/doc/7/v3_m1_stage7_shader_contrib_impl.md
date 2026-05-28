# M1 Stage 7 Implementation - Shader-Side Contribution Diagnostics

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage7_shader_contrib_plan.md`.  
**Result artifact:** `tools/v3_m1_shader_contrib/shader_contrib_results.json`.  
**Verdict:** `BROAD_LOCAL_ENERGY`.

## What Changed

1. Added shader-side contribution diagnostics to `raymarch.frag`.
   - `*_probe_contrib.exr`
     - `rgb`: top contributing trilinear probe coordinate normalized by C0 resolution.
     - `a`: top probe share of raw indirect luminance.
   - `*_probe_bin.exr`
     - `r/g`: top directional bin center normalized by `D`.
     - `b`: top bin share of raw indirect luminance.
     - `a`: raw indirect luminance reconstructed by the contribution-detail path.

2. Added FBO attachments and EXR sidecar dumping.
   - `giProbeContribTex`
   - `giProbeBinTex`

3. Added tooling:
   - `tools/v3_m1_shader_contrib/capture_shader_contrib.ps1`
   - `tools/v3_m1_shader_contrib/analyze_shader_contrib.py`

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed with the existing warning baseline.

Capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_shader_contrib/capture_shader_contrib.ps1 -Scene sponza -N 2048
python tools/v3_m1_shader_contrib/analyze_shader_contrib.py
```

The first runtime attempt caught a GLSL compile error because `sample` is reserved in this shader compiler. Renamed the struct field to `ps`, rebuilt, and reran successfully.

Final capture emitted:

- PNG
- `*_cascade_gi.exr`
- `*_pt_full.exr`
- `*_pt_direct.exr`
- `*_gbuffer.exr`
- `*_probe_diag.exr`
- `*_probe_contrib.exr`
- `*_probe_bin.exr`
- `*_probe_stats.json`

## Results

Screen-level Sponza metrics:

| Metric | Value |
|---|---:|
| `ratio_self` | 4.7148 |
| bad pixels | 100.0% |
| contribution/diag luma mean | 1.0000 |
| contribution/diag luma p05 | 1.0000 |
| contribution/diag luma p95 | 1.0000 |

The new shader-side contribution path exactly matches the existing `probe_diag.a` raw-luma diagnostic. This closes the Stage 6 reconstruction mismatch.

Dominant shader cells:

| Shader cell | Count | Mean ratio | Probe share mean | Probe share p95 | Bin share mean | Bin share p95 | Top probe mode share | Top bin mode share |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `(7,5,4)` | 325 | 5.0774 | 0.0945 | 0.1543 | 0.0344 | 0.0564 | 0.3538 | 0.5446 |
| `(6,5,4)` | 234 | 4.4847 | 0.1250 | 0.1660 | 0.0373 | 0.0484 | 0.2692 | 0.7436 |
| `(6,4,4)` | 112 | 4.3106 | 0.0572 | 0.0753 | 0.0579 | 0.0707 | 0.3929 | 1.0000 |

Top keys:

- `(7,5,4)` pixels most often pick top probe `(5,4,3)` and top bin `(2,1)`.
- `(6,5,4)` pixels most often pick top probe `(7,5,4)` and top bin `(2,0)`.
- `(6,4,4)` pixels all pick top bin `(1,2)`, but that bin still averages only 5.79% of luma.

## Interpretation

This is not a hot-bin failure. Even where one top bin is repeated, its contribution share is small.

This is not a strong hot-probe failure either. Top probe mode share is only 26.9-39.3% in the main cells, and mean top-probe luma share is 5.7-12.5%.

The failure shape is broad local atlas energy: many bins and several neighbor probes together produce a locally high indirect field.

## Self-Critique

The Stage 7 summary is still lossy. It records only top contributors and shares, not the full per-pixel bin table. But unlike Stage 6, it is shader-side and internally consistent, so it is strong enough to reject hot-bin/hot-probe as the primary explanation.

The result does not yet identify whether broad local energy comes from:

- SDF/geometry leakage in the cascade bake;
- temporal multi-bounce feedback;
- upper-cascade merge feeding too much energy into C0;
- alpha/visibility normalization bias spread across many bins;
- scene-scale/reference mismatch.

The next phase should therefore avoid a single-bin clamp. It should test energy-source toggles around the same shader-side cells.

## Improved Next Direction

Run a small, local-energy source A/B:

1. Keep Stage 7 diagnostics enabled.
2. Capture Sponza with:
   - multi-bounce on/off;
   - directional merge on/off;
   - probe jitter on/off or temporal off if needed.
3. Compare only the shader-side cells `(7,5,4)`, `(6,5,4)`, `(6,4,4)`:
   - raw luma;
   - ratio;
   - top probe/bin shares.

Decision rule:

- If multi-bounce off collapses raw luma, investigate feedback gain/history.
- If directional merge off collapses raw luma, inspect upper-cascade merge source.
- If none collapse it, inspect bake-side visibility/source energy for the local probes.

## Decision

Proceed with source-energy A/B, not clamp/replacement. The current evidence says the Sponza failure is broad local atlas energy, not one broken directional bin or one isolated probe.
