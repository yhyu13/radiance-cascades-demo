# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame327.rdc`  
**Analyzed:** 2026-05-11T15:31:05  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 56956.9 |
| Cascade reduction | dispatch | 37.2 |
| Cascade bake | dispatch | 62642.6 |
| Cascade reduction | dispatch | 159.7 |
| Cascade bake | dispatch | 106616.9 |
| Cascade reduction | dispatch | 42099.3 |
| Cascade bake | dispatch | 146172.4 |
| Cascade reduction | dispatch | 75808.8 |
| Raymarching | draw | 1996.7 |
| GI blur | draw | 226.6 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **492727.3** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this signed distance field visualization, I can identify several **significant issues**:

## 🔴 Critical Problems

### 1. **Hard Seams / Flat Regions**
- The image shows large areas of **uniform red** with no gradient falloff
- A proper SDF should exhibit smooth color/intensity transitions radiating outward from surfaces
- The flat red regions indicate the distance values are either clamped, saturated, or incorrectly computed

### 2. **Sharp Discontinuities**
- Visible **hard black edges** around the geometry rather than smooth gradients
- The transition between the object (dark) and exterior (red) is abrupt
- This suggests **voxelization artifacts** — the SDF is behaving more like a binary occupancy grid than a true distance field

### 3. **Missing Geometry / Holes**
- The dark central structure shows **fragmented features** (the row of small rectangles/teeth in the middle)
- Gaps between elements appear as holes rather than connected interior distance
- The two vertical "pillars" on the left/right have unclear interior distance representation

### 4. **Incorrect Distance Encoding**
- No visible isocontours or banding that would indicate proper distance gradation
- Interior of the shape should show **negative distances** with their own gradient — instead it appears flat black
- Exterior should fade from 0 at the surface outward — instead jumps immediately to max red

## 🛠️ Likely Causes

| Issue | Probable Cause |
|-------|---------------|
| Flat red exterior | Distance values clamped too aggressively, or only nearest-voxel computed |
| Hard seams | Used unsigned occupancy instead of true Euclidean distance transform |
| Fragmented interior | Resolution too low for thin features; sub-voxel geometry lost |
| No gradient | Missing normalization or incorrect sign convention |

## ✅ Recommended Fixes

1. **Recompute using a proper EDT** (Euclidean Distance Transform), e.g., Felzenszwalb & Huttenlocher's algorithm
2. **Increase voxel resolution** to capture thin features (the small teeth-like elements)
3. **Verify sign convention** — interior should be negative, exterior positive (or vice versa, but consistent)
4. **Remove value clamping** during visualization, or use a wider color ramp
5. **Check for watertight input mesh** — holes in source geometry cause inside/outside test failures

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged:

**❌ Left Wall (should be RED)**
- Currently appears in **tan/beige and dark brown** tones
- No red coloration detected
- **FLAG: Missing red color**

**❌ Right Wall (should be GREEN)**
- Currently appears in **tan/beige and dark brown** tones
- No green coloration detected
- **FLAG: Missing green color**

**❌ Other Surfaces (should be WHITE)**
- Background/floor: **dark brown** (incorrect)
- Central platform/table: **yellow-tan** with multicolored accents (purple, green, orange spots)
- **FLAG: Missing white base color**

## Summary
| Region | Expected | Actual | Status |
|--------|----------|--------|--------|
| Left wall | Red | Tan/Brown | ❌ Incorrect |
| Right wall | Green | Tan/Brown | ❌ Incorrect |
| Other areas | White | Brown/Yellow/Multi | ❌ Incorrect |

**Overall: No regions match the specified color scheme.** The image appears to depict a pixel-art scene (possibly food/an offering on a table) with an earth-tone palette rather than the red/green/white scheme requested.

## C0 Probe Directional Atlas

I'll analyze this image based on the technical context you've provided about probe-based rendering systems.

## Observations:

**Structure:** The image shows a grid of small tiles (probes), each containing a D×D pattern. This appears to be a **probe atlas** visualization, likely for irradiance/radiance caching, DDGI (Dynamic Diffuse Global Illumination), or similar light probe systems.

**Diagnostic findings:**

1. **No uniform gray tiles visible** → Probes appear to be **baked/populated** (good ✓)

2. **No obvious random noise patterns** → No clear **merge errors** between probe updates (good ✓)

3. **Smoothness check:** The tiles appear to vary fairly smoothly across neighbors, suggesting spatial coherence is reasonable — adjacent probes are sampling similar regions of the scene.

4. **

## C1 Probe Directional Atlas

I can't act

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Validation Notes

This is a diagnostic visualization for a **light probe grid** (e.g., irradiance volumes, DDGI, or similar GI systems). Here's how to interpret and validate it:

## What You Should See ✅

- **Smooth spatial gradients** across the probe grid
- Probes near windows/lights → **brighter, warmer/cooler tints** matching source
- Probes in shadowed corners → **dimmer, ambient-tinted**
- Gradual falloff between lit and unlit regions
- Color tone consistent with scene's dominant bounce lighting

## Red Flags 🚩

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Probes failed to gather radiance — check ray dispatch, miss shader, or probe activation flags |
| **Sharp grid steps / blocky discontinuities** | No interpolation between probes, or probes sampling through walls (missing backface/visibility weighting, no chebyshev/depth test) |
| **Wrong colors** (e.g., magenta, NaN green, oversaturated) | Uninitialized memory, NaN/Inf in accumulation, wrong color space (linear vs sRGB), or incorrect octahedral decode |
| **Single-probe hotspots** | Insufficient ray count or no temporal blending (low hysteresis) |
| **Black probes inside geometry** | Expected if culled, but should be flagged/skipped during sampling |

## Quick Debug Checklist

1. **Verify ray hits**: log average hit distance per probe — all-zero means rays aren't tracing
2. **Check accumulation buffer**: ensure clear-to-zero only on init, not per frame
3. **Color space**: averaged radiance should be in **linear** space; only convert at display
4. **Octahedral mapping**: black seams at edges = missing border texel duplication
5. **Probe placement**: confirm world-space positions match expected grid bounds

If you can share the actual visualization (or describe what you're seeing), I can help narrow down which failure mode you're hitting.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the ceiling and upper walls as blocky, rectangular patches of varying brightness. The grid structure of the probe sampling is clearly exposed in the indirect lighting on the overhead surfaces.

2. **Cascade boundary seams** — Faint ring-like brightness discontinuities are visible in the mid-corridor region, where the lighting transitions between cascade levels rather than blending smoothly.

3. **Outer-wall drift** — The wall edges (particularly the right side and lower-right floor area) show a monotonic darkening toward the frame edges, with a muddy brown gradient that doesn't match expected uniform wall illumination.

4. **Directional bin banding** — Subtle angular color steps are visible on the floor near the light source, where the indirect bounce transitions appear stepped rather than smooth.

5. **Color bleeding errors** — The right wall and floor show an overly warm/orange tint bleeding into areas that should be more neutral, suggesting incorrect directional weighting of bounced light.

**Quality Rating: Poor**

The frame suffers from multiple compounding artifacts. The probe-grid banding on the ceiling is especially severe and immediately breaks immersion, while the overall image has a noisy, smeared quality that obscures scene geometry. Despite a reasonable 48 FPS, the visual fidelity needs significant improvement — likely requiring denser probe placement, better cascade interpolation, and improved directional bin resolution.
