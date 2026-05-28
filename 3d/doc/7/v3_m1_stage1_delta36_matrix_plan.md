# M1 Stage 1 Plan - Delta #3/#6 Matrix Run

**Date:** 2026-05-27.
**Predecessor:** `doc/7/v3_m1_stage0_delta36_impl.md`.
**Goal:** run the full Cornell/Sponza N=2048 2x2 matrix for Delta #3 and #6, analyze against `tools/v3_baseline/baseline_lock.json`, assign a first STRONG/MARGINAL/DEAD verdict, and document whether the flags should proceed.

## Plan

1. **Add matrix analyzer.**
   - Reuse the EXR metric method from `tools/v3_baseline/analyze_baselines.py`.
   - Read matrix captures from `tools/v3_m1_delta36/captures_cornell/` and `captures_sponza/`.
   - Compare each condition against the locked M0 cascade-OFF metrics:
     - ratio shift
     - `abs_p95` drop
     - bright percentage drop
     - dim percentage regression
   - Emit `tools/v3_m1_delta36/matrix_results.json`.

2. **Run full matrix.**
   - Cornell: `capture_matrix.ps1 -Scene cornell -N 2048`.
   - Sponza: `capture_matrix.ps1 -Scene sponza -N 2048`.
   - Conditions: baseline, delta3, delta6, both.

3. **Assign verdict.**
   - STRONG if both scenes meet: ratio shift >= 0.05, `abs_p95` drops >= 30%, bright drops >= 3 pp, dim not worse by >2 pp.
   - MARGINAL if either scene improves materially and the other does not regress.
   - DEAD otherwise, or if Sponza visually/metric-regresses.

4. **Self-critique and doc.**
   - Document narrow masks, scene split, any matrix condition instability, and whether #6 standalone should be dropped per the scope rule.

## Self-Critique and Improvements

- **SC1: Full N=2048 matrix is expensive.** It is now justified because M0 is locked and the flags smoke-run. Improvement: run both scenes once, no extra exploratory variants.
- **SC2: Sponza valid mask is narrow.** Improvement: analyzer records valid count and verdict doc treats Sponza as a veto, not a whole-scene average.
- **SC3: Baseline condition in the matrix may not exactly match M0 hashes.** Improvement: verdict compares metrics to M0 lock, but also reports matrix-baseline drift separately if present.
- **SC4: #6 can look good only because #3 changed the numerator.** Improvement: analyzer reports `delta6` standalone and `both`; #6 can only proceed if standalone or amplification is defensible.
- **SC5: Metrics alone can miss visual regressions.** Improvement: require output PNGs to exist and document that visual review remains a follow-up before landing defaults.

## Acceptance

- Cornell and Sponza matrix folders each contain 16 files (4 conditions x PNG+3 EXR).
- `matrix_results.json` exists and is parseable.
- Implementation doc records verdict, caveats, and next action.
