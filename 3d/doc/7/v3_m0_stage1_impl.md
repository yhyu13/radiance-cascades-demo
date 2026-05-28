# M0 Stage 1 Implementation - Baseline Capture Execution Slice

**Date:** 2026-05-27.
**Plan:** `doc/7/v3_m0_stage1_execution_plan.md`.
**Status:** Partial Stage 1 implementation complete. Cornell hybrid-ON baseline is complete; Sponza capture ladder is scripted but not run.

## Strategic Changes

1. **Proceed, do not revert.** The correct direction remains ShaderToy adoption Path A. The blocker was measurement plumbing, not proof that the ShaderToy-gap work should be abandoned.
2. **Cherry-pick only infrastructure from `3d_2.0_fail`.** The old branch remains a parts bin, not a merge target. The current slice relies on the EXR/CLI unblock already applied to v3.
3. **Separate script readiness from long GPU execution.** Sponza N=128..2048 is now queueable and dry-run verified, but not hidden inside this scripting/doc patch.

## Implemented Files

| File | Change |
|------|--------|
| `doc/7/v3_m0_stage1_execution_plan.md` | Added updated post-EXR-unblock execution plan with self-critique and narrowed acceptance. |
| `tools/v3_baseline/sponza_capture.ps1` | Added parameterized Sponza capture harness with `-FrameList`, `-UseHybrid`, and `-DryRun`. Default semantics are explicit; DM/ST/WS are not forced. |
| `tools/v3_baseline/build_baseline_lock.ps1` | Added conservative lock builder that records expected files, SHA256 hashes, capture status, old Cornell metrics, and PENDING Sponza sign-off. |
| `tools/v3_baseline/baseline_lock.json` | Generated current lock snapshot. Cornell cascade-OFF reused; Cornell hybrid-ON complete; Sponza entries partial/PENDING. |
| `tools/v3_baseline/captures_cornell_hybon/*` | Re-captured Cornell hybrid-ON at N=2048 and produced PNG + three EXR sidecars. |

## Captures

### Cornell cascade-OFF

Reused from `tools/v20_convergence/captures_cv1_postfix/`.

- Tag: `cv1_cornell_cam0_mbon_g100_hyb0_N2048_m17_postfix`
- Status in lock: `complete`
- Metrics source: `cv1_postfix_results.json`, `pre.2048`
- Key metrics: `ratio_self=0.649922`, `abs_p95=1.277565`, `bright_pct=5.441535`, `dim_pct=28.597142`

### Cornell hybrid-ON

Re-captured after EXR unblock.

- Tag: `v3base_cornell_cam0_mbon_g100_hyb1_N2048_m17`
- Output dir: `tools/v3_baseline/captures_cornell_hybon/`
- Files:
  - `v3base_cornell_cam0_mbon_g100_hyb1_N2048_m17.png`
  - `v3base_cornell_cam0_mbon_g100_hyb1_N2048_m17_cascade_gi.exr`
  - `v3base_cornell_cam0_mbon_g100_hyb1_N2048_m17_pt_full.exr`
  - `v3base_cornell_cam0_mbon_g100_hyb1_N2048_m17_pt_direct.exr`
- Runtime log confirmed `ptSamples=2048`.
- Status in lock: `complete`.

### Sponza

Not run in this slice.

- Cascade-OFF script command: `tools/v3_baseline/sponza_capture.ps1 -UseHybrid 0`
- Hybrid-ON script command: `tools/v3_baseline/sponza_capture.ps1 -UseHybrid 1 -FrameList <signoff N>`
- Current lock status: `partial`
- Current convergence verdict: `PENDING`

## Verification

| Check | Result |
|-------|--------|
| Build after EXR unblock | Passed: `cmake --build build --config Release --target RadianceCascades3D`. Existing MSVC warnings remain. |
| Sponza dry run | Passed: `sponza_capture.ps1 -FrameList 128 -UseHybrid 0 -DryRun` printed expected command. |
| Forced leak-suppression grep | Passed: Sponza dry-run command does not include `--use-directional-merge`, `--use-spatial-trilinear`, or `--use-weighted-sample`. |
| Cornell hybrid capture | Passed: 4 expected files emitted and moved to `captures_cornell_hybon/`. |
| Lock generation | Passed: `build_baseline_lock.ps1` wrote parseable `baseline_lock.json`. |
| Lock status sanity | Passed: Cornell hybrid `complete`, Sponza cascade-OFF `partial`, Sponza verdict `PENDING`. |

## Self-Critique and Improvements

- **SC1: Initial Stage 1 plan overreached for one implementation turn.** Full Sponza ladder plus capture analysis can be long and driver-sensitive. Improvement: this slice delivers scripts, lock scaffolding, and Cornell hybrid completion; Sponza remains explicit queue work.
- **SC2: The previous Cornell hybrid artifact was misleading.** It had only the PNG, because EXR plumbing was missing at the time. Improvement: the capture was rerun after the unblock, and the lock now hashes the complete four-file set.
- **SC3: The Sponza script inherits a risky template.** The source template was leak-suppressed, but Stage 1 needs Default Path A semantics. Improvement: the script header and dry-run verification explicitly prove the forced flags are absent.
- **SC4: `baseline_lock.json` could be mistaken for final Stage 1 sign-off.** Improvement: missing Sponza files are marked `partial`, and the verdict is `PENDING`, so downstream M1 should not treat Sponza as locked.
- **SC5: Metrics for new hybrid captures were not computed here.** The lock records file existence and hashes, but does not fabricate metric numbers. Improvement: keep metric computation as a separate capture-analysis step after the Sponza ladder runs, so the lock can be updated from actual analyzed EXRs.

## Remaining Work

1. Run Sponza cascade-OFF ladder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/sponza_capture.ps1 -UseHybrid 0
```

2. Decide Sponza sign-off N from PT convergence.
3. Run Sponza hybrid-ON at sign-off N:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/sponza_capture.ps1 -UseHybrid 1 -FrameList <N>
```

4. Re-run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_baseline/build_baseline_lock.ps1 -SponzaN <N> -SponzaVerdict <CONVERGED|MARGINAL|PROVISIONAL>
```

5. Add metric analysis for Cornell hybrid and Sponza captures before calling M0 fully closed.

## Handoff

M1 Stage 0 can begin only for Cornell-anchored comparison work. Cross-scene Sponza claims remain blocked until the Sponza ladder and hybrid capture complete and `baseline_lock.json` is updated with a non-PENDING verdict.
