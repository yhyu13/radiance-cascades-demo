# Phase 7 — SDF Quantization Root Cause Analysis

**Date:** 2026-04-30
**Evidence:** `tools/frame_17775487414938002.png` (mode 5 step heatmap) +
             `tools/frame_17775487448111600.png` (final render, same frame)
**Finding:** The dominant source of rectangular contour banding in the final render
is SDF voxel quantization at 64³ resolution, not cascade blend zone math.

---

## What mode 5 shows

`uRenderMode == 5` in `res/shaders/raymarch.frag:464-472` is the **step count heatmap**:

```glsl
float t5 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
vec3 heatColor = (t5 < 0.5)
    ? mix(vec3(0,1,0), vec3(1,1,0), t5 * 2.0)   // green → yellow
    : mix(vec3(1,1,0), vec3(1,0,0), (t5-0.5)*2.0); // yellow → red
```

- **Green** = few steps (ray hit geometry quickly)
- **Yellow/Red** = many steps (long free-space traversal)
- Normalized against 32 steps (Cornell Box rays typically hit in <32 even with
  `uSteps=256`)

---

## Why the rectangular shape is expected

In a Cornell Box, the SDF iso-contours are **rectangles**:

```
SDF(p) = min(dist_to_left_wall, dist_to_right_wall,
             dist_to_floor,     dist_to_ceiling,
             dist_to_back_wall)
```

This produces concentric rectangular shells. Rays through the center of the room
encounter large SDF values → large adaptive steps → few total steps → **green**.
Rays grazing a wall encounter small SDF values → tiny steps → many steps → **red**.

The rectangular geometry in the step heatmap is **geometrically correct** for a
Cornell Box. It is not an artifact.

---

## Why the discrete banding is wrong

The step count should transition **smoothly** across those rectangular shells —
a continuous gradient from green (center) to red (near walls).

Instead, both screenshots show discrete iso-luminance contour steps: the step count
jumps in quantized increments at fixed spatial intervals. This is wrong.

**Cause: SDF voxel quantization.**

The SDF is stored in a 64³ 3D texture over a 4m × 4m × 4m volume:

```
Voxel size = 4.0m / 64 = 0.0625m = 6.25 cm
```

The SDF values in this texture were computed by **jump flooding** at 64³ resolution.
Jump flooding approximates the true SDF but produces values that are:
- Quantized to the voxel grid (each voxel holds one distance value)
- Slightly non-Lipschitz at voxel boundaries (the approximation can overshoot or
  undershoot the true distance locally)

When the ray marcher samples this SDF trilinearly, the per-step distance values are
smooth *within* each voxel cell but the gradients across voxel boundaries carry the
quantization error. Over many steps, this causes pixels at geometrically similar
distances to take discretely different step counts — producing the concentric banding
pattern aligned to the 6.25cm voxel grid.

---

## How this connects to the GI banding

The radiance cascade bake rays (**radiance_3d.comp**) also march through the same 64³
SDF to find surface hits. The hit positions fed into the probe atlas are therefore
quantized to the SDF voxel grid.

```
SDF quantization → bake ray hit positions quantized
                 → probe radiance samples at quantized surface points
                 → reconstructed indirect field inherits rectangular step pattern
                 → GI banding visible in final render
```

The banding in the final render and the banding in the step heatmap share the same
spatial frequency and rectangular geometry because they share the same root cause:
**the 64³ SDF is too coarse to represent the Cornell Box geometry smoothly.**

At 64³, each probe cell (C0 probe spacing = 4m/32 = 12.5cm) spans exactly **2 × 2 × 2
SDF voxels**. The SDF has only 2 samples per probe cell — insufficient for smooth
adaptive marching or smooth surface-hit localization.

---

## Comparison with cascade blend hypothesis

| Hypothesis | Evidence for | Evidence against |
|---|---|---|
| Cascade blend linear weight kink | Contours align with cascade interval boundaries | Mode 5 shows same banding with NO cascade GI involved — step count is pure raymarching |
| SDF 64³ voxel quantization | Mode 5 step heatmap shows banding in raw raymarching; matches voxel grid period | None — this is directly observable |

The step heatmap is the falsification. Mode 5 does not use the cascade system at all —
it only shows how many SDF-guided steps the ray took. If the banding appears in mode 5,
the cascade blend is not the cause of that banding. It is in the SDF itself.

The cascade blend smoothstep (Phase 7 Experiment 1) **still has value** — it addresses
the secondary contribution from the D4→D8 blend zone kink — but it will not remove
the dominant rectangular banding.

---

## Fix: increase SDF resolution

| Resolution | Voxel size | SDF samples per C0 probe cell | Memory (R32F) |
|---|---|---|---|
| 64³ (current) | 6.25 cm | 2³ = 8 | ~1 MB |
| 128³ | 3.125 cm | 4³ = 64 | ~8 MB |
| 256³ | 1.5625 cm | 8³ = 512 | ~64 MB |

**128³ is the recommended next step.** It gives 64 SDF samples per probe cell — enough
for smooth adaptive marching and accurate surface-hit localization — at 8× the memory
cost (~8 MB, well within GPU budget). Expected to eliminate or dramatically reduce the
rectangular step banding in both the step heatmap and the final GI.

The `DEFAULT_VOLUME_RESOLUTION` constant (`src/demo3d.h`) and the associated SDF
texture allocation need to change. The SDF compute dispatch size scales automatically
(`calculateWorkGroups()`), so the main code change is the resolution default and the
added memory budget.

**256³** would be close to ground-truth quality but at 64 MB for the SDF alone — viable
for offline analysis but may constrain other textures on lower-VRAM GPUs.

---

## Revised root cause ranking

| Rank | Root cause | Contribution to banding | Fix |
|---|---|---|---|
| 1 (dominant) | SDF 64³ voxel quantization | Rectangular contour steps in both step heatmap and GI | Increase `DEFAULT_VOLUME_RESOLUTION` to 128³ |
| 2 | Cascade blend linear weight kink | Secondary smooth contribution at cascade interval boundaries | **Done — smoothstep applied in radiance_3d.comp** |
| 3 | C0→C1 angular resolution jump (D4→D8, 16→64 bins) | Angular quantization in cosine-weighted GI lookup | Increase `dirRes` to 8 (high display cost) |

---

## Next action

Increase `DEFAULT_VOLUME_RESOLUTION` from 64 to 128 in `src/demo3d.h`. Rebuild,
relaunch, capture mode 5 + final render screenshots and compare with the current
baseline (`frame_17775487414938002.png` / `frame_17775487448111600.png`).

Success criterion: step heatmap shows smooth gradient with no discrete banding steps;
final render shows smooth indirect field without rectangular contour lines.
