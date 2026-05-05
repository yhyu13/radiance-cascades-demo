# Reply to Finding 7 — C1 Interval Math Error in `18_phase14_range_scaling.md`

**Critic finding:** `02_current_codebase_review.md` §7 (Medium)
**Verdict:** Accept in full

---

## What we wrote (wrong)

```
Default (c0MinRange=1.0, c1MinRange=1.0):
C0: [0.02, 1.000] m   ← 8× wider reach
C1: [1.000, 1.000] m  ← effectively same endpoint (or slightly extended)
C2: [0.500, 2.000] m  ← unchanged
C3: [2.000, 8.000] m  ← unchanged
```

The C1 row `[1.000, 1.000]` is wrong on both ends.

---

## What the code does (correct)

From `src/demo3d.cpp:1341-1344` and `res/shaders/radiance_3d.comp:290-295`:

Each cascade's tMin is computed from its level index `i` and the base interval `d`:

```glsl
// Cascade i: tMin = (4^i) * d   (for i>0, simplified)
// Cascade i: tMax = max(4^(i+1) * d, uCnMinRange)
```

For i=0: tMin = 0.02 (fixed near-surface start), tMax = max(d, c0MinRange) = max(0.125, 1.0) = **1.000m**
For i=1: tMin = d = 0.125m (unchanged by min-range), tMax = max(4d, c1MinRange) = max(0.5, 1.0) = **1.000m**
For i=2: tMin = 4d = 0.5m, tMax = 16d = 2.0m
For i=3: tMin = 16d = 2.0m, tMax = 64d = 8.0m

`c1MinRange` only extends **tMax for C1**. It does not move C1's tMin. C1's tMin stays at `d = 0.125m`.

---

## Corrected interval table

```
Legacy (c0MinRange=0, c1MinRange=0):
C0: [0.020, 0.125] m
C1: [0.125, 0.500] m
C2: [0.500, 2.000] m
C3: [2.000, 8.000] m

Default (c0MinRange=1.0, c1MinRange=1.0):
C0: [0.020, 1.000] m   ← tMax extended 8× by c0MinRange
C1: [0.125, 1.000] m   ← tMax extended 2× by c1MinRange; tMin UNCHANGED at 0.125
C2: [0.500, 2.000] m   ← unchanged (4×d=0.5 > 0; no min-range applied to C2+)
C3: [2.000, 8.000] m   ← unchanged
```

---

## Implication for the text in `18_phase14_range_scaling.md`

The doc says:
> "Note: the interval boundaries where C0 hands off to C1 also shift accordingly."

This is misleading. C0 and C1 now **overlap**: both cover 0.125–1.000m. C0's tMax is 1.0m and C1's tMin stays at 0.125m — so for a ray between 0.125m and 1.0m, both cascades have jurisdiction. The shader does not "shift the handoff"; it creates an overlap zone where both cascades independently try to find geometry.

The corrected description:
> c0MinRange and c1MinRange independently extend each cascade's tMax without moving tMin.
> With both set to 1.0, C0 covers [0.020, 1.000] and C1 covers [0.125, 1.000].
> These ranges **overlap** in [0.125, 1.000]. This is by design: the overlap means C0
> probes directly see geometry that the old formula delegated to C1, improving near-field
> coverage, while C1's own interval still starts at 0.125m as before.

---

## Additional error in the "Legacy interval" note

The doc says "tMax_C0 = cellSize = volumeSize / cascadeC0Res = 4.0 / 32 = 0.125m."

This is correct for the base legacy formula. But the doc also says the legacy C1 tMax is `0.5wu` (which is `4 × 0.125 = 0.5m`) — this is correct too. No correction needed there.
