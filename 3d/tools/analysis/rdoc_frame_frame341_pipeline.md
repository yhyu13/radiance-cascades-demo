# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame341.rdc`  
**Analyzed:** 2026-05-11T15:38:10  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 15027.9 |
| Cascade reduction | dispatch | 28.3 |
| Cascade bake | dispatch | 10112.9 |
| Cascade reduction | dispatch | 176.1 |
| Cascade bake | dispatch | 15313.5 |
| Cascade reduction | dispatch | 169.7 |
| Cascade bake | dispatch | 7919.6 |
| Cascade reduction | dispatch | 223.5 |
| Raymarching | draw | 18700.3 |
| GI blur | draw | 6315.8 |
| glDrawElements() | draw | 10.6 |
| **Total** | | **73998.3** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: There's a very abrupt transition between the bright red exterior and the dark interior region. A proper SDF should show a smooth gradient from outside (positive) to inside (negative) values. The near-binary appearance suggests the field is being clamped or thresholded.

2. **Flat regions**: 
   - The outer red area appears largely uniform/saturated, indicating distance values are either clipped at a maximum or the gradient isn't being preserved far from the surface.
   - The dark interior also looks flat in several places, suggesting interior distances aren't being computed (possibly an unsigned DF rather than signed).

3. **Potential holes/missing geometry**:
   - The horizontal bright band in the middle of the dark region is suspicious — it could indicate a discontinuity, a missing surface, or incorrect inside/outside classification.
   - The two vertical dark "pillar" structures on the left and right have very sharp edges with no gradient falloff, indicating voxelization artifacts.

4. **Voxelization errors**: The blocky, pixelated edges around the interior shapes (especially the pillar-like vertical structures and the central horizontal feature) are classic signs of:
   - Insufficient resolution
   - Nearest-neighbor sampling instead of interpolation
   - The SDF being reconstructed from a low-resolution voxel grid

