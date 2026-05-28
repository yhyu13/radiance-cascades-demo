# M1 Stage 3 Plan - Local Final-Sampling Audit

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage2_probe_contract_impl.md`.  
**Goal:** localize the Sponza cascade/PT indirect mismatch by exporting mode-17 GBuffer data, reconstructing hit positions offline, and binning error by screen tile, C0 probe cell, depth, and normal direction.

## Plan

1. **Export the mode-17 GBuffer sidecar.**
   - Extend the EXR writer with RGBA output.
   - Dump `*_gbuffer.exr` beside `*_cascade_gi.exr`, `*_pt_full.exr`, and `*_pt_direct.exr`.
   - Use the existing `giGBufferTex` attachment: RGB = normal*0.5+0.5, A = normalized ray depth.

2. **Add a local-sampling capture wrapper.**
   - Reuse M0/M1 capture settings:
     - measurement camera 0;
     - MB ON gain 1.0;
     - hybrid OFF;
     - scaled directional resolution ON;
     - mode 17;
     - N=2048.
   - Capture Cornell and Sponza into `tools/v3_m1_local_sampling/`.

3. **Add an offline locality analyzer.**
   - Read cascade GI, PT full/direct, and GBuffer EXRs.
   - Reconstruct hit position from measurement camera, FOV 60, volume bounds [-2,2], and GBuffer depth.
   - Bin valid pixels by:
     - 16x9 screen tiles;
     - C0 probe cell;
     - normalized depth bucket;
     - dominant normal axis.
   - Report the worst bright/dim bins and top Sponza C0 cells.

4. **Use results to steer the next algorithm candidate.**
   - If Sponza error is concentrated in a few probe cells: inspect final sampling/atlas mapping there.
   - If error tracks normal axis: inspect isotropic-vs-directional final GI integration.
   - If error tracks depth/screen tile: inspect camera/raymarch/PT reference mismatch.

## Self-Critique and Improvements

- **SC1: Reconstructed world positions depend on camera assumptions.** Improvement: use the same measurement camera JSON and default 60-degree FOV; document this as an offline approximation until the shader exports exact world position.
- **SC2: GBuffer depth is normalized, not raw ray distance.** Improvement: reconstruct `tNear/tFar` with the same ray-box math as `raymarch.frag`.
- **SC3: Downsampling GBuffer to PT resolution can blur edges.** Improvement: use nearest/center-pixel selection per 2x2 block for position bins, while using existing averaged cascade GI for radiance metrics.
- **SC4: Binning gives attribution, not a fix.** Improvement: final doc will only choose the next candidate family, not claim the root cause is proven.

## Acceptance

- Release build passes.
- Local-sampling capture produces 6 files per scene:
  - PNG;
  - cascade GI EXR;
  - PT full EXR;
  - PT direct EXR;
  - GBuffer EXR;
  - probe-stats JSON.
- Analyzer emits `tools/v3_m1_local_sampling/local_sampling_results.json`.
- Implementation doc records concentrated bins and the next direction.
