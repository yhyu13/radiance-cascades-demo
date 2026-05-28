# v4 Phase 3 — Implementation Summary

**Date:** 2026-05-28T17:50+08:00
**Plan:** `doc/8_shadertoy/v4_phase3_plan.md`
**Status:** Phase 3 IN PROGRESS (3C complete; 3A in background; 3B pending capture completion)

---

## Completed Steps

| Step | What | Result |
|------|------|--------|
| 3C | Write v4 closeout report | `v4_closeout_report.md` written (~150 lines, 8 sections) |
| 3A | Queue Sponza pscene capture | Running in background (bgp_e6dff6afd001zYC5L5qAuiRC9K, PID 51864) |
| 3B | Verify metrics (pending) | Will run `analyze_baselines.py` after capture completes |
| 3D | Update lock (pending) | Will update `sponza_cam0_cascade_off_g010_pscene` status after 3B |

---

## Step 3C — Closeout Report

**File:** `doc/8_shadertoy/v4_closeout_report.md`

The report covers:

| Section | Content |
|---------|---------|
| 1. Goal vs Outcome | Original v3 "retire hybrid" goal NOT achieved. Scene-specific fixes shipped instead. |
| 2. What Failed | v2.x 31 commits (all DEAD/MARGINAL), v3 M1 delta port (DEAD on all conditions) |
| 3. What Works | Sponza gain=0.10 (\|p95\|=0.25), Cornell directional (ratio=0.93), hybrid (ratio=0.83) |
| 4. What Was Shipped | Phase 1A (per-scene preset, +32 lines), Phase 2B (flag removal, -42 lines), documentation |
| 5. What Remains Open | Cornell constraint, Path B decision, valid mask, analyzer threshold |
| 6. Path B Decision Tree | Go/No-Go factors with recommendation: do NOT proceed unless blocking issue |
| 7. Volumetric Constraint | Final formulation: not a bug — a sampling limitation. Fix requires Path B or hybrid. |
| 8. Cross-References | Links to all program docs, predecessor programs, and reference shader |

---

## Self-Critique

### SC-3I.1: The closeout report admits failure of the original goal

The v3 scope doc goal was "retire hybrid." The v4 closeout report §1 states: "The original goal of 'retire hybrid' was NOT achieved." This is honest but stark. The report balances this by documenting what WAS achieved: Sponza cascade-only works, Cornell constraint is understood and bounded, the per-scene preset is shipped.

**Improvement:** Added §6 (Path B decision tree) and §7 (volumetric constraint) to provide the reader with a clear path forward — the program didn't fail, it just arrived at a different answer than expected.

### SC-3I.2: The closeout report references Stage 9/10/11d metrics but those are from pre-Phase-2B builds

Same issue as SC-2I.3 from Phase 2. The metrics in §3 (Sponza gain=0.10, Cornell directional, Cornell hybrid) are from Stage 8-11d captures that used pre-Phase-2B binaries. Since Phase 2B is dead-code removal only, behavioral equivalence is assumed. The Phase 3A capture (queued) will confirm Sponza metrics match. Cornell directional and hybrid captures are not re-run in Phase 3 (they would require `--light-direction=0,-1,0` and `--use-hybrid=1` respectively — both CLI flags are unchanged by Phase 2B).

**Decision:** Cornell metrics in the report are annotated with their source stage. If the Phase 3A Sponza capture confirms behavioral equivalence, the Cornell metrics are also valid by transitive assumption.

### SC-3I.3: The capture command uses `--mb-gain-per-scene` not `--multi-bounce-gain=0.10`

This is the key verification point. Stage 9 set gain explicitly (`--multi-bounce-gain=0.10`). Phase 3A sets gain via the per-scene preset (`--mb-gain-per-scene`). If the metrics match, it proves the preset code path (post-load hook in main3d.cpp:789-803) produces the same gain as the explicit CLI path.

### SC-3I.4: The capture EXR filenames follow the non-standard `v4_phase3_` prefix

Stage 9 and M0 captures use the `v3base_` prefix pattern. Phase 3 uses `v4_phase3_` to distinguish this as a v4 verification capture. The analyzer (`analyze_baselines.py:113`) builds the stem from `--scene` + `--hybrid` + `--n`, so it expects `v3base_sponza_cam0_mbon_g100_hyb0_N2048_m17`. The Phase 3 capture uses a different screenshot path (`v4_phase3_sponza_pscene.png`), so the EXR sidecars will be `v4_phase3_sponza_pscene_cascade_gi.exr` etc.

**The analyzer will NOT find these files** because it hardcodes the `v3base_` prefix. The metric verification (Step 3B) needs to either:
- Modify the analyzer to accept arbitrary stems, OR
- Manually compute metrics from the EXRs, OR
- Use a separate comparison script

**Correction (SC-P3.1 partial acceptance):** The standalone `analyze_baselines.py` compares `cascade_gi.exr` vs `pt_full.exr` using `pt_lum > 0.05` mask and ratio_self. I can write a minimal verification script that reads the two EXRs and computes the same metrics, or I can move the files to match the `v3base_` naming convention.

**Simplest fix:** After capture completes, rename the EXRs to the `v3base_` pattern the analyzer expects, then run the analyzer.

### SC-3I.5: The background capture uses `--render-mode=17` which requires EXR plumbing

Mode 17 writes cascade_gi + pt_full + pt_direct EXR sidecars. The `--screenshot-exr=1` flag enables this. The Stage 1 implementation verified this plumbing was restored (cherry-picked from `3d_2.0_fail` branch). The Phase 3 capture should produce the 4 expected files (.png + 3 .exr).

---

## Files Created

| File | Purpose |
|------|---------|
| `doc/8_shadertoy/v4_phase3_plan.md` | This phase's plan |
| `doc/8_shadertoy/v4_phase3_impl.md` | This file |
| `doc/8_shadertoy/v4_closeout_report.md` | Final program closeout report |

## Pending Steps (after capture completes)

1. Verify capture produced 4 files (.png + 3 .exr)
2. Compute metrics from EXRs (ratio_self, |p95|, bright%, dim%)
3. Compare against Stage 9 gain=0.10 reference (|p95|≤0.30, ratio∈[0.96,1.08])
4. Update `baseline_lock.json` entry `sponza_cam0_cascade_off_g010_pscene`: status→complete, new SHA256s, actual metrics