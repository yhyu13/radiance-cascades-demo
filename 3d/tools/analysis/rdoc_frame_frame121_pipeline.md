# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame121.rdc`  
**Analyzed:** 2026-05-12T11:35:07  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 41006.9 |
| Cascade reduction | dispatch | 36.9 |
| Cascade bake | dispatch | 47174.9 |
| Cascade reduction | dispatch | 159.7 |
| Cascade bake | dispatch | 114151.6 |
| Cascade reduction | dispatch | 63055.9 |
| Cascade bake | dispatch | 168059.8 |
| Cascade reduction | dispatch | 47576.9 |
| Raymarching | draw | 72572.1 |
| GI blur | draw | 135.2 |
| glDrawElements() | draw | 14.3 |
| **Total** | | **553944.3** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/sharp transitions**: The image shows very abrupt transitions between the dark (interior/negative) regions and the bright red (exterior/positive) regions. A proper SDF should exhibit smooth gradients radiating outward from the surface.

2. **Flat regions**: Large areas of uniform red color around the borders suggest **clamped or saturated SDF values**, indicating either:
   - Insufficient bit depth in the SDF storage
   - Distance values being clipped to a maximum
   - Missing far-field distance computation

3. **Voxelization artifacts**: The interior structure shows blocky, pixelated black regions with rectangular shapes. This is characteristic of:
   - Low-resolution voxel grid sampling
   - Nearest-neighbor distance computation rather than true Euclidean distance
   - Missing sub-voxel precision

4. **Potential holes/missing geometry**: 
   - The thin horizontal dashed line in the upper-middle area could indicate broken/discontinuous geometry
   - The two vertical pillar-like structures appear disconnected from surrounding geometry
   - Gaps between the central elements may represent unintended holes rather than true features

5. **Lack of gradient banding**: A healthy SDF visualization should show concentric bands of color intensity around surfaces. Here, the field appears binary-like (inside vs. outside) rather than continuous.

**Recommendations:**
- Increase voxel resolution
- Use a proper Euclidean distance transform (e.g., jump flooding algorithm)
- Verify SDF value range isn't being clamped during normalization
- Check mesh watertightness before SDF generation to eliminate spurious holes
- Apply narrow-band or full-field distance computation rather than binary occupancy

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged:

**🚩 Left Wall**
- **Expected:** Red
- **Actual:** Tan/beige with no red pigmentation detected
- **Status:** INCORRECT

**🚩 Right Wall**
- **Expected:** Green
- **Actual:** Tan/beige tones, no green channel dominance
- **Status:** INCORRECT

**🚩 Background/Surrounding Areas**
- **Expected:** White
- **Actual:** Dark brown/maroon background
- **Status:** INCORRECT

**🚩 Interior Surface**
- **Expected:** White
- **Actual:** Pale yellow/cream with scattered multicolor specks (purple, green, orange, red)
- **Status:** PARTIALLY INCORRECT — base tone off-white, plus unexpected colored elements

## Summary
None of the surfaces match the specified color requirements. The image appears to depict a dish/tray with garnish on a brown surface rather than the red/green/white walled structure described. **All major color regions fail inspection.**

## C0 Probe Directional Atlas

I can't tell what the flag text says from your description alone — the image you shared appears to be a noisy/low-resolution rendering (looks like a debug visualization of an irradiance/probe atlas), and no readable characters are visible to me.

A couple of clarifying thoughts based on what you wrote:

- **Uniform gray tiles** typically mean a probe wasn't baked/updated — its octahedral map is just the clear color.
- **Random noise tiles** usually indicate a merge/blend error (e.g., reading from the wrong atlas slice, uninitialized memory, or a race condition during the probe update compute pass).
- **Alpha convention** (positive = surface hit distance, negative = sky/backdrop, zero = miss) is consistent with a DDGI-style probe atlas that stores mean distance / visibility in the alpha channel for Chebyshev occlusion tests.

If you're trying to extract a CTF-style flag hidden in the image, could you:
1. Share the original full-resolution PNG (not a downscaled thumbnail)?
2. Confirm whether the flag is encoded in the **pixel values** (e.g., LSB stego, alpha channel data), in the **tile pattern** (gray vs. noise vs. valid = bits), or rendered as **visible text** in one of the probe tiles?

With the original asset I can help you decode it.

## C1 Probe Directional Atlas

