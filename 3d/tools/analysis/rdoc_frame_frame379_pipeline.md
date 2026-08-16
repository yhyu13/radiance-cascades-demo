# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame379.rdc`  
**Analyzed:** 2026-05-11T15:42:49  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 5499.2 |
| Cascade reduction | dispatch | 28.5 |
| Cascade bake | dispatch | 16048.4 |
| Cascade reduction | dispatch | 155.6 |
| Cascade bake | dispatch | 24363.1 |
| Cascade reduction | dispatch | 169.0 |
| Cascade bake | dispatch | 8077.6 |
| Cascade reduction | dispatch | 227.2 |
| Raymarching | draw | 18587.1 |
| GI blur | draw | 1005.4 |
| glDrawElements() | draw | 11.3 |
| **Total** | | **74172.4** |


## SDF Volume (Signed Distance Field)

Looking at this image, which appears to be a signed distance field (SDF) visualization:

**Issues observed:**

1. **Hard seams / sharp transitions**: There's a very abrupt boundary between the bright red exterior and the dark interior region. A proper SDF should show smooth gradient falloff, but here the transition appears almost binary/stepped.

2. **Flat regions**: The outer red area appears largely uniform in color rather than showing a smooth gradient that increases with distance from the surface. This suggests either:
   - Saturation/clamping of distance values
   - Loss of precision in far-field distances
   - Possible voxelization artifacts

3. **Interior structure problems**: The dark interior shows what appears to be geometric detail (possibly a fence, gate, or grid pattern), but:
   - The shapes appear blocky and pixelated, indicating low-resolution voxelization
   - There's no visible smooth gradient inside the shape (interior SDF values should also vary smoothly toward the medial axis)
   - Hard black regions suggest clamped negative distances

4. **Missing geometry indicators**: The thin horizontal lines in the middle appear broken/discontinuous, which could indicate:
   - Sub-voxel features being lost
   - Holes where thin geometry falls between sample points

5. **Aliasing on edges**: The vertical pillar-like structures show stair-stepping, confirming voxelization at insufficient resolution.

**Recommendations:**
- Increase SDF resolution to capture thin features
- Use higher precision (float32 minimum) to avoid distance clamping
- Verify the SDF computation handles both interior (negative) and exterior (positive) distances with smooth gradients
- Check for proper narrow-band vs. full-field computation

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color requirements:

**Expected:**
- Left wall: Red
- Right wall: Green
- Other surfaces: White

**Findings — Issues Flagged:**

🚩 **Left wall**: INCORRECT
- Appears in pale yellow/cream tones rather than red
- No red pigmentation detected on the left-facing surface

🚩 **Right wall**: INCORRECT / MISSING
- Shows white/light beige coloring instead of green
- Green color region is absent

🚩 **Other surfaces (floor/back)**: PARTIALLY INCORRECT
- Floor shows brown/tan tones instead of white
- Interior surfaces contain mixed colors (purple, orange, green accents from objects/items inside)

**Summary:** All three required color zones fail the specification. The image appears to show a structure (possibly a container or small building) rendered primarily in warm neutral tones (cream, tan, brown) against a brown background, with no red or green wall surfaces present as required.

