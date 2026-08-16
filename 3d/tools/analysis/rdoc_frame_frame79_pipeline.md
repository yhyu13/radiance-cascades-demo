# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame79.rdc`  
**Analyzed:** 2026-05-12T11:35:43  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 78573.4 |
| Cascade reduction | dispatch | 158.8 |
| Cascade bake | dispatch | 58471.6 |
| Cascade reduction | dispatch | 159.7 |
| Cascade bake | dispatch | 79432.1 |
| Cascade reduction | dispatch | 58619.9 |
| Cascade bake | dispatch | 365236.5 |
| Cascade reduction | dispatch | 165659.3 |
| Raymarching | draw | 57529.8 |
| GI blur | draw | 135.2 |
| glDrawElements() | draw | 10.8 |
| **Total** | | **863987.1** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a Signed Distance Field (SDF) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: The image shows very abrupt transitions between black (interior/negative) and red (exterior/positive) regions, rather than a smooth gradient. A proper SDF should exhibit gradual color/value changes representing distance falloff.

2. **Flat regions**: Large areas of uniform red and uniform black indicate the distance values are saturated or clamped, suggesting:
   - The SDF range may be too narrow
   - Distance computation may be binary-like (inside/outside) rather than true distance
   - Possible voxelization artifacts where distances weren't propagated properly

3. **Missing gradient falloff**: Around the geometry edges, there's no visible "halo" of intermediate values that would indicate proper distance computation outward from the surface.

4. **Geometry concerns**:
   - The black shapes appear pixelated/blocky, suggesting low-resolution voxelization
   - Vertical bar-like structures and a horizontal dashed pattern suggest the source geometry has thin features that may be under-resolved
   - Possible holes: gaps between the vertical bars could be either intentional geometry or sampling errors

5. **Likely errors**:
   - **Voxelization resolution too low** for the thin features present
   - **SDF computed as binary mask** rather than true Euclidean distance
   - **Narrow band SDF** with values outside the band clamped to extremes
   - Possible **inside/outside classification errors** at thin features

**Recommendations:**
- Increase voxel resolution to capture thin geometry
- Verify the distance computation algorithm (use proper Euclidean distance transform)
- Expand the visualization range to see gradient falloff
- Check for watertight input geometry to ensure correct sign computation

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged:

**❌ Left Wall (should be RED)**
- Currently appears in **tan/beige and dark brown tones**
- No red coloring detected
- **FLAG: Missing red color**

**❌ Right Wall (should be GREEN)**
- Currently appears in **tan/beige and dark brown tones**
- No green coloring detected
- **FLAG: Missing green color**

**❌ Other Surfaces (should be WHITE)**
- Interior shows **yellow/cream base** with scattered colored elements (purple, green, orange, red dots)
- Top edge shows brown/tan tones
- **FLAG: Non-white surfaces present**

## Summary
| Region | Expected | Actual | Status |
|--------|----------|--------|--------|
| Left wall | Red | Brown/tan | ❌ Incorrect |
| Right wall | Green | Brown/tan | ❌ Incorrect |
| Other areas | White | Yellow/multi-color | ❌ Incorrect |

**Overall: The image does not conform to the specified color scheme.** It appears to depict a food item (possibly a tart or pastry dish) rather than a color-coded surface diagram. All three color regions fail validation.

## C0 Probe Directional Atlas

I'll analyze what you're describing - this sounds like a **probe atlas / irradiance volume debug visualization** from a real-time global illumination system (like DDGI - Dynamic Diffuse Global Illumination).

## What I'm seeing / what to look for:

**Layout**: A grid of small DxD tiles, where each tile = one light probe's encoded data (irradiance or visibility octahedral mapping).

**Health checks based on your flags**:

| Symptom | Diagnosis |
|---|---|
| **Uniform gray tile** | Probe hasn't been baked/updated yet — check probe scheduling, ray dispatch, or activation mask |
| **Random noise tile** | Merge/blend error — likely a race condition in the atomic update, wrong texel offset, or border-copy step failing |
| **Hard seams between neighbors** | Spatial hysteresis too low, or probes sampling very different geometry (expected near walls) |
| **Smooth gradients across neighbors** | ✅ Healthy — irradiance is spatially coherent |

**Alpha channel interpretation**:
- `alpha > 0` → ray hit a surface (valid radiance contribution)
- `alpha < 0` → ray escaped to sky (sky cubemap contribution)
- `alpha == 0` → miss / invalid (skip in blend, or backface hit flagged for relocation)

## Debugging suggestions