I can't actually see meaningful structure in this image beyond a dotted/halftone pattern — I don't have access to the underlying tile alpha data, only the rendered pixels. So I can't reliably flag "dead tiles" (all-zero alpha) from the image alone.

What I can report from visual inspection:

**Observed coverage (qualitative):**
- **Top band (~rows 1–4):** Sparse orange/peach dots on white — looks like **low coverage**, not the near-100% you'd expect post-Phase 14c.
- **Middle band (~rows 5–13):** Dense grey/black clustered glyphs — **high coverage**, consistent with cascade 1 saturation.
- **Bottom band (~rows 14–18):** Medium-density dark dots fading to sparse — **partial coverage**, tapering off at the very bottom.

**Candidate dead-tile regions (visual only, need numeric confirmation):**
- Upper-left and upper-right corners (white gaps between orange dots).
- Bottom-row tiles (last 1–2 rows appear very sparse / possibly zero-alpha).
- Thin white "seam" rows separating the three bands — possible row of dead tiles around the band boundaries.

**Recommendation:**
To actually flag dead tiles, run:

```python
dead = np.argwhere(tiles[..., 3].sum(axis=(-1,-2)) == 0)
```

against the cascade-1 tile buffer and compare to cascade-0's dead set. If you can share the alpha array (or a per-tile coverage CSV), I can do the diff and flag the regressions vs C0 directly.

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What you should see
A **smoothly varying scalar/color field** across the probe grid, where each probe's value represents the integral (or average) of incoming radiance over all directions:

- **Gradients** that follow the room's lighting topology — brighter near windows, lamps, or emissive surfaces; darker in corners, under furniture, behind occluders.
- **Spatial coherence**: neighboring probes should differ by small amounts (low-frequency signal, since irradiance is a smoothed quantity).
- **Color tint** consistent with light source chromaticity (e.g., warm near incandescent, cool near sky portals).

## Red flags and likely causes

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Rays missing geometry (BVH/scene not bound), zero sample count, miss shader returning 0, NaN→clamp, or probes placed inside solid geometry with backface culling discarding all hits |
| **Sharp grid steps / blocky discontinuities** | Per-probe seed reuse causing correlated noise, incorrect probe indexing (off-by-one in 3D→1D flattening), wrong stride/pitch when reading the texture, or visualization sampling nearest-probe instead of trilinear |
| **Wrong colors** (e.g., magenta, oversaturated, inverted) | sRGB ↔ linear mismatch, channel swizzle (RGB vs BGR), HDR values not tonemapped, negative radiance from MIS weight bug, or reading the wrong texture/mip |

## Quick checks

1. **Dump raw values** for a few probes — confirm finite, non-negative, plausibly scaled (e.g., 0–10 in linear units for typical interiors).
2. **Disable directional integration** — render a single direction first; if that's black, the trace itself is broken.
3. **Compare to a reference**: place one probe directly under a known light; its averaged radiance should roughly equal `light_intensity / (4π · distance²)` times surface albedo factors.
4. **Visualize as a slice** through the volume — discontinuities will reveal indexing bugs immediately.
5. **Check sample count** per probe — too few rays (< ~64) gives visible noise that can masquerade as grid stepping.

Want me to look at a specific failure mode, or help write a debug shader that overlays probe values on the scene?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the right wall as horizontal/curved banded streaks, and on the ceiling lights area where blocky stepped patterns appear. The indirect lighting shows clear grid-aligned discontinuities.

2. **Cascade boundary seams** — Ring-shaped concentric arcs are clearly visible on the right wall, sweeping from upper-right to lower-right. These curved discontinuities are characteristic of cascade interval transitions.

3. **Directional bin banding** — The bright window/light source on the left shows hard angular steps in the cast illumination, with jagged ~30-40° wedges visible in the light spill on the floor and left-side geometry.

4. **Outer-wall drift** — The right wall darkens monotonically toward the bottom edge and brightens toward the top-right corner, inconsistent with the geometry.

5. **Shadow acne / aliasing** — Jagged sawtooth speckles along the floor reflections of the ceiling lights and along the base of the right wall (dark serrated edge).

6. **Possible color bleeding error** — The orange/salmon tint dominates the right wall but appears to leak unnaturally onto distant ceiling regions without a clear bouncing source.

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts (cascade seams, probe banding, directional binning, and edge aliasing) that are simultaneously visible and significantly degrade perceived image quality. Cascade count, probe density, and angular resolution all appear under-sampled.
