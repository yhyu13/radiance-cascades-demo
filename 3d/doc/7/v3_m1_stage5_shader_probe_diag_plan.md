# M1 Stage 5 Plan - Shader-Side Probe Coordinate Diagnostics

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage4_final_gi_ab_impl.md`.  
**Goal:** verify whether the Stage 3 offline reconstruction of the Sponza failure cluster matches the shader's actual final-sampling coordinates, before adding any clamp/replacement experiment.

## Plan

1. **Add a mode-17 diagnostic render target.**
   - Extend the GI capture FBO with a fourth RGBA texture.
   - In `raymarch.frag`, write shader-side final-sampling diagnostics for every surface pixel.
   - Keep the existing mode-17 outputs unchanged:
     - cascade GI;
     - PT full/direct;
     - GBuffer normal/depth.

2. **Diagnostic encoding.**
   - `R/G/B`: continuous C0 probe-grid coordinate normalized to `[0,1]`.
   - `A`: raw final indirect luminance before destination albedo.
   - Sky/no-hit pixels write zero.

3. **Capture Sponza and Cornell baselines.**
   - Use the same Stage 3/4 capture settings.
   - Save a new `*_probe_diag.exr` sidecar.

4. **Analyze shader-side vs offline coordinates.**
   - Decode shader `pg = diag.rgb * atlasVolumeSize`.
   - Compute shader `p000 = floor(pg)` and `f = fract(pg)`.
   - Compare shader `p000` to the offline C0 cell bins from Stage 3.
   - On the Sponza bad pixels, report:
     - dominant shader `p000`;
     - dominant offline C0 cell;
     - mismatch rate;
     - raw indirect luminance for the dominant shader cell.

5. **Decision rule.**
   - If shader `p000` disagrees with offline `(28,15,12)`, fix the analysis/reconstruction contract before touching renderer logic.
   - If shader `p000` agrees, next phase should dump per-probe/per-bin contributions around that cell.
   - If the cluster is spread across many shader cells, the failure is not a single-cell atlas outlier and needs a broader local sampling audit.

## Self-Critique and Improvements

- **SC1: There is no single selected directional bin.** `sampleDirectionalGI` integrates all `D x D` bins over up to eight trilinear probes. Improvement: Stage 5 dumps exact continuous probe coordinates and raw indirect luminance first; per-bin contribution dumps come only if the coordinate contract is proven.
- **SC2: Normalized probe coordinate loses tiny precision.** RGBA32F EXR preserves enough precision for `32^3` C0 indexing, and the analysis only needs cell/fraction bins.
- **SC3: Adding another FBO attachment can affect capture plumbing.** Improvement: keep existing attachments and file names intact; add only one optional sidecar.
- **SC4: This still does not prove the atlas value is wrong.** Improvement: the next phase is explicitly per-bin contribution dump or local replacement, not a broad matrix.

## Acceptance

- Release build passes.
- Mode-17 EXR capture emits `*_probe_diag.exr` in addition to existing sidecars.
- Analyzer emits `tools/v3_m1_shader_probe_diag/shader_probe_diag_results.json`.
- Implementation doc records whether the shader-side coordinate matches the Stage 3 offline `(28,15,12)` cluster.
