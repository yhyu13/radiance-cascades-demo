# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame349.rdc`  
**Analyzed:** 2026-05-11T13:33:22  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 59422.4 |
| Cascade reduction | dispatch | 28.0 |
| Cascade bake | dispatch | 49299.5 |
| Cascade reduction | dispatch | 155.7 |
| Cascade bake | dispatch | 61476.7 |
| Cascade reduction | dispatch | 169.7 |
| Cascade bake | dispatch | 38461.4 |
| Cascade reduction | dispatch | 223.9 |
| Raymarching | draw | 55034.1 |
| GI blur | draw | 77404.5 |
| glDrawElements() | draw | 10.6 |
| **Total** | | **341686.6** |


## SDF Volume (Signed Distance Field)

Looking at this image, which appears to be a signed distance field (SDF) visualization:

**Issues identified:**

1. **Hard seams / lack of gradient**: The image shows very stark, binary-like transitions between red and black regions rather than the smooth gradient expected from a proper SDF. A correct SDF should display gradual color/intensity changes radiating outward from surfaces.

2. **Flat regions**: Large areas of uniform red (outside) and uniform black (inside/structure) suggest the distance values are being clamped or the SDF is effectively being rendered as an occupancy/binary field rather than a true distance field.

3. **Voxelization artifacts**: The geometry shows blocky, pixelated edges — particularly visible in:
   - The vertical pillar-like structures on the left and right
   - The horizontal bar/grating in the middle
   - The stepped edges throughout
   
   This indicates the source geometry was voxelized at low resolution before SDF computation.

4. **Potential holes/missing geometry**: There appear to be gaps in the central horizontal structure (the bar with vertical slits) that may represent either intended thin features that weren't captured properly, or missing geometry due to under-resolution.

5. **Sharp boundary at frame edge**: The transition at the outer red boundary is abrupt rather than continuing to gradient outward, suggesting the SDF computation domain was truncated.

**Recommendations:**
- Increase voxel resolution before SDF generation
- Verify the SDF computation uses true Euclidean distances (not just inside/outside classification)
- Check that thin features exceed the voxel size to avoid being lost
- Ensure the visualization isn't applying excessive contrast that masks the gradient

## Albedo Volume

# Surface Color Inspection

Analyzing the rendered image for wall color compliance:

## Expected Color Mapping
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces (floor, ceiling, back wall):** White

## Findings — Issues Detected ⚠️

**1. Left wall — FAIL**
- Expected: Red
- Observed: Pale yellow/cream tone
- The left-side surface shows no red coloring; appears to share the warm off-white palette of the back/floor.

**2. Right wall — FAIL**
- Expected: Green
- Observed: No discernible green region
- The right side appears in muted neutral/brown tones rather than green.

**3. Other surfaces — INCONCLUSIVE**
- Back wall and ceiling appear off-white/cream rather than pure white.
- Floor shows brown tones (could be intentional flooring rather than a wall).

## Summary
Both **colored accent walls (red left, green right) are missing or incorrect**. The scene reads as a uniform warm/neutral palette with cluttered foreground objects (possibly food items on a surface) obscuring wall visibility. 

**Recommendation:** Re-render with stronger color saturation on side walls, or verify the camera angle is actually facing the colored walls. The current image does not satisfy the specified color constraints.

## C0 Probe Directional Atlas

I can't read the specific content of this image clearly, but based on your description, this appears to be a **probe atlas visualization** (likely for irradiance or radiance probes used in global illumination, such as DDGI - Dynamic Diffuse Global Illumination).

Here's what to look for when diagnosing it:

## Diagnostic Checklist

**✅ Healthy signs:**
- Smooth color gradients between neighboring D×D tiles
- Coherent patterns matching scene geometry
- Mix of positive alpha (surface hits) and negative alpha (sky) values distributed sensibly

**🚩 Red flags:**
| Symptom | Likely Cause |
|---|---|
| Uniform gray tile | Probe wasn't baked / update skipped |
| Random noise tile | Merge/blend error, race condition, or uninitialized memory |
| Hard seams between tiles | Border/octahedral wrap not copied correctly |
| All-zero alpha tile | Rays missed entirely (probe inside geometry?) |
| Sudden color discontinuity vs neighbors | Probe placed inside a wall, or relocation failed |

