# Phase 9b — TAA-Style History Clamping

**Date:** 2026-05-02
**Trigger:** Pure EMA temporal accumulation produced three failure modes:
1. Color bleeding — stale jitter-position samples bled wall color onto neutral surfaces
2. Slow convergence — low alpha (0.1) required to suppress ghosting; ~22 frames to 90%
3. Reduced visual quality — temporal smoothing suppressed scene detail, not just banding

**Fix class:** TAA-style AABB history clamping, as used in screen-space temporal AA.

---

## Root cause analysis

With probe jitter, each rebuild samples probes at a different world position. The EMA
history at probe (i,j,k) accumulates samples from positions:

```
frame 0: (i + 0.0,  j − 0.17, k − 0.3 ) * cellSize   (Halton index 0)
frame 1: (i − 0.25, j + 0.17, k − 0.1 ) * cellSize   (Halton index 1)
frame 2: (i + 0.25, j − 0.39, k + 0.1 ) * cellSize   (Halton index 2)
...
```

When a sample from a jitter position near the red wall dominates the history, and the
current bake samples a position away from the red wall, the EMA bleeds the stale red tint
across subsequent frames. With alpha=0.1, that stale sample persists with weight
`(0.9)^N` — it takes 22 frames to fall below 10%.

**The fundamental mismatch:** EMA weights all history equally regardless of whether it is
physically consistent with the current frame. A sample from 10 frames ago at a different
world position may represent a radically different lighting environment.

---

## TAA-style AABB clamping

**Standard TAA approach (screen-space):** Before blending, clamp the history color to the
bounding box of a small neighborhood of the current frame's pixels. History values outside
this box cannot be produced by the current scene and are therefore stale — clip them out.

**Probe-grid adaptation:** Same principle, in probe space:
- For each probe in the current bake, compute the AABB of the current-bake values in a
  6-probe cardinal neighborhood (±1 in each probe axis)
- Clamp the history value to this AABB before blending
- Any history value outside the current neighborhood's range is rejected in one frame

**Result:** With clamping, high alpha values (0.3–0.8) are safe. Stale samples are
rejected by the AABB gate immediately rather than decaying over 22+ frames.

| Mode | Alpha needed | Convergence | Color bleeding |
|---|---|---|---|
| EMA only (pre-fix) | 0.05–0.1 | ~22–69 frames | Present — decays slowly |
| AABB clamp (fix) | 0.3–0.8 | 3–8 frames | Eliminated — rejected in 1 frame |

---

## Neighborhood design

### Isotropic probe grid (`uDirRes = 0`)

The probe grid is a 3D texture at `resolution³`. For probe at `(px, py, pz)`, the 6-tap
cardinal neighborhood is:

```
(px±1, py,   pz  )
(px,   py±1, pz  )
(px,   py,   pz±1)
```

Out-of-bounds samples are skipped (edge probes use fewer than 6 taps). AABB min/max
initialized to the center probe's current value, then expanded by each valid neighbor.

### Directional atlas (`uDirRes = D`)

The atlas has layout `(res×D) × (res×D) × res`. Probe `(px, py, pz)` occupies atlas tile
at `(px*D, py*D, pz)` through `(px*D+D-1, py*D+D-1, pz)`.

For atlas texel at `(ax, ay, az)`:
- Probe: `(ax/D, ay/D, az)`
- Direction bin: `(ax%D, ay%D)`

"Same direction bin in adjacent probe" requires moving in probe space while keeping bin
coordinates constant. Stepping by exactly `D` in atlas x (or y) achieves this:

```
(ax + D, ay,     az  ) → probe (ax/D+1, ay/D,   az),   bin (ax%D, ay%D)  ✓
(ax − D, ay,     az  ) → probe (ax/D-1, ay/D,   az),   bin (ax%D, ay%D)  ✓
(ax,     ay + D, az  ) → probe (ax/D,   ay/D+1, az),   bin (ax%D, ay%D)  ✓
(ax,     ay − D, az  ) → probe (ax/D,   ay/D-1, az),   bin (ax%D, ay%D)  ✓
(ax,     ay,     az+1) → probe (ax/D,   ay/D,   az+1), bin (ax%D, ay%D)  ✓
(ax,     ay,     az-1) → probe (ax/D,   ay/D,   az-1), bin (ax%D, ay%D)  ✓
```

**Why ±D preserves bin identity:** `(ax + D) % D == ax % D` for any ax, D. Adding a
multiple of D to the atlas x coordinate does not change the bin index within the probe
tile. This is the key invariant that makes probe-aware clamping correct.

**Why not clamp across direction bins within a probe:** Adjacent bins within a probe
(stepping by ±1 in atlas x) represent different incoming directions. A bin looking toward
the bright ceiling is legitimately far from a bin looking toward the dark floor. Clamping
across bins would over-clip valid directional variation and destroy the directional GI
signal.

---

## Shader: `temporal_blend.comp`

### New uniforms

```glsl
uniform int uClampHistory; // 0=EMA only, 1=AABB clamp before blend
uniform int uDirRes;       // 0=isotropic grid; >0=directional atlas probe stride (D)
```

### AABB clamping logic

