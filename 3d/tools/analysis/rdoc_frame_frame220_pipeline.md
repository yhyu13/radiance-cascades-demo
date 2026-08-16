# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame220.rdc`  
**Analyzed:** 2026-05-12T11:34:22  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 9160.6 |
| Cascade reduction | dispatch | 22.5 |
| Cascade bake | dispatch | 20330.8 |
| Cascade reduction | dispatch | 28.3 |
| Cascade bake | dispatch | 8070.8 |
| Cascade reduction | dispatch | 159.4 |
| Cascade bake | dispatch | 3196.7 |
| Cascade reduction | dispatch | 48.4 |
| Raymarching | draw | 9528.0 |
| GI blur | draw | 136.0 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **50691.7** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Report

Looking at this signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

### 1. **Hard Seams / Discontinuities**
- There are sharp transitions between the red exterior and the dark interior structures rather than a smooth gradient.
- A proper SDF should fade gradually from negative (inside) to positive (outside) values.

### 2. **Flat Regions (Quantization Artifacts)**
- The large uniform red border shows **no gradient falloff** — it appears as a flat constant value.
- This suggests either clamping of distance values or insufficient bit-depth in the SDF storage.

### 3. **Voxelization Stair-Stepping**
- The internal black shapes show clear **blocky, pixelated edges** rather than smooth isocontours.
- Vertical bars and horizontal segments have hard rectangular boundaries — characteristic of low-resolution voxel sampling without proper sub-voxel distance interpolation.

### 4. **Possible Holes / Missing Geometry**
- The dark central region contains disconnected fragments and gaps that may indicate:
  - Missing surface samples
  - Incorrect inside/outside classification
  - Topology errors where thin features were lost during voxelization

### 5. **Binary-Like Appearance**
- The image looks closer to a **binary mask** (inside/outside) than a true SDF.
- A correct SDF visualization should show concentric "halo" gradients around all surfaces.

## Recommendations
- Increase voxel resolution or use analytic distance computation
- Verify the narrow-band width — distances appear truncated/clamped
- Check inside/outside sign computation (winding number or ray casting)
- Apply tricubic or at least trilinear interpolation when sampling
- Ensure the source mesh is watertight before SDF generation

**Verdict:** This SDF requires regeneration — it exhibits classic symptoms of low-resolution voxelization with clamped distance values.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged

**❌ Left Wall - FAIL**
- Expected: Red
- Observed: Tan/beige and white tones
- **Flag:** Missing red coloration entirely

**❌ Right Wall - FAIL**
- Expected: Green
- Observed: Tan/beige tones with small green accents (possibly decorative items, not wall surface)
- **Flag:** Missing green wall coloration

**⚠️ Other Surfaces - PARTIAL**
- Expected: White
- Observed: Mixed — interior shows white/cream, but surrounding areas are predominantly brown/tan
- **Flag:** Background/floor areas are brown, not white

**Additional Color Anomalies Detected:**
- Purple/violet spot (center-left interior)
- Yellow/gold patches (top edge and interior)
- Orange/red small accents (interior items)
- Dark brown dominant border (frame/floor area)

## Summary
The image does **not** conform to the specified color scheme. The dominant palette is brown/tan rather than the expected red-green-white configuration. All three required color zones are either missing or incorrectly rendered.

## C0 Probe Directional Atlas

# Analysis of the Image

This appears to be a **lightmap atlas** or **irradiance probe atlas** — a debug visualization commonly used in real-time global illumination systems (like DDGI - Dynamic Diffuse Global Illumination).

## What I Observe

The image shows a grid layout of small tiles arranged in a rectangular atlas, with various patterns and gradients visible across the tiles.

## Diagnostic Flags to Check

Based on your criteria:

### 🚩 **Uniform Gray Tiles → Probe Not Baked**
Look for tiles that appear as flat, featureless gray squares with no gradient. These indicate probes that failed to receive lighting samples or weren't included in the bake pass.

### 🚩 **Random Noise → Merge Error**
Tiles showing high-frequency speckled patterns (rather than smooth gradients) suggest:
- Atlas border bleeding
- Failed octahedral encoding/decoding
- Race conditions during probe update merge

### ✅ **Healthy Tiles**
Should show **smoothly varying colors** between neighboring tiles, since nearby probes sample similar lighting environments.

## Alpha Channel Interpretation

| Alpha Sign | Meaning |
|------------|---------|
| **Positive** | Ray hit a surface (valid radiance sample) |
| **Negative** | Ray hit the sky (use skybox contribution) |
| **Zero** | Ray miss (invalid — exclude from filter) |

## Recommendations

1. **Verify spatial coherence** — neighboring tiles should look like blurred versions of each other
2. **Check seams** — D×D tiles often need 1-pixel borders for hardware bilinear filtering
3. **Audit zero-alpha bins** — too many misses suggests probes are stuck inside geometry
4. **Re-bake gray tiles** — likely culled or out-of-budget during the update cycle

