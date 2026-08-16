# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame369.rdc`  
**Analyzed:** 2026-05-11T15:44:10  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 13223.7 |
| Cascade reduction | dispatch | 28.7 |
| Cascade bake | dispatch | 10694.3 |
| Cascade reduction | dispatch | 157.1 |
| Cascade bake | dispatch | 14960.6 |
| Cascade reduction | dispatch | 168.8 |
| Cascade bake | dispatch | 16082.9 |
| Cascade reduction | dispatch | 227.7 |
| Raymarching | draw | 9354.0 |
| GI blur | draw | 8415.7 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **73323.7** |


## SDF Volume (Signed Distance Field)

Looking at this image as an SDF (Signed Distance Field) visualization, I can identify several issues:

## Problems Detected

**1. Hard Seams / Sharp Discontinuities**
- There are abrupt transitions between the bright red exterior and the dark interior regions, with no smooth gradient falloff
- A proper SDF should show gradual intensity changes representing distance from the surface

**2. Flat Regions (Quantization Artifacts)**
- The large uniform red border shows no distance variation — it appears clamped or saturated
- The black interior regions are similarly flat, suggesting the distance values are either truncated or the field wasn't computed beyond a narrow band

**3. Voxelization Artifacts**
- The geometry shows clear stair-stepping and blocky pixel patterns, especially in the central detailed area
- Vertical "pillar" structures on left and right have hard rectangular edges instead of smooth distance contours

**4. Possible Holes / Missing Geometry**
- The central horizontal band of small rectangles appears fragmented — these could be intended features or holes in the SDF where distance computation failed
- Disconnected dark regions inside the bright field suggest inside/outside classification errors

**5. Sign Errors**
- The interior dark regions surrounded by bright red suggest the inside/outside test may be inverted in localized areas, or there are unintended cavities

## Recommendations
- Recompute the SDF using a method that produces continuous distance values (e.g., fast marching or sweeping methods) rather than binary inside/outside
- Increase voxel resolution to reduce stair-stepping
- Verify mesh is watertight before SDF generation to avoid sign-flip errors
- Use a wider narrow-band or full-field computation to eliminate the flat saturated regions
- Check normal orientation consistency on source geometry

## Albedo Volume

# Surface Color Inspection

Analyzing the pixel-art structure for wall color compliance:

## Expected Color Scheme
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings

### ❌ Left Wall — FAIL
- **Detected:** Beige/tan tones (appears as a warm off-white/yellowish hue)
- **Expected:** Red
- **Status:** Incorrect color region

### ❌ Right Wall — FAIL
- **Detected:** Beige/tan tones (matching the left wall, not green)
- **Expected:** Green
- **Status:** Incorrect color region

### ⚠️ Floor / Base Surface
- **Detected:** Light beige/cream with scattered colored objects (purple, green, orange, yellow specks — likely decorative items)
- **Expected:** White
- **Status:** Off-white; borderline non-compliant

### ⚠️ Ceiling / Top Edge
- **Detected:** Irregular tan/beige silhouette
- **Expected:** White
- **Status:** Non-compliant

### Background (outside structure)
- **Detected:** Dark brown
- **Status:** Not part of surface spec — ignored

## Summary of Flags
| Region | Expected | Actual | Flag |
|--------|----------|--------|------|
| Left wall | Red | Beige | 🚩 Missing red |
| Right wall | Green | Beige | 🚩 Missing green |
| Floor | White | Cream | ⚠️ Off-white |
| Ceiling | White | Tan | ⚠️ Off-white |

**Conclusion:** The required red (left) and green (right) wall colors are **absent**. The scene appears rendered in a uniform warm neutral palette instead of the specified tri-color scheme.

## C0 Probe Directional Atlas

I'll analyze this image based on the technical context you've provided.

**Observations:**

This appears to be a **probe atlas visualization** — likely an irradiance or radiance probe texture used in real-time global illumination (e.g., DDGI - Dynamic Diffuse Global Illumination). Each small DxD tile encodes directional lighting information captured by one probe in the scene.

**Diagnostic findings against your flags:**

