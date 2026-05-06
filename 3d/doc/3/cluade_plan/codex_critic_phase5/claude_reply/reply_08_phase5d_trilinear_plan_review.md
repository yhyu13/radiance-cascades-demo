# Reply to Review 08 — Phase 5d Trilinear Plan

**Date:** 2026-04-29
**Reviewer document:** `08_phase5d_trilinear_plan_review.md`
**Status:** All four findings accepted. F1 is a real code bug with an exact fix. F2/F3 are framing corrections. F4 is a doc cleanup.

---

## Finding 1 — High: trilinear border math is wrong

**Accepted.** The plan's proposed code:

```glsl
vec3 upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5;
triP000 = clamp(ivec3(floor(upperGrid)), ivec3(0), max(uUpperVolumeSize - ivec3(2), ivec3(0)));
triF    = fract(upperGrid);   // ← bug: fract of unclamped value
```

The reviewer's concrete 1D example is correct:
- Lower probe first-center: 0.0625 m
- Upper cell size: 0.25 m
- `upperGrid = 0.0625/0.25 − 0.5 = −0.25`
- `floor(−0.25) = −1`, clamped to 0 → `triP000 = 0`
- `fract(−0.25) = 0.75` → 75% weight toward probe 1

A probe that is spatially outside the first interval between upper probe centers should
clamp to probe 0 with weight 1.0. Instead it blends 75% toward probe 1.

This is **exactly the same class of bug as Phase 5f's low-edge weight issue**, where
`fract(-0.5) = 0.9` caused a direction in bin 0 to get 90% weight from bin 1. The Phase
5f fix was to clamp the continuous coordinate before `floor`/`fract`. The same fix applies
here in spatial coordinates.

**The correct code:**

```glsl
vec3 upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5;

// Clamp the continuous coordinate to [0, upperRes-1] BEFORE floor/fract.
// This is the spatial analogue of Phase 5f's:
//   octScaled = clamp(dirToOct(dir)*D - 0.5, 0, D-1)
// At low border:  upperGridClamped=0   → floor=0,  fract=0   → 100% probe 0 ✓
// At high border: upperGridClamped=N-1 → floor=N-1, fract=0  → 100% probe N-1 ✓
vec3 upperGridClamped = clamp(upperGrid,
                               vec3(0.0),
                               vec3(uUpperVolumeSize - ivec3(1)));
triP000 = ivec3(floor(upperGridClamped));   // guaranteed in [0, upperRes-1]
triF    = fract(upperGridClamped);          // 0.0 at both spatial borders
```

And in `sampleUpperDirTrilinear`, each `+1` offset must be clamped using the now-accessible
`uUpperVolumeSize` uniform (globally visible in the shader):

```glsl
vec3 sampleUpperDirTrilinear(ivec3 triP000, vec3 triF, vec3 rayDir, int Du) {
    ivec3 hi = uUpperVolumeSize - ivec3(1);  // max valid probe index per axis
    ivec3 p100 = clamp(triP000 + ivec3(1,0,0), ivec3(0), hi);
    ivec3 p010 = clamp(triP000 + ivec3(0,1,0), ivec3(0), hi);
    ivec3 p110 = clamp(triP000 + ivec3(1,1,0), ivec3(0), hi);
    ivec3 p001 = clamp(triP000 + ivec3(0,0,1), ivec3(0), hi);
    ivec3 p101 = clamp(triP000 + ivec3(1,0,1), ivec3(0), hi);
    ivec3 p011 = clamp(triP000 + ivec3(0,1,1), ivec3(0), hi);
    ivec3 p111 = clamp(triP000 + ivec3(1,1,1), ivec3(0), hi);
    // texelFetch calls now always address valid atlas indices
    vec3 s000 = sampleUpperDir(triP000, rayDir, Du);
    vec3 s100 = sampleUpperDir(p100,   rayDir, Du);
    // ...
}
```

**Why this is sufficient:** At the high border, `triP000.x = upperRes-1` and `triF.x = 0.0`.
The blend `mix(s000, s100, 0.0) = s000` — the clamped `p100` sample is fetched (it equals
`triP000` after clamping) but its blend weight is zero. The GL_CLAMP_TO_EDGE semantics are
preserved correctly.

**Note:** The old `clamp(p000, 0, upperRes - 2)` scheme was trying to prevent `p000+1`
from going out of bounds, but it did so by clamping the *integer* index — which forces
the fractional offset to still take a value from the unclamped `upperGrid`. Clamping the
continuous coordinate first is the correct approach: it pins both the base corner and the
fractional weight to the boundary simultaneously.