```glsl
if (uClampHistory != 0) {
    vec4 nMin = cur, nMax = cur;

    if (uDirRes <= 0) {
        // Isotropic grid: 6-tap cardinal neighborhood
        for (int axis = 0; axis < 3; ++axis) {
            for (int s = -1; s <= 1; s += 2) {
                ivec3 off = ivec3(0); off[axis] = s;
                ivec3 nc  = coord + off;
                if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, uSize))) continue;
                vec4 n = imageLoad(uCurrent, nc);
                nMin = min(nMin, n);
                nMax = max(nMax, n);
            }
        }
    } else {
        // Directional atlas: same direction bin in spatially adjacent probes
        int D = uDirRes;
        ivec3 offsets[6] = ivec3[6](
            ivec3( D, 0, 0), ivec3(-D, 0, 0),
            ivec3( 0, D, 0), ivec3( 0,-D, 0),
            ivec3( 0, 0, 1), ivec3( 0, 0,-1)
        );
        for (int i = 0; i < 6; ++i) {
            ivec3 nc = coord + offsets[i];
            if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, uSize))) continue;
            vec4 n = imageLoad(uCurrent, nc);
            nMin = min(nMin, n);
            nMax = max(nMax, n);
        }
    }

    his = clamp(his, nMin, nMax);
}

imageStore(oHistory, coord, mix(his, cur, uAlpha));
```

### Dispatch: two calls per cascade, different `uDirRes`

```cpp
// Atlas: uDirRes = cascadeDirRes[i] = D
glUniform1i(glGetUniformLocation(tbProg, "uDirRes"), D);
// dispatch atlas blend...

// Grid: uDirRes = 0
glUniform1i(glGetUniformLocation(tbProg, "uDirRes"), 0);
// dispatch grid blend...
```

---

## C++ changes

### `demo3d.h` — new member

```cpp
/** Phase 9b: clamp history to current-neighborhood AABB before EMA blend. */
bool useHistoryClamp;
```

### Constructor

```cpp
, useHistoryClamp(true)   // on by default — TAA-style ghost rejection
```

### `updateSingleCascade()` — pass `uClampHistory` once (shared for both dispatches)

```cpp
glUniform1i(glGetUniformLocation(tbProg, "uClampHistory"), useHistoryClamp ? 1 : 0);
```

### UI — new checkbox and updated alpha tooltip

```
[x] History clamp (TAA-style)    ← defaults ON
    Temporal alpha   [====|   ]  ← tooltip now recommends 0.3-0.8 with clamp ON
```

Alpha tooltip updated:
- With clamp ON: `0.3–0.8 recommended (fast, ghost-free)`
- With clamp OFF: `0.05–0.1 recommended (suppresses ghosting manually)`

---

## Effect on the alpha operating point

Without clamping, low alpha (0.1) was needed to prevent color bleeding over 22+ frames.
With clamping, ghosting is eliminated at the AABB gate regardless of alpha. The new
constraint on alpha is simply convergence speed vs. temporal stability:

```
N_eff = 1 / alpha   (effective EMA sample count)

alpha = 0.5 → N_eff =  2 samples → converges in ~5 frames (95% at ~6 frames)
alpha = 0.3 → N_eff =  3 samples → converges in ~10 frames
alpha = 0.1 → N_eff = 10 samples → converges in ~30 frames (same as before but ghost-free)
```

Recommended default with clamping: **alpha = 0.5**. Fast convergence, ghost-free, stable.

---

## What clamping cannot fix

**Directional banding (Type B):** AABB clamping operates on the values stored in the
current bake. If D=8 still produces visible directional quantization steps, clamping does
not help — it can only prevent history from diverging from current, not fix the current
bake's angular resolution.

**Spatial banding at probe-grid scale:** Clamping the history to the current neighborhood
AABB still allows each probe to converge to its box-filtered value. The spatial step at
probe boundaries remains. Jitter + temporal is still the mechanism for softening this;
clamping just prevents jitter from introducing color bleeding as a side effect.

**Over-clamping in uniform regions:** In spatially uniform GI regions (e.g., center of a
large wall), all 6 neighbors have nearly the same current value. The AABB is very tight.
Any history sample outside this tight box gets clamped — even a valid 2-frame-old sample
that represents the same lighting from a slightly different jitter position. This increases
temporal instability (slight flickering) in exchange for ghost rejection.

The alpha slider is the user-facing control: higher alpha down-weights older samples
faster, reducing the temporal footprint that clamping needs to correct.

---

## RDC analysis — `3d_temporal_fail3.rdc`

Thumbnail extracted via `renderdoccmd.exe thumb`. Visible issues:
- Inner Cornell Box boxes show wall-color tinting (red/green bleeding from adjacent walls)
- This matches the EMA ghost-bleeding failure mode: stale jitter samples from positions
  near the colored walls accumulated in history, bleeding into the box surfaces
- Direct lighting on boxes is unaffected (confirming it is a history-texture artifact,
  not a direct lighting bug)

The AABB clamping fix directly addresses this: the red-tinted history samples are outside
the AABB of the current-bake neighborhood (which samples positions away from the red wall)
and are therefore rejected in one frame.

---

## Files changed

| File | Change |
|---|---|
| `res/shaders/temporal_blend.comp` | Added `uClampHistory`, `uDirRes` uniforms; AABB clamping path for grid and atlas |
| `src/demo3d.h` | Added `useHistoryClamp` member |
| `src/demo3d.cpp` | Constructor init; `uClampHistory` + `uDirRes` uniforms in dispatch; UI checkbox + updated alpha tooltip |