**Recommendation:** Recolor left wall to red (#FF0000 or similar), right wall to green (#00FF00 or similar), and normalize remaining surfaces to white (#FFFFFF).

## C0 Probe Directional Atlas

I can't actually see fine detail in that thumbnail, but based on your description, here's how to interpret what you're looking at:

## Reading the Atlas

**Layout**
- Each probe contributes a **D×D tile** (octahedral or cube-unwrapped irradiance/radiance).
- The full texture is a grid of these tiles, one per probe in the volume.

## Diagnostic Flags

| Symptom | Meaning | Likely Cause |
|---|---|---|
| **Uniform gray tile** | Probe never baked / has no samples | Probe culled, occluded inside geometry, or skipped by the bake job |
| **Random noise tile** | Merge / accumulation error | Atomic race, wrong tile stride, or unnormalized accumulation buffer |
| **Hard seams between neighbors** | Spatial discontinuity | Probes failing to interpolate — check world-space placement or hash |
| **Sm

## C1 Probe Directional Atlas

# C

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅

A **direction-averaged radiance** visualization (one color value per probe, typically the DC/L₀ spherical harmonic term or mean of the cube map faces) should display:

- **Smooth spatial gradients** across the probe grid
- **Brighter probes** near light sources, windows, or emissive surfaces
- **Darker probes** in occluded areas (under furniture, in corners, behind walls)
- **Color tinting** consistent with bounce lighting (e.g., warm near incandescent lamps, cool near skylights, colored bounces from saturated walls)
- **Gentle falloff** with distance from emitters

## Red Flags 🚩

### 1. All-Black Probes
**Symptoms:** Entire probe grid is black or near-zero.

**Likely causes:**
- Probe capture pass not running / not dispatched
- Cube map render targets not cleared or not bound
- Sky/emissive contribution missing during capture
- Exposure or tonemapping clamping HDR values to zero
- Reading from wrong mip / wrong texture slice
- SH coefficients written to wrong buffer offset

### 2. Sharp Grid Steps
**Symptoms:** Visible blocky discontinuities between adjacent probes; no interpolation.

**Likely causes:**
- Trilinear interpolation between probes disabled or broken
- Sampling **nearest probe only** instead of 8-probe blend
- Probe index computed with `floor` but weights not applied
- Probe grid origin/spacing mismatch between capture and sampling
- Visibility/occlusion weights collapsing to a single probe

### 3. Wrong Colors
**Symptoms:** Probes show unexpected hues (e.g., green where it should be neutral, magenta in lit areas).

**Likely causes:**
- **Channel swizzle** (RGB↔BGR) on capture or readback
- Color space mismatch (linear vs. sRGB double-conversion)
- SH band sign/normalization error leaking higher bands into DC
- Negative SH coefficients not clamped (producing invalid colors)
- Wrong cube face orientation → sampling sky where floor should be
- HDR format precision loss (R11G11B10F dropping blue)

## Quick Debug Checklist

| Check | Action |
|---|---|
| Capture actually runs | GPU capture / RenderDoc on probe pass |
| Values non-zero in buffer | Readback first probe's L₀ coefficient |
| Interpolation active | Move camera between probes — should blend |
| Color space | Confirm linear throughout, sRGB only at final output |
| SH reconstruction | `irradiance = max(0, L₀ * Y₀₀)` for DC-only test |
| Grid alignment | Overlay probe positions on world geometry |

## Sanity Test
Place a single bright white point light in an empty room. The DC visualization should produce a **smooth radial falloff** centered on the light

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Probe-grid banding** — Visible across the right wall as soft blocky patches of varying brown intensity, and on the ceiling where the indirect lighting forms irregular grid-aligned blobs rather than a smooth gradient.

2. **Directional bin banding** — The right wall shows distinct angular shifts in the brown coloration (notably the darker arc sweeping across the upper-right), consistent with discrete directional bins quantizing the indirect radiance.

3. **Color bleeding errors** — Strong red/brown bleed appears on the right wall and ceiling without a clear corresponding red surface in view; the bleed extends much further than physically plausible, dominating the right half of the frame. The left wall and floor remain neutral, suggesting asymmetric/incorrect bleed propagation.

4. **Outer-wall drift** — The right wall darkens monotonically toward the camera (lower-right corner is significantly darker than mid-wall), and the left wall brightens toward the foreground edge — characteristic edge drift rather than true shading.

5. **Cascade boundary seams** — A faint ring-like transition is visible mid-depth on the right wall where the brown tone steps between cascade levels.

**No significant:** shadow acne, missing shadows (the floor light streaks read correctly).

## Quality Rating: **Poor**

The frame is dominated by smeared, low-resolution indirect lighting with overwhelming asymmetric color bleed, visible directional quantization, and edge drift. Geometry and direct light streaks resolve, but the global illumination solution is unstable and physically implausible.
