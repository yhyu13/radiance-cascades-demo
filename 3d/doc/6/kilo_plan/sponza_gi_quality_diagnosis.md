# Sponza GI Quality Diagnosis

**Date:** 2026-05-12T10:12+08:00
**Status:** Root-cause analysis complete. Three P0/P1/P2 blockers identified.
**Scope:** Why Sponza bouncing GI looks significantly worse than Cornell Box — light leaking, insufficient bounces, and why probe merge/trilinear/direction merge settings don't affect quality.

---

## Summary

Sponza GI is broken at three independent levels. The bake-side settings (probe merge, trilinear, direction merge) don't help because they operate on fundamentally broken data. The algorithm is not wrong in principle — it's the scene coverage and render-path gaps that produce the bad output.

| Root Cause | Severity | Impact |
|---|---|---|
| **P0: 0% probe occupancy** — Sponza probes find zero geometry across all 4 cascade levels | Critical | No real radiance data; NaN/Inf output; all tweaks operate on broken data |
| **P1: No multi-cascade merge in raymarch.frag** — render shader only reads C0, ignoring C1/C2/C3 | High | Long-range indirect (bounces across the corridor) completely absent |
| **P2: No visibility/occlusion between probe and surface** — light leaks through thin walls freely | Medium | Light bleeding across arches, columns, curtains; no backface/occlusion test |

---

## P0: Sponza Probes Are Catastrophically Empty

### Probe occupancy comparison (from `tools/probe_stats_*.json`)

| Cascade | Sponza anyPct | Sponza surfPct | Sponza meanLum | Cornell anyPct | Cornell surfPct | Cornell meanLum |
|---------|--------------|----------------|---------------|---------------|----------------|---------------|
| C0      | **4.3%**     | **0%**         | **NaN**       | 100%          | 68.9→98.3%     | 0.052         |
| C1      | **0.02%**    | **0%**         | **-1.37**     | 100%          | 99.1%          | 0.056         |
| C2      | **0.39%**    | **0%**         | **-376**      | 100%          | 100%           | 0.049         |
| C3      | **0%**       | **0%**         | **-320**      | 100%          | 100%           | 0.029         |

**Every cascade level has 0% surface hits for Sponza.** No direct lighting gets injected, no radiance accumulates, and the output is NaN/Inf (C0 meanLum=NaN, maxLum=inf) or wildly negative (C2 meanLum=-376, C3 meanLum=-320). The cascade data is not "low quality" — it's **completely broken**.

### Why Sponza probes miss geometry while Cornell hits it

**Three compounding factors:**

#### 1a. Isotropic 4×4×4 volume forced onto elongated scene

The cascade volume is hardcoded at `volumeOrigin=(-2,-2,-2)` and `volumeSize=(4,4,4)` (`demo3d.cpp:169-170`, `demo3d.h:999-1002`). It never adapts per scene.

Sponza's aspect ratio is **X:Y:Z = 3.8 : 1.59 : 2.34** (`demo3d.cpp:5130`). After normalization to `halfExtent=1.9` (`demo3d.cpp:5414-5426`), X fills [-1.9, 1.9] but Y only [-0.79, 0.79] and Z [-1.14, 1.14]. The 32³ probe grid uniformly distributes probes across the entire [-2, 2]³ cube. Most probes sit in empty air above the ceiling, below the floor, or outside the thin shell walls — only 4.3% of C0 probes have any non-zero radiance at all.

Cornell's near-cubic shape fits well in [-1, 1]³ (halfExtent=1.0). Thick 0.4-wu walls fill substantial volume. Nearly every interior probe is within a few voxels of a surface.

**The fix**: Anisotropic or scene-adaptive volume sizing. Replace the hardcoded `(4,4,4)` with bounds computed from the OBJ mesh's actual aspect ratio. For Sponza: `volumeSize ≈ (4, 1.67, 2.47)` so probes distribute proportionally along the corridor's elongated shape. Or use non-uniform probe grid spacing where cellSize differs per axis.

#### 1b. Sponza thin walls are sub-voxel or near-voxel thickness

At 128³ resolution in a 4-unit volume, voxels are ~3cm (`voxelSize = 4/128 = 0.03125`). Sponza's walls, arches, columns, and curtains are thin surface shells — a few cm thick at best. The OBJ material hints (`obj_loader.h:314-349`) confirm: arches (thin shells), columns (slender cylinders), curtains/fabric (near-zero-thickness surfaces), chains (thin metal strands).

