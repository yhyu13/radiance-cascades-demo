# M1 Stage 0 Plan - Delta #3/#6 A/B Matrix

**Date:** 2026-05-27.
**Predecessor:** `doc/7/v3_m0_stage1_sponza_ladder_impl.md`.
**Goal:** implement the first Path A A/B switches for Delta #3-redefined and Delta #6, then provide a repeatable capture matrix against the locked M0 baselines.

## Plan

1. **Delta #3 switch.**
   - Current shader already computes `sampleUpperDirWeighted().rgb` as visible-corner-normalized RGB, but live code discards it and only uses `.a` as scalar attenuation.
   - Add `--m1-delta3-gated-trilinear=1`.
   - When ON: consume `WeightedSample.rgb` directly and do not multiply by scalar `aFactor`.
   - When OFF: preserve the M0 baseline path.

2. **Delta #6 switch.**
   - Add `--m1-delta6-geometric-cone=1`.
   - When ON: use a ShaderToy-like permissive cone candidate for WeightedSample visibility.
   - When OFF: preserve current octahedral-bin-width cone.
   - This is an A/B candidate, not a final geometric proof.

3. **Matrix harness.**
   - Add a script that captures the 2x2 matrix:
     - baseline: #3 OFF, #6 OFF
     - delta3: #3 ON, #6 OFF
     - delta6: #3 OFF, #6 ON
     - both: #3 ON, #6 ON
   - Run on Cornell and Sponza at N=2048.
   - Output EXR triplets into `tools/v3_m1_delta36/captures_<scene>/`.

4. **Verification.**
   - Build succeeds.
   - Smoke one low-frame capture with both flags ON.
   - Do not run the full 2x2 N=2048 matrix in this slice unless explicitly queued; the harness makes it reproducible.

## Self-Critique and Improvements

- **SC1: #6 geometric cone is not yet a rigorous volumetric derivation.** Improvement: keep it as an A/B candidate and require a later verdict doc to decide whether it is principled enough to land.
- **SC2: #3 might repeat the previously observed dimming.** Improvement: isolate it behind a flag and compare against M0 via EXR metrics before deciding.
- **SC3: The old `--use-weighted-sample` flag is ambiguous.** Improvement: add explicit M1 flags that imply weighted sampling but distinguish the new behavior from the old scalar-attenuation path.
- **SC4: Full matrix captures are expensive.** Improvement: implement script plus smoke now; leave full N=2048 matrix as a deliberate run.
- **SC5: Baseline recapture can pollute M0.** Improvement: M1 outputs go to a separate `tools/v3_m1_delta36/` tree and never overwrite `tools/v3_baseline/`.

## Acceptance

- New flags compile and default OFF.
- Matrix capture script dry-runs all four conditions.
- Low-frame smoke with both flags ON produces PNG + EXR sidecars.
- Implementation doc records the switches, caveats, and next commands.
