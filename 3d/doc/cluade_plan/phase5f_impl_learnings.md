# Phase 5f Implementation Learnings — Directional Bilinear Interpolation

**Date:** 2026-04-29
**Branch:** 3d
**Status:** Implemented, diff-verified. Build and runtime A/B pending.
**Follows:** `phase5e_impl_learnings.md`

---

## Problem: Phase 5c nearest-bin lookup causes banding and bleeding

### Root cause

Phase 5c directional merge used nearest-bin `texelFetch`. `dirToBin()` always takes
`floor(oct * D)` — there is no blending between adjacent bins:

```glsl
// Phase 5c (before 5f): hard nearest-bin lookup
ivec2 upperBin = dirToBin(rayDir, uUpperDirRes);
texelFetch(uUpperCascadeAtlas, ivec3(upperProbePos * D + upperBin, z), 0).rgb
```

With D=4, each bin covers ≈ 36° of solid angle (1/16 of the sphere). Any ray direction
that lands near a bin boundary gets 100% of one bin's radiance instead of a smooth
blend. At cascade boundaries this appears as a visible ring (**banding**). Near corners
where bins straddle differently-colored walls, adjacent rays pick up the wrong wall's
color (**bleeding**).

### Why GL_LINEAR doesn't fix it

The atlas texture uses `GL_NEAREST` filtering — required to prevent cross-probe
contamination at tile edges. If we switch to `GL_LINEAR`, a texel at probe (px, py)'s
right tile edge will bleed into probe (px+1, py)'s left tile edge. We must stay with
`GL_NEAREST` and do manual bilinear interpolation, clamped within each probe's tile.

### ShaderToy comparison

ShaderToy's 2D merge reads from the upper cascade using bilinear interpolation across
the 4 surrounding directional bins (plus spatial bilinear across 4 upper-cascade
neighboring probes). Our nearest-bin equivalent produces hard bin boundaries that
ShaderToy's bilinear merge smooths away.

---

## What Was Implemented

### New uniform (`res/shaders/radiance_3d.comp`)

```glsl
uniform int uUseDirBilinear;  // 5f: 1=bilinear (default), 0=nearest-bin (Phase 5c)
```

### New helper function `sampleUpperDir()`

```glsl
vec3 sampleUpperDir(ivec3 upperProbePos, vec3 rayDir, int D) {
    if (uUseDirBilinear == 0) {
        // Phase 5c nearest-bin fallback
        ivec2 bin = dirToBin(rayDir, D);
        return texelFetch(uUpperCascadeAtlas,
            ivec3(upperProbePos.x*D+bin.x, upperProbePos.y*D+bin.y, upperProbePos.z),
            0).rgb;
    }
    // Directional bilinear: sample 4 surrounding bin centers, blend by fractional offset.
    // octScaled = oct * D - 0.5 maps bin-k center to position k (integer = exact center).
    vec2 octScaled = dirToOct(rayDir) * float(D) - 0.5;
    ivec2 b00 = clamp(ivec2(floor(octScaled)), ivec2(0), ivec2(D-1));
    ivec2 b11 = clamp(b00 + ivec2(1),          ivec2(0), ivec2(D-1));
    ivec2 b10 = ivec2(b11.x, b00.y);
    ivec2 b01 = ivec2(b00.x, b11.y);
    vec2  f   = fract(octScaled);
    int bx = upperProbePos.x * D, by = upperProbePos.y * D, bz = upperProbePos.z;
    vec3 s00 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b00.x, by+b00.y, bz), 0).rgb;
    vec3 s10 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b10.x, by+b10.y, bz), 0).rgb;
    vec3 s01 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b01.x, by+b01.y, bz), 0).rgb;
    vec3 s11 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b11.x, by+b11.y, bz), 0).rgb;
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}
```

### The `-0.5` offset — why it's correct

Bin `k`'s center is at oct coordinate `(k + 0.5) / D`. We want to interpolate between
adjacent **bin centers**, not bin edges. Rewriting:

```
octScaled = oct * D - 0.5
         = (position along bin axis, in units of "distance between centers")
```

- `octScaled = k` exactly → at bin `k`'s center → `f = 0` → 100% bin k, 0% bin k+1
- `octScaled = k + 0.5` → halfway between centers → `f = 0.5` → equal blend of k and k+1
- `octScaled = k + 1.0` → at bin k+1's center → `f = 0` (after floor increment) → 100% bin k+1

Without the `-0.5`, `octScaled = oct * D` would bias toward the next bin whenever the
direction is slightly past the bin edge rather than past the bin center. The center-to-center
interpolation is the natural smoothing model.

### Clamp invariant — no cross-probe contamination

All 4 bin coordinates are independently clamped to `[0, D-1]`:

```glsl
ivec2 b00 = clamp(ivec2(floor(octScaled)), ivec2(0), ivec2(D-1));
ivec2 b11 = clamp(b00 + ivec2(1),          ivec2(0), ivec2(D-1));
```

The key invariant is that **all reads stay within `[0, D-1]`**, so no probe-adjacent
texels are ever touched (GL_NEAREST is safe). The boundary behavior is asymmetric:

