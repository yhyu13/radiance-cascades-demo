# 07 — Phase 5d: Non-Co-Located Probe Layout

**Purpose:** Understand co-located vs non-co-located cascades, why non-co-located makes
physical sense, and why the visibility check inside Phase 5d is currently inert.

---

## Co-located vs non-co-located — which is default?

**Current default: non-co-located** (`useColocatedCascades = false`).

Co-located mode (all cascades at the same 32³ grid) is **not** the default. It is
available as a debugging toggle.

---

## Co-located: all cascades at the same grid positions

In co-located mode, every cascade level uses the same 32³ probe grid.
Probe (5, 3, 7) in C0 sits at exactly the same world position as probe (5, 3, 7) in C1, C2, C3.

**Advantage:** Simple. `upperProbePos = probePos`. No spatial offset to compute. Upper cascade trilinear displacement is 0, so spatial approximation error is zero.
**Disadvantage:** Wastes compute. C3 covers [2, 8 m]. Placing 32³ = 32,768 probes that
densely across the whole volume for a 2–8 m band is overkill — far-field illumination
varies slowly and doesn't need 12.5 cm probe spacing.

---

## Non-co-located: probe count halves per level

ShaderToy-style layout: each cascade level has half the probe count of the level below.

| Level | Probe count | Spacing | Covers |
|---|---|---|---|
| C0 | 32³ = 32,768 | 0.125 m | [0.02, 0.125 m] — near field needs dense probes |
| C1 | 16³ = 4,096 | 0.250 m | [0.125, 0.5 m] — can be coarser |
| C2 | 8³ = 512 | 0.500 m | [0.5, 2.0 m] — medium sparsity |
| C3 | 4³ = 64 | 1.000 m | [2.0, 8.0 m] — far field, 1 m spacing is fine |

**Why does this make physical sense?**

The probe spacing for each level roughly matches the scale of variation in that level's
radiance. C0 sees contact lighting — it changes rapidly over 12.5 cm (a shadow edge can
appear within 1 probe spacing). C3 sees the ceiling, sky, and distant walls — light from
these sources varies slowly over meters (1 m probe spacing is enough).

The 4× interval growth and 2× probe halving maintain this ratio: each level's probe
spacing is always significantly smaller than its interval width, so no probe misses
relevant variation within its assigned band.

---

## What changes in the code for non-co-located

**Probe resolution per cascade:**
```cpp
int   probeRes = useColocatedCascades ? baseRes    : (baseRes >> i);
float cellSz   = useColocatedCascades ? baseCellSz : volumeSize.x / float(probeRes);
```

**Upper probe index (in shader):**
```glsl
// co-located: upperProbePos = probePos (same index)
// non-co-located: upper level has half the probes, so map by halving
ivec3 upperProbePos = (uUpperToCurrentScale > 0)
    ? (probePos / uUpperToCurrentScale)
    : ivec3(0);
// uUpperToCurrentScale = 1 for co-located, 2 for non-co-located (C1 has half the probes)
```

**Atlas dimensions per cascade (non-co-located, D=4):**
```
C0: 128×128×32 (32³ probes, 4 MB)
C1:  64× 64×16 (16³ probes, 1 MB)
C2:  32× 32× 8 ( 8³ probes, 0.25 MB)
C3:  16× 16× 4 ( 4³ probes, 0.0625 MB)
Total: ~5 MB (vs 16 MB co-located)
```

---

## The probeToWorld change

In Phase 5d, `probeToWorld()` was changed to use a **per-cascade cell size** instead
of always using the base interval (0.125 m):

```glsl
// Before (wrong for non-co-located):
vec3 probeToWorld(ivec3 probePos) {
    return uGridOrigin + (vec3(probePos) + 0.5) * uBaseInterval;  // always 0.125
}

// After Phase 5d (correct):
vec3 probeToWorld(ivec3 probePos) {
    return uGridOrigin + (vec3(probePos) + 0.5) * uProbeCellSize;  // per-cascade
}
```

