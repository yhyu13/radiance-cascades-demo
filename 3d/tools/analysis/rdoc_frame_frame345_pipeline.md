# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame345.rdc`  
**Analyzed:** 2026-05-11T15:39:33  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 6309.7 |
| Cascade reduction | dispatch | 28.6 |
| Cascade bake | dispatch | 10260.5 |
| Cascade reduction | dispatch | 156.2 |
| Cascade bake | dispatch | 14155.2 |
| Cascade reduction | dispatch | 169.6 |
| Cascade bake | dispatch | 16096.6 |
| Cascade reduction | dispatch | 225.0 |
| Raymarching | draw | 24463.7 |
| GI blur | draw | 19386.4 |
| glDrawElements() | draw | 12.3 |
| **Total** | | **91263.8** |


## SDF Volume (Signed Distance Field)

I'll analyze this SDF (Signed Distance Field) visualization for issues.

## Observations

**Critical Issues Detected:**

1. **Hard binary appearance** - The image shows an almost entirely flat red field with sharp black geometry, lacking the smooth gradient transitions characteristic of a proper SDF. A correct SDF should display a smooth falloff from the surface outward.

2. **No visible distance gradient** - The red region appears uniform/saturated, suggesting either:
   - Distance values are clamped or saturated
   - The visualization is showing a binary inside/outside mask rather than true distance values
   - The SDF is only computed in a narrow band near the surface

3. **Sharp seams at geometry edges** - The transition between the black geometry and red background is abrupt with no gradient ring, indicating voxelization artifacts or that distances aren't being properly propagated.

4. **Flat black regions** - The interior of the shapes (pillars/columns and central structure) shows flat black with no internal gradient. A proper SDF should show increasing negative distance toward the medial axis of solid regions.

5. **Possible holes/missing geometry** - The small bright/lighter pixels within the central horizontal band could indicate:
   - Missing voxels (holes in geometry)
   - Sign flip errors where interior points were classified as exterior
   - Incomplete flood-fill during SDF generation

## Recommendations

- **Verify distance computation**: Ensure you're using a proper distance transform (e.g., fast marching, brute force, or jump flooding) rather than just inside/outside classification
- **Check value range/normalization**: The visualization may be clipping values - try remapping the distance range
- **Inspect narrow-band width**: If using narrow-band SDF, expand the band
- **Validate sign assignment**: Use winding number or ray-casting to verify interior/exterior classification
- **Increase resolution**: Voxelization at low resolution could explain the blocky seams

The SDF appears to be functioning more as an occupancy grid than a true signed distance field.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color regions:

## Expected Configuration
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings — Issues Detected ⚠️

**1. Left wall — INCORRECT**
- Expected: Red
- Observed: Pale yellow/beige tones
- ❌ Flag: Missing red coloration

**2. Right wall — INCORRECT**
- Expected: Green
- Observed: Pale yellow/beige tones (similar to left)
- ❌ Flag: Missing green coloration

**3. Other surfaces (floor/ceiling/back) — PARTIALLY INCORRECT**
- Expected: White
- Observed: Yellowish/cream interior, with brown exterior surroundings
- ❌ Flag: Not white as specified

**4. Interior contents**
- Multiple colored objects visible (purple, green, orange, red specks) along the floor
- These appear to be objects/items rather than surface colors, but they obscure surface verification

## Summary
**No surfaces match the expected color scheme.** The scene appears to use a warm monochromatic palette (browns, creams, yellows) rather than the specified red/green/white Cornell-box-style configuration. All three color regions require correction.

## C0 Probe Directional Atlas

I'll analyze this image based on your description of a probe-baked lightmap or irradiance volume visualization.

**Observations:**

1. **Tile structure visible**: I can see a grid of D×D tiles arranged in what appears to be a 2D layout of probes, with denser sampling in the center and border regions.

2. **No uniform gray tiles detected**: I don't see solid gray patches that would flag unbaked probes — the tiles appear to contain detail/pattern data.

3. **No obvious random noise**: The patterns look structured (symmetric, repeating motifs) rather than high-frequency random noise, suggesting merge operations completed correctly.

4. **Smooth neighbor variation**: Adjacent tiles appear to share similar tonal values and pattern continuity, consistent with a properly baked probe volume where irradiance varies smoothly across space.