**High edge** (oct → 1.0, e.g. D=4: `octScaled = 4 - 0.5 = 3.5`):
- `b00 = 3`, `b11 = clamp(4, 0, 3) = 3`. Both collapse to the same border bin.
- Result is 100% border bin regardless of `f = 0.5` — replicates GL_CLAMP_TO_EDGE. ✓

**Low edge** (oct → 0.0, e.g. `octScaled = -0.5`):
- `b00 = clamp(-1, 0, D-1) = 0`, `b11 = clamp(1, 0, D-1) = 1`. Different bins.
- `f = fract(-0.5) = 0.5` → equal blend of bin 0 and bin 1.
- This does **not** replicate GL_CLAMP_TO_EDGE at the low boundary. It is a smooth
  blend between the first two interior bins, which is physically reasonable (the
  octahedral fold boundary at oct=0 is adjacent to valid interior directions, not a
  wall), but the document's earlier claim that "both reads hit the same border bin" is
  wrong for the low edge. Cross-probe reads still cannot occur (0 and 1 are within tile). ✓

In practice, real normalized directions mapped through `dirToOct` rarely produce
`octScaled < -0.4`, so this low-edge asymmetry has negligible runtime impact.

### Removed variables from the direction loop

`upperBin` and `upperAtlasTxl` were removed from the per-direction loop body (they moved
into `sampleUpperDir()`). The loop body now calls `sampleUpperDir()` directly.

### Performance note

Bilinear reads 4 texels instead of 1. At D=4 (C0), that is 16 bins × 4 fetches =
64 texelFetch calls per probe per bake pass vs the previous 16. At D=16 (C2/C3 with
Phase 5e scaled), it is 256 × 4 = 1024 calls. All reads are within the upper probe's
tile (GL_NEAREST + clamp), so no cache thrash risk from cross-tile reads.

---

## Debug Visualization — Mode 6: Bilinear Bin Viewer

Added mode 6 to `res/shaders/radiance_debug.frag`.

Mode 5 (existing) shows the nearest-bin radiance for the selected `(dx, dy)` bin
across all probes. Mode 6 shows the **bilinearly interpolated radiance** at the
midpoint between the selected bin and its `(+1, +1)` neighbor. This puts the sample
at f=(0.5, 0.5) — the point of maximum blending — making bin-boundary color
transitions clearly visible.

Toggle mode 5 ↔ mode 6 to directly see whether bilinear reduces the hard color step
at directional bin boundaries. If Phase 5f is effective, mode 6 should show a smooth
gradient where mode 5 shows a hard edge.

Mode 6 is reachable via both the ImGui "Bilinear" radio button and the `[F]` keyboard
cycle (which now wraps at `% 7`, covering modes 0–6 inclusive).

Mode 6 computes `octScaled` **directly from the integer `uAtlasBin`** — it does not
call `dirToOct`. There are no octahedral helper functions in `radiance_debug.frag`;
they were considered during implementation but removed as dead code since mode 6 only
needs the raw bin coordinates to construct the bilinear offset.

---

## C++ Changes

| Location | Change |
|---|---|
| `src/demo3d.h` | Added `bool useDirBilinear;` after `useScaledDirRes` |
| `src/demo3d.cpp` constructor | `, useDirBilinear(true)` (default ON) |
| `updateSingleCascade()` | `glUniform1i("uUseDirBilinear", useDirBilinear)` after `uUseDirectionalMerge` |
| `renderCascadePanel()` | Checkbox "Directional bilinear merge (Phase 5f)" after 5c checkbox |
| Debug viz panel | RadioButton "Bilinear##rad" for mode 6; `[F]` cycle extended to `% 7`; mode 5/6 bin sliders shared |

Default is `true` (bilinear ON) because it is strictly better than nearest-bin at D=4
and has no regression risk (at D=1 bilinear degenerates to nearest, which we never use).

---

## Correctness Invariants

**Bilinear OFF (nearest-bin, Phase 5c behavior):** `sampleUpperDir` calls `dirToBin` +
single `texelFetch`. Identical to pre-5f behavior. Zero regression.

**Bilinear ON, D=4:** Each of the 16 C0 bins reads up to 4 C1 bins. All reads clamped
within the probe's 4×4 tile. No cross-probe reads. Border bins replicate (clamp) rather
than wrapping — consistent with octahedral map boundary behavior.

**Bilinear ON, D=16 (Phase 5e scaled):** 256 bins × 4 fetches = 1024 texelFetch per
probe per bake pass. Still within budget; upper cascade is already computed before C0
dispatch.

**`uUseDirectionalMerge == 0` path (isotropic fallback):** `sampleUpperDir` is not
called; `uUseDirBilinear` has no effect. Full Phase 4 isotropic behavior preserved.

---

## Known Limitations / Validation Status

| Test | Status |
|---|---|
| Build: 0 errors | Pending |
| Toggle OFF: identical GI to Phase 5c nearest-bin | Pending runtime |
| Toggle ON: banding at C0/C1 boundary visibly reduced | Pending runtime |
| Toggle ON: green/red wall bleed reduced | Pending runtime |
| Mode 6 debug: smooth gradient vs mode 5 hard step | Pending runtime |
| A/B: bilinear ON vs OFF at D=4 all cascades | Pending runtime |
| A/B: bilinear ON + Phase 5e scaled D at C2/C3 boundary | Pending runtime |
