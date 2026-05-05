# 14 — Phase 9: Temporal Accumulation + Probe Jitter

**Purpose:** Understand how the EMA history blend and Halton probe jitter work together
to trade per-frame noise for smooth convergence over a 16-frame cycle.

---

## The problem this solves

Without temporal accumulation each cascade bake fires D² rays per probe (D=4 → 16 rays).
That is enough to integrate a single point light cleanly but not enough to suppress noise
from probe jitter offsets or area sources. The result is per-frame noise in the indirect term.

Phase 9 answers: **bake one jittered position per frame, then blend into a running history.**
After N frames the history approximates what you would get from N×D² rays per probe.

---

## Two companion textures per cascade

`RadianceCascade3D` gains two history textures alongside each live texture:

```
probeGridTexture   → probeGridHistory    (same res³ RGBA16F)
probeAtlasTexture  → probeAtlasHistory   (same (res×D)²×res RGBA16F)
```

The default path — **fused EMA** — embeds the blend inside `radiance_3d.comp` itself.
The bake shader reads `probeAtlasHistory` and writes `mix(history, bake, alpha)` directly
into `probeAtlasTexture`. After the bake, C++ swaps the live and history handles.
The renderer (`raymarch.frag`) reads from **history** when temporal is enabled.

`temporal_blend.comp` runs only as a fallback when history textures are not yet allocated
(first frame after a cascade resize or initial enable).

---

## EMA blend

The blend formula is:

```
h_new = mix(h_old, bake, alpha)  =  (1−α)·h_old + α·bake
```

`uAlpha = temporalAlpha` (default **0.05**).

- Lower alpha → smoother output, slower response to changes.
- Higher alpha → noisier but updates quickly when geometry or lights change.

**Fused path (default):** The EMA blend runs inside `radiance_3d.comp`. After the dispatch,
C++ swaps live ↔ history texture handles. This is controlled by the local variable
`doFusedEMA` in `updateSingleCascade()`:

```cpp
// src/demo3d.cpp:1381
const bool doFusedEMA = useTemporalAccum && tb != shaders.end()
                     && c.probeAtlasHistory != 0 && c.probeGridHistory != 0;
```

**Convergence:** The stale-value coefficient decays as `(1−α)^n = 0.95^n`.

| frames (n) | weight on stale value |
|---|---|
| 1  | 0.95 |
| 7  | 0.95^7 ≈ 0.70 |
| 14 | 0.95^14 ≈ 0.49 |
| 28 | 0.95^28 ≈ 0.24 |
| 60 | 0.95^60 ≈ 0.05 |

After 14 frames the stale value is still at ~49% (halfway, not converged). Full convergence
to <5% of stale takes ~60 frames.

---

## History seeding (historyNeedsSeed)

On first enable, history textures contain zeros. Blending into zeros produces a dark
warm-up ramp for the first ~60 frames. To eliminate this, when `historyNeedsSeed=true`
the blend sets `history = bake` (alpha=1.0) for one dispatch, then clears the flag.

`historyNeedsSeed` is set when temporal accumulation is re-enabled after being off.

---

## History clamp (Phase 9b, useHistoryClamp=true default)

Before the EMA blend, the history value is clamped to the AABB of the current-bake
values in its neighborhood. This is the same ghost-rejection used in TAA for screen space.

**Why:** If a light moves or a surface appears, old history values outside the AABB of
current data represent a ghost. Clamping discards those ghosts immediately rather than
waiting for alpha to fade them out.

Clamping is slightly aggressive — it reduces effective convergence if probes vary widely.
The default ON is appropriate for a real-time scene with interactive light changes.

---

## Probe jitter (useProbeJitter=true default)

Before each cascade bake, each probe's world position is shifted by a sub-cell offset:

```cpp
// In update() before bake dispatch:
float h0 = halton(probeJitterIndex, 2) - 0.5f;  // [−0.5, 0.5]
float h1 = halton(probeJitterIndex, 3) - 0.5f;
float h2 = halton(probeJitterIndex, 5) - 0.5f;
currentProbeJitter = glm::vec3(h0, h1, h2) * probeJitterScale;
```

`probeJitterScale = 0.06` (default) → jitter is ±6% of a probe cell width.

This is intentionally small. Large jitter (old default was ±0.25) caused visible
warping between frames because the EMA was not fast enough to blend away the large
inter-frame variation. Small jitter still samples sub-cell variation but stays close
enough to the true probe center that blending is stable.

---

## Halton sequence

```cpp
static float halton(uint32_t idx, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (idx > 0) { f /= base; r += f * (idx % base); idx /= base; }
    return r;
}
```

Halton(base=2,3,5) for x,y,z axes: a low-discrepancy sequence that fills [0,1)
uniformly with no clustering. Random sequences cluster; Halton does not.

**jitterPatternSize = 8** (default): after 8 samples the sequence repeats.
The 8 positions form a well-distributed lattice across the probe cell.

---

## Hold frames (jitterHoldFrames=2 default)

Each jitter position is held for N frames before advancing:

```
frame  0,1 → Halton(0)  → position A
frame  2,3 → Halton(1)  → position B
...
frame 14,15 → Halton(7) → position H
frame 16,17 → Halton(0) → position A again
```

Full cycle = `jitterPatternSize × jitterHoldFrames = 8 × 2 = 16 frames`.

Holding for 2 frames gives the EMA (alpha=0.05) enough integration time at each
position before moving to the next — otherwise the history barely converges before
the position shifts.

---

## `jitterHoldCounter` (internal)

Counts down from `jitterHoldFrames` to 0. At 0, `probeJitterIndex` increments mod
`jitterPatternSize` and the counter resets. Not a user-facing parameter.

---

## What happens in the staggered case (Phase 10)

Cascade i updates when `renderFrameIndex % min(1<<i, staggerMaxInterval) == 0`.
When C1 does not update this frame, its temporal blend also does not run.
The history from the previous update remains unchanged — no additional blending occurs.

This is correct: the history already holds a valid accumulated value; skipping a frame
does not degrade it.

---

## Defaults summary

| Parameter | Default | Effect |
|---|---|---|
| `useTemporalAccum` | true | EMA blend active |
| `temporalAlpha` | 0.05 | 5% new bake per frame |
| `useProbeJitter` | true | Sub-cell position shift |
| `probeJitterScale` | 0.06 | ±6% of cell width |
| `jitterPatternSize` | 8 | 8-position Halton(2,3,5) lattice |
| `jitterHoldFrames` | 2 | 2 frames per position |
| `useHistoryClamp` | true | TAA ghost rejection |
| `historyNeedsSeed` | false | Set automatically on re-enable |
| Full cycle | 16 frames | 8 positions × 2 frames |
