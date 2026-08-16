# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame339.rdc`  
**Analyzed:** 2026-05-11T15:45:34  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 6317.0 |
| Cascade reduction | dispatch | 26.9 |
| Cascade bake | dispatch | 10277.3 |
| Cascade reduction | dispatch | 157.9 |
| Cascade bake | dispatch | 22429.1 |
| Cascade reduction | dispatch | 167.9 |
| Cascade bake | dispatch | 8828.6 |
| Cascade reduction | dispatch | 221.3 |
| Raymarching | draw | 10227.7 |
| GI blur | draw | 10743.7 |
| glDrawElements() | draw | 10.5 |
| **Total** | | **69408.0** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/sharp transitions**: The image shows very abrupt transitions between black (interior/negative) and red (exterior/positive) regions. A proper SDF should display a **smooth gradient** from dark to light as distance increases from the surface. Here, the red appears nearly uniform in saturation across most of the exterior.

2. **Flat regions**: The large red areas surrounding the geometry appear to have nearly constant values rather than increasing distance values radiating outward. This suggests either:
   - Distance values are clamped/saturated
   - The SDF was computed with a very limited narrow band
   - Values outside a threshold were set to a constant

3. **Missing gradient near the surface**: There's no visible "halo" of intermediate values around the black geometry. A correct SDF would show a soft falloff, not a binary-like edge.

4. **Geometry concerns**:
   - The structure appears to be a building/facade with columns and a central detailed region (possibly windows or a grid pattern)
   - The two vertical black bars (columns) on the left and right look like they may be **disconnected** from the main mass, which could indicate missing geometry connections
   - The central horizontal band of small features looks **voxelized/aliased** — individual cells are visible rather than smooth distance contours

5. **Possible holes**: The small bright red speckles within the central dark band could represent holes in the SDF where interior points were incorrectly classified as exterior (sign-flip errors), common in non-watertight mesh inputs.

**Likely errors:**
- **Voxelization artifacts** in the central detailed region
- **Sign computation errors** producing pinhole leaks
- **Clamped distance range** eliminating the smooth far-field gradient
- Possible **non-manifold input geometry** causing inconsistent inside/outside classification

**Recommendation**: Recompute the SDF using a higher resolution, ensure the input mesh is watertight, use a robust generalized winding number for sign determination, and avoid clamping distance values during visualization.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Detected Issues

**❌ Left Wall - INCORRECT**
- Expected: Red
- Observed: Tan/beige and white tones
- Status: **FLAGGED** — no red pigmentation present

**❌ Right Wall - INCORRECT**
- Expected: Green
- Observed: Tan/beige and white tones
- Status: **FLAGGED** — no green pigmentation present

**⚠️ Other Surfaces - PARTIALLY CORRECT**
- Expected: White
- Observed: Mostly white/cream with scattered colored elements (purple, yellow, green, orange spots — likely decorative items on the surface)
- Status: Base surface acceptable, but contains non-white inclusions

## Summary
The image appears to show a decorated tray or platter rather than a room with colored walls. **Both the red left wall and green right wall are missing entirely.** The dominant palette is brown (background/border) and off-white (central surface), which does not match the specified color regions.

**Recommendation:** Verify the image corresponds to the intended scene, or update the color specification to match the actual content.

## C0 Probe Directional Atlas

I can

## C1 Probe Directional Atlas

# C

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅
- **Smooth spatial gradients** across the probe grid
- Brighter values near light sources (windows, lamps, emissive surfaces)
- Darker values in occluded areas (under furniture, in corners, behind walls)
- Color tints reflecting nearby bounce surfaces (warm near wood, cool near sky-lit areas)
- Gentle falloff with distance from emitters

## Red Flags 🚩

### 1. All-Black Probes
**Possible causes:**
- Probes failed to gather/integrate radiance (zero samples accumulated)
- Ray budget = 0 or rays terminated immediately
- Probe placed inside geometry → all rays hit backfaces and got culled
- Missing emissive contribution / sky not bound
- Accumulation buffer cleared but never written
- NaN/Inf clamped to zero

**Check:** sample count per probe, ray hit distances, backface handling, sky/emissive inputs.

### 2. Sharp Grid Steps
**Possible causes:**
- No trilinear interpolation between probes (nearest-neighbor lookup)
- Probe spacing too large relative to lighting frequency
- Per-probe normalization differs (inconsistent sample counts)
- Visibility/occlusion weights not blended smoothly
- Probes on opposite sides of a wall being averaged without occlusion test (causes hard discontinuities at wall boundaries)

**Check:** interpolation weights, depth/visibility test (Chebyshev / mean-distance squared), probe density.

### 3. Wrong Colors
**Possible causes:**
- **Color space mismatch:** linear vs sRGB, gamma applied twice or not at all
- **Channel swap:** BGR vs RGB
- **HDR not tonemapped** for visualization (looks washed out or oversaturated)
- **Wrong primaries:** Rec.709 vs Rec.2020
- **Negative radiance** from spherical harmonics ringing
- Albedo baked into probe (should be irradiance, not reflected radiance)
- Light intensity in wrong units (lux vs nits vs arbitrary)

**Check:** swizzle, gamma pipeline, SH reconstruction clamps, units.

## Quick Triage Workflow
1. **Visualize raw sample count** → catches dead probes
2. **Visualize single channel (luminance)** → isolates color vs intensity issues
3. **Toggle interpolation off** → confirms whether stepping is in storage or sampling
4. **Compare to reference path-traced single point** → ground-truth check

Want me to focus on a specific renderer (DDGI, irradiance volumes, light probes in Unity/Unreal) or debugging shader code?

## Final Frame (from capture thumbnail)

# Artifact Analysis

The frame shows significant rendering artifacts in this corridor-like view:

**Identified artifacts:**

1. **Probe-grid banding** (severe): Highly visible blocky/voxelized patterns across the ceiling and along the floor highlights. The bright streaks on the floor appear as discrete chunks rather than smooth specular/lit regions, indicating coarse probe sampling.

2. **Directional bin banding**: The ceiling shows hard angular steps in the indirect lighting, with brown/orange "tooth" or "comb" shaped patterns repeating in a directional manner — characteristic of low angular resolution in directional bins.

3. **Color bleeding errors**: Strong brown/red tint dominates the right wall and bleeds heavily onto the ceiling and floor edges. The left wall appears washed-out white while the right side is excessively saturated, suggesting asymmetric or incorrect color transport.

4. **Outer-wall drift**: The walls show monotonic darkening toward the right edge and brightening toward the left, with no clear geometric justification — the right side fades into deep brown shadow while the left fades to near-white.

5. **Cascade boundary hints**: Faint transitions visible in the mid-distance ceiling area where lighting character changes abruptly.

**Excessive blur**: The overall image is very blurry, possibly from aggressive cascade upsampling/interpolation masking finer detail but not hiding the structural artifacts.

## Quality Rating: **Poor**

The combination of visible probe grid structure, directional banding on the ceiling, and strong asymmetric color bleeding makes this frame clearly unfinished. The 48 FPS suggests performance is being prioritized over cascade resolution.