## What I can observe from your image
The atlas shows a **regular grid of small tiles** with what appears to be **fine speckled/dotted patterns** rather than smooth gradients. If those tiles should contain low-frequency irradiance data, that speckle pattern itself is suspicious — it resembles **noise from insufficient ray samples per probe** or a **missing tem

## C1 Probe Directional Atlas

# Cascade 1 Analysis (tMax ~1.0wu, Post-Phase 14c)

## Coverage Assessment

Scanning the 256×256 frame for surface coverage and dead tile detection:

**Overall Coverage:** ~96-97% (below the expected ~100% threshold)

## Tile Grid Status

The tessellation shows the expected dense diamond/

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅

A **direction-averaged radiance** visualization (i.e., the DC/0th-order spherical harmonic, or mean over all sample directions per probe) should display:

- **Smooth spatial gradients** — radiance varies continuously between neighboring probes
- **Plausible color tint** matching the dominant light sources (warm near incandescent, cool near sky portals, neutral in mixed areas)
- **Brighter probes near light sources / windows**, darker in occluded corners
- **Soft falloff** following inverse-square-ish behavior modulated by occlusion
- Energy levels that look like a low-pass filtered version of the scene irradiance

## Red Flags 🚩

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Rays not hitting anything (miss shader returns 0); probe placement inside geometry; integration accumulator never written; wrong buffer bound; NaN/Inf clobbered to 0 |
| **Sharp grid steps / blocky discontinuities** | Probe indexing off-by-one; cascade/level boundary not blended; trilinear sample weights wrong; per-probe normalization divisor inconsistent (e.g., dividing by sample count that varies) |
| **Wrong colors** (green sky, magenta walls, etc.) | Channel swizzle (RGB↔BGR); sRGB vs linear mismatch; HDR tonemap applied twice; wrong texture format (R11G11B10 sign issues); accumulating in wrong color space |
| Banding / quantization | Storing in LDR format (RGBA8) without scale factor; insufficient bits for HDR range |
| Random sparkles | Firefly samples not clamped; division by tiny PDF; uninitialized memory |
| Uniform gray everywhere | Normalization bug — dividing by total ray count across *all* probes instead of per-probe |
| Probes "leak" through walls | Visibility/chebyshev test missing; bias too large |

## Quick Sanity Checks

1. **Pick one probe near a known light** — its averaged radiance should roughly match `light_color × solid_angle / (4π)` plus bounce contribution
2. **Log min/max/mean** of the probe buffer — if min == max, normalization is broken; if max is 0, integration is broken
3. **Visualize a single channel** to rule out swizzle
4. **Disable tonemapping** and view raw values via false-color ramp
5. **Check probe world positions** are where you think they are (overlay as gizmos)

If you can share what the visualization actually looks like (or the shader snippet doing the averaging), I can narrow it down further.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Severe motion blur / temporal smearing** — The entire frame shows aggressive blurring, particularly on the right wall and ceiling. This isn't in your listed artifact types but dominates the image.

2. **Probe-grid banding** — Visible on the ceiling as the regular horizontal stripe pattern of brown/dark blocky segments. The repeating chunked structure across the ceiling indicates probe interpolation artifacts at grid resolution.

3. **Directional bin banding** — The floor shows hard angular streaks of bright white/cream patches in a stepped pattern down the center. These look like discrete directional bins rather than smooth indirect lighting falloff.

4. **Color bleeding errors** — The right side of the frame is saturated with warm brown/orange tones bleeding heavily onto what should be neutral surfaces. The bleed extent appears excessive and lacks proper directional falloff.

5. **Outer-wall drift** — The left wall shows a monotonic gradient from bright (far) to dark (near foreground), and the right wall darkens toward the edge inconsistently with the apparent light direction.

6. **Cascade boundary hints** — Subtle ring-like brightness transitions visible in the mid-distance ceiling area where lighting cascades likely transition.

**Location summary:**
- Ceiling: probe-grid banding, cascade seams
- Floor centerline: directional bin banding (the bright stepped patches)
- Right wall: excessive color bleed, outer-wall drift
- Left wall: outer-wall drift (brightness gradient)

**Quality rating: Poor**

The combination of heavy blur obscuring detail, visible probe grid structure on the ceiling, stepped directional binning on the floor, and unbalanced color bleeding makes this frame difficult to read as a coherent Cornell Box scene.
