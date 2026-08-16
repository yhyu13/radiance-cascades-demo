# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame407.rdc`  
**Analyzed:** 2026-05-05T23:31:01  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Raymarching | draw | 5090.6 |
| GI blur | draw | 1490.4 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **6591.2** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this image as a signed distance field visualization, I can identify several **concerning artifacts**:

## Issues Detected

**1. Hard Seams / Sharp Discontinuities**
- The boundary between the red shape and the black interior shows an abrupt transition rather than a smooth gradient falloff
- A proper SDF should exhibit continuous gradient values radiating outward from the surface

**2. Flat/Constant Regions**
- The black square interior appears as a uniform flat region with no gradient information — this suggests either:
  - Distance values are clamped/saturated
  - The interior SDF computation failed (missing inside/outside sign)
  - Voxelization collapsed fine detail into binary values

**3. Blocky Voxelization Artifacts**
- The outer red glow shows stepped, square-aligned falloff rather than smooth radial distance
- The corners of the bounding region show axis-aligned banding typical of low-resolution voxel grids
- This indicates the SDF was sampled at insufficient resolution

**4. Possible Missing Geometry**
- The arch/torii-like shape inside appears truncated or incomplete at the bottom
- Thin features (legs/pillars) may have fallen below the voxel resolution threshold and dropped out

**5. Incorrect Distance Magnitudes**
- The red coloring intensity doesn't appear to scale linearly with distance
- The transition from red → black is too abrupt, suggesting either narrow-band SDF truncation or incorrect normalization

## Recommendations
- **Increase voxel resolution** (likely 2–4× current density)
- Verify **inside/outside sign** computation (winding number or ray casting)
- Check that **distance values aren't clamped** prematurely
- Ensure thin geometry features exceed the **Nyquist limit** of your voxel grid
- Use **trilinear interpolation** when sampling rather than nearest-neighbor

## Albedo Volume

# Surface Color Inspection Report

## Observed Colors:
- **Left region**: Red wall (arrow/chevron shape pointing right) ✓
- **Right region**: Green wall (arrow/chevron shape pointing left) ✓
- **Background/elsewhere**: **Light gray** ✗

## Flagged Issues:

### ❌ Incorrect Background Color
- **Expected**: White
- **Actual**: Light gray (~RGB 204, 204, 204)
- **Location**: All non-wall regions (top, bottom, center, corners)
- **Severity**: Fails specification — entire background area is the wrong color

### ✓ Wall Colors Pass
- Red region (left): Correct hue and placement
- Green region (right): Correct hue and placement

## Summary:
**1 of 3 color regions fails inspection.** The red and green wall markers are correctly placed and colored, but the surrounding surface should be **white**, not gray. Recommend repainting/recoloring the background to meet the white specification.

## C0 Probe Directional Atlas

# Probe Tile Analysis

Looking at this image, I can identify it as a **light probe atlas** (likely an irradiance/radiance probe volume visualization) where each DxD tile encodes directional lighting information for one probe location.

## Observations

**Spatial coherence check:**
- Neighboring tiles show **smoothly varying colors** across most of the grid — good sign that probes are baked consistently
- The red→green gradient flowing diagonally (red dominant upper-left, green dominant upper-right) suggests directional lighting variation that's spatially correlated ✓

**Potential issues spotted:**

1. **Uniform gray tiles** along the outer border rings — these probes appear **not baked** (or baked in empty space with no contribution). Common at atlas padding or outside the bake volume.

2. **Lower portion tiles** (bottom rows) appear washed-out/near-white with low chroma — could indicate:
   - Probes inside geometry (all rays miss → sky/clear color)
   - Or correctly: probes in a bright open region

3. **No visible random noise blocks** — no obvious merge/atlas-stitching errors detected.

## Alpha-channel interpretation

Per your legend:
- **α > 0** (surface hit): the colored bins carrying actual radiance
- **α < 0** (sky): would appear in upper-hemisphere bins of outdoor probes
- **α = 0** (miss): the gray/empty bins, including the uniform border tiles

## Recommendations

- ✅ Re-bake the **gray border probes** or shrink the probe volume to exclude them
- ⚠️ Verify the **bottom-region probes** aren't stuck inside geometry (check with a probe-validity/backface heuristic)
- ✅ Directional gradient looks physically plausible — no merge corruption

Want me to focus on a specific region or estimate the probe grid resolution?

## C1 Probe Directional Atlas
*Extraction failed: resource not found*

## C0 Isotropic Probe Grid (reduction output)
*Extraction failed: resource not found*

## Final Frame (from capture thumbnail)

# Artifact Analysis

## Detected Artifacts

1. **Outer-wall drift / geometry leakage**: The exterior of the Cornell Box is visible — red and green wall slabs protrude on the left/right sides, and gray top/bottom slabs extend beyond the front opening. This suggests the camera is outside or the front wall is missing, exposing the box shell.

2. **Color bleeding errors (weak/missing)**: The white floor and back wall show very little red/green color bleed from the side walls. In a correct Cornell Box, the floor near the red wall should have a noticeable reddish tint and similarly green near the green wall. Bounce lighting appears under-saturated or under-propagated.

3. **Cascade/probe interpolation softness**: Shadows under both blocks are very soft and slightly detached from the contact points (mild contact-shadow loss), consistent with low-resolution probe interpolation rather than sharp direct shadows.

4. **Mild luminance falloff at back-wall corners (outer-wall drift)**: Subtle darkening gradient toward the upper corners of the back wall, slightly asymmetric.

5. **No strong probe-grid banding, directional bin banding, or shadow acne** visible in this frame.

## Quality Rating: **Fair**

The lighting is smooth and free of obvious banding/seams, but the missing color bleed and visible exterior box geometry are significant correctness issues for a Cornell Box reference scene.