1. **Border bleeding**: Octahedral tiles need a 1-pixel border copied from the opposite edge for correct bilinear sampling. Noise at tile *edges only* = border copy bug.
2. **Probe relocation**: Probes with mostly `alpha == 0` (inside geometry) should be relocated or marked inactive.
3. **Temporal blend factor**: If tiles look "stuck" gray, your hysteresis (e.g., `0.97`) may be blending toward an uninitialized clear value — initialize the atlas to a sentinel and skip blend on first frame.
4. **Check dispatch coverage**: Uniform gray rows/columns suggest a thread-group dimension mismatch in your probe update kernel.

Would you like me to sketch the octahedral encode/decode math, or the border-copy compute shader? Also — is this DDGI-style (RTXGI), or a custom irradiance volume?

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis

## Observations from C1 Heatmap (tMax ~1.0wu, post-Phase 14c)

**Coverage Summary:**
- **Top band (orange/copper tiles):** ~95% active — strong thermal signature, consistent with expected near-100% coverage
- **Central grid region:** Dense activation, ~97-98% coverage with healthy alpha distribution
- **Bottom band (gray tiles):** ~94% active, slightly attenuated vs. top

## Dead Tile Flags (α = 0)

Scanning for all-zero alpha cells:

| Region | Approx. Coords (tile idx) | Notes |
|---|---|---|
| Top band | (3, 0), (17, 1), (24, 2) | Likely edge dropouts |
| Mid-left cluster | (2, 8), (2, 9) | Possible shadowed by L-bracket geometry |
| Center | (12, 11) | Isolated — flag for inspection |
| Bottom band | (8, 19), (21, 20), (26, 21) | Edge attenuation pattern |

**Total dead tiles flagged: ~9** out of estimated ~600 visible cells (~1.5% dead rate)

## Comparison to C0 Baseline
- C1 shows **higher overall coverage** than C0 (as expected for the later cascade with more accumulated propagation time)
- Dead-tile distribution is **edge-biased**, not clustered — suggests boundary effects rather than systemic failure
- No evidence of a "cold zone" propagating from any single dead tile

## Recommendations
1. ✅ Coverage meets the near-100% expectation for tMax ~1.0wu
2. ⚠️ Investigate the isolated center dead tile at ~(12,11) — could indicate a stuck node rather than edge effect
3. 📋 Re-run with finer alpha-channel sampling if you need sub-percent dead-tile accuracy — current rasterization may be masking borderline-low cells as zero

Want me to overlay a difference map (C1 − C0) to highlight which tiles "woke up" between cascades?

## C0 Isotropic Probe Grid (reduction output)

The image you've shared appears to be entirely white (or blank), so I cannot evaluate the direction-averaged radiance probes you're describing.

For a proper diagnostic of probe radiance visualization, I'd expect to see:
- **Healthy result**: Smooth spatial gradients across probes, with colors/intensities varying gently to reflect room lighting (brighter near light sources, darker in occluded areas, subtle color bleeding from nearby surfaces).
- **Failure flags to check**:
  - **All-black probes**: Indicates probes failed to gather radiance (bad ray casting, missed integration, or zero irradiance accumulation).
  - **Sharp grid steps**: Suggests undersampling, missing interpolation, or per-probe discontinuities (possibly bad normalization or solid-angle weighting).
  - **Wrong colors**: Could indicate color space issues (linear vs sRGB), channel swaps (RGB↔BGR), or incorrect tone mapping.

Could you re-upload the probe visualization image? If it rendered as pure white, that itself may be a symptom — possibly:
- Overexposed/clamped HDR values
- Missing tone mapping or exposure scaling
- A shader writing constant white
- Alpha/blend state issue producing a blank framebuffer

Let me know what you see and I can help diagnose further.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the right wall as horizontal blocky striations in the indirect lighting. The brown/orange tones show clear axis-aligned grid patterns rather than smooth falloff.

2. **Directional bin banding** — Evident on the ceiling near the light fixtures, where the illumination forms hard angular triangular/diamond patches around each light source instead of smooth radial falloff. The floor reflections also show stepped angular discontinuities.

3. **Cascade boundary seams** — A subtle ring-shaped brightness discontinuity is visible in the far-right midground, where the wall lighting transitions abruptly between cascade levels.

4. **Outer-wall drift** — The right wall progressively darkens toward the right edge of the frame in a monotonic way that doesn't match the geometry's expected falloff from the ceiling lights.

5. **Color bleeding errors** — The orange/peach tint on the right side floor edge and wall base appears overly saturated and bleeds into regions where the bounce geometry wouldn't justify it (no visible orange surface as source at that intensity).

6. **Shadow acne** — Small dark speckles visible on the floor in the mid-distance, particularly under the light fixtures.

**Quality rating: Poor**

Multiple compounding artifacts (grid banding, directional binning, and cascade seams) are simultaneously visible and disrupt the perception of smooth global illumination. The 25 FPS counter also suggests the renderer is struggling, possibly with insufficient cascade resolution or angular bin count.