1. **Uniform gray tiles (unbaked probes):** I don't see solid flat-gray tiles dominating the atlas. Most cells contain structured patterns, suggesting the probes have received ray data and been integrated.

2. **Random noise (merge error):** I don't observe high-frequency salt-and-pepper noise that would indicate atomic merge races or blend failures. The patterns appear coherent within tiles.

3. **Smoothly varying neighbors:** Adjacent tiles do appear to share similar low-frequency structure (the central region shows a smooth gradient of similar motifs), which is the expected behavior — neighboring probes in world space should observe similar lighting.

**Edge/border tiles:**
- The **top, bottom, and side margins** show much sparser, dotted pat

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 14c)

## Observations

Examining the provided tile grid for cascade 1 at tMax ~1.0wu:

**Coverage Estimate:** ~95–97% (below the expected near-100% threshold)

## Dead Tile Flags (all-zero alpha)

Scanning row-by-row for tiles with f

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What You Should See
- **Smooth spatial gradients** across the probe grid, mirroring the room's lighting falloff
- Brighter values near light sources (windows, lamps, emissive surfaces)
- Gradual darkening toward occluded corners, under furniture, behind walls
- Color tints that match dominant light bounces (warm near incandescent, cool near sky portals)

## Failure Flags to Watch For

### 🚩 All-Black Probes
Likely causes:
- Probes failed to gather any radiance (rays missed all geometry, or hit only non-emissive surfaces with no indirect contribution)
- Probe placed inside solid geometry (backface culling kills all hits)
- Bad hemisphere sampling (zero-weight directions)
- SH/octahedral encoding writing zeros (NaN → clamp, or wrong buffer binding)

### 🚩 Sharp Grid Steps
Likely causes:
- **No interpolation** between probes (nearest-neighbor lookup instead of trilinear)
- Probe spacing too coarse relative to lighting frequency
- Visibility/chebyshev weights collapsing to a single probe
- Per-probe normalization differing (each probe averaged with different sample counts)

### 🚩 Wrong Colors
Likely causes:
- **Channel swap** (RGB ↔ BGR) in storage or readback
- Tone-mapping/gamma applied twice (or not at all)
- HDR values clipped to LDR before averaging
- Encoding/decoding mismatch (e.g., RGBM stored, raw read)
- Wrong color space (linear written, sRGB sampled)

## Quick Triage Checklist
1. Visualize a **single probe's hemisphere** raw — is the integrand sane?
2. Disable interpolation and view raw probe centers — gradients still smooth?
3. Dump min/max/mean radiance across the grid — any NaN/Inf/negatives?
4. Force a known constant (e.g., white sky) — do all probes return ~1.0?
5. Check probe placement against scene — any embedded in walls?

Want me to focus on a specific renderer (DDGI, irradiance volumes, light probes in Unity/Unreal, custom GI), or look at code for the gather/encode step?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Probe-grid banding** — Visible across the ceiling and along the right wall as blocky, pixelated patches of indirect light. The discrete probe samples are clearly resolvable rather than smoothly interpolated.

2. **Directional bin banding** — The ceiling shows hard angular steps where the brown indirect bounce transitions to white, particularly the staircase-like color steps along the upper portions of the scene.

3. **Color bleeding errors** — The right wall shows excessive brown/red saturation that bleeds aggressively across surfaces that should remain neutral, and the brown tint extends onto the ceiling in chunky patches inconsistent with smooth diffuse bounce.

4. **Cascade boundary seams** — Faint ring-like brightness discontinuities are visible in the mid-distance along the floor and where the floor meets the right wall, suggesting a transition between cascade levels.

5. **Outer-wall drift** — The right wall darkens noticeably toward the bottom-right corner in a monotonic gradient unrelated to scene geometry.

6. **Heavy blur/smearing** — Likely from temporal accumulation or low-resolution probe interpolation, masking some detail but not a listed artifact category.

**No significant evidence of:** shadow acne, missing shadows.

## Quality Rating: **Poor**

The frame suffers from multiple overlapping artifacts — probe banding and directional binning are both prominent on the ceiling, color bleeding is over-saturated and chunky, and cascade seams are visible on the floor. The overall image lacks the smooth indirect lighting that radiance cascades should produce.
