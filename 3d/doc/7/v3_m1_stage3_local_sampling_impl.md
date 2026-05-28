# M1 Stage 3 Implementation - Local Final-Sampling Audit

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage3_local_sampling_plan.md`.  
**Result artifact:** `tools/v3_m1_local_sampling/local_sampling_results.json`.  
**Verdict:** the Sponza error is highly localized by surface direction, depth, and C0 cell. Next phase should target final directional GI / normal-aware sampling for that local region, not broad cascade-chain brightness.

## What Changed

1. Added RGBA EXR output support.
   - `exrw::save_rgba32f_exr(...)`
   - Used for mode-17 GBuffer sidecars.

2. Extended mode-17 EXR dump.
   - Existing sidecars:
     - `*_cascade_gi.exr`
     - `*_pt_full.exr`
     - `*_pt_direct.exr`
   - New sidecar:
     - `*_gbuffer.exr`
   - GBuffer meaning:
     - RGB = `normal * 0.5 + 0.5`
     - A = normalized ray depth from `raymarch.frag`

3. Added local audit tooling.
   - `tools/v3_m1_local_sampling/capture_local.ps1`
   - `tools/v3_m1_local_sampling/analyze_local.py`

4. Captured Cornell and Sponza at N=2048.
   - Same baseline settings as Stage 2:
     - measurement camera 0;
     - MB ON, gain 1.0;
     - hybrid OFF;
     - scaled directional resolution ON;
     - mode 17;
     - probe-stats JSON enabled.

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed. Existing MSVC warnings remain. TinyEXR also emits existing third-party warning `C4702`.

Captures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_local_sampling/capture_local.ps1 -Scene cornell -N 2048
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_local_sampling/capture_local.ps1 -Scene sponza -N 2048
python tools/v3_m1_local_sampling/analyze_local.py
```

Result:

- Cornell produced 6/6 files.
- Sponza produced 6/6 files after rerun. The first Sponza wrapper pass returned no artifacts; a debug N=256 run proved the path, and rerunning N=2048 succeeded.
- `local_sampling_results.json` is parseable.

## Results

Screen metrics with GBuffer-valid mask:

| Scene | valid | ratio_self | abs_p95 | bright_pct | dim_pct |
|---|---:|---:|---:|---:|---:|
| Cornell | 37047 | 0.4922 | 0.8577 | 3.85 | 86.42 |
| Sponza | 693 | 4.7148 | 4.5279 | 100.00 | 0.00 |

Sponza local summary:

| Bin family | Dominant bin | Count | Mean ratio | Bright % |
|---|---:|---:|---:|---:|
| normal axis | `-z` | 677 / 693 | 4.7326 | 100.00 |
| depth decile | `1` | 679 / 693 | 4.7297 | 100.00 |
| C0 cell by count | `(28,15,12)` | 463 / 693 | 4.5475 | 100.00 |
| screen tile by count | `(3,4)` | 264 / 693 | 4.4593 | 100.00 |

Worst Sponza C0 cells by absolute error include:

| C0 cell | Count | Mean ratio | Bright % |
|---|---:|---:|---:|
| `(27,14,12)` | 4 | 5.6526 | 100.00 |
| `(27,15,12)` | 29 | 5.2276 | 100.00 |
| `(28,14,12)` | 176 | 5.1578 | 100.00 |
| `(28,15,12)` | 463 | 4.5475 | 100.00 |

## Interpretation

This is no longer a broad "Sponza is generally brighter" signal. The valid error mask is small and concentrated:

- 97.7% of valid Sponza pixels are `-z` normal-facing.
- 98.0% are in depth decile `1`.
- 66.8% land in one reconstructed C0 cell: `(28,15,12)`.
- Every Sponza valid bin is over-bright; no dim counter-signal exists in this mask.

That points away from:

- broad upper-cascade energy scaling;
- global merge brightness constants;
- scene-wide probe chain over-energy.

It points toward:

- final directional GI / normal-aware integration for `-z` surfaces;
- atlas/world mapping at the reconstructed Sponza C0 cell cluster;
- PT-reference mask behavior for shallow-depth, low-PT-indirect pixels.

## Self-Critique

1. **World reconstruction is approximate.**
   - It uses measurement camera JSON, assumed 60-degree vertical FOV, and shader-equivalent ray-box reconstruction.
   - Good enough to bin by coarse C0 cell, not proof of exact sub-voxel position.

2. **GBuffer is downsampled by 2x center selection.**
   - This avoids averaging normals/depth across edges, but can miss edge fragments.
   - The Sponza signal is strong enough that this does not change the main conclusion.

3. **Sponza mask is still narrow.**
   - 693 pixels is not a final tuning set.
   - It is enough to identify a concentrated failure region.

4. **The first Sponza full wrapper pass was artifact-empty.**
   - Debug N=256 succeeded, then N=2048 rerun succeeded.
   - Treat this as a harness hiccup, not a renderer conclusion.

## Improved Next Direction

Do not proceed with another global ShaderToy fragment or scalar normalization.

Next phase should run a targeted final-GI A/B only on the mode-17 final sampling contract:

1. Compare isotropic `texture(uRadiance, uvw)` vs directional `sampleDirectionalGI(pos, normal).irrad` in mode 17 sidecars.
2. Add a `--mode17-directional-gi=1` or reuse `--use-directional-gi` if already CLI-controllable.
3. Re-run only Sponza and Cornell N=2048.
4. Accept the direction only if Sponza `-z`/cell `(28,15,12)` ratio drops without making Cornell dimmer.

If directional final GI is already active for the capture, then the next audit should dump per-bin directional GI components for cell `(28,15,12)` and inspect whether the `-z` normal hemisphere is integrating too much back-facing or same-surface radiance.
