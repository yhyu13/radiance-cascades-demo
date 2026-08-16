# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame425.rdc`  
**Analyzed:** 2026-05-11T15:29:38  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 4702.2 |
| Cascade reduction | dispatch | 26.8 |
| Cascade bake | dispatch | 46243.1 |
| Cascade reduction | dispatch | 159.6 |
| Cascade bake | dispatch | 16437.2 |
| Cascade reduction | dispatch | 160.1 |
| Cascade bake | dispatch | 55843.8 |
| Cascade reduction | dispatch | 227.7 |
| Raymarching | draw | 1976.1 |
| GI blur | draw | 229.4 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **126016.2** |


## SDF Volume (Signed Distance Field)

Looking at this image as an SDF (Signed Distance Field) visualization, I can identify several issues:

## Problems Detected

**1. Hard Seams / Sharp Discontinuities**
- There's an abrupt transition between the bright red exterior and the dark interior region with no smooth gradient falloff
- A proper SDF should show gradual intensity changes radiating from surfaces

**2. Flat/Uniform Regions**
- Large areas of constant red color on the outside indicate clamped or saturated distance values
- The interior dark region also appears mostly uniform black, suggesting distance values aren't being computed continuously
- This is a classic sign of **voxelization artifacts** or insufficient SDF resolution

**3. Missing Geometry / Holes**
- The bracket-like shapes "[" and "]" on the left and right appear disconnected from the central structure
- Gaps visible between the vertical pillars and horizontal elements suggest broken or non-manifold geometry
- The central horizontal bar (resembling teeth/grating) shows pixelated edges rather than smooth distance contours

**4. Incorrect Distance Encoding**
- The binary-looking appearance (mostly pure red vs pure black) suggests the SDF may have been thresholded rather than preserving true signed distances
- No visible isocontours or gradient bands that would indicate proper distance field computation

**5. Aliasing on Internal Features**
- The horizontal "teeth" pattern in the center shows blocky, stair-stepped edges
- Indicates the SDF resolution is too low for the feature size, or nearest-neighbor sampling was used

## Recommendations
- Increase SDF grid resolution
- Verify the source mesh is watertight/manifold
- Use trilinear interpolation when sampling
- Ensure distance values are stored as floats, not quantized
- Check that the narrow band around surfaces is wide enough to capture gradients

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged:

**🚩 Left Wall (Expected: RED)**
- **Status: INCORRECT/MISSING**
- Observed: Pale yellow/cream and white tones
- No red coloration detected on the left vertical surface

**🚩 Right Wall (Expected: GREEN)**
- **Status: INCORRECT/MISSING**
- Observed: Light beige/white tones
- No green coloration detected on the right vertical surface

**🚩 Other Surfaces (Expected: WHITE)**
- **Status: PARTIALLY INCORRECT**
- Floor/background: Brown (should be white)
- Ceiling area: Tan/beige with darker streaks (should be white)
- Interior contains scattered multi-colored elements (purple, yellow, green, orange dots) — these may be intentional objects rather than surface colors

## Summary
| Region | Expected | Actual | Flag |
|--------|----------|--------|------|
| Left wall | Red | Cream/white | ❌ |
| Right wall | Green | Beige/white | ❌ |
| Floor | White | Brown | ❌ |
| Ceiling | White | Tan | ❌ |
| Back wall | White | Off-white | ⚠️ Close |

**Recommendation:** The scene does not match the specified color configuration. The red and green accent walls are entirely absent, and the surrounding surfaces show brown/tan tones instead of white.

## C0 Probe Directional Atlas

# Image Analysis

This appears to be a **probe atlas/lightmap visualization** — likely an irradiance probe texture or similar baked lighting data structure used in real-time rendering.

## What I observe:

**Layout:**
- A rectangular atlas containing a grid of small DxD tiles
- Dense central region of tiles surrounded by border rows/columns
- Strong structural regularity suggesting a packed probe array

**Per-tile content:**
- Tiles show fine geometric/diamond-like patterns rather than uniform fills
- Variation between neighbors appears smooth and structured
- No obvious solid gray blocks or random salt-and-pepper noise zones

## Diagnostic check against your flags:

