# M1 Stage 6 Plan - Targeted Atlas Attribution

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage5_shader_probe_diag_impl.md`.  
**Goal:** attribute the Sponza over-bright pixels to raw C0 atlas data around the shader-side cells `(7,5,4)`, `(6,5,4)`, and `(6,4,4)`.

## Plan

1. **Add targeted atlas JSON readback.**
   - Add `--atlas-attribution-json=PATH`.
   - Add `--atlas-attribution-cells=x,y,z;x,y,z;...`.
   - If no cells are supplied, default to the Stage 5 Sponza shader-side cells:
     - `(7,5,4)`
     - `(6,5,4)`
     - `(6,4,4)`

2. **Dump the exact atlas texels needed by final sampling.**
   - For each target `p000`, dump its eight trilinear neighbor probes.
   - For each neighbor probe, dump every `D x D` bin:
     - RGB
     - alpha
     - luminance
     - bin direction
   - Keep this as JSON, not EXR, because the next question is numeric attribution.

3. **Analyze contribution using Stage 5 bad-pixel stats.**
   - Load `shader_probe_diag_results.json`.
   - Use each shader cell's mean fractional `pg` for trilinear weights.
   - Use normal `-z` first, because earlier GBuffer analysis showed Sponza bad pixels are dominated by `-z` surfaces.
   - Reproduce `sampleProbeDir` math:
     - `wcos = max(0, dot(binDir, normal))`
     - `w = wcos * alpha`
     - normalized irradiance = `sum(rgb*w) / max(sum(w), 1e-4)`

4. **Classify the failure shape.**
   - One hot probe: one neighbor dominates trilinear-weighted luma.
   - One hot bin: one bin dominates weighted contribution.
   - Alpha-renormalization risk: `sum(w)` is small while normalized irradiance is high.
   - Broad local energy: many probes/bins contribute similarly.

## Self-Critique and Improvements

- **SC1: This is not per-pixel SSBO truth.** It uses Stage 5 mean fractional coordinates and a representative normal. Improvement: keep it as attribution, not final proof; if a single bin is implicated, follow with a shader-side per-pixel dump or clamp.
- **SC2: Normal may vary inside a shader cell.** Improvement: start with `-z` because it dominated the earlier GBuffer local analysis; the analyzer can be extended to multiple normals if needed.
- **SC3: Reading the full C0 atlas is heavier than reading only cells.** Improvement: read once with `glGetTexImage`, then serialize only the requested probes and bins.
- **SC4: This could expose broad energy rather than a fix.** Improvement: that is still useful; broad energy means the next target is bake/merge source energy, not final sampling.

## Acceptance

- Release build passes.
- Sponza capture emits `*_atlas_attribution.json`.
- Analyzer emits `tools/v3_m1_atlas_attribution/atlas_attribution_results.json`.
- Implementation doc records whether the failure is hot-probe, hot-bin, alpha-renormalization, or broad local energy.
