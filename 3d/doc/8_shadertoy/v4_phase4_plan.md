# v4 Phase 4 — Measurement Cleanup & Final State Plan

**Date:** 2026-05-28T18:00+08:00
**Predecessor:** Phase 3 verification (bit-identical metrics confirmed)
**Goal:** Lock the confirmed capture, fix the analyzer threshold, produce final state

---

## 1. What Phase 4 Fixes

Phase 3 confirmed the per-scene preset produces bit-identical metrics to Stage 9. Two remaining measurement-quality issues need resolution before v4 is fully closed:

| Issue | Impact | Fix |
|-------|--------|-----|
| Valid mask = 693 pixels (0.08%) | Sponza metrics cover only 693 pixels. The threshold `pt_lum > 0.05` excludes 99.92% of the image at N=2048. | Lower threshold to 0.01; re-analyze Sponza cascade-off + pscene captures |
| Lock `pending_capture` status | The lock has `sponza_cam0_cascade_off_g010_pscene` with `status: "pending_capture"` | Update to `complete` with Phase 3 SHA256s and confirmed metrics |

---

## 2. Step-by-Step

### Step 4A — Lock Update (Confirmed Capture)

**File:** `tools/v3_baseline/baseline_lock.json`

Update the `sponza_cam0_cascade_off_g010_pscene` entry:
- `status`: `"pending_capture"` → `"complete"`
- Add SHA256 for the 4 core files (PNG + 3 EXR)
- Replace `expected_metrics` with actual metrics:
  ```json
  "metrics": {
      "ratio_self": 1.040154,
      "abs_p95": 0.253074,
      "bright_pct": 1.7316,
      "dim_pct": 1.1544,
      "valid": 693,
      "source": "Phase 3 verification capture, --mb-gain-per-scene"
  }
  ```

### Step 4B — Analyzer Threshold Fix

**File:** `tools/v3_baseline/analyze_baselines.py` (line 55)

**Current:**
```python
mask = (pt_lum > 0.05) & (casc_lum > 0.001)
```

**Change to:**
```python
mask = (pt_lum > 0.01) & (casc_lum > 0.001)
```

**Rationale:** At N=2048, the PT indirect mean on Sponza is 0.059. Threshold 0.05 excludes the majority of pixels. Lowering to 0.01 captures genuinely lit indirect pixels while still filtering the noise floor (PT direct = 0 for most pixels on Sponza, so `pt_indirect = pt_full` at those pixels ≈ pure noise). The cascade threshold (0.001) is already permissive — the PT threshold is the bottleneck.

**Expected impact:** Valid pixel count should increase from 693 to a larger number. The ratio_self and |p95| may shift slightly because more pixels are included — but Stage 9's non-monotonic gain=0.10 minimum is robust to mask changes since it was measured on relative deltas, not absolute magnitudes.

**Verification:** Re-run analyzer on Sponza cascade-off at N=2048 and Sponza pscene at gain=0.10. Compare against Stage 9 metrics to ensure the gain=0.10 advantage persists on the wider mask.

### Step 4C — Final State

1. **Verify analyzer output** after threshold fix for both Sponza default (gain=1.0) and Sponza pscene (gain=0.10)
2. **Record final metrics** in the lock
3. **Summarize final state** in a one-liner added to this impl doc

---

## 3. Self-Critique

### SC-P4.1: Lowering threshold risks including noise-dominated pixels

At N=2048, PT indirect = PT_full - PT_direct. For Sponza, PT_direct is tiny (mean ~0.001 from the EXR). Most pixels with PT indirect < 0.05 are genuinely noise-dominated — lowering the threshold to 0.01 will include pixels where PT_GI is essentially zero and cascade/PT ≈ ∞. The ratio_self metric (mean of ratio) will be inflated by these pixels.

**Mitigation:** The ratio_self is MEAN of ratio, not median. A few ∞-ratio pixels won't dominate the mean if there are enough well-behaved pixels. But if the wider mask includes thousands of noise pixels, the mean shifts. The actual test is: does the gain=0.10 conclusion hold on the wider mask?

**Alternative:** Use an adaptive threshold (target a fixed valid-pixel fraction, e.g., 10% of image) rather than a fixed luminance threshold. More robust but more complex. Out of scope for Phase 4 — document as a Phase 5 improvement.

### SC-P4.2: The threshold change invalidates historical comparisons

If we change the mask, the Stage 9 metrics are no longer directly comparable to Phase 4 metrics. The gain=0.10 minimum might shift or disappear. This is a measurement-regime change, not a code change — the cascade behavior is identical.

**Decision:** DON'T change the analyzer in-place. Instead, add a `--mask-threshold` parameter (default 0.05 for backward compat). This preserves the historical comparison while enabling wider-mask analysis.

**Revised approach:** Add `--pt-threshold` CLI arg to `analyze_baselines.py`, default 0.05. Run with `--pt-threshold=0.01` for the Phase 4 analysis. Historical comparisons remain with the 0.05 default.

### SC-P4.3: The Phase 3 locked metrics have the `source` field but no `_locked_at`

The existing lock entries don't have timestamps. The Phase 4 update should add `_validated_at` to the pscene entry to establish provenance.

---

## 4. Acceptance Gates

| Step | Check | Expected |
|------|-------|----------|
| 4A | Lock `sponza_cam0_cascade_off_g010_pscene.status` | `"complete"` |
| 4A | Lock SHA256s present | 4 file hashes matching on-disk files |
| 4B | `analyze_baselines.py --pt-threshold=0.01` runs | Valid count > 693 |
| 4B | Sponza gain=0.10 still best on wider mask | ratio ≈ 1.0, |p95| ≤ 0.50 |
| 4C | Final state documented | This impl doc updated with final metrics |

---

## 5. Queueability

All steps are immediate (no GPU needed):
- 4A: SHA256 computation + JSON edit (~2 min)
- 4B: Python script modification + run (~3 min)
- 4C: Documentation (~5 min)