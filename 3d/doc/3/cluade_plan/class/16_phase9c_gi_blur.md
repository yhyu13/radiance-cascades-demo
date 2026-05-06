# 16 — Phase 9c/13b: Bilateral GI Blur

**Purpose:** Understand the 3-attachment FBO layout, how the bilateral filter works,
and what each edge stop does.

---

## The problem this solves

Probe GI is spatially low-resolution: at 32³ with a 4 m scene, each probe covers 0.125 m.
The indirect term is sampled from this coarse grid. At screen resolution the per-probe
steps are visible — the indirect image is blocky.

Bilateral blur smooths the indirect spatially while preserving edges at:
- Depth discontinuities (foreground/background separation)
- Normal discontinuities (wall corners, surface boundaries)
- Luminance discontinuities (Phase 13b: bright/dark surface transitions)

---

## FBO layout (giFBO)

When `useGIBlur = true` and `raymarchRenderMode` is in **{0, 3, 6}**, the raymarch pass
renders into a 3-attachment FBO instead of the default framebuffer:

```cpp
// src/demo3d.cpp:1699
const bool giBlurActive = useGIBlur &&
    (raymarchRenderMode == 0 || raymarchRenderMode == 3 || raymarchRenderMode == 6);
```

```
giFBO attachment 0: giDirectTex    — direct lighting in linear space (from raymarch location=0)
giFBO attachment 1: giGBufferTex   — packed: normal×0.5+0.5 (rgb) + linearDepth (a), location=1
giFBO attachment 2: giIndirectTex  — indirect/GI in linear space (from raymarch location=2)
```

All three textures are the same screen resolution as the window. Created / destroyed on
resize by `initGIBlur(w, h)` / `destroyGIBlur()`.

The `giDirectTex` and `giGBufferTex` are written by the unmodified raymarch shader output
at locations 0 and 1. `giIndirectTex` is the raw (unblurred) indirect term.

---

## gi_blur.frag

After the raymarch pass, `gi_blur.frag` runs a full-screen pass:

1. Read `giDirectTex` and `giGBufferTex` for the current pixel.
2. For each neighbor within radius `giBlurRadius`, compute bilateral weight:
   ```glsl
   float wDepth  = exp(-abs(dCenter - dNeighbor) / uDepthSigma);
   float wNormal = exp(-abs(1.0 - dot(nCenter, nNeighbor)) / uNormalSigma);
   float wLum    = exp(-abs(lumCenter - lumNeighbor) / uLumSigma);  // Phase 13b
   float w = wDepth * wNormal * wLum;
   ```
3. Accumulate weighted `giIndirectTex` samples.
4. Write `giDirectTex + blurredIndirect` to the default framebuffer.

The direct term is passed through unblurred — direct lighting has sharp shadows and
should not be softened. Only indirect is blurred.

---

## Edge stops explained

**Depth stop (`giBlurDepthSigma = 0.05`)**
`exp(-|Δdepth| / 0.05)` — weight drops to ~14% at 0.05 linear depth units difference.
At a wall edge, the near surface and the far background have very different depths;
the blur correctly avoids bleeding indirect between them.

**Normal stop (`giBlurNormalSigma = 0.2`)**
`exp(-|1 − dot(N₁, N₂)| / 0.2)` — weight drops to ~14% when normals differ by ~65°.
At a corner (wall meeting floor), normals are perpendicular — indirect from the wall
does not bleed onto the floor.

**Luminance stop (`giBlurLumSigma = 0.4`, Phase 13b)**
`exp(-|Y₁ − Y₂| / 0.4)` where Y is `dot(color, vec3(0.2126, 0.7152, 0.0722))`.
Prevents within-surface tonal leakage: a dark-painted box next to a bright white wall
(same depth, same normal) would otherwise bleed indirect across the color boundary.
Set `giBlurLumSigma = 0.0` to disable (equivalent to Phase 9c behavior before 13b).

---

## Radius

`giBlurRadius = 8` — the filter samples a `(2×8+1)² = 17×17` pixel neighborhood.
This covers approximately one probe's footprint at 1280×720 with 32³ probes.

Larger radius: smoother indirect but more GPU work (O(r²) per pixel).
Smaller radius: less smoothing, probe steps still visible.

---

## When the blur is active / skipped

GI blur is active when `useGIBlur=true` AND `raymarchRenderMode ∈ {0, 3, 6}`:

| Mode | Name | Blur? |
|---|---|---|
| 0 | Final composite (direct + indirect) | **Yes** |
| 3 | Indirect × 5 debug view | **Yes** |
| 6 | GI-only debug view | **Yes** |
| 1, 2, 4, 5, 7+ | Normals, SDF dist, atlas views, etc. | No |

When blur is skipped (mode not in {0,3,6}, or `useGIBlur=false`): the raymarch renders
directly to the default framebuffer; `giDirectTex` and `giIndirectTex` are not used.

---

## Defaults

| Parameter | Default | Effect |
|---|---|---|
| `useGIBlur` | true | Bilateral blur active in final mode |
| `giBlurRadius` | 8 | 17×17 pixel neighborhood |
| `giBlurDepthSigma` | 0.05 | Stops at depth edges |
| `giBlurNormalSigma` | 0.2 | Stops at normal edges |
| `giBlurLumSigma` | 0.4 | Stops at luma edges (Phase 13b) |