5. **Alpha/hit interpretation**:
   - **Center region** (dense ornamental patterns): likely **positive alpha** — surface hits with rich directional radiance encoded.
   - **Border rows/columns** (thinner, repetitive tick marks): could represent **sky samples** (negative alpha) along the volume boundary.
   - **Sparse dotted areas** near edges: possibly **zero alpha / misses** where rays escaped without intersecting geometry or sky.

**Verdict**: The bake appears **healthy** — no unbaked-probe flags (uniform gray) and no merge corruption (random noise). The volume shows expected structure: h

## C1 Probe Directional Atlas

# Cascade 1 Analysis (tMax ~1.0 wu, post-Phase 14c)

## Surface Coverage Assessment

Comparing against

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

This visualization shows the **mean radiance** (averaged over all directions/SH bands) at each probe location in your light probe grid. It's essentially a low-frequency "ambient" view of your lighting volume.

## ✅ What a Healthy Result Looks Like

- **Smooth spatial gradients** between adjacent probes
- Brighter probes near light sources (windows, lamps, emissive surfaces)
- Gradual falloff into shadowed regions (under furniture, in corners)
- Color tints consistent with nearby bounce surfaces (e.g., warm near wood floors, cool near sky-lit windows)
- Probes inside geometry may be dark/zero — that's expected if you cull them

## 🚩 Failure Modes to Flag

### 1. All-Black Probes
**Symptoms:** Entire grid (or large regions) reads zero radiance.

**Likely causes:**
- Probe capture pass never ran / output buffer not bound
- Cubemap/octahedral capture cleared but not written
- SH coefficients zeroed during accumulation (divide-by-zero, NaN propagation)
- Wrong texture format (e.g., UNORM clamping HDR to 0)
- Camera/probe near-plane culling all geometry

### 2. Sharp Grid Steps
**Symptoms:** Visible blocky discontinuities; each probe looks like a flat cell.

**Likely causes:**
- Trilinear interpolation disabled or sampling nearest-neighbor
- Probe indices off-by-one (sampling wrong cell)
- Visualization is rendering probe **cells** instead of interpolated field
- World-to-probe-space transform incorrect (scale/offset mismatch)
- *Note:* if you're rendering one sphere per probe, steps are expected — switch to a volumetric slice view to verify interpolation

### 3. Wrong Colors
**Symptoms:** Magenta/green tint, inverted hues, gamma-crushed values, or oversaturation.

**Likely causes:**
- **Color space mismatch:** sRGB data sampled as linear or vice versa
- **Channel swizzle:** BGR vs RGB on capture
- **SH band 0 scaling:** missing `1/(2√π)` factor → wrong magnitude
- **Negative SH reconstruction** clamped incorrectly (should clamp ≥0 for radiance)
- HDR tonemapping applied twice, or not at all
- Wrong exposure when comparing to reference

## 🔧 Suggested Debug Steps

1. **Dump a single probe's cubemap** — verify capture is correct in isolation
2. **Visualize SH band 0 only** — should match this averaged view
3. **Toggle interpolation off/on** — confirms sampling path
4. **Compare to a path-traced reference** at a few probe locations
5. **Check histogram** — all-zero or all-saturated indicates pipeline break

Would you like a shader snippet for the

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Probe-grid banding** — Visible across the ceiling and upper walls as blocky, stair-stepped patches of brown/dark pixels. The indirect light interpolation between probes is producing clearly visible grid-aligned chunks rather than smooth gradients.

2. **Directional bin banding** — The blocky shadow patterns on the ceiling show hard angular steps consistent with too few directional bins; the indirect contribution forms discrete "tiles" rather than smooth shading.

3. **Color bleeding errors / over-bleeding** — The right wall and right side of the floor show heavy reddish-brown contamination spreading far from any visible red source. The bleed appears smeared and asymmetric rather than physically localized.

4. **Outer-wall drift** — The left wall darkens noticeably toward the bottom and brightens toward the top in a monotonic gradient unrelated to scene geometry; similar drift on the right wall going from bright orange near camera to dark in distance.

5. **Cascade boundary seam (mild)** — A faint ring-like brightness change is visible in the mid-distance floor where the road markings change density, suggesting a cascade transition.

6. **Aliased/under-sampled features** — The lane markings on the floor break into disconnected speckles in the distance (related to low probe resolution rather than true shadow acne).

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts — coarse probe spacing, insufficient angular resolution, and excessive color bleed dominate the image, obscuring scene detail.
