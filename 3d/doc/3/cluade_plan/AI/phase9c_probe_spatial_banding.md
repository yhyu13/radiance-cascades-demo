# Phase 9c — Probe Spatial Banding: Root Cause and Mode 5 Redesign

**Date:** 2026-05-02
**TAA/jitter status:** Excluded from this phase (too costly for performance).
**Trigger:** Persistent spatial banding after D=8 fix; user request to cross-check against
ShaderToy 3D reference and reinvestigate what mode 5 actually shows.

---

## ShaderToy Reference Architecture vs. Our Implementation

### ShaderToy (reference)

| Property | Value |
|---|---|
| Probe placement | Surface-attached 2D hemispheres — probes live ON each wall/floor/ceiling |
| Ray coverage | Outward hemisphere only (cannot sample behind the surface) |
| Cascade structure | 6 levels; C0 has 4 probes/surface face, C5 has 4096 probes/surface face |
| Cascade merge | Reads from NEXT cascade (C0 ← C1 ← ... ← C5) |
| Spatial interpolation | 2D bilinear across 4 adjacent probes within the surface atlas |
| Visibility weighting | `WeightedSample()`: check if upper probe's stored ray toward current probe is shorter than the actual separation; occluded probes contribute 0 |
| Bounce light source | Reads radiance FROM the HIT SURFACE'S OWN atlas, not from the current probe's cascade merge |

### Our implementation

| Property | Value |
|---|---|
| Probe placement | Volumetric 3D grid — 32³ probes fill the entire scene volume |
| Ray coverage | Full sphere (wastes half the rays firing into the surface behind the probe) |
| Cascade structure | 4 levels; all at 32³ resolution |
| Cascade merge | C0 upper = C1 (same grid size, non-co-located for higher cascades) |
| Spatial interpolation | 3D trilinear across 8 surrounding probe corners |
| Visibility weighting | **Not implemented** |
| Bounce light source | Upper cascade at the current probe's world position |

---

## What Mode 5 Actually Showed (Old)

Mode 5 was a **SDF raymarching step count heatmap**:

```glsl
float t5 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
// green=few, yellow=moderate, red=many/miss
```

This visualizes how many primary view-ray SDF march steps were needed to reach a surface.
It has NO direct relationship to probe spatial distribution or probe cell boundaries.

**Why the old mode 5 was misleading:** Step-count banding and GI probe-cell banding are
independent artifacts. The step count is determined by SDF curvature near surfaces, not
by which probe cells are sampled. Observing "banding in mode 5" would tell us about SDF
structure, not GI probe layout.

The old step-count mode has been moved to **mode 8**.

---

## Mode 5 Redesign: Probe Cell Boundary Visualization

New mode 5 shows `fract(pg)` at the surface hit point, where `pg` is the continuous
probe-grid coordinate:

```
pg = clamp(uvw * N - 0.5, 0, N-1)    where uvw = (pos - gridOrigin) / gridSize
```

With probe center k at `pg = k` (exactly), `fract(pg)` gives:
- `0.0` at probe k's center
- `0.5` at the cell boundary between probes k and k+1 (equal trilinear blend weight)
- Wraps `1.0 → 0.0` at probe k+1's center

The three channels `fract(pg.xyz)` are output as RGB. One full color cycle per probe cell in
each axis.

**How to use:**

1. Switch to **mode 5** (probe boundary viz).
2. Note the spatial frequency of color transitions on each surface.
   - Cornell box: probe cells are 4m/32 = 12.5cm. At normal viewing distances,
     you should see ~8-16 color cycles per visible wall face.
3. Switch to **mode 6** (GI-only). Observe GI banding frequency and orientation.
4. Compare:

| Observation | Diagnosis | Fix |
|---|---|---|
| Mode 6 banding aligns spatially with mode 5 transitions | **Type A**: probe-cell-size limited | Higher probe density OR cubic interpolation |
| Mode 6 banding doesn't align with mode 5 | **Type B**: directional quantization (finite D) | Higher D (D=16) |
| Both sources visible | Both Type A and Type B together | Need both fixes |

---

## Root Cause of Type A Spatial Banding

Trilinear interpolation between 8 probe corners is **C0-continuous but C1-discontinuous**:
- No jumps in GI value across probe cell boundaries ✓
- First derivative has a kink at probe center positions (integer pg values) ✗

