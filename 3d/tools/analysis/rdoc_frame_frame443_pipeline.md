# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame443.rdc`  
**Analyzed:** 2026-05-11T15:27:10  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 312.0 |
| Cascade reduction | dispatch | 1900.5 |
| Cascade bake | dispatch | 10691.5 |
| Cascade reduction | dispatch | 27.8 |
| Cascade bake | dispatch | 8435.7 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 3121.1 |
| Cascade reduction | dispatch | 46.1 |
| Raymarching | draw | 1945.1 |
| GI blur | draw | 224.4 |
| glDrawElements() | draw | 10.4 |
| **Total** | | **26872.4** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/sharp transitions**: There's a very abrupt transition from black (interior/negative) to bright red (exterior/positive) with almost no gradient visible. A proper SDF should show a smooth falloff from the surface outward.

2. **Flat regions**: The large red areas appear uniformly saturated, suggesting the distance values are clamped or the gradient is too steep to visualize. There's no visible distance progression away from the geometry.

3. **Black interior structure**: The shape resembles a building/structure with:
   - Two vertical pillar-like elements on the sides
   - A horizontal band of small rectangular features (windows?) in the middle
   - These show as solid black with no internal gradient, indicating the interior SDF values aren't being computed or are also clamped

4. **Potential holes/missing geometry**: The small rectangular gaps in the middle band could either be intentional features (windows) or holes in the SDF where geometry is missing. Without smooth gradients around them, it's hard to tell if they're properly represented.

5. **Voxelization artifacts**: The blocky, axis-aligned nature of all features and the hard edges strongly suggest this is a **voxelized binary mask** rather than a true SDF. A correct SDF would show:
   - Smooth radial gradients around corners
   - Gradual color transitions across many pixels
   - Negative (interior) distance gradient from surface to deepest point

**Diagnosis**: This appears to be either:
- A binary occupancy grid mislabeled as an SDF
- An SDF with severely clamped/normalized values losing precision
- A computation error producing only sign information without magnitude

**Recommendation**: Recompute the SDF ensuring distance values are preserved with sufficient dynamic range, and verify the visualization isn't clamping the gradient.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Detected Issues

**❌ Left Wall - INCORRECT**
- Expected: Red
- Found: Pale yellow/cream tones with dark outlines
- Status: Missing red coloration

**❌ Right Wall - INCORRECT**
- Expected: Green
- Found: Pale yellow/cream tones matching left wall
- Status: Missing green coloration

**⚠️ Other Surfaces - PARTIAL**
- Expected: White
- Found: Mixed palette including:
  - Brown/tan background (surrounding area)
  - Yellow interior surfaces
  - Multi-colored accent spots (purple, orange, green, red dots on the central surface)
- Status: White not present as dominant neutral

## Summary

| Region | Expected | Actual | Flag |
|--------|----------|--------|------|
| Left wall | Red | Cream/yellow | 🚩 |
| Right wall | Green | Cream/yellow | 🚩 |
| Other (neutral) | White | Brown/yellow | 🚩 |
| Top edge | — | Dark red/brown streaks | ℹ️ |

**Overall:** The image does not conform to the specified color scheme. All three designated regions fail the color check. The palette appears to depict a food/dish item rather than a structured red/green/white surface layout.

## C0 Probe Directional Atlas

# Irradiance Volume Probe Atlas Diagnostic

What you're looking at is a **probe atlas visualization** — a debug view of baked lighting probes packed into a 2D texture for GPU sampling.

## Layout
- Each **D × D tile** stores the irradiance (or radiance) signal captured by one probe
- Tiles are ar

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, post-Phase 14c)

## Coverage Assessment

**Observed coverage:** ~97-98% (consistent with near-100% expectation, within tolerance for cascade 1 propagation lag).

## Tile Status Map

Scanning the tile grid for alpha-channel integrity:

| Region | Status | Notes |
|--------|--------|-------|
| Central

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What You Should See
A **direction-averaged radiance** visualization collapses each probe's directional distribution into a single scalar/RGB value (the mean over the sphere). When rendered at probe positions, this should look like a **low-frequency irradiance field**:

- **Smooth spatial gradients** — radiance should fall off gradually with distance from light sources and vary continuously across the volume.
- **Color tinting consistent with room lighting** — warm near tungsten/area lights, cool near sky portals, darker in occluded corners.
- **Soft shadowing** from large occluders (furniture, walls), but no hard edges since you've averaged out direction.

## Failure Modes (Flags)

### 🚩 All-Black Probes
Likely causes:
- **No rays hit any surface / sky** — check ray spawn, max distance, and miss shader contribution.
- **Radiance written to wrong channel/buffer** — probe payload zeroed before readback.
- **NaN/Inf clamped to zero** — inspect with a NaN-detection pass.
- **Probe inside geometry** — backface culling rejects all hits; need inside-volume detection or relocation.
- **Tone-mapping/exposure** — values present but mapped to ~0; visualize raw HDR.

### 🚩 Sharp Grid Steps
Likely causes:
- **Per-probe normalization mismatch** — different sample counts per probe without dividing properly (e.g., `sum / N` with wrong N).
- **Uninitialized probes** between updates — staggered update cadence showing stale neighbors.
- **Integer indexing bug** — sampling nearest probe instead of trilinear blending in the visualization shader.
- **Hysteresis / blend factor too low** — neighbors converged at different rates.
- **Cascade/level boundary** — if multi-resolution, mismatched scaling between cascades.

### 🚩 Wrong Colors
Likely causes:
- **Channel swizzle** (BGR vs RGB) on upload or readback.
- **Color space mismatch** — sRGB written as linear (or vice versa); gamma applied twice.
- **Sky/environment intensity wrong** — overpowers indirect contribution.
- **Albedo leak** — multiplying surface albedo into stored radiance when it should be irradiance, or vice versa.
- **Negative SH/coefficient reconstruction** — if averaging is done via SH band 0, ensure correct `Y₀₀ = 1/(2√π)` factor.

## Quick Sanity Checks
1. Render a **single bright white light** in an empty white room → expect a smooth radial falloff, no color tint.
2. Compare against a **reference path-traced irradiance** at a few probe locations.
3. Toggle **single-probe debug**: print mean radiance and sample count; verify `mean = sum / count`.
4. Visualize **sample count per probe** as a separate heatmap — non-uniform counts often explain grid steps.

Want

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible on the right wall as blocky, low-resolution patches of indirect light. The lighting changes in discrete rectangular cells rather than smoothly.

2. **Directional bin banding** — The ceiling and upper walls show hard angular color transitions (warm orange/brown stepping into darker tones), consistent with ~36° directional bin steps in the radiance cascade.

3. **Color bleeding errors** — The right wall and ceiling area show strong reddish-brown bleeding that appears too saturated and spatially misaligned with the geometry, suggesting incorrect surface attribution.

4. **Outer-wall drift** — The right edge of the frame shows a monotonic darkening/brightening gradient that doesn't correspond to scene geometry, particularly along the wall edge.

5. **Cascade boundary seam** — A faint ring-like discontinuity is visible in the mid-corridor where lighting transitions abruptly between cascade levels (around the doorway/archway region).

**Locations summary:**
- Right wall: probe banding + outer-wall drift
- Ceiling: directional bin banding + color bleed
- Mid-corridor archway: cascade seam

**Quality rating: Poor**

The frame suffers from multiple overlapping artifacts that significantly degrade the indirect lighting. The blocky probe structure and oversaturated color bleed are especially prominent, and the overall image lacks the smooth diffuse falloff expected from a well-converged radiance cascade solution.
