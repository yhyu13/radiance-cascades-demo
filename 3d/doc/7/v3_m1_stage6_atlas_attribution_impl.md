# M1 Stage 6 Implementation - Targeted Atlas Attribution

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage6_atlas_attribution_plan.md`.  
**Result artifact:** `tools/v3_m1_atlas_attribution/atlas_attribution_results.json`.  
**Verdict:** `ATTRIBUTION_RECONSTRUCTION_MISMATCH`.

## What Changed

1. Added targeted C0 atlas JSON readback:
   - `--atlas-attribution-json=PATH`
   - `--atlas-attribution-cells=x,y,z|x,y,z|...`
   - default Stage 5 target cells remain `(7,5,4)`, `(6,5,4)`, `(6,4,4)`.

2. Added `Demo3D::dumpAtlasAttributionJson()`.
   - Reads the same C0 atlas handle used by display when temporal accumulation is active: `probeAtlasHistory`.
   - Dumps each target cell's eight trilinear neighbor probes.
   - Dumps all `D x D` bins per neighbor:
     - RGB
     - alpha
     - luminance
     - bin direction

3. Added tooling:
   - `tools/v3_m1_atlas_attribution/capture_atlas_attribution.ps1`
   - `tools/v3_m1_atlas_attribution/analyze_atlas_attribution.py`

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed with the existing warning baseline.

Capture and analysis:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_atlas_attribution/capture_atlas_attribution.ps1 -Scene sponza -N 2048
python tools/v3_m1_atlas_attribution/analyze_atlas_attribution.py
```

The capture emitted the normal mode-17 sidecars plus:

- `m1atlas_sponza_baseline_N2048_m17_atlas_attribution.json`
- `tools/v3_m1_atlas_attribution/atlas_attribution_results.json`

## Results

The analyzer intentionally fails closed:

```text
ATTRIBUTION_RECONSTRUCTION_MISMATCH
```

The best CPU-side reconstruction still reaches only about half of the shader-side luminance recorded in Stage 5.

| Shader cell | Stage 5 shader luma | Best reconstructed luma | Reconstruction ratio | Shape if trusted |
|---|---:|---:|---:|---|
| `(7,5,4)` | 0.1221 | 0.0589 | 0.4826 | broad local energy |
| `(6,5,4)` | 0.1174 | 0.0589 | 0.5020 | broad local energy |
| `(6,4,4)` | 0.1148 | 0.0587 | 0.5113 | broad local energy |

The broad-local-energy classification is not promoted as final because the reconstruction mismatch is too large.

Measured bad-pixel normals from the Stage 6 GBuffer were:

| Shader cell | Count | Mean ratio | Mean normal |
|---|---:|---:|---|
| `(7,5,4)` | 325 | 5.0774 | `[-0.3292, -0.5878, -0.7391]` |
| `(6,5,4)` | 234 | 4.4847 | `[-0.3179, -0.5902, -0.7420]` |
| `(6,4,4)` | 112 | 4.3106 | `[-0.4202, -0.5014, -0.7563]` |

The analyzer also tested six axis normals. The closest simple axis normal was `+y`, but it still reconstructed only roughly half of Stage 5's shader-side luma.

## Self-Critique

The plan assumed CPU readback plus reconstructed final-sampling math would be enough. That was too optimistic. It caught useful issues:

- The first implementation parsed only two cells because the CLI substring length for `--atlas-attribution-cells=` was wrong. Fixed.
- The first atlas readback used the wrong atlas handle under temporal accumulation. Fixed to match display's `probeAtlasHistory`.
- The old `-z` normal assumption was stale. The analyzer now computes bad-pixel mean normals from the Stage 6 GBuffer and also checks axis-normal candidates.

Even after those fixes, CPU reconstruction is still not close enough to the shader diagnostic. Possible reasons:

- The per-cell mean normal/fraction is not representative enough for nonlinear normalized atlas sampling.
- The GBuffer/downsample grouping is still not exactly the shader's per-pixel inputs.
- There may be another display-path detail not mirrored by the CPU analyzer.

Because the mismatch is about 2x, any hot-probe/hot-bin conclusion would be overclaimed.

## Improved Next Direction

Stop approximating `sampleDirectionalGI` on the CPU. Add shader-side contribution diagnostics for mode 17.

Minimum next implementation:

1. Add extra diagnostic outputs or a tiny SSBO for selected pixels/cells.
2. In `sampleDirectionalGI`, emit the actual values used by the shader:
   - final `pg`, `p000`, `f`
   - normal
   - per-neighbor trilinear weight
   - per-neighbor `wsum`
   - per-neighbor normalized irradiance
   - top contributing bin per neighbor
3. Capture only pixels whose shader `p000` is one of:
   - `(7,5,4)`
   - `(6,5,4)`
   - `(6,4,4)`

This is more invasive than CPU readback, but Stage 6 proves the CPU approximation is not reliable enough for the next decision.

## Decision

Keep the atlas JSON readback tooling as a support artifact, but do not use it as final attribution. Proceed to shader-side contribution diagnostics before attempting any clamp, replacement, or bake/merge change.
