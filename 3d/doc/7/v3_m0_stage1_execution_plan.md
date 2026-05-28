# M0 Stage 1 Execution Plan - EXR-Unblocked Baseline Capture

**Date:** 2026-05-27.
**Parent plan:** `doc/7/v3_m0_stage1_plan.md`.
**Trigger:** Stage 1 was blocked because the current v3 branch accepted the capture flags but did not emit mode-17 EXR sidecars. The unblock patch restored only the measurement plumbing from `3d_2.0_fail`, not the failed MBRC v2.x diagnostic line.

## Decision

Proceed with the ShaderToy-adoption direction. Do not revert the ShaderToy-gap work, and do not merge `3d_2.0_fail`. Treat that branch as a parts bin for measurement infrastructure only.

## Scope

In scope for this execution slice:
- Add a Sponza capture script that mirrors the Cornell v3 baseline flags and strips the leak-suppression forcing from `cv1_capture_leaksupp.ps1`.
- Add a baseline-lock builder that records file hashes and current capture availability without pretending missing captures are complete.
- Re-run the Cornell hybrid-ON capture now that EXR sidecars work.
- Dump an implementation record with verification and self-critique.

Out of scope for this slice:
- Full Sponza N=128..2048 ladder execution. The script must be ready, but the long GPU run can be queued separately.
- M1 delta implementation.
- Any further RC shader algorithm changes.

## Steps

1. **Script fork:** create `tools/v3_baseline/sponza_capture.ps1`.
   - Parameterize `-FrameList`, `-UseHybrid`, and `-DryRun`.
   - Use `--load-obj=sponza`, `tools/v20_pre_measurement/sponza_cam.json`, `--measurement-camera=0`.
   - Preserve MB-ON, `multi-bounce-gain=1.0`, mode 17, `--screenshot-exr=1`, scaled-D, seed offset 0, probe jitter 1.
   - Do not force `--use-directional-merge`, `--use-spatial-trilinear`, or `--use-weighted-sample`; engine defaults govern.

2. **Lock builder:** create `tools/v3_baseline/build_baseline_lock.ps1`.
   - Read existing Cornell cascade-OFF metrics from `tools/v20_convergence/captures_cv1_postfix/cv1_postfix_results.json`.
   - Record all expected capture file paths and SHA256 hashes when present.
   - Mark missing entries as `status: missing`, not as pass/fail.

3. **Cornell hybrid rerun:** run `tools/v3_baseline/cornell_hybon_capture.ps1`.
   - Expected files: `.png`, `_cascade_gi.exr`, `_pt_full.exr`, `_pt_direct.exr`.
   - Existing PNG-only output must be overwritten by the complete set.

4. **Verification:**
   - Build must pass.
   - `sponza_capture.ps1 -DryRun -FrameList 128 -UseHybrid 0` must print the expected command without forced leak-suppression flags.
   - `build_baseline_lock.ps1` must emit parseable JSON.

5. **Impl doc:** write `doc/7/v3_m0_stage1_impl.md`.

## Self-Critique and Improvements

- **SC1: Original Stage 1 plan became stale after the unblock.** It said engine edits were out of scope, but the measured branch lacked EXR plumbing. Improvement: this execution plan explicitly records that the engine patch is already done and narrows this slice to scripts plus Cornell hybrid rerun.
- **SC2: Full Sponza ladder is too large to hide inside a scripting patch.** Improvement: script readiness and dry-run verification are acceptance for this slice; full Sponza captures remain an explicit next queue item.
- **SC3: A baseline lock can become misleading if it omits missing files.** Improvement: the lock builder records `present` or `missing` per file and uses `status: partial` for incomplete captures.
- **SC4: The leak-suppression template is dangerous as a fork source.** Improvement: the Sponza script header and dry-run check explicitly state that DM/ST/WS are not forced.
- **SC5: Capture metrics are not identical to capture existence.** Improvement: this slice hashes files and records old Cornell metrics, but does not invent new metric numbers for captures that have not been analyzed yet.

## Acceptance

This slice is complete when:
- `sponza_capture.ps1` and `build_baseline_lock.ps1` exist and pass dry-run/basic execution.
- Cornell hybrid-ON has the complete 4-file set in `tools/v3_baseline/captures_cornell_hybon/`.
- `baseline_lock.json` exists and is parseable, even if Sponza entries remain marked missing.
- `v3_m0_stage1_impl.md` records what is complete and what remains queued.
