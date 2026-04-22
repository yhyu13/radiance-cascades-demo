# Multi-Cascade N-Level Switch — Implementation Learnings

**Date:** 2026-04-22  
**Branch:** 3d  
**Covers:** Adding N cascade levels with per-cascade ray intervals and a UI cascade selector.

---

## What Was Built

Four cascade levels (C0–C3), all 32³ probes at identical world positions.  
The cascade **index** determines which distance shell each probe samples:

| Level | Interval | World range (d=0.125) |
|---|---|---|
| C0 | [0.02, d] | 0–0.125 m (near-field only) |
| C1 | [d, 4d] | 0.125–0.5 m |
| C2 | [4d, 16d] | 0.5–2.0 m |
| C3 | [16d, 64d] | 2.0–8.0 m |

A `C0 C1 C2 C3` radio button in the Cascades panel binds the chosen level to
`uRadiance` in the raymarch pass, so mode 3 (Indirect×5) and mode 0 (GI blend)
both reflect that specific cascade's captured light.

---

## Design Decisions

### Probe positions fixed across all levels

All cascades use the same `cellSize = volumeSize/probeRes = 0.125`.  
`probeToWorld()` maps probe indices identically for every level:

```glsl
return uGridOrigin + (vec3(probePos) + 0.5) * uBaseInterval;  // uBaseInterval=0.125 for all
```

This makes each cascade a "shell view" of the same probe grid rather than a
sparser/coarser grid. The simplification trades memory efficiency for
implementation clarity (no need to handle different grid resolutions during
visualization). Resolution reduction is a Phase 3 concern.

### Interval formula in the shader

```glsl
float d = uBaseInterval;
float tMin, tMax;
if (uCascadeIndex == 0) {
    tMin = 0.02;   // self-intersection offset
    tMax = d;
} else {
    float f = pow(4.0, float(uCascadeIndex - 1));
    tMin = f * d;
    tMax = f * 4.0 * d;
}
```

`4^(N-1)` gives the start scale; `4^N` the end scale.  
`uBaseInterval` is the one constant bridging CPU and GPU — always 0.125 for now.

---

## Bugs Found During Self-Review

### Bug 1 — UI tMin display showed 0.0 for cascade 0

The interval table in `renderCascadePanel()` used `tMin = 0.0f` for cascade 0,  
while the shader uses `tMin = 0.02` (self-intersection offset).  
**Fix:** use `0.02f` in the UI display to match the shader exactly.

```cpp
// Wrong:
float tMin = (i == 0) ? 0.0f : d * std::pow(4.0f, float(i - 1));
// Correct:
float tMin = (i == 0) ? 0.02f : d * std::pow(4.0f, float(i - 1));
```

**Lesson:** When shader constants appear in display code, keep them in sync.
A named constant (e.g. `const float CASCADE0_TMIN = 0.02f`) shared between
shader and display would prevent this class of drift.

### Bug 2 — Cascade time label didn't reflect N dispatches

`cascadeTimeMs` now measures all 4 cascade dispatches, but the UI label said
only "Cascade  X.XX ms", making it ambiguous whether that's 1 or 4 levels.  
**Fix:** append the level count: `"Cascade  %.2f ms (%d levels)"`.

---

## Design Notes (Not Bugs, But Traps to Understand)

### C0 is nearly all-zero by design

Cascade 0 only samples the `[0.02, 0.125]` shell around each probe.  
In a 4-unit Cornell Box, the center probe is 2.0 m from every wall.  
Therefore **all C0 probes farther than 0.125 m from a wall are zero** — which is
most of the 32³ grid.

This is the *correct* behavior: C0 captures the immediate near-field light, and
merging (Phase 3) propagates it outward through C1→C2→C3.  
Before merging is in place, selecting C0 in the UI will look mostly dark except
for the thin shell of wall-adjacent probes — this is expected, not broken.

### C3 barely covers the Cornell Box interior

C3 interval [2.0, 8.0] starts at 2.0 m. The Cornell Box half-size is 2.0 m.  
A probe at the exact center sees all six walls at 2.0 m — right at C3's tMin.  
Most interior probes are **closer** than 2.0 m to at least one wall, meaning
C3 rays start *past* the wall and mostly return `INF` (out-of-volume SDF).  
C3 is only non-trivially useful for diagonal paths or very asymmetric probe positions.  
In larger scenes (Phase 3+) it becomes relevant; for this 4-unit box it's mostly empty.

### Probe readback is pinned to cascade 0

The `probeDumped` readback always reads `cascades[0].probeGridTexture`.  
This is correctly labeled "Probe Readback (cascade 0)" in the UI.  
However, when the user selects C1/C2/C3 for rendering, the stats shown still
describe C0, which can be misleading.  
**Future fix:** read back the `selectedCascadeForRender` cascade, or add a
readback button per level.

### `cascadeReady` is a single flag for all levels

There is one `static bool cascadeReady` covering all N cascades.  
All cascades recompute together when the SDF changes (`!sdfReady`).  
This means changing only `selectedCascadeForRender` (no SDF change) does NOT
trigger a recompute — correct, since the textures are already populated.  
But if a cascade's parameters change mid-run (e.g., `raysPerProbe`) without an
SDF change, the probe grid will be stale. This is not exposed in the UI currently.

### 4× GPU work on scene switch

Going from 1 to 4 cascades multiplies the cascade dispatch cost by ~4.  
For 32³ × 8 rays × 128 march steps, this is still fast on modern hardware  
(typically <50 ms total), but it shows up in the "Cascade X.XX ms (4 levels)"
timing. If performance becomes an issue, lazy-per-cascade updates (only
recompute the level that was invalidated) would help.

---

## Infrastructure That Was Already Correct

- `updateRadianceCascades()` already looped `for (int i = 0; i < cascadeCount; ++i)` — no change needed.
- `updateSingleCascade(i)` already passed `uCascadeIndex` and `uBaseInterval` — no change needed.
- `destroyCascades()` already looped `cascadeCount` — automatically destroys all 4.
- `probeToWorld()` in the shader correctly uses `uBaseInterval` as cell spacing for all levels.
- `sampleSDF()` boundary guard returns `INF` for out-of-volume positions — C3's over-reaching rays are safe.

---

## What Phase 3 Merging Needs

The N-cascade switch is the prerequisite for merging.  
Merging propagates light from fine to coarse by filling C(N) misses using C(N+1):

1. Dispatch C0 → populates wall-adjacent near-field probes
2. Dispatch C1 → populates mid-field probes; for rays that miss, use C0 as the "near cap"
3. Dispatch C2 → uses C1 as the near cap; etc.

The merge pass needs C(N+1)'s probeGridTexture as a sampler while writing C(N):  
this requires the cascade textures to be readable as samplers (they already are RGBA16F
with `GL_TEXTURE_FETCH_BARRIER_BIT` after the dispatch).

The current selector (C0 Radio → C3 Radio) will be replaced by "merged result"  
once Phase 3 is implemented.