These kinks are visible as a spatial stepping pattern when adjacent probes have significantly
different irradiance values (e.g., at shadow boundary edges, near colored walls).

**Probe grid parameters** (current):
```
resolution = 32³   → 32,768 probes total
cell size  = 4m / 32 = 0.125m = 12.5cm
```

For the Cornell box (4m³), each probe cell subtends 12.5cm — fine enough for smooth
open-room GI, but the trilinear kinks at probe centers (every 12.5cm) can be visible at
close viewing distances.

---

## Why Probe Visibility Weighting Does NOT Help Here

The ShaderToy `WeightedSample()` addresses a specific failure mode: the upper cascade
probe is on the OTHER SIDE of an internal wall from the current probe. By checking
the probe's stored ray distance in the direction toward the current probe, it detects
when the probes are occluded from each other and zeros out that contribution.

In our Cornell box:
- All 32³ probe centers are in open air (SDF > 0) — no probe is inside wall geometry
- The room has no internal occluders between probe positions and surface points
- Every midpoint visibility check returns "unoccluded"
- Visibility weighting = 1.0 for all probes at all surface points → no effect

Visibility weighting is the correct approach for **scenes with internal walls or obstacles**
(like the ShaderToy's partition wall). It is not the solution for our open Cornell box.

---

## Why TAA Probe Jitter Is Excluded

TAA probe jitter was previously implemented (Phase 9b) but is excluded from this analysis
because the performance cost is too high for the frame rate target. The jitter+temporal
approach conceptually widens each probe's spatial footprint by averaging samples across
multiple jitter positions per cell, which softens Type A banding over time. Without TAA,
we need spatial or structural fixes.

---

## Potential Fixes for Type A Spatial Banding (not yet implemented)

### Option 1: Increase probe resolution (32 → 64)
- Cell size: 12.5cm → 6.25cm
- Memory: 32³ × D² × 8B × 4 bytes → 64³ × ... (8× increase per cascade)
- Compute: 8× more probe bakes
- Effect: halves the spatial frequency of the kinks — likely eliminates visible banding

### Option 2: Tricubic (Catmull-Rom) spatial interpolation
- Replace trilinear (linear, C0) with tricubic (cubic, C1 continuous)
- Requires 4³ = 64 probe fetches instead of 2³ = 8
- Effect: eliminates derivative kinks — GI varies smoothly at probe-grid frequency

### Option 3: Screen-space GI blur (post-process)
- Blur GI in screen space after the raymarching pass
- Very cheap (single-pass 2D blur)
- Side-effect: blurs sharp shadow edges in the GI
- Effect: suppresses high-frequency probe-grid noise without fixing the underlying cause

---

## Mode Numbering Reference (updated)

| Mode | Description | Location |
|---|---|---|
| 0 | Final rendering (direct + indirect) | surface hit |
| 1 | Surface normals as RGB | surface hit |
| 2 | Depth map | surface hit |
| 3 | Indirect radiance × 5 (isotropic, magnified) | surface hit |
| 4 | Direct light only | surface hit |
| 5 | **Probe cell boundary** — `fract(pg)` RGB | surface hit (new) |
| 6 | GI-only — `albedo * indirect` | surface hit |
| 7 | Ray travel distance heatmap (continuous) | surface hit |
| 8 | SDF step count heatmap (discrete, was mode 5) | post-loop |

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/raymarch.frag` | Mode 5 → probe cell boundary viz (`fract(pg)` RGB) inside `dist < EPSILON` block; old step count moved to mode 8 at end of main |
| `src/demo3d.cpp` | Always set `uAtlasVolumeSize`, `uAtlasGridOrigin`, `uAtlasGridSize` before atlas availability check, so mode 5 works without directional GI |

---

## Probe Spatial Banding Verification Checklist

| Check | Procedure |
|---|---|
| Confirm mode 5 works | Switch to mode 5 — should see RGB color gradient cycling at ~12.5cm frequency on walls |
| Confirm cycle count | On a 4m wall face (Cornell box wall): expect 4m / 0.125m = 32 cycles |
| Align with GI banding | Compare mode 5 and mode 6 frequency and orientation |
| Diagnose banding type | If aligned → Type A (probe cell size); if not aligned → Type B (directional D) |
