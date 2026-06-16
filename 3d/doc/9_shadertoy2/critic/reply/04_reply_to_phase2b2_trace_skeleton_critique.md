# Reply — Critique 04 on Phase 2B-2 Trace Skeleton

**Date:** 2026-05-29  
**Critique:** `doc/9_shadertoy2/critic/04_critique_phase2b2_trace_skeleton.md`  
**Files updated:** `src/surface_rc.h`, `src/surface_rc.cpp`, `src/demo3d.h`, `src/demo3d.cpp`, `src/main3d.cpp`, `res/shaders/surface_radiance_debug.comp`  
**Disposition:** Accepted. Bias sweep blocker is now implemented and run. Result: trace is usable but shows meaningful bias sensitivity; proceed with caution and prefer bias `0.02` for subsequent diagnostics unless a better bias model is added.

---

## 0. Summary

Critique 04 correctly identified that Phase 2B-2 had adequate hit/miss functionality but lacked self-hit/bias characterization. I implemented:

```text
--surface-ray-bias=<float>
SurfaceRC::setRayBias/getRayBias
UI slider for Surface ray bias
named TRACE_ESCAPE_INF_FRACTION constant
bias sweep captures over 0.005, 0.01, 0.015, 0.02, 0.03, 0.05
per-chart hit/miss/escape analysis
```

The sweep shows that the hit count is **not fully stable** across bias. Between `0.01` and `0.03`, hits drop from `11264` to `9708`, a **13.8% decrease**.

According to the critique's decision tree:

```text
10-30% drop -> proceed with caution, document self-hit/bias sensitivity, consider increasing default bias to 0.02
```

Therefore, Phase 2B-3 can proceed, but all subsequent classification/NEE/feedback conclusions must treat trace bias as an active caveat.

---

## 1. Code Changes

### 1.1 Surface ray bias control

Added to `SurfaceRC`:

```cpp
void setRayBias(float bias);
float getRayBias() const;
float rayBias = 0.01f;
```

Clamp:

```text
0.0005 <= rayBias <= 0.10
```

CLI:

```text
--surface-ray-bias=<float>
```

UI:

```text
Surface ray bias slider: 0.0005 .. 0.05
```

### 1.2 Named constant

In `surface_radiance_debug.comp`:

```glsl
const float TRACE_ESCAPE_INF_FRACTION = 0.5;
```

Replaced:

```glsl
if (d >= INF * 0.5)
```

with:

```glsl
if (d >= INF * TRACE_ESCAPE_INF_FRACTION)
```

---

## 2. Bias Sweep Commands

Executed:

```powershell
foreach($bias in @(0.005,0.01,0.015,0.02,0.03,0.05)) {
  .\build\RadianceCascades3D.exe `
    --load-obj=cornell `
    --use-surface-rc=1 `
    --surface-debug-target=radiance `
    --surface-radiance-debug-mode=4 `
    --surface-ray-bias=$bias `
    --screenshot="tools/phase2b2_bias/bias_$tag.png" `
    --exit-frames=2 `
    --window-size=1024,768
}
```

Captured:

```text
tools/phase2b2_bias/bias_0p005.png
tools/phase2b2_bias/bias_0p010.png
tools/phase2b2_bias/bias_0p015.png
tools/phase2b2_bias/bias_0p020.png
tools/phase2b2_bias/bias_0p030.png
tools/phase2b2_bias/bias_0p050.png
```

---

## 3. Bias Sweep Results

Active overlay sample count:

```text
12096
```

Aggregate results:

| Bias | Hits | Misses | Escapes | Hit % | Hit change vs 0.01 |
|---:|---:|---:|---:|---:|---:|
| 0.005 | 11815 | 14 | 267 | 97.68% | +4.89% |
| 0.010 | 11264 | 18 | 814 | 93.12% | baseline |
| 0.015 | 10310 | 616 | 1170 | 85.24% | -8.47% |
| 0.020 | 10077 | 573 | 1446 | 83.31% | -10.54% |
| 0.030 | 9708 | 782 | 1606 | 80.26% | -13.81% |
| 0.050 | 9633 | 628 | 1835 | 79.64% | -14.48% |

Decision-tree result:

```text
0.01 -> 0.03 hit drop = 13.81%
=> 10-30% band
=> proceed with caution, document bias sensitivity, consider default bias 0.02
```

---

## 4. Per-Chart Breakdown

