# M1 Stage 5 Implementation - Shader-Side Probe Coordinate Diagnostics

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage5_shader_probe_diag_plan.md`.  
**Result artifact:** `tools/v3_m1_shader_probe_diag/shader_probe_diag_results.json`.  
**Verdict:** `OFFLINE_RECONSTRUCTION_CONTRACT_MISMATCH`.

## What Changed

1. Added a fourth mode-17 FBO attachment:
   - `giProbeDiagTex`
   - shader output `layout(location=3) fragProbeDiag`
   - EXR sidecar: `*_probe_diag.exr`

2. Added shader-side diagnostic encoding:
   - `rgb`: continuous C0 probe-grid coordinate, normalized by `uAtlasVolumeSize`
   - `a`: raw final indirect luminance before destination albedo
   - sky/no-hit pixels remain zero

3. Added tooling:
   - `tools/v3_m1_shader_probe_diag/capture_shader_probe_diag.ps1`
   - `tools/v3_m1_shader_probe_diag/analyze_shader_probe_diag.py`

The existing Stage 3/4 EXR sidecars remain unchanged.

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed with the existing warning baseline.

Captures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_shader_probe_diag/capture_shader_probe_diag.ps1 -Scene cornell -N 2048
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_shader_probe_diag/capture_shader_probe_diag.ps1 -Scene sponza -N 2048
python tools/v3_m1_shader_probe_diag/analyze_shader_probe_diag.py
```

Result: Cornell and Sponza each produced 7 artifacts:

- PNG
- `*_cascade_gi.exr`
- `*_pt_full.exr`
- `*_pt_direct.exr`
- `*_gbuffer.exr`
- `*_probe_diag.exr`
- `*_probe_stats.json`

## Results

Sponza verdict:

```text
OFFLINE_RECONSTRUCTION_CONTRACT_MISMATCH
```

Coordinate agreement on Sponza over-bright pixels:

| Comparison | Mismatch |
|---|---:|
| Stage 3 legacy offline cell `floor(uvw*32)` vs shader cell | 100.0% |
| Centered offline cell `floor(uvw*32 - 0.5)` vs shader cell | 100.0% |

Dominant Stage 3 offline cell:

| Cell | Count | Mean ratio | Mean shader raw luma |
|---|---:|---:|---:|
| `(28, 15, 12)` | 463 | 4.5475 | 0.1174 |

Dominant centered offline cell:

| Cell | Count | Mean ratio | Mean shader raw luma |
|---|---:|---:|---:|
| `(27, 14, 12)` | 395 | 4.9779 | 0.1203 |

Dominant shader-side cells:

| Shader cell | Count | Mean ratio | Mean shader raw luma | Mean fractional pg |
|---|---:|---:|---:|---|
| `(7, 5, 4)` | 325 | 5.0774 | 0.1221 | `[0.0930, 0.3439, 0.6742]` |
| `(6, 5, 4)` | 234 | 4.4847 | 0.1174 | `[0.9620, 0.0948, 0.5918]` |
| `(6, 4, 4)` | 112 | 4.3106 | 0.1148 | `[0.8850, 0.9267, 0.5409]` |

Dominant offline-to-shader pairs:

| Offline cell | Shader cell | Count |
|---|---|---:|
| `(28, 15, 12)` | `(6, 5, 4)` | 233 |
| `(28, 14, 12)` | `(7, 5, 4)` | 176 |
| `(28, 15, 12)` | `(7, 5, 4)` | 119 |
| `(28, 15, 12)` | `(6, 4, 4)` | 110 |

## Self-Critique

The Stage 3 analyzer was useful for proving locality, but its cell identity was not shader-true. Even after correcting for the shader's `-0.5` center alignment, the offline reconstruction still disagrees with the shader on 100% of Sponza over-bright pixels. The likely problem is in the offline camera/depth/world-position reconstruction contract, not in a single `floor(uvw*32)` vs `floor(uvw*32-0.5)` convention.

The new diagnostic does not yet explain why cells `(7,5,4)`, `(6,5,4)`, and `(6,4,4)` are over-bright. It only establishes that these are the shader-side cells to inspect. That is still a major improvement because further per-bin or clamp experiments can now target the real shader path.

The diagnostic stores raw indirect luminance, not the full raw indirect RGB. This is enough for the current coordinate-contract question, but the next per-bin dump should preserve RGB and alpha/visibility contributions.

## Improved Next Direction

Do not target offline cell `(28,15,12)` anymore. Treat it as a stale analysis artifact.

Next phase should dump directional atlas contribution details for shader-side Sponza cells:

- `(7,5,4)`
- `(6,5,4)`
- `(6,4,4)`

Minimum useful dump:

1. For bad pixels, record the eight trilinear neighbor probes around `p000`.
2. For each neighbor, dump all `D x D` bins:
   - atlas RGB;
   - alpha/visibility;
   - cosine weight;
   - final normalized contribution.
3. Summarize whether the over-brightness is:
   - one hot probe;
   - one hot direction bin;
   - alpha renormalization making low-visibility bins too strong;
   - broad local atlas energy.

Self-critique for next phase:

- A per-pixel SSBO dump would be cleaner but more invasive.
- A targeted CPU readback of the C0 atlas around these cells is less invasive and enough for first attribution.
- The next implementation should avoid new visual modes until the numeric atlas contribution summary is available.

## Decision

Proceed forward with shader-side atlas attribution around `(7,5,4)`, `(6,5,4)`, and `(6,4,4)`. Do not revert Stages 2-5 instrumentation. Do not pursue Stage 3's offline `(28,15,12)` cell as the target.
