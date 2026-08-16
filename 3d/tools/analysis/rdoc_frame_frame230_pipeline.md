# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame230.rdc`  
**Analyzed:** 2026-05-11T15:22:53  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 6004.2 |
| Cascade reduction | dispatch | 28.7 |
| Cascade bake | dispatch | 9846.6 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 15102.4 |
| Cascade reduction | dispatch | 167.9 |
| Cascade bake | dispatch | 8630.0 |
| Cascade reduction | dispatch | 222.3 |
| Raymarching | draw | 18966.7 |
| GI blur | draw | 18284.5 |
| glDrawElements() | draw | 10.3 |
| **Total** | | **77421.4** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/sharp transitions**: The image shows very abrupt transitions between black (interior/negative) and red (exterior/positive) regions. A proper SDF should display smooth gradients radiating outward from the surface.

2. **Flat regions**: The large uniform red areas surrounding the geometry indicate the distance field is saturated or clamped — there's no visible falloff gradient. This suggests either:
   - The SDF range is too narrow (values clipped)
   - Only a binary inside/outside mask was computed rather than true distances

3. **Voxelization artifacts**: The blocky, pixelated edges of the black geometry (looks like a chair or seat-like structure) reveal coarse voxel resolution. Stair-stepping is visible along curved or angled surfaces.

4. **Missing geometry / holes**: 
   - The internal black region appears fragmented with small disconnected pixels and gaps
   - Vertical bands at the sides suggest legs/supports, but they appear hollow or incomplete
   - The horizontal slatted area in the middle has irregular spacing that may indicate missing voxels

5. **Incorrect distances**: True SDF would show concentric gradient bands (darker near surface, lighter far away on the outside; inverse inside). Here the field appears essentially binary, indicating the distance computation likely failed or was replaced with an occupancy grid.

**Recommendations:**
- Increase voxel resolution to reduce stair-stepping
- Verify the distance transform algorithm (e.g., use proper Euclidean distance transform, not just inside/outside test)
- Expand the visualization range to show gradient falloff
- Check mesh watertightness — holes in source geometry cause SDF computation to fail or produce incorrect signs
- Inspect for non-manifold edges that may corrupt sign determination

## Albedo Volume

# Surface Color Inspection Report

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Detected Issues

**❌ Left Wall - INCORRECT**
- Expected: Red
- Detected: Pale yellow/cream tones with brown shadowing
- Status: **FLAGGED** - Missing red coloration

**❌ Right Wall - INCORRECT**
- Expected: Green
- Detected: Light beige/off-white with brown edges
- Status: **FLAGGED** - Missing green coloration

**⚠️ Other Surfaces - PARTIALLY INCORRECT**
- Expected: White
- Detected: Predominantly tan/beige and brown tones
- Multicolored accents present (purple, yellow, green, red small elements — appear to be decorative objects/items on a surface)
- Status: **FLAGGED** - Not white

## Summary
| Region | Expected | Actual | Status |
|--------|----------|--------|--------|
| Left wall | Red | Cream/tan | ❌ Fail |
| Right wall | Green | Beige | ❌ Fail |
| Other | White | Brown/tan | ❌ Fail |

**Overall:** The image does not match the specified color scheme. It appears to depict a tan/brown structure (possibly a table or shelf with items) rather than the red/green/white configuration described. All major color regions are flagged as incorrect.

## C0 Probe Directional Atlas

I'll analyze the image based on your description of probe tiles and baking diagnostics.

**Observations:**

- **Tile structure:** The image shows a clear grid of D×D tiles arranged in rows and columns, consistent with a probe atlas layout (each probe contributing one tile).
- **Color content:** Tiles appear predominantly **near-white/very light** with faint patterned detail, not uniform mid-gray and not high-frequency random noise.
- **Smoothness across neighbors:** Adjacent tiles look visually similar — no abrupt discontinuities or scrambled blocks that would indicate a merge/stitching error.
- **Borders:** The outer rows/columns show denser tick-like patterning, suggesting boundary or edge probes with different hit distributions.

**Diagnostic flags:**

