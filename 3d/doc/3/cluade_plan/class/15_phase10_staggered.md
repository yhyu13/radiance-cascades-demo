# 15 — Phase 10: Staggered Cascade Updates

**Purpose:** Understand how per-cascade update gating reduces bake cost without
degrading image quality for upper (far-field) cascades.

---

## The problem this solves

The cascade bake runs C3→C2→C1→C0 each time `cascadeReady == false`.
In temporal mode, `cascadeReady` is cleared once per **jitter hold step** — i.e., every
`jitterHoldFrames` render frames (default: every **2** frames). C0 therefore bakes at
most every 2 frames, not every single frame.

Baking all 4 cascades every frame is expensive:
- C0 at 32³ with D=4: 32,768 probes × 16 rays each
- C3 at 4³ with D=16: 64 probes × 256 rays each

C3 (far field: 2–8 m) changes very slowly — light at 8 m barely moves between frames.
Recomputing it every frame wastes compute.

---

## The rule

```cpp
// In update(), before dispatching cascade i:
bool shouldUpdate = (renderFrameIndex % std::min(1 << i, staggerMaxInterval)) == 0;
```

With `staggerMaxInterval = 8` (default):

| Cascade | Stagger divisor | With stagger=8 |
|---|---|---|
| C0 (i=0) | `1 << 0 = 1` | every bake trigger (≈ every 2 render frames with `jitterHoldFrames=2`) |
| C1 (i=1) | `1 << 1 = 2` | every other bake trigger (≈ every 4 render frames) |
| C2 (i=2) | `1 << 2 = 4` | every 4th bake trigger (≈ every 8 render frames) |
| C3 (i=3) | `1 << 3 = 8` | every 8th bake trigger (≈ every 16 render frames) |

The stagger divisor is applied to the bake-trigger count, not the raw frame count.
With `jitterHoldFrames=2`, a bake trigger fires every 2 render frames; so C0 bakes
every ~2 render frames, C1 every ~4, C2 every ~8, C3 every ~16.

The cap `min(1<<i, staggerMaxInterval)` means setting `staggerMaxInterval=4` would make
C2 and C3 both update every 4 frames (C3's natural 8 is capped at 4).

---

## Interaction with temporal accumulation

When cascade i is skipped:
- The bake compute shader is **not dispatched** for that level.
- Because the default fused-EMA path embeds blending inside the bake shader, skipping the bake also skips the EMA blend for that level. No separate `temporal_blend.comp` call is made.
- The history texture remains unchanged — no stale blend occurs.

This is correct: the accumulated history from the last bake is the best estimate we
have. Blending in zeros (from a skipped bake) would darken the history incorrectly.

---

## Why this is safe

C3 covers 2–8 m. The light and geometry at those distances change at the same rate as
the near field. But the probe response at 8 m is lower frequency — fewer probes,
each covering 1 m cells. A value from 8 frames ago is nearly identical to today's value
for a static Cornell Box.

The 16-frame jitter cycle (Phase 9) also means C3 completes a full update cycle
(1 update per 8 frames × 2 positions visited) well within one jitter period. The
temporal history will have valid blended data even for far cascades.

---

## Disabling staggering

Set `staggerMaxInterval = 1`. Now every cascade updates every frame.
Useful for debugging to ensure you are seeing the current bake, not stale history.

---

## Defaults

| Parameter | Default | Effect |
|---|---|---|
| `staggerMaxInterval` | 8 | C3 updates every 8 frames |
| `renderFrameIndex` | 0 | Monotonic, increments every render() |