The SDF at 128³ barely represents these features. The `c0MinRange=0.5` reduction (from 1.0, `demo3d.cpp:283-286`) was specifically for Sponza — extending C0 ray reach from 0.125 to 0.5 wu so probes near walls could register surface hits at ~3cm voxel spacing. But even at 0.5 wu reach, probes in open air (96% of all C0 probes) can't find surfaces.

**The fix**: (a) Increase `volumeResolution` to 256³ (halves voxel size to ~1.5cm) — requires texture reallocation infrastructure. (b) Create an analytic SDF for Sponza (like Cornell's `sdf_analytic.comp`) that exactly represents thin walls without voxelization errors. (c) Use the GPU voxelizer at higher resolution.

#### 1c. Sponza normalization fills volume edge-to-edge with 5% margin

`halfExtent=1.9` (`demo3d.cpp:5414`) means Sponza walls/ceilings sit at the very edge of the probe grid coverage. Only 0.1 wu boundary margin. The raymarch shader's boundary clamping (`raymarch.frag:328-335`) duplicates edge probes rather than returning zero — surfaces just outside the volume boundary receive interior probe radiance leaking outward, which is a **direct light leaking source** on top of the occupancy problem.

**The fix**: Either increase the margin (reduce halfExtent to ~1.7) or replace the clamp-to-edge with proper out-of-volume handling (return black/zero for positions outside the grid, not the nearest edge probe's radiance).

---

## P1: No Multi-Cascade Merge in raymarch.frag

### What the bake does correctly

The cascade bake in `radiance_3d.comp` (lines 368-397) correctly implements inter-cascade transfer:

- When a C0 ray misses (no surface in the 0.125 wu interval, alpha=0), it inherits radiance from C1 at the same direction bin (line 396: `rad = upperDir`).
- When a C0 ray hits near the far boundary of its interval, it smoothstep-blends between surface hit and upper cascade radiance (lines 389-393).
- This flows top-down: C3 → C2 → C1 → C0. The bake-side settings (`useDirectionalMerge`, `useSpatialTrilinear`, `useDirBilinear`) control how this upper-to-current transfer happens during baking.

### What the render shader does NOT do

`raymarch.frag` calls `sampleDirectionalGI(pos, normal)` (lines 315-341) which does **trilinear interpolation over 8 spatial neighbors within a single cascade level (C0)**. There is no code that merges C0 short-range + C1/C2/C3 long-range radiance.

This means:
- **Short-range indirect** (within C0's 0.125 wu interval) comes from C0 probes that hit nearby surfaces
- **Long-range indirect** (bounces across Sponza's 3.8-unit corridor, multi-bounce through arches, etc.) should come from C1/C2/C3 — but the render shader ignores them
- For Sponza specifically, most surfaces are >0.125 wu from most probes, so even C0 misses are common

**The fix**: Add multi-cascade sampling to `raymarch.frag`. The hierarchy data already exists in the bake output — it just isn't consumed. The merge logic should:
1. Sample C0 for short-range radiance (within cellSize distance of the surface)
2. When the sample point is far from any C0 surface hit (or C0 has no data), blend in C1/C2/C3 radiance at the same direction
3. Weight by interval coverage — C0 covers [0, 0.125], C1 covers [0.125, 0.5], C2 covers [0.5, 2.0], C3 covers [2.0, 8.0]

This is the "cascade" part of "radiance cascades" — the hierarchy exists in the bake but is missing in the render path.

---

## P2: No Visibility/Occlusion Between Probe and Surface

### Current state

`sampleProbeDir()` in `raymarch.frag` (lines 297-310) has **no check whether the probe can see the sample point**. The only filtering is a soft cosine weight: `max(0, dot(bdir, normal))` (line 303). A probe behind a wall still contributes its radiance to a surface on the other side.

For Cornell's thick 0.4-wu walls, this is somewhat mitigated — probes inside the box are physically separated from probes outside by thick solid geometry. But for Sponza's thin shell walls:
- A probe 0.125 wu on the bright side of a thin arch has outward-facing direction bins that bleed through to the dark side
- The octahedral sphere map covers both hemispheres — bins facing "through" the wall contribute to the wrong side's normal
- Boundary-clamping at volume edges (`raymarch.frag:328-335`) duplicates edge probes instead of returning zero, so surfaces near the [-2, 2] boundary receive interior radiance leaking outward

### The fix

Add a visibility/occlusion term. Options (in order of practicality):
1. **SDF shadow ray from probe to sample point** — cheap, already available in the codebase (`shadowRay()` at `raymarch.frag:348-362`). Cast a short ray from the probe center to the surface point; if occluded, zero out that probe's contribution.
2. **Chebyshev/variance-based occlusion** — statistical approach from LPV/Voxel GI literature. Uses variance of probe radiance to estimate occlusion probability. More approximate but avoids per-sample ray cost.
3. **Backface heuristic** — reject probes where the probe-to-surface direction is opposite to the surface normal AND the probe is on the other side of a detected wall (SDF distance < threshold along the probe-to-surface line).

---

## Why Settings Don't Affect Quality

| Setting | What it controls | Where it operates | Why it doesn't help Sponza |
|---------|-----------------|-------------------|----------------------------|
| Probe merge (`useDirectionalMerge`) | How C0 inherits upper-cascade radiance during bake | `radiance_3d.comp:368-397` | Bake data is 0% surfPct (broken). Merging broken upper data into broken C0 doesn't fix it. |
| Trilinear (`useSpatialTrilinear`) | 8-neighbor spatial interpolation for non-co-located upper cascade during bake | `radiance_3d.comp:180-204` | Interpolating between empty probes produces empty results. |
| Direction bilinear (`useDirBilinear`) | Directional bilinear within upper-cascade atlas during bake | `radiance_3d.comp:150-172` | Averaging directional bins of probes with NaN/Inf radiance produces NaN/Inf. |
| GI blur radius | Bilateral smoothing of rendered indirect image | `gi_blur.frag` | Smoothing broken probe output just spreads wrong values more smoothly. |
| `probeJitterScale=0.06` | Sub-cell spatial jitter ±0.03 wu per frame | `demo3d.cpp:847-853` | At C0 cellSize=0.125, jitter is ±24% of a cell — too small to redistribute probes from empty air to near surfaces. |
| `temporalAlpha=0.05` | EMA blend weight for temporal accumulation | `temporal_blend.comp` | Slow convergence is irrelevant when the data being accumulated is NaN/Inf. |

**All these settings operate on cascade data that is fundamentally broken (0% surface hits, NaN/Inf output). Tweaking interpolation and filtering on broken data cannot produce correct results. The data must first be fixed (P0), then the render path must consume it properly (P1), then visibility must prevent leaking (P2).**

---

## Detailed Technical Evidence

### Cascade volume configuration

| Parameter | Value | Source |
|-----------|-------|--------|
| `volumeOrigin` | `(-2, -2, -2)` | `demo3d.cpp:169`, `demo3d.h:999` |
| `volumeSize` | `(4, 4, 4)` | `demo3d.cpp:170`, `demo3d.h:1002` |
| `cascadeC0Res` | 32 (default) | `demo3d.cpp:206` |
| `baseInterval` | `4.0 / 32 = 0.125` wu | `demo3d.cpp:2836` |
| `c0MinRange` | 0.5 wu (reduced from 1.0) | `demo3d.cpp:283-286` |
| `c1MinRange` | 1.0 wu | `demo3d.cpp:287` |
| `volumeResolution` | 128³ | `demo3d.h:52` (DEFAULT_VOLUME_RESOLUTION) |
| `voxelSize` | 0.03125 wu (~3cm) | `4.0 / 128` |

### Sponza normalization

| Parameter | Cornell | Sponza | Source |
|-----------|---------|--------|--------|
| `halfExtent` | 1.0 | 1.9 | `demo3d.cpp:5414-5426` |
| Mesh bounds after normalization | [-1, 1]³ | [-1.9, 1.9]×[-0.79, 0.79]×[-1.14, 1.14] | Computed from aspect 3.8:1.59:2.34 |
| Boundary margin | 50% | 5% | Volume is [-2, 2]³ |
| Light type | Point light | Directional light | `demo3d.cpp:5181-5186` |
| Wall thickness | 0.4 wu (analytic) / 0.1 wu (voxel-box) | ~3cm (sub-voxel thin shells) | `sdf_analytic.comp`, `obj_loader.h:314-349` |

### Cascade interval structure

| Cascade | Interval (wu) | MinRange override | CellSize (co-located) |
|---------|---------------|-------------------|----------------------|
| C0 | [0, 0.125] | 0.5 (overrides tMax to ≥0.5) | 0.125 |
| C1 | [0.125, 0.5] | 1.0 (overrides tMax to ≥1.0) | 0.125 |
| C2 | [0.5, 2.0] | — | 0.125 |
| C3 | [2.0, 8.0] | — | 0.125 |

Note: `c0MinRange=0.5` means C0 probes trace rays at least 0.5 wu (4× their nominal 0.125 interval). `c1MinRange=1.0` means C1 probes trace at least 1.0 wu (2× their nominal 0.5 interval). These overrides exist specifically to help Sponza probes near walls register surface hits, but they're insufficient given the 0% occupancy.

### Raymarch shader sampling path

`raymarch.frag` has two sampling paths:
- **Isotropic**: `texture(uRadiance, uvw).rgb` (line 555) — simple 3D texture lookup, no direction data
- **Directional**: `sampleDirectionalGI(pos, normal)` (lines 315-341) — trilinear over 8 spatial neighbors within ONE cascade level

Neither path reads C1, C2, or C3. The multi-cascade hierarchy produced by the bake is not consumed by the render.

### Light leaking sources in raymarch.frag

1. **Boundary probe clamping** (lines 328-335): `clamp(p000 + offset, ivec3(0), hi)` duplicates edge probes instead of returning zero. Positions slightly outside the grid get the nearest interior probe's radiance.
2. **No probe-to-surface visibility** (lines 297-310): `sampleProbeDir` has no occlusion check. Only cosine-weight filtering on direction bins.
3. **Small wsum denominator** (line 309): `max(wsum, 1e-4)` amplifies noise for near-vertical normals (common in Sponza columns/walls) that have few facing direction bins.

---

## Fix Priority and Estimated Impact

| Priority | Fix | Estimated effort | Expected impact |
|----------|-----|------------------|-----------------|
| **P0** | Scene-adaptive volume sizing (anisotropic `volumeSize` matching Sponza's 3.8:1.59:2.34 aspect ratio) | Medium (change `volumeSize` computation, adjust `volumeOrigin`, test OBJ normalization) | **Critical** — moves probes from empty air to near surfaces. Should raise Sponza surfPct from 0% to >50% at minimum. |
| **P0** | Increase SDF resolution to 256³ or add analytic SDF for Sponza | Medium-High (256³ needs texture realloc; analytic SDF needs scene-specific shader) | **High** — represents thin walls accurately. At 256³, voxels are ~1.5cm, which can represent Sponza's thin features. |
| **P0** | Fix boundary handling in raymarch.frag (return zero for out-of-grid instead of clamping to edge) | Low (change `clamp` to conditional zero-return at `raymarch.frag:328-335`) | **Medium** — eliminates one direct light leaking source at volume edges. |
| **P1** | Multi-cascade render merge in raymarch.frag (sample C0+C1+C2+C3, blend by interval coverage) | Medium (add upper-cascade texture bindings, sample + blend logic in `sampleDirectionalGI`) | **High** — enables long-range indirect bounces across the Sponza corridor. The data already exists in the bake output. |
| **P2** | SDF shadow ray from probe to surface point in `sampleProbeDir` | Low (reuse existing `shadowRay()` logic, add probe-to-surface visibility check) | **Medium** — prevents light leaking through thin walls/arches. |
| **P2** | Increase `probeJitterScale` to 0.25 and `temporalAlpha` to 0.12 (codex 09 P1) | Low (change two float defaults) | **Low** — improves convergence once probes have data, but irrelevant until P0 is fixed. |

---

## Open Questions

1. **Anisotropic vs non-uniform probe grid**: Should the volume be stretched anisotropically (simpler — just change `volumeSize`), or should probes have non-uniform spacing (more complex — changes atlas layout)? Anisotropic volume stretching is the simpler fix but produces non-cubic voxels which affect the SDF raymarch step sizes.

2. **C0 cellSize target for Sponza**: At 32³ probes in a 3.8-unit-wide volume, cellSize ≈ 0.119 (nearly unchanged from 0.125). To meaningfully increase probe density near Sponza surfaces, we may need cascadeC0Res=48 or 64, or scene-adaptive probe resolution.

3. **Upper cascade probe resolution**: The scaling experiment (Exp 2) confirmed that C0 bake cost scales ~267× from 8³ to 64³ probes. Sponza may need more probes than Cornell but can't afford 64³ at current bake cost. A sparse-probe placement (only near surfaces) would be ideal but requires significant architecture changes.

4. **Direction resolution D scaling**: Currently D=8 for C0, D=16 for C1/C2/C3. Sponza's complex geometry with many thin occluders may need higher D for accurate directional radiance, but the atlas cost scales as D² per probe.

5. **Temporal convergence speed**: Once P0 is fixed and probes have real data, `temporalAlpha=0.05` means 20 frames for 63% convergence. With staggered updates (C0 every frame, C1 every 2, C2 every 4, C3 every 8), full convergence takes ~8× longer. Sponza's larger scene may need faster convergence — consider increasing `temporalAlpha` or reducing `staggerMaxInterval`.