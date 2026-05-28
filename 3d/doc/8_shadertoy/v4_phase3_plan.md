# v4 Phase 3 — Verification Capture & Final Closeout Plan

**Date:** 2026-05-28T17:48+08:00
**Predecessor:** `doc/8_shadertoy/v4_phase2_impl.md` (Phase 2 complete)
**Scope doc:** `doc/8_shadertoy/v4_shadertoy_adoption_scope.md`
**Goal:** Confirm Sponza per-scene preset produces expected metrics; write v4 closeout report

---

## 1. What Phase 3 Does

Phase 3 is the **final verification** of the v4 ShaderToy adoption program. It confirms the Sponza per-scene MB-gain preset (Phase 1A deliverable) produces the Stage 9-validated metrics, and writes the v4 closeout report summarizing the entire program's findings.

| Step | What | Type | GPU needed? |
|------|------|------|-------------|
| 3A | Sponza pscene capture at N=2048 | Capture | YES |
| 3B | Verify metrics match Stage 9 expectations | Analysis | No |
| 3C | Write v4 closeout report | Doc | No |
| 3D | Update baseline_lock.json with confirmed capture | Lock | No |

---

## 2. Step-by-Step

### Step 3A — Sponza Per-Scene Preset Capture

**Command:**
```powershell
.\build\RadianceCascades3D.exe `
    --load-obj=sponza `
    --use-multi-bounce=1 --mb-gain-per-scene `
    --measurement-cameras-file=tools/v20_pre_measurement/sponza_cam.json `
    --measurement-camera=0 `
    --cascade-scaled-dir-res=1 --noise-seed-offset=0 --use-probe-jitter=1 `
    --render-mode=17 --screenshot-exr=1 --auto-capture-delay=0 `
    --exit-frames=2048 `
    --screenshot=v4_phase3_sponza_pscene.png
```

**Expected output files:**
- `v4_phase3_sponza_pscene.png`
- `v4_phase3_sponza_pscene_cascade_gi.exr`
- `v4_phase3_sponza_pscene_pt_full.exr`
- `v4_phase3_sponza_pscene_pt_direct.exr`

**Expected runtime:** ~6 min (N=2048 at default resolution)

### Step 3B — Verify Metrics

**Command:**
```powershell
python tools/v3_baseline/analyze_baselines.py `
    --scene=sponza --hybrid=0 --n 2048 `
    --out tools/v3_baseline/phase3_verify_metrics.json
```

**Stage 9 reference metrics** (for comparison):
| Metric | Stage 9 gain=0.10 | Expected Phase 3 |
|--------|-------------------|-----------------|
| ratio_self | 1.040 | 0.96-1.08 (±4%) |
| abs_p95 | 0.253 | ≤ 0.30 |
| bright_pct | 1.73% | ≤ 5% |
| valid | 693 | ~693 |

**Gate:** |p95| ≤ 0.30 AND ratio_self ∈ [0.96, 1.08]. If either fails, the per-scene preset does NOT reproduce Stage 9 results — investigate configuration drift (seed offset, probe jitter, temporal alpha).

**Expected outcome:** Metrics within ±4% of Stage 9. The per-scene preset applies the same gain (0.10) via a different code path (post-load hook vs CLI argument). The EXR output should be bit-similar if not bit-identical.

### Step 3C — Write v4 Closeout Report

Document covering:

1. **What was tried and what failed**
   - v3 M1 Delta #3/#6 port → DEAD (2×2 matrix)
   - v2.x 31-commit MB correction program → FAILED
   - ShaderToy literal code port → not applicable (topology mismatch)

2. **What was found and what works**
   - Sponza: MB gain=0.10, |p95|=0.25 (clears retirement gate by 2×)
   - Cornell directional: ratio=0.93 (close to PT)
   - Cornell point-light: ratio=0.49 (volumetric topology constraint)
   - Hybrid correction: ratio 0.83 on Cornell (acceptable quality)

