# Phase 5d Implementation Learnings -- Non-Co-Located Cascades with Runtime Toggle

**Date:** 2026-04-28
**Branch:** 3d
**Status:** Layout toggle: implemented, compiled (0 errors), runtime A/B pending. Visibility-weighting: implemented but inert (proven analytically -- distToUpper ~= 0.108m < tMin_upper = 0.125m for all cascade pairs).
**Follows:** `phase5d_logic_check.md` (co-location no-op proof) + `phase5bc_impl_learnings.md`

---

## What Was Implemented

### Toggle field (`src/demo3d.h`)

```cpp
bool useColocatedCascades;               // true=all 32^3 (default); false=ShaderToy halving
int  probeTotalPerCascade[MAX_CASCADES]; // per-cascade probe count for fill-rate display
```

`probeTotalPerCascade[ci]` is filled during probe readback so the UI fill-rate
denominator is per-cascade rather than always `probeTotal` (C0's 32^3=32768).

### `initCascades()` -- per-cascade resolution

Old: `const int probeRes = 32; const float cellSz = 0.125;` applied to all levels.

New: branches on the toggle:
```cpp
const int   baseRes    = 32;
const float baseCellSz = volumeSize.x / float(baseRes);  // 0.125

int   probeRes = useColocatedCascades ? baseRes    : (baseRes >> i);   // 32,16,8,4
float cellSz   = useColocatedCascades ? baseCellSz : volumeSize.x / float(probeRes);
```

Non-co-located atlas sizes at D=4: C0=128x128x32, C1=64x64x16, C2=32x32x8, C3=16x16x4.

### `render()` toggle tracking -- destroy+rebuild on switch

A `static bool lastColocated = true` block detects changes. Setting `cascadeReady = false`
alone is insufficient because texture *dimensions* change. The block calls:
```cpp
destroyCascades();
initCascades();
cascadeReady = false;   // this triggers probeDumped=false in the Pass 3 block below
```

**Gotcha logged:** `probeDumped` is a `static bool` declared later in `render()` (Pass 3
block). Referencing it from the `lastColocated` block -- which appears before the
declaration -- causes `C2065: undeclared identifier`. Fix: setting `cascadeReady = false`
is sufficient, because the Pass 3 block `if (!cascadeReady) { probeDumped = false; ... }`
runs immediately after. Do not move `probeDumped` declaration earlier; leave it scoped to
where the probe readback logic lives.

### `updateSingleCascade()` -- critical `uBaseInterval` split

Old: `glUniform1f("uBaseInterval", c.cellSize)` -- this served dual duty as both the
interval base for tMin/tMax and the probe cell size for `probeToWorld()`.

In non-co-located mode `c.cellSize` varies per cascade (0.125, 0.25, 0.5, 1.0). If
this were passed as `uBaseInterval`, the interval formula breaks:
```glsl
float d = uBaseInterval;
// C1 tMin = d = 0.25,  tMax = 4*d = 1.0  -- WRONG (should be 0.125 and 0.5)
```

Fix -- two separate uniforms:
```cpp
glUniform1f(glGetUniformLocation(prog, "uBaseInterval"),  cascades[0].cellSize);  // always 0.125
glUniform1f(glGetUniformLocation(prog, "uProbeCellSize"), c.cellSize);             // per-cascade
```

Additionally three new uniforms:
```cpp
// upperToCurrentScale: 0=no upper, 1=co-located (same probe index), 2=non-co-located (halved)
int upperToCurrentScale = hasUpper ? (useColocatedCascades ? 1 : 2) : 0;
glUniform1i(glGetUniformLocation(prog, "uUpperToCurrentScale"), upperToCurrentScale);
glUniform1f(glGetUniformLocation(prog, "uUpperProbeCellSize"), upperProbeCellSz);
```

### Shader changes (`res/shaders/radiance_3d.comp`)

**`probeToWorld()`**: now uses `uProbeCellSize` instead of `uBaseInterval`. `uBaseInterval`
is still present and still used only for the tMin/tMax interval formula.

**Inner loop -- split atlas texel address into write and read:**

Before: single `atlasTxl` for both writing the current cascade and reading the upper one.
This was correct only for co-located grids (same probe index -> same texel).

After:
```glsl
// Write target -- always current probe
ivec3 atlasTxl = ivec3(probePos.x * uDirRes + dx, probePos.y * uDirRes + dy, probePos.z);

// Upper probe index (0=no upper, 1=same, 2=halved)
ivec3 upperProbePos  = (uUpperToCurrentScale > 0) ? (probePos / uUpperToCurrentScale) : ivec3(0);
ivec3 upperAtlasTxl  = ivec3(upperProbePos.x * uDirRes + dx,
                              upperProbePos.y * uDirRes + dy, upperProbePos.z);

upperDir = texelFetch(uUpperCascadeAtlas, upperAtlasTxl, 0).rgb;
```

Co-located (`uUpperToCurrentScale=1`): `upperProbePos = probePos`, so `upperAtlasTxl ==
atlasTxl`. Exactly Phase 5c behaviour -- zero regression.

Non-co-located (`uUpperToCurrentScale=2`): `upperProbePos = probePos/2`. For C0 probe
at `(31,31,31)`: `upperProbePos=(15,15,15)`, `upperAtlasTxl.x = 15*4+dx in [60,63]` <
64 (C1 atlas width 64). Bounds are safe by construction.

**Phase 5d visibility block:**

Activated only when `uUpperToCurrentScale == 2`:
```glsl
vec3 wUpper = uGridOrigin + (vec3(upperProbePos) + 0.5) * uUpperProbeCellSize;
vec3 toCurrentFromUpper = worldPos - wUpper;
float distToUpper = length(toCurrentFromUpper);
if (distToUpper > 1e-4) {
    ivec2 visBin = dirToBin(toCurrentFromUpper / distToUpper, uDirRes);
    ivec3 visTxl = ivec3(upperProbePos.x * uDirRes + visBin.x,
                          upperProbePos.y * uDirRes + visBin.y, upperProbePos.z);
    float visHit = texelFetch(uUpperCascadeAtlas, visTxl, 0).a;
    if (visHit > 0.0 && visHit < distToUpper * 0.9)
        upperDir = vec3(0.0);
}
```

The threshold `0.9` gives a 10% margin. `visHit` stores the hit distance (t value from
`raymarchSDF`). If the upper probe's ray toward the current probe hit a surface at
`visHit < distToUpper * 0.9`, the upper probe cannot see the current probe's world
position -- its radiance estimate belongs to a different lighting context.

### Probe readback -- per-cascade buffer sizing

Old: single `buf` (32^3*4 floats) and `atlasBuf` ((32*D)^2*32*4 floats) allocated once,
reused for all cascades. For non-co-located C3 (4^3=64 probes), accessing `buf` beyond
index 63 reads garbage.

New: per-cascade allocation inside the loop:
```cpp
int res     = cascades[ci].resolution;  // 32,16,8,4
int ciTotal = res * res * res;
int atlasWH = res * dirRes;
probeTotalPerCascade[ci] = ciTotal;

std::vector<float> ciBuf(ciTotal * 4);
std::vector<float> atlasBuf(atlasWH * atlasWH * res * 4);
```

All lum/variance/histogram loops use `ciTotal` and `ciBuf`. Spot-samples (center +
backwall) are still taken from C0 (always 32^3) via a separate `buf` allocation after
the loop.

### `renderCascadePanel()` -- UI updates

1. **Cascade count title** shows current mode dynamically:
   ```cpp
   ImGui::Text("Cascade Count: %d  [%s]", cascadeCount,
       useColocatedCascades ? "co-located, all 32^3" : "non-co-located, 32/16/8/4");
   ```
   HelpMarker updated to describe both modes instead of hardcoding "all 32^3".

2. **Phase 5d checkbox** with HelpMarker explaining the co-located vs ShaderToy trade-off,
   placed between the Phase 5c directional merge checkbox and the env fill section.

3. **Cascade hierarchy table**: `cellSize` per cascade column added; stale `r=N rays` label
   replaced with `D^2=16 rays` to match the fixed dispatch. Example output:
   ```
   C0: 32^3  cell=0.1250m  [0.020, 0.125]m  D^2=16 rays
   C1: 16^3  cell=0.2500m  [0.125, 0.500]m  D^2=16 rays   (non-co-located)
   ```

4. **Fill-rate denominators**: use `ciProbeTot = probeTotalPerCascade[ci]` (falls back to
   `probeTotal` if not yet populated). Fill-rate row format changed from `r=%2d` (stale
   raysPerProbe) to `n=%d` (actual probe count for that cascade level).
   HelpMarker updated: removed hardcoded "32^3 = 32768" and non-ASCII em dash characters.

5. **Probe-coverage bars**: same `ciProbeTot` denominator (reused from fill-rate block,
   scoped within the per-cascade loop body).

---

## Files That Needed No Changes

| File | Reason |
|---|---|
| `res/shaders/reduction_3d.comp` | Uses `uVolumeSize` pushed per-cascade; atlas indexing correct for any res |
| `res/shaders/radiance_debug.frag` | `probeFromUV()` uses `uVolumeSize`; pushed from `renderRadianceDebug()` as `cascades[selC].resolution` -- already per-cascade |
| `res/shaders/raymarch.frag` | Normalizes to [0,1]; works regardless of probe resolution |

---

## Correctness Invariants

**Co-located ON (default) -- zero regression:**
- `uProbeCellSize == uBaseInterval == 0.125`
- `uUpperToCurrentScale == 1` -> `upperProbePos = probePos` -> `upperAtlasTxl == atlasTxl`
- Phase 5d block: `uUpperToCurrentScale != 2` -> skipped entirely

**Non-co-located (ShaderToy-style):**
- `uBaseInterval = 0.125` always for interval formula
- `uProbeCellSize` per cascade: 0.125 / 0.25 / 0.5 / 1.0
- `upperProbePos = probePos / 2`: maps [0,31] -> [0,15] for C0->C1
- Atlas bounds: `upperProbePos * D + dx` in [0,63] < 64 (C1 atlas width). Safe.
- Phase 5d visibility: `wUpper` is 0.108m from `worldPos` (max 3D, 1 cascade step) --
  less than `tMin_upper = 0.125m`, so the check never fires (see no-op analysis below)

---

## Phase 5d Visibility Check: Structural No-Op

**The Phase 5d visibility check cannot fire with our current interval scheme.**

Proof: For C0->C1 non-co-located, the 3D Euclidean distance from a C0 probe to its C1
parent is at most `sqrt(3) * cellSz_C0/2 = sqrt(3) * 0.0625 ~= 0.108m`. C1's `tMin = d = 0.125m`.

The atlas alpha `visHit` stores the hit `t` value for surface hits, which is `>= tMin`.
The check condition `visHit < distToUpper * 0.9` requires `tMin <= visHit < 0.108 * 0.9 = 0.097m`.
Since `tMin = 0.125m > 0.097m`, this is impossible. **The check never fires for any cascade pair.**

The same analysis holds for C1->C2 (`distToUpper ~= 0.217m < tMin_C2 = 0.5m`) and
C2->C3 (`distToUpper ~= 0.433m < tMin_C3 = 2.0m`).

**Root cause:** the cascade intervals scale 4x per level while probe resolution halves.
The gap between the probe spacing and the next cascade's tMin is always >= 15%.

**Impact:** Phase 5d is implemented but has no effect. The non-co-located mode still
produces different GI from co-located mode because the upper probes are at coarser
spatial positions (2x sparser per axis), so the merge reads from a different world-space
location instead of the exact same position.

**Possible future fix:** Use `distAlongRay = dot(worldPos - wUpper, rayDir)` (projection
onto the current ray direction, which starts at tMin_upper and extends to tMax_upper)
instead of the Euclidean distance `distToUpper`. If the current probe is within the upper
cascade's ray interval along that direction, the check could fire. But this changes the
semantics from "can the upper probe see the current probe" to "does the upper probe's ray
in this direction hit geometry before the current probe". Deferred to Phase 5e analysis.

---

## Known Limitations / Validation Status

| Test | Status |
|---|---|
| Build: 0 errors | OK |
| Phase 5d visibility check fires | NEVER (proven analytically -- distToUpper < tMin_upper) |
| Debug shaders correct for non-co-located | OK (code-reviewed: probeFromUV uses uVolumeSize pushed per-cascade) |
| Reduction pass correct for non-co-located | OK (code-reviewed: dispatch and bounds match c.resolution) |
| Toggle ON: identical GI vs pre-change | Pending runtime |
| Toggle OFF: log shows C3=4^3, C2=8^3 | Pending runtime |
| Toggle OFF: no GL errors | Pending runtime |
| Toggle OFF + Bin viewer (C3 shows 4x4 coarse probes) | Pending runtime |
| Live toggle (destroy+rebuild): no crash | Pending runtime |

**Expected behaviour in non-co-located mode:**
- UI: cascade count title changes to `[non-co-located, 32/16/8/4]`; each row shows
  `C3: 4^3 cell=1.0000m`, `C2: 8^3 cell=0.5000m` etc.
- Fill-rate: `C3 n=64` (4^3), `C2 n=512` (8^3), `C1 n=4096` (16^3), `C0 n=32768` (32^3)
- Bin viewer for C3: 4x4 coarse probes in the 400x400 debug viewer (each probe = 100px)
- GI may differ subtly from co-located: upper probes 2x coarser per axis, so the merge
  reads from a different world-space location. No interpolation between upper neighbors
  (blocky 2x2x2 group reads the same parent) -- potential cascade-boundary seam artifact.
- Phase 5d visibility weighting: no visual effect (check never fires per analytic proof)

**Open cleanup:** The Phase 5d checkbox HelpMarker in `src/demo3d.cpp:2077-2081` still says "Phase 5d probe visibility check is meaningful" and "upper probes behind a wall zero their contribution" -- text that contradicts the analytic no-op proof. The tooltip should be updated to state the check is currently inert under the 4x-interval / 2x-halving scheme.

---

## Files Changed

| File | Change |
|---|---|
| `src/demo3d.h` | Added `bool useColocatedCascades`; `int probeTotalPerCascade[MAX_CASCADES]` |
| `src/demo3d.cpp` | Constructor init + memset; `initCascades()` per-cascade branch; `render()` `lastColocated` tracking; `updateSingleCascade()` `uBaseInterval` split + 3 new uniforms; probe readback per-cascade buffers; `renderCascadePanel()` checkbox, cascade count title, fill-rate `n=` label, HelpMarker de-hardcoded |
| `res/shaders/radiance_3d.comp` | New uniforms; `probeToWorld()` -> `uProbeCellSize`; split `atlasTxl`/`upperAtlasTxl`; Phase 5d visibility block |
| `doc/cluade_plan/phase5d_logic_check.md` | Fixed cross-reference: `phase5d_noncolocated_plan.md` -> `phase5d_impl_learnings.md` |

---

## Design Rationale Addendum (2026-04-29)

### Why 1:8 (not 1:4) — ShaderToy is 2D, we are 3D

The ShaderToy reference uses **2D probes**. It halves each spatial axis once per level:
N → N/2. In 2D, one upper probe covers a 2×2 block = **4 lower probes**.

We use **3D volumetric probes**. The same halve-per-axis rule applies:
N → N/2 per axis. In 3D, one upper probe covers a 2×2×2 block = **8 lower probes**.

`baseRes >> i` implements this: 32→16→8→4, halving each axis once per level.
The ratio 1:4 is the 2D answer; 1:8 is the correct 3D extension of the same rule.

### Why not 4× halving per axis?

If we halved each axis by 4× (instead of 2×) to match the 4× interval growth:
```
C0: 32³, spacing 0.125 m
C1:  8³, spacing 0.500 m
C2:  2³, spacing 2.000 m   ← spacing equals C2's tMin; barely 2 probes across the scene
C3: 0.5³                   ← non-integer, degenerate
```

4× per axis means probe spacing grows at the same rate as interval tMin. By C3, one probe
covers the entire scene. The correctness requirement is that probe spacing stays
**sub-interval** at every level so that each point in the scene is close enough to a probe
that the probe's answer is representative. 2× per axis satisfies this; 4× does not.

### Probe centering — algebraic proof

`probeToWorld()` places probe at index j at: `gridOrigin + (j + 0.5) × cellSize`

C1 probe j=0 (cellSize=0.25 m): world = gridOrigin + **0.125 m**

The 2×2×2 block of C0 probes it covers (per axis: index 0 and 1):
```
C0[0]: gridOrigin + 0.5 × 0.125 = gridOrigin + 0.0625 m
C0[1]: gridOrigin + 1.5 × 0.125 = gridOrigin + 0.1875 m
centroid:                          gridOrigin + 0.1250 m  ✓
```

The `+ 0.5` convention in `probeToWorld` guarantees centering algebraically for any level.
This is not a coincidence — for the general case, C_i probe j covers C_{i-1} probes 2j
and 2j+1. Their centroid is `(2j + 0.5 + 2j + 1.5)/2 × old_cellSize = (j + 0.5) × 2 × old_cellSize
= (j + 0.5) × new_cellSize`. This matches the C_i probe position exactly.

**No code change needed.** The `+ 0.5` convention makes centering automatic at all levels.

### Centering is necessary but not sufficient — the remaining gap

Even with correct centering, reading only one upper probe is an approximation. A C0 probe
at position P reads C1's answer for the centroid of its 8-probe block, which is up to
**half a C1 cell away** from P:

```
max displacement (C0 probe to C1 parent) = √3 × (C1 cellSize / 2) = √3 × 0.125 ≈ 0.217 m
```

For a C1 interval of [0.125, 0.5 m], a 0.217 m spatial error is large (the offset is
1.7× the interval width).

The correct fix is **spatial trilinear interpolation across the 8 nearest upper-cascade
probes** (trilinear = 8-neighbor blend in 3D; ShaderToy's `WeightedSample()` is the 2D
analogue blending 4 neighbors). The full implementation math (from Codex doc
`09_phase5d_trilinear_upper_lookup.md`):

**Step 1 — map world position into upper probe-center space:**
```glsl
// -0.5 converts edge-aligned grid coordinates to center-aligned, same pattern as
// directional bilinear's -0.5 offset in sampleUpperDir()
vec3 upperGrid = (worldPos - uGridOrigin) / upperCellSize - 0.5;
// clamp to upperRes-2, not upperRes-1, so p000+1 never goes out of bounds
ivec3 p000 = clamp(ivec3(floor(upperGrid)), ivec3(0), upperRes - ivec3(2));
vec3  f    = fract(upperGrid);
```

**Step 2 — read directional sample from each of the 8 corner upper probes:**
```glsl
vec3 sampleUpperProbeDir(ivec3 p, vec3 rayDir, int Du) {
    ivec2 bin = dirToBin(rayDir, Du);
    return texelFetch(uUpperCascadeAtlas,
        ivec3(p.x * Du + bin.x, p.y * Du + bin.y, p.z), 0).rgb;
}
vec3 s000 = sampleUpperProbeDir(p000,                 rayDir, Du);
vec3 s100 = sampleUpperProbeDir(p000 + ivec3(1,0,0), rayDir, Du);
// ... s010, s110, s001, s101, s011, s111 (8 total)
```

**Step 3 — trilinear blend:**
```glsl
vec3 sx00 = mix(s000, s100, f.x);  vec3 sx10 = mix(s010, s110, f.x);
vec3 sx01 = mix(s001, s101, f.x);  vec3 sx11 = mix(s011, s111, f.x);
vec3 sxy0 = mix(sx00, sx10, f.y);  vec3 sxy1 = mix(sx01, sx11, f.y);
vec3 upperDir = mix(sxy0, sxy1, f.z);
```

**Visibility weighting attaches per-neighbor, not globally:**
The current Phase 5d visibility check fires once and zeroes the entire upper contribution.
The correct design weights each of the 8 upper probes individually, then renormalizes:
```glsl
// per corner Pi: w = trilinear_weight(Pi) * vis_weight(Pi, worldPos, rayDir)
// result: accum / wsum  (fallback vec3(0) if wsum == 0)
```
Our current global-zero approach discards good data from 7 unoccluded neighbors when
only 1 is blocked — worse than ignoring the check entirely.

**Our implementation reads only the single nearest upper probe.** This is the remaining
structural gap vs ShaderToy in non-co-located mode. Co-located mode sidesteps the issue
(displacement = 0 m; nearest probe IS the correct probe; all 8 neighbors are identical).

### Summary

| Question | Answer |
|---|---|
| ShaderToy ratio | 1:4 (2D: one upper covers 2×2 lower) |
| Our 3D ratio | 1:8 (3D: one upper covers 2×2×2 lower) |
| Centering correct? | Yes — algebraically proven via the `+0.5` convention |
| Centering enough? | No — spatial **trilinear** across 8 upper neighbors also needed |
| Do we have spatial trilinear? | No. Single nearest probe only. Known gap. |
| Visibility weighting | Structurally wrong (global zero); should be per-neighbor weighted sum |
| When does gap not matter? | Co-located mode: displacement = 0 m |

---

## Phase 5d Trilinear Implementation (2026-04-29)

**Status:** Implemented, pending build verification. Closes the structural gap documented above.

### What was implemented

**Goal:** replace the blocky `probePos / 2` single-parent lookup in non-co-located mode
with 8-neighbor spatial trilinear interpolation. The global `upperOccluded` visibility
check was removed (it is analytically inert for all cascade pairs; a per-neighbor version
would be correct but adds 8× cost for zero measured benefit).

### Critical math bug found and fixed (Codex review F1)

The initial trilinear plan had a border-weight bug matching the Phase 5f low-edge issue:

```glsl
// WRONG — fract computed from unclamped value
vec3 upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5;
triP000 = clamp(ivec3(floor(upperGrid)), ivec3(0), max(uUpperVolumeSize - ivec3(2), ivec3(0)));
triF    = fract(upperGrid);  // ← bug: -0.25 gives fract=0.75 → 75% toward probe 1
```

Concrete failure for first C0 probe (worldPos-origin = 0.0625m, upperCellSize = 0.25m):
- `upperGrid = 0.0625/0.25 - 0.5 = -0.25`
- `floor(-0.25) = -1`, clamped to 0 → `triP000.x = 0` ✓
- `fract(-0.25) = 0.75` → 75% weight toward probe 1 ✗ (should be 100% probe 0)

**Fix — clamp the continuous coordinate BEFORE floor/fract:**

```glsl
vec3 upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5;
// Same invariant as Phase 5f directional bilinear:
//   octScaled = clamp(dirToOct(dir)*D - 0.5, 0, D-1)
// At low border:  clamped=0   → floor=0,  fract=0 → 100% probe 0 ✓
// At high border: clamped=N-1 → floor=N-1, fract=0 → 100% probe N-1 ✓
vec3 upperGridClamped = clamp(upperGrid, vec3(0.0), vec3(uUpperVolumeSize - ivec3(1)));
triP000 = ivec3(floor(upperGridClamped));
triF    = fract(upperGridClamped);
```

This pattern (`clamp(continuousCoord, 0, N-1)` before `floor`/`fract`) is now consistent
across both directional (Phase 5f, octahedral space) and spatial (Phase 5d, world space)
interpolation.

### New `sampleUpperDirTrilinear()` function

```glsl
vec3 sampleUpperDirTrilinear(ivec3 triP000, vec3 triF, vec3 rayDir, int Du) {
    ivec3 hi   = uUpperVolumeSize - ivec3(1);  // max valid probe index per axis
    ivec3 p100 = clamp(triP000 + ivec3(1,0,0), ivec3(0), hi);
    // ... 6 more +1 corners, all clamped to hi
    vec3 s000 = sampleUpperDir(triP000, rayDir, Du);  // Phase 5f bilinear inside
    // ... 7 more corners
    vec3 sx00 = mix(s000, s100, triF.x); vec3 sx10 = mix(s010, s110, triF.x);
    vec3 sx01 = mix(s001, s101, triF.x); vec3 sx11 = mix(s011, s111, triF.x);
    vec3 sxy0 = mix(sx00, sx10, triF.y); vec3 sxy1 = mix(sx01, sx11, triF.y);
    return mix(sxy0, sxy1, triF.z);
}
```

Key design: each corner delegates to `sampleUpperDir()`, which already handles Phase 5f
directional bilinear. Each `+1` corner is clamped to `hi = uUpperVolumeSize - ivec3(1)`.
At the high spatial border `triF = 0.0`, so the clamped `+1` samples have zero blend
weight — correct GL_CLAMP_TO_EDGE semantics without border probes.

**Why `uUpperVolumeSize - ivec3(1)` (not `- ivec3(2)`):**
The old plan used `- ivec3(2)` on the *integer* base corner to ensure `p000+1` didn't go
OOB. But this was paired with unclamped `fract`, giving wrong weights. After clamping the
continuous coord to `[0, N-1]`, `floor(N-1) = N-1`, so `p000.max = N-1` and
`p000+1 = N` — only safe if we then clamp `p000+1` to `hi = N-1`. The two-clamp
approach (continuous coord + each +1 offset) is safer and cleaner.

### New uniforms

```glsl
uniform ivec3 uUpperVolumeSize;     // upper cascade probe grid dims (for +1 clamping)
uniform int   uUseSpatialTrilinear; // 1=8-neighbor trilinear, 0=nearest-parent
```

### C++ changes

| Location | Change |
|---|---|
| `demo3d.h` | `bool useSpatialTrilinear;` (default true) |
| constructor | `, useSpatialTrilinear(true)` |
| `render()` | tracking block: sets `cascadeReady=false` on toggle (no atlas rebuild needed) |
| `updateSingleCascade()` | pushes `uUpperVolumeSize = ivec3(cascades[ci+1].resolution)` and `uUseSpatialTrilinear` |
| `renderCascadePanel()` | checkbox disabled when `useColocatedCascades` is true |

### Routing logic in the direction loop

```glsl
if (uHasUpperCascade != 0) {
    if (uUseDirectionalMerge != 0) {
        if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0)
            upperDir = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
        else
            upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
    } else if (uUseDirBilinear != 0) {
        upperDir = texture(uUpperCascade, uvwProbe).rgb;
    } else {
        upperDir = texelFetch(uUpperCascade, upperProbePos, 0).rgb;
    }
}
```

Co-located (`uUpperToCurrentScale=1`): trilinear branch not taken; `sampleUpperDir` used
with `upperProbePos=probePos` (displacement=0, exact). **Zero regression.**

Non-co-located + trilinear OFF: `sampleUpperDir(probePos/2, ...)` — same as original
Phase 5d. **Zero regression.**

### Scope limitation

This implements the **spatial interpolation half** of ShaderToy's `WeightedSample()`.
The per-neighbor visibility weighting (second half) is intentionally deferred:
- Current visibility check is analytically inert (distToUpper < tMin_upper)
- Per-corner weighting would add 8× cost for a check that has never fired
- A per-neighbor version would be correct but is not needed for the current quality target
