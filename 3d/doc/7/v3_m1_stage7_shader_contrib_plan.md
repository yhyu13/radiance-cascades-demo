# M1 Stage 7 Plan - Shader-Side Contribution Diagnostics

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage6_atlas_attribution_impl.md`.  
**Goal:** replace CPU-side approximation with shader-side contribution summaries from the actual `sampleDirectionalGI` path.

## Plan

1. **Extend mode-17 diagnostic sidecars.**
   - Keep existing outputs unchanged.
   - Add `*_probe_contrib.exr`:
     - `rgb`: top contributing trilinear probe normalized by C0 resolution.
     - `a`: top probe share of raw indirect luminance.
   - Add `*_probe_bin.exr`:
     - `r/g`: top directional bin center normalized by `D`.
     - `b`: top bin share of raw indirect luminance.
     - `a`: shader-side reconstructed raw indirect luminance from the contribution path.

2. **Compute diagnostics inside the shader.**
   - Add a contribution-detail variant of the final directional sampler.
   - Use the same atlas samples, alpha gate, cosine weights, and trilinear weights as `sampleDirectionalGI`.
   - Emit diagnostics only for mode 17.

3. **Analyze Sponza bad pixels.**
   - Use Stage 5 mask logic:
     - PT GI > threshold
     - cascade GI > threshold
     - depth > 0
     - ratio > 1.3
   - Group by shader `p000` from `*_probe_diag.exr`.
   - Report:
     - top probe histogram;
     - top bin histogram;
     - top-probe/share distribution;
     - top-bin/share distribution;
     - contribution-luma agreement with `probe_diag.a`.

4. **Decision rule.**
   - If contribution luma matches `probe_diag.a`, use these diagnostics for next fix.
   - If one probe dominates, test a targeted probe clamp/replacement.
   - If one bin dominates, test a targeted bin clamp/replacement.
   - If many probes/bins contribute, inspect bake/merge source energy for the local region.
   - If contribution luma does not match, fix the shader diagnostic before any renderer change.

## Self-Critique and Improvements

- **SC1: This adds more FBO attachments.** Improvement: only enabled through the existing mode-17 EXR capture path; normal rendering ignores the sidecars.
- **SC2: Summary loses full per-bin detail.** Improvement: the summary is enough to choose between hot-probe, hot-bin, and broad-energy paths; if needed, a later SSBO dump can target only the implicated pixels.
- **SC3: Extra shader work duplicates sampling.** Improvement: compute the detailed sampler only in mode 17 diagnostics, not in normal mode 0.
- **SC4: The top-bin metric can hide several medium bins.** Improvement: analyzer reports share distributions, not just the top key.

## Acceptance

- Release build passes.
- Sponza mode-17 capture emits `*_probe_contrib.exr` and `*_probe_bin.exr`.
- Analyzer emits `tools/v3_m1_shader_contrib/shader_contrib_results.json`.
- Implementation doc records whether the failure shape is hot-probe, hot-bin, broad local energy, or diagnostic mismatch.