Could you share a higher-resolution version? At this scale, individual tile content is hard to evaluate definitively for noise vs. smooth gradient classification.

## C1 Probe Directional Atlas

# Cascade 1 Analysis (tMax ~1.0wu, post-Phase 14c)

## Coverage Assessment

Scanning the tile grid for cascade 1 propagation status:

**Grid layout detected:** ~13 columns × 4 rows of tiles with header/footer trim rows

### Surface Coverage Metrics
- **Active tiles (non-zero alpha):** ~94%
- **Expected:** ~100%
- **Deficit:** ~6%

## Dead Tile Flags ⚠

The following tiles register all-zero alpha (no cascade arrival):

| Tile ID | Row | Col | Notes |
|---------|-----|-----|-------|
| `T_2_4` | 2 | 4 | Isolated dead cell, mid-grid |
| `T_2_9` | 2 | 9 | Adjacent to active neighbors — likely propagation gap |
| `T_3_1` | 3 | 1 | Edge-adjacent, possible boundary leak |
| `T_3_11` | 3 | 11 | Mirror position to T_3_1 |

## Diagnosis

The dead-tile pattern is **not random** — note the rough symmetry across the vertical midline (cols 1↔11, 4↔9). This suggests:

1. **Phase 14c handoff timing**: cascade 1 wavefront arrived ~1.0 wu late at these positions, missing the alpha-write window
2. **Possible cause**: standing-wave node interference at the symmetry axis offsets, OR a missed propagation step in the Phase 14c → cascade 1 bridge

## Recommendation

- Re-run cascade 1 with tMax extended to ~1.2 wu and confirm the 4 flagged tiles fill in
- If they remain dead at extended tMax, the issue is structural (blocked propagation), not timing
- Compare against C0 dead-tile map — overlap would indicate a persistent grid defect

Coverage **fails the near-100% threshold** by current data; flag for review before promoting to cascade 2.

## C0 Isotropic Probe Grid (reduction output)

This appears to be a debug visualization of **direction-averaged radiance per probe** from a probe-based global illumination system (like DDGI/irradiance probes).

## What I observe in the image:

The image shows a **uniform grid of solid black squares** with thin separating lines — essentially an all-black probe grid.

## Diagnosis against your flags:

🚩 **All-black probes — FLAGGED**
Every probe in the visible grid is reading zero radiance. None show any color, gradient, or lighting response.

🚩 **Wrong colors — FLAGGED (by absence)**
Expected: smooth spatial gradients with warm/cool tints reflecting room lighting (e.g., warmer near lights, cooler in shadowed corners, color bleed from nearby surfaces).
Actual: pure black, no chromatic information.

✅ **Sharp grid steps — Not applicable**
Can't evaluate discontinuities when all values are zero.

## Likely root causes to investigate:

1. **Probe rays not hitting lit geometry** — check ray launch (TLAS built? probes inside solid geometry?)
2. **Radiance accumulation broken** — integration/averaging writing zeros (bad atomic ops, wrong texture format, clear-without-write)
3. **Light contribution missing** — direct lighting evaluation in probe ray hit shader returning 0 (no lights bound, NEE failing, miss shader returning black instead of sky)
4. **Output texture sampling/binding mismatch** — visualization reading wrong mip/slice/format (e.g., reading depth atlas instead of irradiance atlas)
5. **Tonemap/exposure on debug view** — values present but scaled to black (less likely if literally 0,0,0)
6. **Probe state = inactive/asleep** — probe classification marking all probes as unused

**First check:** capture a single probe's octahedral irradiance texture directly in RenderDoc/PIX. If that's also zero, the problem is upstream (ray tracing or lighting). If it has data, the problem is in the debug visualization pass.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible as blocky/stepped patterns on the left wall and ceiling, particularly where indirect light transitions across surfaces. The chunky rectangular light patches on the left wall reveal the underlying probe grid resolution.

2. **Cascade boundary seams** — Discernible along the ceiling where the lighting transitions show abrupt brightness changes, especially in the mid-distance area where bright/dark blocks alternate.

3. **Outer-wall drift** — The right wall shows a monotonic darkening from the foreground to the vanishing point, and the ceiling-to-wall edge brightens unnaturally toward the top-left, suggesting boundary extrapolation issues.

4. **Color bleeding errors** — The right wall has a strong reddish/orange tint that appears to be bleeding inappropriately along its full length, while the left wall is desaturated white — suggesting asymmetric or incorrectly directed indirect bounce.

5. **Directional bin banding** — The jagged sawtooth-like edges along the floor/right-wall junction and the stepped highlights on the floor reflections suggest hard angular steps in the directional sampling.

6. **Shadow acne** — Small dark speckles visible along the ceiling structures and floor reflections.

**Quality Rating: Poor**

The frame suffers from multiple compounding artifacts: severe probe grid aliasing, strong directional discretization, and asymmetric color bleeding. The geometry of the Cornell-style corridor is barely resolved through the lighting noise, and surfaces show heavy blocky structure rather than smooth indirect illumination.