With co-located mode, all `uProbeCellSize` = 0.125 m (same as before). Zero regression.
With non-co-located, C1 uses 0.250 m, so C1 probe (0,0,0) maps to different world position.

`uBaseInterval` still controls tMin/tMax (the interval formula always uses 0.125 m as
the base unit regardless of mode). `uProbeCellSize` only controls where the probe sits.

---

## The Phase 5d visibility check — and why it never fires

Phase 5d added a visibility check: before merging from the upper cascade, verify that
the upper probe can actually see the current probe's world position. If geometry blocks
the line of sight, mark `upperOccluded = true` and use `vec3(0)` instead of the upper
cascade value.

```glsl
bool upperOccluded = false;
if (uUpperToCurrentScale == 2) {  // non-co-located only
    vec3 wUpper = probeToWorld(upperProbePos);  // upper probe world position
    float distToUpper = length(worldPos - wUpper);
    ivec2 visBin = dirToBin(normalize(worldPos - wUpper), uUpperDirRes);
    float visHit = texelFetch(uUpperCascadeAtlas, visTxl, 0).a;
    if (visHit > 0.0 && visHit < distToUpper * 0.9)
        upperOccluded = true;
}
```

**Why it never fires (analytically):**

The maximum distance between a C0 probe and its C1 parent (in the 2× halving scheme):
```
C0 cell = 0.125 m, so adjacent probes are 0.125 m apart.
C1 cell = 0.250 m, so C1 probes are 0.250 m apart.
Maximum C0-to-parent displacement = √3 × 0.125 / 2 ≈ 0.108 m (half-diagonal of one C0 cell)
```

C1's tMin = 0.125 m. The visibility check looks for a hit stored in C1's atlas at alpha < distToUpper × 0.9 = 0.097 m. But C1 only stores hits beyond tMin = 0.125 m > 0.097 m. So no stored C1 hit can possibly satisfy the condition.

The same gap exists for all pairs (C1→C2: distToUpper≈0.217 m < tMin=0.5 m, etc.).
The interval growth (4×) always outpaces the probe halving (2×), ensuring the parent's
near range exceeds the child-to-parent distance.

**Result:** The check is preserved in code for structural completeness but is a documented
no-op for all current configurations.

---

## Why 1:8 and not 1:4 — ShaderToy is 2D, we are 3D

ShaderToy uses 2D probes. It halves each axis once per level: N → N/2.
In 2D: one upper probe covers a 2×2 block = **4 lower probes**.

We use 3D volumetric probes. Same halve-per-axis rule, three axes:
In 3D: one upper probe covers a 2×2×2 block = **8 lower probes**.

The ratio 1:4 is correct for 2D ShaderToy. Our 1:8 is the correct 3D extension of
the same rule. `baseRes >> i` (32→16→8→4) implements this.

### Why not 4× halving per axis?

If we halved each axis by 4× (matching the 4× interval growth):
```
C0: 32³, spacing 0.125 m
C1:  8³, spacing 0.500 m
C2:  2³, spacing 2.000 m   ← 2 probes across the whole scene
C3: 0.5³                   ← non-integer, broken
```

4× per axis means probe spacing grows at the same rate as the interval. By C3 one probe
covers the entire scene. The requirement is that probe spacing stays sub-interval at every
level so that every scene point has a representative nearby probe. 2× satisfies this; 4× does not.

### Centering — algebraic verification

`probeToWorld` places probe at index j at: `gridOrigin + (j + 0.5) × cellSize`

C1 probe j=0 (cellSize=0.25 m): world = gridOrigin + **0.125 m**

The 8 C0 probes it covers (2j and 2j+1 per axis, i.e. index 0 and 1):
```
C0[0]: gridOrigin + 0.5 × 0.125 = gridOrigin + 0.0625 m
C0[1]: gridOrigin + 1.5 × 0.125 = gridOrigin + 0.1875 m
centroid:                          gridOrigin + 0.1250 m  ✓
```

The `+ 0.5` convention in `probeToWorld` guarantees this algebraically at every level.
In general: C_i probe j covers C_{i-1} probes 2j and 2j+1; centroid = (j + 0.5) × new_cellSize.
This matches the C_i probe position exactly. No code change needed.

