# 18 — Phase 14b/c: Range Scaling (c0MinRange / c1MinRange)

**Purpose:** Understand why the default cascade intervals are too short for most scenes
and how the minimum tMax override fixes it.

---

## The problem

At 32³ probes in a 4 m scene, the cell size is:

```
cellSize_C0 = volumeSize / cascadeC0Res = 4.0 / 32 = 0.125 m
```

The original tMax formula sets `tMax_C0 = cellSize_C0 = 0.125 m`.
That means each C0 probe only captures light arriving within **12.5 cm** — less than the
width of a hand. In the Cornell Box, the point light hangs ~1.5 m above the floor.
C0 probes on the floor see no direct contribution from the light in their interval;
the contribution must pass through C1→C2 and be merged down.

The merged result is accurate but it arrives at C0 averaged over the upper cascade's
larger probe cells. Fine near-surface detail is lost.

---

## The fix: minimum tMax

Phase 14b adds `c0MinRange` and Phase 14c adds `c1MinRange`:

```cpp
// In initCascades() / updateSingleCascade():
float tMax_C0 = std::max(cascades[0].cellSize, c0MinRange);  // Phase 14b
float tMax_C1 = std::max(cascades[1].cellSize * 4.0f, c1MinRange);  // Phase 14c (approx)
```

With `c0MinRange = 1.0` (default): `tMax_C0 = max(0.125, 1.0) = 1.0 m`.
C0 probes now shoot rays up to 1 m, covering most of the Cornell Box.

With `c1MinRange = 1.0` (default): `tMax_C1 = max(0.5, 1.0) = 1.0 m`.
C1 extends to at least 1 m as well, bridging the gap between C0 and C2.

---

## Legacy mode

Setting either value to 0 restores the original `cellSize`-based formula:

```
c0MinRange = 0 → tMax_C0 = 0.125 m  (Phase 1–13 behavior)
c1MinRange = 0 → tMax_C1 = 0.500 m  (Phase 1–13 behavior)
```

This is useful for A/B comparison or when the probe count is high enough that
the small cell size is actually fine (e.g., `cascadeC0Res = 128` gives 3 cm cells
in a 4 m scene — appropriate for some scenes).

---

## Tradeoffs

**Pro:** C0 rays see the light source directly; indirect arrives at higher spatial
frequency than a merged C1 value.

**Con:** C0 is now responsible for a 1 m interval with D²=16 rays. That is 16 samples
over 1 m instead of 0.125 m — the per-ray spatial resolution drops by 8×.
For complex lighting with many emitters at close range, this can under-sample.

At default D=4 and 32³ probes, the 1 m interval is the right tradeoff for Cornell Box
scale scenes. For sub-centimeter accuracy (e.g., architectural details at 0.01 m),
increase `cascadeC0Res` and reduce `c0MinRange` toward 0.

---

## Effect on the interval table

Legacy (c0MinRange=0, c1MinRange=0):
```
C0: [0.02, 0.125] m
C1: [0.125, 0.500] m
C2: [0.500, 2.000] m
C3: [2.000, 8.000] m
```

Default (c0MinRange=1.0, c1MinRange=1.0):
```
C0: [0.020, 1.000] m   ← tMax extended 8× by c0MinRange; tMin unchanged
C1: [0.125, 1.000] m   ← tMax extended 2× by c1MinRange; tMin unchanged at 0.125
C2: [0.500, 2.000] m   ← unchanged (c2/c3 have no min-range override)
C3: [2.000, 8.000] m   ← unchanged
```

`c0MinRange` and `c1MinRange` extend each cascade's **tMax only**. They do not move tMin.
With both at 1.0, C0 and C1 **overlap** in [0.125, 1.000 m]: both cascades independently
try to find geometry in that band. This is by design — C0 probes directly see geometry
the old formula delegated to C1, improving near-field coverage without breaking C1's own
interval structure. The blend zone (Phase 4c) still applies at each cascade's new tMax.

---

## Does this trigger a rebuild?

**No.** `c0MinRange` and `c1MinRange` are uniforms passed to the bake shader each
dispatch. Changing them takes effect on the next cascade bake without destroying or
re-creating any textures.

If temporal accumulation is on, the history blends toward the new tMax values over
the next few frames (~20–30 frames at alpha=0.05).

---

## Defaults

| Parameter | Default | Legacy (=0) |
|---|---|---|
| `c0MinRange` | 1.0 wu | 0.125 m (cellSize) |
| `c1MinRange` | 1.0 wu | 0.500 m (4×cellSize) |
