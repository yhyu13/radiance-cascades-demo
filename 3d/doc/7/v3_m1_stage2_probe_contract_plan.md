# M1 Stage 2 Plan - Probe Contract Audit

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage1_delta36_matrix_impl.md`.  
**Goal:** add a low-risk diagnostic export that records per-cascade energy and hit coverage during the same headless captures used by the EXR baseline, then use it to decide where the next algorithm candidate should focus.

## Plan

1. **Add a headless probe-stats export.**
   - Add `--probe-stats-json=PATH`.
   - Dump the already-computed per-cascade readback stats on the clean screenshot exit frame.
   - Do not alter shader output, cascade scheduling, or default UI behavior.

2. **Add a small capture wrapper.**
   - Capture Cornell and Sponza cascade-only baseline at N=2048.
   - Keep the same camera/MB/scaled-D/jitter settings as M0/M1.
   - Store PNG, mode-17 EXR sidecars, and probe-stats JSON in `tools/v3_m1_probe_contract/`.

3. **Add a contract analyzer.**
   - Read probe-stats JSON and the existing mode-17 EXR sidecars.
   - Report:
     - screen-space cascade/PT indirect error;
     - cascade mean-luminance chain;
     - surf/sky/any coverage per cascade;
     - simple ratios between adjacent cascades.

4. **Use the result to choose the next direction.**
   - If upper cascades are already over-energetic before C0: prioritize merge-energy/interval normalization.
   - If upper cascades are reasonable but screen output is wrong: prioritize raymarch atlas/world mapping or final sampling.
   - If hit coverage is sparse or sky-dominated: prioritize probe placement/visibility topology.

## Self-Critique and Improvements

- **SC1: Per-cascade averages are coarse.** They can hide local outliers. Improvement: pair them with existing EXR screen metrics and keep the conclusion as "next direction", not proof of final root cause.
- **SC2: Readback stats depend on the current probe-readback cadence.** With jitter enabled, readback is rate-limited. Improvement: dump only on long N=2048 captures where the readback has enough time to refresh.
- **SC3: The export can be mistaken for a renderer change.** Improvement: make it CLI-only and call out that it does not affect shaders or defaults.
- **SC4: Sponza valid mask is narrow.** Improvement: use Sponza as a veto/stress case and Cornell as the regression case, matching M1 Stage 1.

## Acceptance

- `cmake --build build --config Release --target RadianceCascades3D` succeeds after the CLI addition.
- One Cornell and one Sponza contract capture produce:
  - PNG
  - `*_cascade_gi.exr`
  - `*_pt_full.exr`
  - `*_pt_direct.exr`
  - `*_probe_stats.json`
- Analyzer emits `tools/v3_m1_probe_contract/probe_contract_results.json`.
- Implementation doc records the measured direction and caveats.
