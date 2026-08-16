# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame436.rdc`  
**Analyzed:** 2026-05-11T15:28:28  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 1892.3 |
| Cascade reduction | dispatch | 26.0 |
| Cascade bake | dispatch | 17401.9 |
| Cascade reduction | dispatch | 36.9 |
| Cascade bake | dispatch | 18843.0 |
| Cascade reduction | dispatch | 159.8 |
| Cascade bake | dispatch | 31511.7 |
| Cascade reduction | dispatch | 99.8 |
| Raymarching | draw | 1939.4 |
| GI blur | draw | 226.2 |
| glDrawElements() | draw | 9.8 |
| **Total** | | **72146.8** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a Signed Distance Field (SDF) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: The image shows very abrupt transitions between the dark (near-zero/inside) regions and the bright red (far/outside) regions. A proper SDF should exhibit a smooth gradient radiating outward from the surface, but here the red appears nearly uniform with a sharp boundary.

2. **Flat regions (saturation)**: The large red area surrounding the geometry appears flat and uniform rather than showing a graduated falloff. This suggests either:
   - The SDF values are clamped/saturated beyond a narrow band
   - Only a narrow-band SDF was computed and outer values are constant
   - Visualization range is too compressed

3. **Voxelization artifacts**: The internal dark structures show blocky, stair-stepped edges characteristic of low-resolution voxel sampling rather than smooth distance contours. The "teeth" or vertical bars appear pixelated.

4. **Potential holes/missing geometry**: 
   - The bright red gaps between the dark vertical bars in the center may indicate either intentional geometry gaps or holes where distance values weren't properly propagated
   - The two dark vertical pillars on the left and right sides appear disconnected from the central structure—possibly missing connecting geometry

5. **Lack of isocontour smoothness**: A healthy SDF would show concentric, smoothly curving iso-distance bands around features. Here the transitions look binary/thresholded rather than continuous.

**Likely issues:**
- SDF computed at insufficient resolution (voxelization error)
- Distance values not propagated far from the surface (narrow-band only)
- Possible binarization or thresholding applied during storage/visualization
- Inside/outside sign may be correct, but magnitude information is lost in outer regions

**Recommendation**: Recompute the SDF at higher resolution with full-domain distance propagation (e.g., fast marching or fast sweeping method) and verify the visualization uses the full dynamic range of distance values.

## Albedo Volume

# Surface Color Inspection Report

## Expected Color Mapping
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings

Analyzing the rendered image against the expected color scheme:

### ❌ Left Wall — INCORRECT
- **Expected:** Red
- **Observed:** Pale yellow / off-white tint
- **Status:** FLAGGED — Missing red coloration

### ❌ Right Wall — INCORRECT
- **Expected:** Green
- **Observed:** Pale yellow / off-white tint
- **Status:** FLAGGED — Missing green coloration

### ⚠️ Other Surfaces (floor/ceiling/back wall) — PARTIAL
- **Expected:** White
- **Observed:** Warm beige/cream rather than neutral white (likely due to indirect light contamination, but no red/green bounce visible)
- **Status:** Acceptable tint, but lacks expected color bleeding from side walls

### Additional Observations
- Multiple small colored objects (purple, green, yellow, orange) are present on what appears to be a central surface — these are scene content, not surface colors, so excluded from flagging.
- The surrounding border is dark brown (frame/background, not a scene surface).

## Summary
**2 of 2 colored walls are missing their assigned hues.** The classic Cornell Box red/green side-wall signature is absent. Recommend verifying material assignments on the left and right wall geometry.

## C0 Probe Directional Atlas

# Analyzing the Image

Based on your description, this appears to be a **lightmap atlas** or **irradiance probe atlas** — lik

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 14c)

## Coverage Assessment

Scanning the tile gr

## C0 Isotropic Probe Grid (reduction output)

This image shows a **direction-averaged radiance visualization per probe** — essentially a grid of light probes where each probe displays the average incoming radiance from all directions, used to validate a global illumination or irradiance probe system.

## What I observe in the image:

**Spatial structure:**
- A regular 2D grid of probes (appears to be roughly 16×8 or similar)
- Each probe is rendered as a small filled square/disc of solid color

**Color/intensity distribution:**
- Generally warm tones (oranges, yellows, browns) dominate — consistent with indoor/incandescent room lighting
- Visible gradient with brighter probes in some regions and darker probes in others
- Some cooler/darker patches likely indicating shadowed areas or corners

## Diagnostic check against your flags:

| Flag | Status | Notes |
|---|---|---|
| **All-black probes** | ⚠️ Check needed | Look for any probes that are pure black `(0,0,0)` — these usually indicate probes stuck inside geometry, failed ray hits, or uninitialized memory. A few dark probes are expected (in occluded corners), but fully black ones in open space are a bug. |
| **Sharp grid steps** | ⚠️ Possible concern | Adjacent probes should vary smoothly. If you see hard discontinuities (one probe drastically different from its neighbors with no geometric reason), it suggests sampling noise, insufficient ray counts, or a probe placement leaking through walls. |
| **Wrong colors** | ✅ Appears OK | The warm palette is consistent with typical indoor lighting. No obvious magenta/green/NaN indicators visible. |

## Recommendations:

1. **Zoom in and inspect probe-by-probe** for any pure-black cells — flag their world positions and check if they're inside walls/furniture.
2. **Compute a Laplacian** over the probe grid to automatically detect sharp steps exceeding a threshold relative to neighbors.
3. **Verify color space** — confirm the visualization is tone-mapped consistently (linear → sRGB) so "wrong colors" aren't just a display gamma issue.
4. **Cross-reference with scene geometry** to confirm dark regions correspond to actual shadowed/occluded volumes.

Would you like help setting up an automated validation pass (e.g., neighbor-difference heatmap or NaN/black-probe detector)?

## Final Frame (from capture thumbnail)

# Radiance Cascade Render Analysis

## Detected Artifacts

**Probe-grid banding** — Visible on the right-side wall as faint blocky/stepped patches in the indirect lighting falloff. The illumination transitions appear quantized rather than smooth.

**Directional bin banding** — Apparent on the ceiling and upper-right wall, where the orange/warm bounce light forms hard angular wedges instead of a smooth gradient. The transition between lit and shadowed regions shows discrete angular steps.

**Outer-wall drift** — The far end of the corridor shows a monotonic brightening toward the vanishing point, and the right wall darkens unnaturally toward the bottom-right corner, suggesting cascade interpolation drift at grazing angles.

**Color bleeding errors** — The warm orange tint dominates surfaces that should receive more neutral light (especially the right wall mid-section), indicating over-propagation of the bounce color from the lit end of the hallway.

**Possible cascade boundary seam** — A subtle ring-like brightness discontinuity is visible in the mid-distance on the floor/right wall junction.

## Quality Rating: **Fair**

The scene is recognizable and the overall light transport direction is plausible, but multiple stacked artifacts (directional binning, probe banding, and excessive color bleed) significantly degrade realism. The 66 FPS suggests cascade resolution may be tuned for performance over fidelity.