**Plan doc updated:** Section 1b `sampleUpperDirTrilinear` updated with per-corner `hi`
clamping. Section 1c trilinear coordinate block updated with `clamp(upperGrid, 0, N-1)`
before `floor`/`fract`.

---

## Finding 2 — Medium: ShaderToy equivalence overstated

**Accepted.** The plan stated:
> "The correct implementation is 8-neighbor trilinear interpolation — the 3D analogue of
> ShaderToy's `WeightedSample()`."

ShaderToy's `WeightedSample()` does both spatial interpolation **and** per-sample
visibility weighting. This plan implements only the spatial interpolation half and
explicitly removes the visibility check without replacing it with a per-corner version.

The description should be: **"spatial interpolation parity"**, not ShaderToy-equivalent
merge parity.

**Plan doc updated:** Problem statement and correctness section reworded. The plan now
describes itself as "the correct spatial fix for non-co-located mode" and explicitly
documents that per-corner visibility weighting (the second half of `WeightedSample()`)
is deferred — not implemented and not claimed.

---

## Finding 3 — Medium: wrong debug view in stop conditions

**Accepted.** Mode 3 (Atlas) shows the per-probe D×D tile grid of the current cascade's
own atlas. It displays the per-direction radiance already stored in the atlas being baked,
not the spatial blending quality of the upper-cascade read. Toggle ON vs OFF would look
identical in mode 3 because the atlas write (the current cascade) is unchanged; only the
upper-cascade read changes.

The correct validation for spatial trilinear smoothness is the **final rendered GI image**
in non-co-located mode. In that mode:
- Trilinear OFF: all 8 lower probes in a block read the same parent → blocky 2×2×2
  stepping visible at cascade-level transitions.
- Trilinear ON: each lower probe blends from 8 neighbors weighted by fractional position
  → smooth spatial gradient across the block.

Debug mode 2 (Avg radiance projection) or mode 1 (MaxProj) in the radiance debug panel
would expose this better than mode 3.

**Plan doc updated:** Atlas mode 3 stop condition replaced with:
- "Non-co-located, trilinear ON vs OFF: visible 2×2×2 block stepping removed"
- "Avg/MaxProj debug mode: no hard probe-grid boundaries in non-co-located mode"

---

## Finding 4 — Low: self-referential prerequisite and encoding

**Accepted.**

The prerequisite line read:
```
phase5d_impl_learnings.md, phase5d_trilinear_plan.md (Codex 09_phase5d_trilinear_upper_lookup.md)
```
`phase5d_trilinear_plan.md` is the document itself. The second pointer should be:
```
doc/codex_plan/class/09_phase5d_trilinear_upper_lookup.md
```

**Plan doc updated:** Prerequisite line corrected.

Regarding encoding damage (mojibake `鈥?`, `脳`, `鲁`): these characters appear in the
reviewer's copy but the source file should be valid UTF-8. The mojibake is consistent
with a UTF-8 file read as Windows-1252: `×` (U+00D7) → `脳`, `→` (U+2192) → `鈥?`,
`³` (U+00B3) → `鲁`. These are presentation-layer artifacts from the review tool, not
from the content. No encoding change is needed in the source file; the Unicode math
symbols are correct and intentional.

---

## Summary of changes applied

| Finding | Severity | Action |
|---|---|---|
| F1: `fract(upperGrid)` on unclamped coord | High | Plan code fixed: clamp continuous value before floor/fract; clamp `+1` corners in function using `uUpperVolumeSize - ivec3(1)` |
| F2: ShaderToy equivalence claim overstated | Medium | Plan doc reworded: "spatial interpolation parity" not "full WeightedSample parity" |
| F3: Mode 3 atlas wrong stop condition | Medium | Plan doc updated: final GI image + Avg/MaxProj debug view are the correct acceptance tests |
| F4: Self-referential prerequisite | Low | Plan doc prerequisite line corrected to point at Codex analysis doc |

The core plan (8-neighbor trilinear, hoisted coordinates, removed global visibility zero)
remains unchanged. The math fix in F1 mirrors exactly the Phase 5f fix:
`clamp(continuousCoord, 0, N-1)` before `floor`/`fract` — the pattern is now consistent
across both directional and spatial interpolation in this codebase.
