# M1 Stage 4 Implementation - Final GI Directional A/B

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage4_final_gi_ab_plan.md`.  
**Result artifact:** `tools/v3_m1_final_gi_ab/final_gi_ab_results.json`.  
**Verdict:** `DIRECTIONAL_GI_NOT_PRIMARY`.

## What Changed

1. Added a display-path CLI switch:
   - `--use-directional-gi=1`: current normal-aware final GI sampling.
   - `--use-directional-gi=0`: isotropic `texture(uRadiance, uvw)` final sampling.
   - This intentionally does not rebuild cascades because it only changes final display sampling.

2. Added capture tooling:
   - `tools/v3_m1_final_gi_ab/capture_final_gi_ab.ps1`
   - Captures Cornell and Sponza with `diron` and `diroff`.
   - Each condition preserves PNG, cascade GI EXR, PT full/direct EXRs, GBuffer EXR, and probe stats JSON.

3. Added analyzer tooling:
   - `tools/v3_m1_final_gi_ab/analyze_final_gi_ab.py`
   - Reuses the Stage 3 GBuffer/local-bin contract.
   - Writes full JSON to disk and prints a compact summary to console.

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed. Existing warning baseline remains, including the TinyEXR unreachable-code warning.

Captures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_final_gi_ab/capture_final_gi_ab.ps1 -Scene cornell -N 2048
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_final_gi_ab/capture_final_gi_ab.ps1 -Scene sponza -N 2048
python tools/v3_m1_final_gi_ab/analyze_final_gi_ab.py
```

Result: Cornell and Sponza each produced 12 artifacts: 2 conditions x 6 files.

## Results

### Cornell

| Condition | ratio_self | abs_p95 | bright_pct | dim_pct |
|---|---:|---:|---:|---:|
| `diron` | 0.4922 | 0.8577 | 3.8546 | 86.4200 |
| `diroff` | 0.8927 | 0.7502 | 6.4802 | 41.3085 |

Delta `diroff - diron`:

- `ratio_self`: +0.4005
- `abs_p95`: -0.1075
- `bright_pct`: +2.6257 pp
- `dim_pct`: -45.1115 pp

Cornell is mixed: isotropic final sampling reduces dimming but increases mean ratio and bright pixels.

### Sponza

| Condition | ratio_self | abs_p95 | bright_pct | dim_pct |
|---|---:|---:|---:|---:|
| `diron` | 4.7148 | 4.5279 | 100.0000 | 0.0000 |
| `diroff` | 5.5014 | 5.3551 | 100.0000 | 0.0000 |

Delta `diroff - diron`:

- `ratio_self`: +0.7866
- `abs_p95`: +0.8272
- `bright_pct`: +0.0000 pp
- `dim_pct`: +0.0000 pp

Sponza gets materially worse when final directional GI is disabled.

Local Sponza cluster:

| Condition | Dominant C0 cell | Count | Mean ratio | Bright |
|---|---|---:|---:|---:|
| `diron` | `(28, 15, 12)` | 463 | 4.5475 | 100% |
| `diroff` | `(28, 15, 12)` | 463 | 5.3460 | 100% |

Other local signals:

- `diron` dominant normal: `-z`, 677 pixels, mean ratio 4.7326.
- `diroff` keeps the same dominant C0 cell and depth bucket, but worsens the ratio.
- Depth bucket 1 worsens from 4.7297 to 5.4799.

## Self-Critique

The original Stage 4 suspicion was reasonable but weak: Stage 3 already failed with directional GI enabled, so this could only falsify whether the normal-aware final lookup itself was the harmful part. The result falsifies that. Directional final sampling is not the primary source; it is currently reducing the Sponza error relative to isotropic sampling.

The analyzer still reconstructs C0 cells offline from camera JSON and normalized depth. This is good enough for a localized differential test because both conditions share the same camera/GBuffer contract, but it should not be treated as exact shader-side probe indexing proof.

The A/B does not explain why the `(28, 15, 12)` cluster is over-bright. It only removes one branch from the suspect list.

## Improved Next Direction

Do not revert Stage 3/4 instrumentation. Do not spend more effort copying isolated ShaderToy #3/#6 fragments. Keep the current renderer path and localize the contract mismatch around the Sponza near-depth C0 cluster.

Next phase should inspect the actual sample path for the bad pixels:

1. Dump shader-side final-sampling diagnostics for mode 17:
   - world position;
   - reconstructed C0 cell/probe UVW;
   - selected directional face/bin;
   - sampled atlas value before display composition;
   - normal and depth.

2. Compare shader-side diagnostics to the offline Stage 3 reconstruction:
   - If shader-side C0 cell differs from `(28,15,12)`, the offline reconstruction is misleading and the next target is GBuffer/depth mapping.
   - If shader-side C0 cell matches, inspect the atlas value and directional bin contents for that cell.

3. Add a tight A/B only after the dump:
   - clamp or replace the bad C0 cell/directional bin with neighbor or isotropic probe value;
   - verify whether Sponza cluster improves without Cornell regression.

Self-critique of this next plan:

- It is more invasive than a scalar A/B, but the previous broad toggles have already failed to identify a fix.
- It avoids another large scene matrix and targets the 463-pixel cluster that dominates the Sponza failure.
- The first implementation should be dump-only. A clamp/replacement experiment should come after the shader-side indices are proven.

## Decision

Proceed forward with focused local diagnostics. Do not revert the instrumentation from Stages 2-4. Revert or ignore the attempted #3/#6 ShaderToy-gap closure as a fix path; it is still useful only as a comparison branch, not as the direction to promote.
