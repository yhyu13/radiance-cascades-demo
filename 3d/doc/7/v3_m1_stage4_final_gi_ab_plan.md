# M1 Stage 4 Plan - Final GI Directional A/B

**Date:** 2026-05-27.  
**Predecessor:** `doc/7/v3_m1_stage3_local_sampling_impl.md`.  
**Goal:** determine whether final normal-aware directional GI sampling is helping or causing the localized Sponza `-z` over-bright cluster by comparing mode-17 directional ON vs isotropic OFF under the same capture/analyzer pipeline.

## Plan

1. **Expose final GI sampling as CLI.**
   - Add `--use-directional-gi=0|1`.
   - This is display-path only and should not trigger cascade rebuild.
   - Default remains unchanged.

2. **Capture a 2x2 A/B.**
   - Scenes: Cornell, Sponza.
   - Conditions:
     - `diron`: current default final sampling.
     - `diroff`: isotropic `texture(uRadiance, uvw)` final sampling.
   - Use the Stage 3 local sidecars: cascade GI, PT full/direct, GBuffer, probe stats.

3. **Analyze with the Stage 3 local bins.**
   - Compare screen metrics.
   - Compare Sponza local summary:
     - dominant normal `-z`;
     - dominant depth bucket;
     - dominant C0 cell `(28,15,12)` cluster.

4. **Decision rule.**
   - If `diroff` reduces Sponza local ratio materially and does not worsen Cornell: investigate `sampleDirectionalGI`.
   - If `diron` is better than `diroff`: directional final GI is not the cause; dump per-bin directional components next.
   - If both are bad: investigate PT-mask/local reference or atlas/world mapping around the C0 cell cluster.

## Self-Critique and Improvements

- **SC1: Directional GI is already default ON.** Improvement: this phase is an A/B/falsification step, not a proposed fix.
- **SC2: Isotropic OFF may be visibly worse while still improving the local ratio.** Improvement: evaluate both Cornell global regression and Sponza local cluster metrics.
- **SC3: Capture cost is nontrivial.** Improvement: only two conditions per scene, no #3/#6 matrix.
- **SC4: A/B still will not identify which directional bins are wrong.** Improvement: if ON is implicated, next phase dumps directional bin contributions for the localized C0 cluster.

## Acceptance

- Release build passes.
- Cornell and Sponza each produce 12 artifacts: 2 conditions x 6 files.
- Analyzer emits `tools/v3_m1_final_gi_ab/final_gi_ab_results.json`.
- Implementation doc records the verdict and next target.
