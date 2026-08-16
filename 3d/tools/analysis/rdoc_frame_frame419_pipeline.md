# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame419.rdc`  
**Analyzed:** 2026-05-11T15:34:09  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 38664.2 |
| Cascade reduction | dispatch | 28.7 |
| Cascade bake | dispatch | 28448.7 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 42741.8 |
| Cascade reduction | dispatch | 169.9 |
| Cascade bake | dispatch | 36183.9 |
| Cascade reduction | dispatch | 231.4 |
| Raymarching | draw | 4111.4 |
| GI blur | draw | 2836.1 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **153584.0** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a Signed Distance Field (SDF) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: There's a very abrupt transition between the bright red exterior and the dark interior region. A proper SDF should show a smooth gradient from outside (positive) through the surface (zero) to inside (negative). Here the transition appears binary-like rather than gradual.

2. **Flat regions**: 
   - The outer red area appears largely uniform/saturated, suggesting clamped or saturated distance values rather than a continuous gradient extending outward.
   - The dark interior also looks relatively flat in many areas, indicating the inside distances aren't being computed with proper falloff toward the medial axis.

3. **Holes / missing geometry**: The pattern in the center (resembling text or structured geometry like "B...") shows isolated dark pixels and fragmented shapes. This suggests:
   - Possible voxelization artifacts where thin features are under-sampled
   - Disconnected components that shouldn't be disconnected
   - Missing interior fill for what should be solid shapes

4. **Voxelization errors**: The blocky, pixelated appearance of the interior features (especially the small repeating elements that look like a grid or text) indicates the source geometry was voxelized at too low a resolution relative to feature size. Thin walls and small details are being lost or aliased.

5. **Incorrect distances**: The lack of smooth iso-contours (you'd expect to see gradual color bands) suggests the distance computation may be using only nearest-neighbor or a limited search radius, not true Euclidean distance.

**Recommendations:**
- Increase voxel resolution to capture thin features
- Use a proper Euclidean distance transform (e.g., jump flooding or fast sweeping)
- Verify sign computation (inside/outside test) is robust for thin/complex geometry
- Check that the visualization isn't clamping values too aggressively, hiding the actual gradient
- Ensure narrow-band SDF computation extends far enough from the surface

## Albedo Volume

# Surface Color Inspection Report

Analyzing the image against the specified color scheme (red left wall, green right wall, white elsewhere):

## Detected Issues

**❌ Left Wall - INCORRECT**
- Expected: Red
- Observed: Pale yellow/cream tones with brown/tan shading
- **Flag:** Missing red coloration entirely

**❌ Right Wall - INCORRECT**
- Expected: Green
- Observed: Cream/beige with brown shadows
- **Flag:** Missing green coloration entirely

**⚠️ Other Surfaces - PARTIALLY INCORRECT**
- Expected: White
- Observed: Predominantly yellow/cream interior with scattered colored elements (purple, green, orange, red dots — possibly decorative items or objects on the surface)
- **Flag:** Background is off-white/yellow rather than pure white

## Additional Observations
- The surrounding border/frame is dark brown (not part of the specified scheme)
- Small multi-colored specks in the central area may be intentional objects rather than surface color errors

## Summary
**All three specified color regions fail validation.** The image appears to show a warm-toned scene (possibly food or a tray) rather than the red/green/white configuration described. Recommend recoloring or verifying the source asset.

## C0 Probe Directional Atlas

I can't actually see fine detail in the image clearly, but based on your description, here's how to interpret what you're looking at — this appears to be an **irradiance probe at

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

This visualization computes the **mean radiance over all directions** for each probe in your light field / irradiance volume, giving a single color per probe that represents its overall incident lighting.

## What a Healthy Result Looks Like

- **Smooth spatial gradients** across the probe grid — radiance should vary continuously as probes move through the scene
- **Plausible color tints** matching room lighting (e.g., warm near incandescent sources, cool near windows/sky)
- **Brightness falloff** with distance from light sources, modulated by occluders
- **Soft transitions** at wall/ceiling boundaries (probes near surfaces pick up bounce color)

## Failure Flags to Watch For

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Capture never ran, wrong buffer bound, missing exposure/tonemap, NaNs zeroed out, or probes placed inside geometry with no rays escaping |
| **Sharp grid steps** | Per-probe normalization bug, integer overflow in accumulation, missing solid-angle weights, or quantization (e.g., 8-bit storage of HDR values) |
| **Wrong colors** | Channel swizzle (RGB↔BGR), gamma applied twice or not at all, color space mismatch (linear vs sRGB), or sampling the wrong cubemap face |

## Quick Debug Checklist

1. **Log a single probe's raw samples** — confirm non-zero, finite, and in expected HDR range
2. **Verify averaging weights** — uniform direction sampling needs `1/N`; cubemap/octahedral needs solid-angle weights
3. **Check probe placement** — probes inside walls produce black or single-color artifacts
4. **Compare against a reference probe** — render one probe location with a path tracer and match the averaged value
5. **Disable tonemap/exposure** in the debug view to isolate data vs display issues

Want me to suggest a specific visualization layout (slice view, isosurface, splat) or help interpret an actual capture if you share the image?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Severe artifacts detected:**

1. **Missing geometry / ray miss regions** — Large black void occupying the center-left of the frame. This appears to be rays escaping the scene or hitting un-shaded back-faces, not a legitimate shadow.

2. **Cascade boundary seams** — Visible as the jagged, stair-stepped silhouette edges around the black region (top edge near the ceiling and along the right border of the void). The discrete stepping is characteristic of cascade interval transitions.

3. **Probe-grid banding** — The dashed/dotted bright streak across the floor (lower-center) reveals the underlying probe sampling lattice; light is leaking through at regular probe intervals.

4. **Outer-wall drift** — The right wall shows a strong monotonic brightening gradient from dark brown (center) to nearly white (upper-right corner), inconsistent with a uniform Cornell wall.

5. **Color bleeding errors** — The right wall exhibits a saturated orange/brown tint that bleeds across most of its surface, far stronger than expected from a single bounce, and the left wall shows pinkish contamination near the floor.

6. **Light leak** — Bright white spot in the upper-right corner suggests indirect light bleeding through the wall/ceiling join.

## Quality Rating: **Poor**

The frame is dominated by a large unshaded/black region, the camera or geometry appears broken, and multiple compounding artifacts (cascade seams, probe banding, drift, leaks) make the scene essentially unreadable as a Cornell Box.