- ❌ **No uniform gray tiles** detected → probes appear to be **baked**.
- ❌ **No random noise blocks** detected → **no merge errors** evident.
- ✅ Neighboring tiles v

## C1 Probe Directional Atlas

I can

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

You're looking at a visualization where each probe displays its **average incoming radiance** (irradiance / 4π, or similar). Here's how to interpret and debug it:

## ✅ What a Healthy Result Looks Like

- **Smooth spatial gradients** between neighboring probes
- Probes near windows/lights are **brighter and tinted** toward the light's color
- Probes in corners, under furniture, or in occluded regions are **darker**
- Color temperature shifts gradually (e.g., warm near tungsten lamps, cool near sky-lit windows)
- Floor-level vs. ceiling-level probes show subtle differences (bounce vs. direct)

## 🚩 Failure Modes & Likely Causes

### 1. All-Black Probes
| Possible Cause | Check |
|---|---|
| Probes never received rays / not dispatched | Verify probe update kernel ran; check probe count > 0 |
| Ray payload not writing radiance | Inspect closest-hit / miss shaders |
| Accumulation buffer cleared every frame without integration | Check temporal blend factor (`hysteresis`) |
| Probes inside geometry (backface hits) | Enable backface detection; relocate probes |
| Sky/miss shader returns 0 | Verify environment lighting feeds the miss shader |
| Wrong texture binding / sampler | Check UAV/SRV bindings, format (e.g., R11G11B10F vs R8) |

### 2. Sharp Grid Steps (Blocky Look)
| Possible Cause | Check |
|---|---|
| No interpolation between probes when sampling | Use trilinear weights across the 8 surrounding probes |
| Visualization shows raw probe values (expected!) | This is fine for the *debug view* — only a problem in final lighting |
| Probe spacing too large for scene scale | Reduce grid spacing or add cascades |
| Visibility/chebyshev weights collapsing to 1 probe | Inspect depth probe variance; tune `depth_sharpness` |
| Leaking mitigations too aggressive | Lower the visibility bias threshold |

### 3. Wrong Colors
| Possible Cause | Check |
|---|---|
| Color space mismatch (linear vs sRGB) | Ensure probe stores linear; convert only at display |
| Channel swizzle (RGB ↔ BGR) | Check texture format and shader swizzles |
| Tone mapping applied twice / not at all | Apply once, at final composite |
| HDR values clipped to LDR storage | Use float format (R11G11B10F or RGBA16F) |
| Wrong octahedral decode | Verify oct mapping matches encode side |
| Gamma applied to irradiance directly | Irradiance should remain linear until display |

## 🔧 Quick Sanity Checks

```text
1. Force miss shader to return constant white → all probes should glow uniformly white.
2. Disable hysteresis (blend =

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Highly visible across the ceiling and upper walls as horizontal streaks/dashes of brown pixels. The regular grid spacing of the radiance probes is clearly visible as discrete horizontal bands of color samples that fail to interpolate into a smooth field.

2. **Directional bin banding** — The floor shows angular streaks of bright/white samples arranged in arcs and lines (especially mid-floor and lower-center). These are the discrete directional bins of the cascade leaking through as hard angular steps rather than a smooth radiance distribution.

3. **Outer-wall drift** — The left wall shows monotonic brightening toward the camera (washed-out near-white) while the right wall darkens into heavy brown. The scene's symmetry is broken by a gradient that suggests cascade falloff bias rather than true lighting.

4. **Color bleeding errors** — The right wall and ceiling corners are saturated with strong reddish-brown tones that appear to be bleeding too aggressively/asymmetrically; the left side shows almost no corresponding bleed, suggesting incorrect directional weighting.

5. **Cascade boundary seam** — A subtle ring/discontinuity is visible in the mid-distance where the ceiling transitions from heavily speckled to smoother — consistent with a cascade level boundary.

6. **Heavy blur / under-resolved upper cascade** — The overall image is extremely soft, indicating the upper cascades are being upsampled too aggressively over a sparse probe grid.

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts: probe interpolation is failing (visible grid), directional resolution is too coarse (angular streaks on floor), and the cascade merging is biased asymmetrically (wall drift). The Cornell Box's expected clean diffuse GI is not being reconstructed.
