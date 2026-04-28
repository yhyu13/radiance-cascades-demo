# Phase 5d Logic Check -- Probe Visibility Weighting & Co-Located Cascades

**Date:** 2026-04-28
**Branch:** 3d
**Status:** Phase 5d is a verified no-op under the current co-located architecture.
**Outcome:** Co-location is an architectural choice with measurable trade-offs. Non-co-located cascades (ShaderToy-style) are a planned follow-on feature with a runtime toggle.

---

## What Phase 5d Is

The ShaderToy reference implements `WeightedSample()`, which fetches the upper cascade probe's stored ray distance in the direction toward the current probe, then zeros out the upper contribution if that probe's ray was blocked before it reached the current probe's position.

This is a visibility filter on the merge: *"Can the upper probe actually see where I am? If not, its radiance estimate is for a different lighting context and should be discarded."*

---

## Why It Is a No-Op in This Codebase

### Evidence from `initCascades()` (`src/demo3d.cpp:1374-1378`)

```cpp
const int   probeRes = 32;
const float cellSz   = volumeSize.x / float(probeRes);  // 0.125, same for all cascades

for (int i = 0; i < cascadeCount; ++i)
    cascades[i].initialize(probeRes, cellSz, volumeOrigin, ...);
```

Every cascade is initialized with identical `probeRes`, `cellSz`, and `volumeOrigin`. All four cascades occupy the same 32^3 world-space grid.

### Evidence from `updateSingleCascade()` (`src/demo3d.cpp:948-953`)

```cpp
glUniform1f(glGetUniformLocation(prog, "uBaseInterval"),  c.cellSize);  // always 0.125
glUniform3iv(glGetUniformLocation(prog, "uVolumeSize"), 1, ...);         // always ivec3(32)
glUniform3fv(glGetUniformLocation(prog, "uGridSize"),   1, ...);         // always volumeSize
glUniform3fv(glGetUniformLocation(prog, "uGridOrigin"), 1, ...);         // always volumeOrigin
```

The spatial uniforms pushed to every cascade dispatch are identical. In the shader:

```glsl
vec3 probeToWorld(ivec3 probePos) {
    return uGridOrigin + (vec3(probePos) + 0.5) * uBaseInterval;
}
```

With `uBaseInterval` and `uGridOrigin` the same for all cascades, probe `(px, py, pz)` maps to the same world position in C0, C1, C2, and C3.

### What differs per cascade

Only the ray interval `[tMin, tMax]` changes:

| Cascade | tMin | tMax |
|---|---|---|
| C0 | 0.02m | 0.125m |
| C1 | 0.125m | 0.5m |
| C2 | 0.5m | 2.0m |
| C3 | 2.0m | 8.0m |

The probe positions are identical; only the ray lengths differ.

### Why probeDist == 0

In `WeightedSample()`, the visibility check computes the distance from the current probe to the upper probe. With co-located grids, the upper probe for cascade `ci` is `cascades[ci+1]` at the same probe index `(px, py, pz)` -- the same world position. Distance = 0. The visibility check `stored_hit_distance < distance_to_upper_probe` becomes `anything < 0`, which always passes. Phase 5d would implement code that always returns "visible" and produces zero visual change.

---

## The Architectural Trade-Off

Co-location was a deliberate simplification. It buys:
- **Simple merge addressing:** upper atlas texel == `ivec3(px*D+dx, py*D+dy, pz)`, same index as the current cascade. No spatial interpolation across upper-cascade probe neighbors.
- **No Phase 5d complexity:** visibility check is trivially satisfied by construction.

It costs:
- **Over-dense upper cascades:** C3 probes are 0.125m apart but serve a 2m-8m interval. There is no sub-0.125m geometric feature that changes what a 2m+ ray sees. The 32^3 = 32768 C3 probes cover the same spatial resolution as C0, but C3 rays do not benefit from that resolution.
- **Memory inefficiency at large D:** if per-cascade D scaling is adopted (Phase 5e), C3 with D=16 would be a 512x512x32 atlas -- 64 MB. With a coarser C3 probe grid (e.g., 4^3), it would be 64x64x4 -- 1 MB.
- **Phase 5d is inert:** the visibility weighting that guards against cross-wall light leakage in the merge is completely bypassed. In scenes with thick walls relative to cascade intervals this can cause bleed that non-co-located cascades would suppress.

---

## ShaderToy Reference: Spatially Differentiated Cascades

The ShaderToy uses different probe densities per cascade level:

| Cascade | Probe count | Cell size | Interval |
|---|---|---|---|
| C0 | 32^3 (or N^3) | fine | 0 - d |
| C1 | (N/2)^3 | 2x coarser | d - 4d |
| C2 | (N/4)^3 | 4x coarser | 4d - 16d |
| C3 | (N/8)^3 | 8x coarser | 16d - 64d |

Upper cascade probes are at different world positions from lower cascade probes. The merge must spatially interpolate between the 8 upper-cascade neighbors surrounding the current probe's world position. Phase 5d becomes load-bearing: the fetched neighbor may be on the wrong side of a wall.

---

## Decision

**Co-located cascades are retained as the default** -- they are simpler and correct for scenes where cascade intervals are small relative to geometry thickness.

**Non-co-located cascades will be added as a runtime-switchable mode** with a "Co-located / ShaderToy-style" toggle. This enables an A/B comparison of:
- Memory and compute cost per cascade (lower for coarser upper cascades)
- Merge quality at cascade boundaries
- Phase 5d visibility weighting (active only in non-co-located mode)

See `phase5d_noncolocated_plan.md` for the implementation plan.

---

## Files Relevant to This Analysis

| File | Relevant section |
|---|---|
| `src/demo3d.cpp:1374-1398` | `initCascades()` -- all cascades get same probeRes/cellSz/volumeOrigin |
| `src/demo3d.cpp:948-953` | `updateSingleCascade()` -- same spatial uniforms for all dispatches |
| `res/shaders/radiance_3d.comp:63-66` | `probeToWorld()` -- world pos from probePos * uBaseInterval |
| `res/shaders/radiance_3d.comp:173-183` | tMin/tMax interval computation from uCascadeIndex |
