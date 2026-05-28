# M0 Stage 1 Sponza Ladder Plan

**Date:** 2026-05-27.
**Predecessor:** `doc/7/v3_m0_stage1_impl.md`.
**Goal:** complete the Sponza side of M0 Stage 1 by running the default-flag cascade-OFF PT convergence ladder, selecting sign-off N, capturing hybrid-ON at that N, updating `baseline_lock.json`, and writing the implementation record.

## Plan

1. **Add metric analyzer.**
   - Read `_cascade_gi.exr`, `_pt_full.exr`, `_pt_direct.exr`.
   - Compute PT indirect as `max(pt_full - pt_direct, 0)`.
   - Compute `pt_indirect_mean`, `cascade_mean`, `ratio_self`, relative-error percentiles, dim/bright percentages, and valid pixel count.
   - Compute Sponza PT convergence deltas between adjacent N values.

2. **Run Sponza cascade-OFF ladder.**
   - Command: `tools/v3_baseline/sponza_capture.ps1 -UseHybrid 0`.
   - Frame list: 128, 256, 512, 1024, 2048.
   - Default Path A semantics: do not force DM/ST/WS flags.

3. **Analyze and select sign-off N.**
   - `CONVERGED`: highest adjacent PT-mean delta <= 5% at N=2048.
   - `MARGINAL`: delta in (5%, 10%]; use N=2048 unless user explicitly wants N=4096.
   - `PROVISIONAL`: delta > 10%; lock N=2048 as provisional for M1 comparison only.

4. **Run Sponza hybrid-ON at sign-off N.**
   - Command: `tools/v3_baseline/sponza_capture.ps1 -UseHybrid 1 -FrameList <N>`.

5. **Update lock and implementation doc.**
   - Rebuild `baseline_lock.json` with `-SponzaN <N> -SponzaVerdict <verdict>`.
   - Add `doc/7/v3_m0_stage1_sponza_ladder_impl.md`.

## Self-Critique and Improvements

- **SC1: Sponza can be slow or unstable.** Improvement: run the standard ladder first and stop at N=2048; do not silently escalate to N=4096 inside this turn.
- **SC2: PT convergence should use PT indirect, not the displayed PNG.** Improvement: analyzer reads EXR sidecars and computes `pt_full - pt_direct`.
- **SC3: Capture existence is not convergence.** Improvement: verdict is derived only after analyzer output exists.
- **SC4: Hybrid-ON should not influence sign-off N.** Improvement: choose N from cascade-OFF PT convergence, then capture hybrid-ON only at that N.
- **SC5: Missing files must not be ignored.** Improvement: analyzer reports missing stems and the lock builder keeps partial statuses if any expected file is absent.

## Acceptance

- `tools/v3_baseline/analyze_baselines.py` exists and produces JSON.
- Sponza cascade-OFF has the complete 5-point ladder or a documented failure.
- Sponza hybrid-ON exists at sign-off N if the ladder completes.
- `baseline_lock.json` marks Sponza entries complete and records a non-PENDING verdict.
- Implementation doc records commands, verdict, self-critique, and remaining risks.
