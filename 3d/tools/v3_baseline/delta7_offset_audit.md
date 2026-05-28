# Delta #7 — Probe-position -0.5 offset audit

**Stage 0 Deliverable C** of M0 ([v3_m0_stage0_plan.md](../../doc/7/v3_m0_stage0_plan.md)).
**Date:** 2026-05-26.
**Source:** grep `-0\.5|+\s*0\.5` across `res/shaders/` plus inspection of code intent.

## Verdict

**CONFORMANT.** All probe-position derivation and probe-sampling sites use the standard texel/cell-center convention consistently. Delta #7 is a no-op in the current volumetric topology and is **removed from the M1 work order**.

## Convention found

Two distinct mappings, each with its own offset, applied uniformly:

1. **Grid coord → world position:** `worldPos = origin + (vec3(gridCoord) + 0.5) * cellSize`. The +0.5 places the world position at the **center** of the grid cell.
2. **World position → grid coord (for sampling):** `gridCoord = worldToGrid(worldPos) - 0.5`. The -0.5 converts edge-aligned grid coords into center-aligned probe-center coords, so that trilinear/bilinear weights interpolate between cell centers rather than cell corners.

These are inverses of each other; the consistent application across all sites means a probe sampled at world position `p` reads from the grid cell whose center is closest to `p` (modulo trilinear blend with neighbors).

## Sites audited

### Group 1 — Grid coord → world (probe-cell center)

| Site | Code | Purpose |
|------|------|---------|
| [radiance_3d.comp:162](../../res/shaders/radiance_3d.comp#L162) | `uGridOrigin + (vec3(probePos) + 0.5 + uProbeJitter) * uProbeCellSize` | C0/CN bake probe world position (with Phase 9 jitter) |
| [radiance_3d.comp:306](../../res/shaders/radiance_3d.comp#L306) | `uGridOrigin + (vec3(cornerPos) + 0.5) * uUpperProbeCellSize` | Upper-cascade neighbor probe world positions for merge |
| [inject_radiance.comp:142](../../res/shaders/inject_radiance.comp#L142) | `uGridOrigin + (vec3(probePos) + 0.5f) * (uGridSize / vec3(uVolumeSize))` | Injection probe world position |
| [lighting_debug.frag:84](../../res/shaders/lighting_debug.frag#L84) | `gridOrigin + (vec3(probePos) + 0.5f) * (gridSize / vec3(uVolumeSize))` | Debug visualization probe position |
| [sdf_analytic.comp:71](../../res/shaders/sdf_analytic.comp#L71) | `(vec3(coord) + 0.5) / vec3(size)` | Voxel-center for SDF evaluation |
| [voxelize.comp:138](../../res/shaders/voxelize.comp#L138) | `uVolumeOrigin + (vec3(x,y,z) + 0.5) * voxStep` | Voxelization sample point |

### Group 2 — World → grid coord (sampling with -0.5)

| Site | Code | Purpose |
|------|------|---------|
| [radiance_3d.comp:439](../../res/shaders/radiance_3d.comp#L439) | `clamp(uvw * vec3(uC0VolumeSize) - 0.5, ...)` | C0 atlas sampling base for multi-bounce feedback |
| [radiance_3d.comp:608](../../res/shaders/radiance_3d.comp#L608) | `(worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5` | Upper-cascade trilinear merge sampling base |
| [raymarch.frag:437](../../res/shaders/raymarch.frag#L437) | `clamp(uvw * vec3(uAtlasVolumeSize) - 0.5, ...)` | Phase 5d trilinear consumer probe sampling |
| [raymarch.frag:628](../../res/shaders/raymarch.frag#L628) | `clamp(uvw5 * vec3(uAtlasVolumeSize) - 0.5, ...)` | Phase 5f bilinear consumer probe sampling (mode 5) |

### Group 3 — Directional (bin) center

| Site | Code | Purpose |
|------|------|---------|
| [radiance_3d.comp:197](../../res/shaders/radiance_3d.comp#L197) | `vec2 oct = (vec2(bin) + 0.5) / float(D)` | Bake-side bin-center octahedral direction |
| [radiance_3d.comp:213](../../res/shaders/radiance_3d.comp#L213) | `clamp(dirToOct(rayDir) * float(D) - 0.5, ...)` | Bake-side direction → bin lookup (with -0.5 for center-aligned sampling) |
| [raymarch.frag:347](../../res/shaders/raymarch.frag#L347) | `octToDir((vec2(bin) + 0.5) / float(D))` | Consumer-side bin-center direction reconstruction |
| [radiance_debug.frag:187](../../res/shaders/radiance_debug.frag#L187) | `vec2(uAtlasBin) + 0.5` | Debug bin-center oct coord |

## Sources of authoritative comment

[radiance_3d.comp:599-600](../../res/shaders/radiance_3d.comp#L599-L600):
> "The -0.5 converts edge-aligned grid coords to center-aligned probe-center coords (same pattern as directional bilinear's -0.5 in sampleUpperDir)."

[raymarch.frag:427](../../res/shaders/raymarch.frag#L427):
> "-0.5 center-aligned offset: same convention as Phase 5d trilinear and Phase 5f bilinear."

These comments document the convention as an intentional, documented choice — not accidental.

## What this means for the v3 pivot

- Delta #7 in §2.1 of [v3_shadertoy_adoption_scope.md](../../doc/7/v3_shadertoy_adoption_scope.md) is now marked **already conformant**.
- The M1 work order shrinks from {#3, #4, #6, #7} to {#3, #4, #6} (and conditionally #3 per [delta3_alpha_audit.md](delta3_alpha_audit.md)).
- No code change required for Delta #7. The audit doc itself is the deliverable.

## What this does NOT verify

- That the offset convention is **correct** in absolute terms (only that it is **uniformly applied**). The ShaderToy reference uses the same convention, so the volumetric port inherits whatever correctness the reference has.
- That hybrid per-pixel correction shaders use the convention — **no hybrid shader files exist** in `res/shaders/` (the hybrid correction lives as a code path inside `raymarch.frag` per `grep hybrid res/shaders/`). The raymarch.frag sites at lines 437 and 628 are the hybrid-relevant sample sites; both use -0.5.