5. **Incorrect distances**: The lack of smooth isocontours (you'd expect to see gradual color bands radiating from surfaces) suggests the distance computation may only be evaluating immediate neighbors rather than true Euclidean distance to the nearest surface.

**Recommendations:**
- Verify the SDF generation uses true Euclidean distance (e.g., fast marching or brute-force) rather than chamfer/grid-step approximations
- Check that interior points are being assigned negative values
- Increase voxel resolution or use trilinear interpolation when sampling
- Ensure the visualization isn't clamping the value range too aggressively

## Albedo Volume

# Surface Color Inspection

Analyzing the pixelated structure in the image:

## Expected Color Scheme
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Detected Issues

🚩 **Left wall — FAIL**
- Observed: Pale yellow/cream tones
- Expected: Red
- Status: Incorrect color

🚩 **Right wall — FAIL**
- Observed: Pale yellow/cream tones (similar to left)
- Expected: Green
- Status: Incorrect color

🚩 **Floor/base surface — PARTIAL**
- Observed: Mixed colors including purple, yellow, green, and orange specks
- Expected: White
- Status: Contains foreign color regions (likely objects/items, but no clean white base visible)

⚠️ **Ceiling/top surface — PARTIAL**
- Observed: Light cream with brown/tan streaks
- Expected: White
- Status: Off-white, contamination present

✅ **Background (outside structure):** Brown — not part of the colored-surface spec (neutral)

## Summary
**No surfaces match the expected Cornell-box-style color scheme.** The characteristic red-left / green-right / white-diffuse arrangement is **missing entirely**. The image appears to show a scene (possibly a table or container with items) rather than the expected color-coded reference geometry.

## C0 Probe Directional Atlas

I can't actually verify what's in the image with certainty, but based on your description this looks like a **baked irradiance/light probe atlas** (likely for global illumination, similar to Unity's APV or Unreal's volumetric lightmaps).

Here's how to read it against your flags:

**What you're looking at:**
- A grid of **D×D tiles**, each tile = one probe's directional irradiance (or visibility/octahedral) data
- Neighboring tiles should blend smoothly because nearby probes sample similar lighting

**Diagnostic checklist:**

| Symptom | Meaning |
|---|---|
| Uniform gray tile | Probe wasn't baked / skipped |
| Random noise tile | Merge/atlas packing error, or uninitialized memory |
| Hard seams between neighbors | Probe placement discontinuity (wall, occluder) — may be correct |
| Soft gradients across tiles | ✅ Healthy b

## C1 Probe Directional Atlas

# C

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

You're describing a **debug visualization for a light probe grid** (likely an irradiance volume or DDGI-style setup). Here's a breakdown of what to look for and what failure modes indicate:

## ✅ Expected Appearance
- **Smooth spatial gradients** across the grid — neighboring probes should have similar averaged radiance since lighting varies continuously in space
- **Plausible color tint** matching the room's dominant light sources (warm near lamps, cool near windows, etc.)
- **Soft falloff** in occluded regions (under furniture, in corners) — darker but not black
- **Energy levels** consistent with your tonemapping / exposure

## 🚩 Failure Flags

### All-black probes
Likely causes:
- Probes never received any ray hits (BLAS/TLAS not built, or rays missed geometry)
- Radiance accumulator cleared but never written (dispatch size mismatch, wrong probe index)
- NaN/Inf poisoning → clamped to zero downstream
- Probes are inside geometry and back-face culled
- Sky/miss shader not contributing, and no direct lighting reached them

### Sharp grid steps (discontinuities between adjacent probes)
Likely causes:
- **Per-probe coordinate frame mismatch** — each probe sampling directions in a different basis
- Hysteresis / temporal blend factor too low (probes converging to different stochastic samples)
- Indexing bug: probe `(x,y,z)` writing to wrong slot, scrambling neighbors
- Octahedral encoding seam not handled, leaking into the average
- Different ray counts per probe (some starved)

### Wrong colors
Likely causes:
- **Color space confusion** — linear vs sRGB at write or read time
- Channel swizzle (BGR vs RGB) in the storage texture format
- Albedo applied twice (once at hit, once at probe sample)
- Emissive surfaces over-contributing (no clamp on first-bounce radiance)
- Tonemap applied before averaging instead of after

## Quick Triage Checklist
1. Print raw radiance values for one probe — sane magnitude?
2. Visualize ray hit count per probe — uniform?
3. Disable temporal blending — does single-frame look correct?
4. Render with constant white albedo — does the gradient appear?
5. Check probe world positions against grid origin/spacing

Want me to go deeper on any specific failure mode, or look at shader code if you have it?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the ceiling and upper walls as regular blocky/pixelated patches of brown coloring. The discrete probe samples are clearly visible as rectangular tiles rather than smooth indirect lighting.

2. **Color bleeding errors** — The brown/red color from what appears to be a side wall is bleeding excessively onto the ceiling and floor. The far right wall shows large smooth brown gradients that don't correspond to a plausible light transport path; the ceiling shows scattered brown patches that look like misprojected bounce light.

3. **Outer-wall drift** — The right wall shows a strong monotonic darkening from center toward the edges/foreground, and the left wall shows similar brightening/washout near the camera, suggesting cascade interpolation drift at the outer regions.

4. **Directional bin banding** — The streak-like white/bright artifacts along the floor (the dashed bright marks leading toward the light source) appear to be hard angular steps from coarse directional binning, rather than a smooth specular/glossy reflection.

5. **Cascade boundary seams** — Faint ring-like transitions visible in the mid-distance ceiling region where the probe-tile size appears to change scale.

**Location summary:**
- Ceiling: probe-grid banding + color bleed
- Floor: directional bin banding (bright streaks)
- Right wall: outer-wall drift (darkening)
- Left wall: outer-wall drift (over-brightening/wash)

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts. The probe resolution appears too low for the scene scale, directional resolution is insufficient for the bright light source causing aliased streaks, and cascade interpolation is producing visible drift on lateral walls.