| Flag condition | Status |
|---|---|
| **Uniform gray tiles** (probe not baked) | ✅ Not visible — all tiles show detail |
| **Random noise tiles** (merge error) | ✅ Not visible — patterns are coherent, not stochastic |
| **Smooth neighbor variation** | ✅ Tiles transition gradually across the atlas |

## Bin alpha interpretation:

Given your legend:
- **Positive α (surface hit):** the bright/patterned interior bins — bulk of the atlas
- **Negative α (sky):** would appear as distinct sentinel-valued bins (hard to isolate at this resolution, but the lighter border ticks may

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 14c)

## Observation Summary

Examining the provided tile mosaic for

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✓

A grid of probes where each probe displays a **single averaged color/intensity** representing the mean incoming radiance over all directions (the DC / 0th-order spherical harmonic term).

- **Smooth spatial gradients** between neighboring probes
- Brighter probes near light sources, windows, or emissive surfaces
- Darker probes in occluded corners, under furniture, behind walls
- Color tints matching local lighting (warm near incandescent, cool near sky light)
- Gradual falloff with distance from emitters

## Red Flags 🚩

### 1. All-Black Probes
**Possible causes:**
- Probe rendering/relighting pass never executed
- Rays not hitting any emissive geometry (missing emissive materials, miss shader returns 0)
- Accumulation buffer cleared but never written
- Probes placed inside solid geometry (all rays self-intersect)
- Wrong sampling: NaN/Inf values clamped to 0
- Exposure/tonemapping crushing values
- Sky/environment map not bound

### 2. Sharp Grid Steps (Discontinuities)
**Possible causes:**
- **Leaking through walls** — probes on opposite sides of geometry averaged together; needs visibility weighting / depth-aware filtering
- Per-probe sample budget too low → high variance, not actually a gradient artifact
- Probes "stuck" inside walls being mixed with valid probes
- Missing or broken probe-relocation / classification step
- Indexing bug: probe coordinates flipped or off-by-one between bake and display
- Each probe using a different RNG seed without temporal accumulation

### 3. Wrong Colors
**Possible causes:**
- **Color space mismatch** (linear vs sRGB applied twice or not at all)
- Channel swizzle (BGR ↔ RGB)
- HDR values not tonemapped, or tonemapped twice
- Texture format mismatch (e.g., RG11B10F losing blue precision)
- Indirect-only vs direct+indirect confusion
- Wrong sky/environment intensity or tint
- Negative SH coefficients clamped incorrectly

## Quick Debug Checklist

| Check | How |
|---|---|
| Probes receiving rays | Visualize ray count or hit-distance buffer |
| Geometry visibility | Render probe positions as spheres in scene |
| Value range | Print min/max/mean of probe buffer |
| Single probe sanity | Force one probe near a known light, verify color |
| Leaking | Toggle visibility weighting on/off |
| Color pipeline | Bypass tonemap; sample raw buffer in a debugger |

## Suggested Visualization

Render each probe as a small sphere colored by its mean radiance, with optional log-scale exposure slider. Add a toggle to overlay probe **validity** (classified as inside-geometry / backface / valid) so misplaced probes are obvious.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Probe-grid banding** — Visible across the ceiling and upper walls as regular blocky/grid-shaped patterns in the indirect lighting. The ceiling shows clear rectangular tile-like discontinuities.

2. **Cascade boundary seams** — Apparent in the mid-corridor depth where lighting transitions show ring/band-like discontinuities along the wall surfaces.

3. **Color bleeding errors** — The right side of the frame shows strong reddish/brown bleeding onto surfaces that appear to be receiving incorrect color contribution (the large blurry red mass on the right wall/foreground looks like misprojected indirect light rather than a real surface).

4. **Outer-wall drift** — The wall edges show monotonic darkening toward the corners and a brightening trend along the far end of the corridor that doesn't match expected falloff.

5. **Directional bin banding** — Subtle angular color steps visible on the ceiling near the light source, where indirect contributions form hard angular transitions.

**Quality rating: Poor**

The frame suffers from multiple overlapping artifacts. The scene geometry is barely readable due to heavy probe-grid structure, aggressive color bleeding, and what appears to be either severe motion blur or undersampled radiance interpolation creating the smeared red region on the right.