```text
bias=0.005 hit=11815 miss=14 escape=267
  C1 floor   h=3406 m=9   e=41
  C2 ceiling h=3225 m=5   e=226
  C3 left    h=1728 m=0   e=0
  C4 right   h=1728 m=0   e=0
  C5 back    h=1728 m=0   e=0

bias=0.010 hit=11264 miss=18 escape=814
  C1 floor   h=3345 m=12  e=99
  C2 ceiling h=2958 m=5   e=493
  C3 left    h=1614 m=0   e=114
  C4 right   h=1619 m=1   e=108
  C5 back    h=1728 m=0   e=0

bias=0.015 hit=10310 miss=616 escape=1170
  C1 floor   h=3100 m=242 e=114
  C2 ceiling h=2587 m=373 e=496
  C3 left    h=1502 m=0   e=226
  C4 right   h=1498 m=0   e=230
  C5 back    h=1623 m=1   e=104

bias=0.020 hit=10077 miss=573 escape=1446
  C1 floor   h=2944 m=223 e=289
  C2 ceiling h=2554 m=347 e=555
  C3 left    h=1482 m=1   e=245
  C4 right   h=1476 m=1   e=251
  C5 back    h=1621 m=1   e=106

bias=0.030 hit=9708 miss=782 escape=1606
  C1 floor   h=2993 m=99  e=364
  C2 ceiling h=2615 m=229 e=612
  C3 left    h=1263 m=197 e=268
  C4 right   h=1216 m=255 e=257
  C5 back    h=1621 m=2   e=105

bias=0.050 hit=9633 miss=628 escape=1835
  C1 floor   h=2943 m=80  e=433
  C2 ceiling h=2546 m=227 e=683
  C3 left    h=1349 m=67  e=312
  C4 right   h=1340 m=90  e=298
  C5 back    h=1455 m=164 e=109
```

Observations:

1. Ceiling escapes are consistently high relative to other charts.
2. Back wall is stable at low/mid bias and only degrades at high bias.
3. Left/right walls begin showing misses/escapes as bias grows past `0.02`.
4. Very low bias (`0.005`) likely includes more near-surface/self-hit contribution because hit count is highest and escapes lowest.

---

## 5. Acceptance Criteria Assessment

Critique acceptance:

```text
Hit count stable (±10%) across bias range 0.01-0.02
```

Measured:

```text
0.01 -> 0.02 hit drop = 10.54%
```

This narrowly misses the ±10% criterion by `0.54%`.

However:

```text
0.01 -> 0.015 hit drop = 8.47%  PASS
0.015 -> 0.02 hit drop = 2.26% PASS
0.02 -> 0.03 hit drop = 3.66%  PASS
```

Interpretation:

```text
The largest transition is 0.01 -> 0.015, suggesting 0.01 still contains some near-surface/self-hit sensitivity. Past 0.015, the trace distribution is more stable.
```

Recommendation:

```text
Use 0.02 as the safer diagnostic default for hit classification / NEE debugging.
```

I did not change the default yet to avoid silently invalidating previous captures. The next phase should explicitly choose `--surface-ray-bias=0.02` when capturing classification/NEE evidence.

---

## 6. Remaining Critique Items

### Per-chart statistics

Done via screenshot analysis.

Caveat:

```text
This is display-overlay-space chart segmentation, not direct atlas readback. It is sufficient for current debug evidence but should eventually be replaced by SSBO/readback stats.
```

### Non-self-hit distance distribution

Partially done.

Mode 5 distance variation was previously measured:

```text
trace_m5.png activeUnique4=14
```

But the specific criterion:

```text
>=10% of hits have normalized distance > 0.1
```

was not completed because screenshots of mode 5 are tonemapped/displayed, not direct RGBA16F readbacks. This remains open.

### Analytical ray-plane comparison

Not done. Still recommended before production feedback, but the bias sweep de-risks the immediate Phase 2B-3 continuation enough to proceed cautiously.

### Performance metrics

Not done. Debug dispatch performance remains unmeasured.

### Null SDF handling

Not yet implemented. Recommended minimal follow-up:

```cpp
if (sdfTexture == 0) {
    std::cerr << "[SurfaceRC] WARN: radiance debug skipped; sdfTexture is 0\n";
    return;
}
```

---

## 7. Verdict

Critique 04 is accepted.

Bias sweep result:

```text
0.01 -> 0.03 hit drop = 13.81%
```

Decision:

```text
Proceed with caution, not block completely.
```

Required operating assumptions for subsequent phases:

1. Use `--surface-ray-bias=0.02` for Phase 2B-3/2B-4 diagnostic captures unless testing bias sensitivity.
2. Treat hit classification statistics as bias-sensitive.
3. Do not use this trace for final visibility/feedback quality claims yet.
4. Add hit normal and direct-light debug before feedback.
5. Keep analytical comparison and direct texture readback on the near-term TODO list.

---

## 8. Phase 2B-3 Status Update

Phase 2B-3 implementation already exists from the previous step. Given this critique result:

```text
Phase 2B-3 remains useful as a diagnostic phase,
but its captures should be regenerated/evaluated with --surface-ray-bias=0.02.
```

Do not interpret the existing Phase 2B-3 classification counts as final until regenerated with the chosen safer diagnostic bias.
