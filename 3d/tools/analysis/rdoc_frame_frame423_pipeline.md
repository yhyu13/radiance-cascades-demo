# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame423.rdc`  
**Analyzed:** 2026-05-11T15:20:03  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 5791.8 |
| Cascade reduction | dispatch | 34.4 |
| Cascade bake | dispatch | 13670.4 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 16085.4 |
| Cascade reduction | dispatch | 159.7 |
| Cascade bake | dispatch | 12209.0 |
| Cascade reduction | dispatch | 222.6 |
| Raymarching | draw | 4594.2 |
| GI blur | draw | 684.4 |
| glDrawElements() | draw | 10.3 |
| **Total** | | **53620.1** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/sharp transitions**: The image shows very abrupt transitions between black (interior/negative) and red (exterior/positive) regions. A proper SDF should display a smooth gradient from the surface outward, but here the red appears nearly uniform in intensity across most of the exterior.

2. **Flat/saturated regions**: The bright red background lacks gradient variation. This suggests either:
   - The SDF values are clamped/saturated beyond a narrow band
   - Only a narrow-band SDF was computed (values outside the band defaulted to a max value)
   - The visualization range is too compressed

3. **Voxelization artifacts**: The geometry edges appear blocky and pixelated, particularly visible in:
   - The rectangular "tooth-like" structures at the bottom
   - The two vertical pillar shapes
   - The top horizontal bar
   
   These hard, axis-aligned edges suggest the source was voxelized at low resolution before SDF computation.

4. **Potential holes/missing geometry**: 
   - The gaps between the bottom "teeth" appear as sharp red intrusions with no smooth falloff
   - The interior black regions show no gradient toward the surface (interior SDF values appear missing or not computed)

5. **Missing interior gradient**: A proper SDF should show darkening gradient *inside* the shape approaching the medial axis. The interior is uniformly black, suggesting **only exterior distances were computed** (this is an unsigned distance field, or the interior was not processed).

**Recommendations:**
- Recompute the SDF with full interior/exterior signed values
- Increase voxel resolution before SDF generation to reduce blockiness
- Expand the visualization value range to reveal the full gradient
- Verify the narrow-band width if using a truncated SDF

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings — Multiple Incorrect/Missing Regions Flagged

**🚩 Left Wall - FAIL**
- Expected: Red
- Observed: Pale yellow/cream tones with no red pigment detected
- Status: Missing required red coloration

**🚩 Right Wall - FAIL**
- Expected: Green
- Observed: Light beige/off-white, no green channel dominance
- Status: Missing required green coloration

**🚩 General Surface - PARTIAL**
- Expected: White (non-wall areas)
- Observed: Mostly cream/yellow interior with scattered multi-colored elements (purple, orange, green, red spots — appears to be decorative/food items)
- Status: Not white; contaminated with various hues

**🚩 Surrounding Frame - FAIL**
- Observed: Brown/dark tan border
- Status: Not part of specified palette (should be white)

## Summary
| Region | Expected | Actual | Flag |
|--------|----------|--------|------|
| Left wall | Red | Cream | ❌ |
| Right wall | Green | Beige | ❌ |
| Interior | White | Yellow + mixed | ❌ |
| Border | White | Brown | ❌ |

**Overall: Color scheme does not match specification.** The image appears to depict an object (possibly a decorated tray or pixel-art food item) rather than a room with colored walls. Recommend recapturing or reconfirming the target reference.

## C0 Probe Directional Atlas

# Image Analysis

This appears to be a **probe atlas / lightmap debug visualization**, likely from a real-time global illumination system (similar to DDGI — Dynamic Diffuse Global Illumination) or a baked irradiance probe grid.

## What I observe:

- **Grid of tiles**: The image shows a regular array of small D×D tiles, each representing one probe's stored data (irradiance, visibility, or depth octahedral encoding).
- **Symmetric, repeating patterns**: Most tiles show fine cross-hatch / diamond patterns, suggesting **octahedral-

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analys

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

You're describing a **probe irradiance visualization** (typically the DC/L0 spherical harmonic term, or a uniform directional average). Here's what to validate:

## ✅ Expected Appearance
- **Smooth spatial gradients** between adjacent probes
- Brightness falls off with distance from light sources
- Color tint matches local lighting (warm near incandescent, cool near windows/sky)
- Probes in shadow regions are dim but **non-zero** (bounce light)
- Continuous transitions across the probe grid

## 🚩 Failure Modes & Causes

### All-Black Probes
| Cause | Check |
|---|---|
| Probes never rendered | Verify capture pass executed; check render target bindings |
| Cleared but not accumulated | Inspect SH/irradiance buffer write |
| Sky/emissive missing from capture | Confirm capture shader includes all light sources |
| Wrong buffer read | Index/stride mismatch in visualization shader |
| HDR values clipped to 0 | Check for NaN/Inf → clamped to black |
| Probes inside geometry | Backface hits returning 0; need fallback or relocation |

### Sharp Grid Steps (Discontinuities)
| Cause | Check |
|---|---|
| No trilinear interpolation | Visualization showing per-probe constant instead of interpolated field |
| Probe leaking through walls | Some probes capture wrong room → neighbors disagree sharply |
| Mismatched exposure/tonemap | Per-probe normalization differs |
| Wrong probe indexing | Off-by-one in grid → shuffled values |
| Stale probes | Partial update; some probes from previous frame/state |

### Wrong Colors
| Cause | Check |
|---|---|
| Color space mismatch | Linear vs sRGB on read or write |
| Channel swizzle | RGB↔BGR in storage format |
| SH reconstruction error | Negative lobes producing magenta/green; check coefficient signs |
| Tonemap applied twice | Or not applied where expected |
| Wrong primaries | Rec.709 vs Rec.2020 confusion |

## 🔧 Quick Debug Steps
1. **Render a single bright probe** — confirm capture works in isolation
2. **Disable interpolation** — view raw per-probe values as flat spheres
3. **Output luminance only** — isolates color vs intensity issues
4. **Compare to ground truth** — bake one probe with a path tracer
5. **Log min/max/mean** of probe buffer per frame

Want me to focus on a specific engine (Unity, Unreal, custom DDGI, etc.) or a particular failure mode?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible as blocky, pixelated patches across the ceiling and upper walls, particularly the dark brown/red rectangular blocks scattered along the ceiling. The indirect light shows clear grid-aligned discontinuities.

2. **Directional bin banding** — The right wall shows hard angular color steps where the brown/red bleed transitions in distinct angular zones rather than smoothly. Large soft-edged but discretized lobes are visible.

3. **Color bleeding errors** — Excessive red/brown spillage onto the ceiling and right wall in patchy, non-physical distributions. The ceiling near the back has dark red blocks that don't correspond to plausible light transport from the visible red wall.

4. **Cascade boundary seams** — Faint ring-like brightness transition visible in the mid-distance along the floor and where the blurred near-field meets the sharper far-field rendering.

5. **Outer-wall drift** — The left wall darkens monotonically toward the bottom-left corner, and the right wall shows progressive brightening toward the viewer that doesn't match the lighting geometry.

6. **Shadow acne** — Small bright speckles along the floor's specular highlights near center-frame, though some of this may be intentional caustics.

**Quality Rating: Poor**

The frame suffers from multiple compounding artifacts. The probe grid is highly visible, directional binning is undersampled, and color bleeding is being splattered in grid-aligned chunks rather than smoothly diffused. The overall image reads more as a debug visualization than a finished render.