### Centering is necessary but not sufficient

Even with correct centering, reading only **one** upper probe is an approximation. A C0
probe at position P reads C1's answer for the centroid, which is up to half a C1 cell away:

```
max displacement = √3 × (C1 cellSize / 2) = √3 × 0.125 ≈ 0.217 m
```

For a C1 interval of [0.125, 0.5 m], a 0.217 m spatial offset is large (1.7× the interval width).

The correct fix is **spatial trilinear interpolation across the 8 nearest upper-cascade
probes** — "trilinear" because we blend in all 3 spatial dimensions (8 = 2³ corners).
ShaderToy's `WeightedSample()` is the 2D analogue: bilinear across 4 neighbors (2² = 4).

The formula (from Codex analysis `09_phase5d_trilinear_upper_lookup.md`):

```glsl
// Convert world position to upper probe-center space
// The -0.5 is the same center-to-center trick as directional bilinear in Phase 5f
vec3 upperGrid = (worldPos - uGridOrigin) / upperCellSize - 0.5;
ivec3 p000 = clamp(ivec3(floor(upperGrid)), ivec3(0), upperRes - ivec3(2));
vec3  f    = fract(upperGrid);

// Read one directional bin from each of the 8 surrounding upper probes
vec3 s000 = sampleUpperProbeDir(p000,                 rayDir, Du);
vec3 s100 = sampleUpperProbeDir(p000 + ivec3(1,0,0), rayDir, Du);
// ... 6 more corners ...

// Trilinear blend
vec3 sx00 = mix(s000, s100, f.x);  vec3 sx10 = mix(s010, s110, f.x);
vec3 sx01 = mix(s001, s101, f.x);  vec3 sx11 = mix(s011, s111, f.x);
vec3 sxy0 = mix(sx00, sx10, f.y);  vec3 sxy1 = mix(sx01, sx11, f.y);
upperDir  = mix(sxy0, sxy1, f.z);
```

Note the `-0.5` offset: integer upper-grid index i corresponds to center position
`(i + 0.5) × cellSize`. Subtracting 0.5 maps that center to integer i, so `fract()`
gives the fractional position between probe centers — exactly the same reasoning as the
directional bilinear `-0.5` offset in `sampleUpperDir()`.

The clamp uses `upperRes - 2` (not `- 1`) so that `p000 + ivec3(1)` never goes out of bounds.

**Visibility weighting should be per-neighbor, not global:**
The current Phase 5d check fires once and zeroes the entire upper contribution. The correct
design assigns a visibility weight to each of the 8 corners and renormalizes. Zeroing
globally discards 7 potentially unoccluded neighbors — worse than ignoring the check.

**Spatial trilinear IS implemented** (`useSpatialTrilinear = true`, ON by default). The 8-corner
blend described above is the live code path. Co-located mode sidesteps the need for it (displacement = 0,
all 8 neighbors coincide), but non-co-located (the default) requires and uses it.

| Question | Answer |
|---|---|
| ShaderToy ratio | 1:4 (2D: one upper covers 2×2 lower) |
| Our 3D ratio | 1:8 (3D: one upper covers 2×2×2 lower) |
| Centering correct? | Yes — proven algebraically via `+0.5` convention |
| Centering enough? | No — spatial **trilinear** across 8 upper neighbors also needed |
| Do we have spatial trilinear? | **Yes** — `useSpatialTrilinear=true`, ON by default |
| Visibility weighting | Structurally wrong (global zero); should be per-neighbor weighted sum |
| When does gap not matter? | Co-located mode: displacement = 0, all 8 corners the same |

---

## Atlas debug mode 3 reveals the layout

Debug mode 3 (Atlas slice) shows one Z-slice of the atlas texture. With non-co-located
mode ON:
- C0 atlas: 128×128 tile grid (32×32 probes × 4×4 bins each)
- C3 atlas: 16×16 tile grid (4×4 probes × 4×4 bins each)

The visual density difference confirms which cascade is being displayed.
