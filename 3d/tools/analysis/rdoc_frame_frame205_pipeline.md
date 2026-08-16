# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame205.rdc`  
**Analyzed:** 2026-05-11T15:32:25  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 44634.0 |
| Cascade reduction | dispatch | 159.2 |
| Cascade bake | dispatch | 73977.9 |
| Cascade reduction | dispatch | 159.9 |
| Cascade bake | dispatch | 82525.9 |
| Cascade reduction | dispatch | 82548.7 |
| Cascade bake | dispatch | 354318.3 |
| Cascade reduction | dispatch | 133773.3 |
| Raymarching | draw | 2156.5 |
| GI blur | draw | 229.4 |
| glDrawElements() | draw | 10.5 |
| **Total** | | **774493.6** |


## SDF Volume (Signed Distance Field)

Looking at this image as an SDF (Signed Distance Field) visualization, I can identify several issues:

## Problems Detected

**1. Hard Seams / Sharp Discontinuities**
- There are abrupt transitions between the bright red exterior and the dark interior structures, with no visible gradient falloff. A proper SDF should show smooth value interpolation across the boundary.

**2. Flat/Saturated Regions**
- The outer red area appears uniformly saturated, suggesting distance values are clamped or the field isn't being computed beyond a narrow band near the surface.
- The black interior regions also look flat, indicating missing interior distance information (potentially an unsigned field or clipped negative values).

**3. Voxelization Artifacts**
- The interior geometry shows clear blocky, pixelated edges on the vertical "pillar" shapes and the horizontal bar, characteristic of low-resolution voxel sampling rather than a smooth distance function.
- The small repeating notches along the top of the central bar look like aliasing from undersampling fine features.

**4. Possible Holes / Missing Geometry**
- The gaps between the vertical structures and the central horizontal element appear as hard black voids rather than smoothly varying interior distances.
- The symmetry suggests intended structure, but the disconnections between elements may indicate broken/missing connective geometry.

**5. Missing Gradient Band**
- A healthy SDF visualization typically shows iso-contour bands (concentric value rings) around surfaces. None are visible here — only binary-looking inside/outside regions.

## Recommendations
- Increase voxel resolution to resolve thin features
- Verify the SDF computation extends a sufficient distance from surfaces (widen the narrow band)
- Check that signed (negative interior) values are being preserved, not clamped to zero
- Use a remapped colormap (e.g., diverging) to better visualize the full distance range

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings — Multiple Incorrect/Missing Regions Flagged

**🚩 Left Wall - INCORRECT**
- Expected: Red
- Observed: Tan/beige and white tones
- Status: Missing red coloration entirely

**🚩 Right Wall - INCORRECT**
- Expected: Green
- Observed: Tan/beige tones with no green present
- Status: Missing green coloration entirely

**🚩 Background/Surrounding Area - INCORRECT**
- Expected: White
- Observed: Dark brown/maroon background
- Status: Does not match white specification

**⚠️ Interior Surface - PARTIAL MATCH**
- Observed: Yellowish-cream base (close to white but tinted)
- Contains scattered colored elements (purple, green, orange, red dots) that don't conform to a uniform white surface

## Summary
The image appears to depict a pixel-art tray/platter with food items rather than the room/surface configuration described. **None of the three specified color regions (red left, green right, white elsewhere) are correctly represented.** All three zones require correction to meet the stated color scheme.

## C0 Probe Directional Atlas

I'll analyze the image based on your description of probe tiles and baking diagnostics.

**Observations:**

1. **Overall appearance**: The image shows a grid-like structure with tile patterns, consistent with a probe atlas layout (likely an irradiance/radiance probe volume visualization, similar to DDGI octahedral probes).

2. **Tile uniformity**: The tiles appear predominantly **uniform light gray/white** across most of the atlas. Per your flag criteria, **uniform gray tiles indicate probes that have not been baked** — this is a significant issue covering the majority of the volume.

3. **No visible random noise**: I don't see high-frequency speckle patterns that would indicate merge errors, so atlas merging appears intact.

4. **Neighbor smoothness**: Where structure is visible (the denser dotted regions forming a rectangular border and a few interior blocks on the left), neighboring tiles do appear to vary smoothly — no harsh discontinuities between adjacent probes in those regions.

5. **Structural features**: 
   - A clear rectangular border of "active" probes
   - Two pr

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 

## C0 Isotropic Probe Grid (reduction output)

The image you've shared appears to be entirely white (or blank), so I cannot evaluate the direction-averaged radiance per probe visualization you're describing.

For a proper diagnostic of this kind of probe visualization, I would expect to see:

**Healthy signs:**
- Smooth spatial gradients across the probe grid
- Brighter probes near light sources, darker in shadowed/occluded regions
- Color tints consistent with nearby emitters and bounce surfaces
- Gradual falloff with distance from lights

**Red flags to check for:**
- ❌ **All-black probes** — suggests probes failed to gather radiance (bad ray origins, missed scene, zero-initialized buffer not written, or sampling/accumulation bug)
- ❌ **Sharp grid steps** — indicates lack of interpolation, quantization in storage (e.g., bad encoding range), or per-probe normalization errors
- ❌ **Wrong colors** — could mean swapped color channels (RGB vs BGR), incorrect tonemapping, gamma/linear mismatch, or contaminated SH/octahedral encoding

Could you re-upload the image? If it's a render target dump, make sure it was saved with valid pixel data and proper exposure — a fully white output itself could indicate an overflow, NaN-to-white clamp, or uninitialized buffer being displayed.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the floor and ceiling as blocky, rectangular luminance patches, especially in the mid-corridor where indirect light transitions occur. The grid structure is clearly resolvable.

2. **Cascade boundary seams** — Noticeable as a brightness discontinuity roughly mid-depth in the corridor, where the lighting steps from a warmer/brighter near-field to a darker far-field band.

3. **Directional bin banding** — Angular color steps are apparent on the right-hand wall and ceiling, where the indirect illumination forms hard wedge-shaped transitions rather than smooth falloff.

4. **Outer-wall drift** — The upper-right wall/ceiling region shows a monotonic darkening toward the edge, inconsistent with the corridor's symmetry, suggesting probe interpolation drift near the frame boundary.

5. **Color bleeding errors (mild)** — The reddish/orange tint dominates surfaces that should remain more neutral (e.g., the floor far from the light source), suggesting overreach of the emissive bounce.

**No significant:** shadow acne or missing shadows (scene is mostly diffuse and the occlusion structure looks plausible).

---

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts (probe grid, cascade seams, directional bins, edge drift) that are simultaneously visible and structurally obvious, undermining the smooth low-frequency indirect lighting that radiance cascades should deliver.
