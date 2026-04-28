# Phase 5d Implementation Learnings -- Non-Co-Located Cascades with Runtime Toggle

**Date:** 2026-04-28
**Branch:** 3d
**Status:** Implemented, compiled (0 errors). Phase 5d visibility check proven no-op analytically. Runtime GI A/B pending.
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

---

## Files Changed

| File | Change |
|---|---|
| `src/demo3d.h` | Added `bool useColocatedCascades`; `int probeTotalPerCascade[MAX_CASCADES]` |
| `src/demo3d.cpp` | Constructor init + memset; `initCascades()` per-cascade branch; `render()` `lastColocated` tracking; `updateSingleCascade()` `uBaseInterval` split + 3 new uniforms; probe readback per-cascade buffers; `renderCascadePanel()` checkbox, cascade count title, fill-rate `n=` label, HelpMarker de-hardcoded |
| `res/shaders/radiance_3d.comp` | New uniforms; `probeToWorld()` -> `uProbeCellSize`; split `atlasTxl`/`upperAtlasTxl`; Phase 5d visibility block |
| `doc/cluade_plan/phase5d_logic_check.md` | Fixed cross-reference: `phase5d_noncolocated_plan.md` -> `phase5d_impl_learnings.md` |