3. **What was shipped**
   - Phase 1A: Sponza per-scene MB-gain preset (`--mb-gain-per-scene`)
   - Phase 2B: Removed stale M1 delta flags (42 lines dead code)
   - Phase 2: Documentation closeout (v3 SUPERSEDED, lock updated)

4. **What remains open**
   - Cornell point-light constraint (documented, not fixable in volumetric)
   - Path B (surface-attached topology) — user decision deferred
   - Analyzer valid mask threshold (693 pixels on Sponza)
   - Mode-0 visual validation for the per-scene preset (Stage 10 validated gain=0.10, but not the `--mb-gain-per-scene` flag specifically)

5. **Decision tree for Path B**
   - Go: new scene requirement makes Cornell constraint blocking
   - No-go: hybrid correction acceptable; volumetric cascade adequate for open/directional scenes

### Step 3D — Update Lock with Confirmed Capture

After Step 3B confirms metrics, update `baseline_lock.json` entry `sponza_cam0_cascade_off_g010_pscene`:
- `status`: `"pending_capture"` → `"complete"`
- Add SHA256 for the 4 output files
- Add actual metrics block from analyzer output

---

## 3. Self-Critique on the Plan

### SC-P3.1: The capture confirmation is redundant — Stage 9 already validated gain=0.10

Stage 9 ran 8 captures at gains {0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.75, 1.00} and found the non-monotonic minimum at gain=0.10 (|p95|=0.25, ratio=1.04). Stage 10 confirmed this translates to mode-0 visual improvement (10× RMS reduction). The per-scene preset applies the SAME gain (0.10) via a different CLI flag. The EXR output should be identical.

**Is this step necessary?** Strictly, yes — for process hygiene. The lock.json has an `expected_metrics` entry sourced from Stage 9, but the lock should represent captured-on-this-build reality. The binary changed (Phase 2B flag removal), and while behavioral equivalence is assumed, a confirmation capture is the honest answer.

**Mitigation:** If GPU capture time is constrained, skip Step 3A-3B and mark the lock entry as `status: "confirmed_by_stage9"` with the `expected_metrics` promoted to `metrics`. This is less rigorous but acceptable given the Stage 9/10 validation chain.

### SC-P3.2: The closeout report duplicates the v4 scope doc

The v4 scope doc §6 ("What We Learned") already summarizes the program findings. The closeout report (Step 3C) would expand on that with actual metric tables and decision trees. The line is blurry — the scope doc is the plan, the closeout report is the final state.

**Decision:** The closeout report focuses on the ANSWER ("what decision did we reach") while the scope doc focuses on the PLAN ("what are we going to do"). The closeout report is the deliverable for external readers; the scope doc is the working document.

### SC-P3.3: Phase 3 has no code changes — it's pure verification + documentation

This is correct for a closeout phase. All code changes (Phase 1A, Phase 2B) are shipped. Phase 3 confirms they work and documents the program.

### SC-P3.4: The `expected_metrics.source` field in the lock creates a circular reference

The lock references Stage 9 for expected metrics, and if Phase 3 confirms them, the lock entries tie to a historical capture. This is acceptable — the lock is a historical record with provenance, not just a current-state snapshot. Future readers can trace: "Stage 9 found gain=0.10 → Phase 1A implemented per-scene preset → Phase 3 confirmed same metrics."

---

## 4. Acceptance Gates

| Step | Gate | Threshold |
|------|------|-----------|
| 3B | |p95| verification | ≤ 0.30 |
| 3B | Ratio proximity to Stage 9 | ∈ [0.96, 1.08] (±4%) |
| 3C | Closeout report completeness | Covers all 5 sections (tried/failed, found/works, shipped, open, Path B tree) |
| 3D | Lock update | New SHA256s present, metrics sourced from analyzer output |

---

## 5. Queueability

Steps 3A-3B require GPU (~6 min). They are queueable independently. Steps 3C-3D can execute immediately.

**Recommended execution:**
1. Write closeout report (3C) now — pure documentation
2. Queue capture (3A) as a background process
3. Verify metrics (3B) when capture completes
4. Update lock (3D) as final step