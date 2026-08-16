# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame421.rdc`  
**Analyzed:** 2026-05-11T15:18:38  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 4460.5 |
| Cascade reduction | dispatch | 24.6 |
| Cascade bake | dispatch | 20038.1 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 16564.6 |
| Cascade reduction | dispatch | 160.4 |
| Cascade bake | dispatch | 33210.8 |
| Cascade reduction | dispatch | 225.3 |
| Raymarching | draw | 1967.4 |
| GI blur | draw | 227.3 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **77046.8** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/discontinuities**: The image shows very abrupt transitions between the dark (interior/negative) regions and the red (exterior/positive) regions. A proper SDF should exhibit a smooth gradient radiating outward from the surface, but here the red appears nearly uniform in saturation.

2. **Flat regions**: The outer red area looks largely flat in value rather than showing a smooth falloff with distance. This suggests the SDF may be **clamped, binarized, or under-sampled** — behaving more like an occupancy mask than a true distance field.

3. **Missing/black geometry interior**: The dark central structure has sharp, blocky black areas with what look like vertical slits and rectangular voids. These could indicate:
   - **Holes in the geometry** (unclosed meshes producing undefined inside/outside)
   - **Voxelization artifacts** where thin features collapse into stair-stepped blocks
   - Incorrect sign assignment causing interior pockets to read as far-negative

4. **Aliasing on edges**: The boundary between black and red is pixel-sharp with no anti-aliased or sub-voxel transition band — another sign of low-resolution voxel sampling rather than analytic SDF computation.

5. **Symmetry breaks**: The vertical "pillars" on the left and right are not perfectly mirrored, and the central horizontal band shows irregular tooth-like spacing, hinting at sampling misalignment with the underlying geometry grid.

**Likely issues:**
- SDF resolution too low (voxelization)
- Distance values clamped to a narrow range, losing the gradient
- Source mesh has non-manifold edges or holes producing undefined interior distances
- Possibly storing only sign (binary mask) rather than true distance magnitudes

**Recommendation**: Increase SDF grid resolution, verify the source mesh is watertight, and ensure the visualization maps the full distance range (not a clamped/binary version) to color.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings — Multiple Incorrect/Missing Regions Flagged

**🚩 Left Wall — FAIL**
- Expected: Red
- Actual: Beige/tan with no red pigment detected
- Status: Missing red coloration entirely

**🚩 Right Wall — FAIL**
- Expected: Green
- Actual: Beige/tan matching left wall
- Status: Missing green coloration entirely

**🚩 Other Surfaces — PARTIAL FAIL**
- Expected: White
- Actual: Predominantly yellow/cream interior with scattered multi-colored elements (purple, green, orange, red spots)
- Status: Base is off-white at best; foreign color regions present

**🚩 Surrounding Area — FAIL**
- Expected: White (implied background)
- Actual: Dark brown/maroon border surrounding the structure
- Status: Non-conforming background color

## Summary
| Region | Expected | Detected | Result |
|--------|----------|----------|--------|
| Left wall | Red | Tan | ❌ |
| Right wall | Green | Tan | ❌ |
| Interior | White | Yellow + mixed | ❌ |
| Background | White | Brown | ❌ |

**Overall: Color scheme does not match specification in any inspected region.**

## C0 Probe Directional Atlas

I'll analyze this image based on your description of probe tiles for what appears to be a light probe atlas (likely for irradiance or radiance caching in rendering).

**Observations:**

1. **Overall structure**: The image shows a grid of small DxD tiles arranged in an atlas layout — consistent with a baked probe volume (e.g., DDGI, irradiance volume, or reflection probe atlas).

2. **Smooth variation**: Most interior tiles appear to have **smoothly varying patterns** between neighbors, which is the expected/healthy signature — adjacent probes sample similar nearby geometry, so their encoded directions should correlate.

3. **No uniform gray tiles spotted**: I don't see obvious solid-gray cells, so probes appear to be **baked** rather than skipped.

4. **No high-frequency random noise blocks**: I don't see tiles that look like TV static, so there's no obvious **merge/atlas-write race condition**.

5. **Border rows/columns**: The top, bottom, and side margins show denser, more repetitive patterns — these look like the **border/gutter texels** used for bilinear filtering across probe boundaries (octahedral encoding gutters). They appear consistent.

6. **Symmetry**: The atlas shows left-right symmetry in the central region, suggesting the captured scene it

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe: Diagnostic Guide

This is a standard debug visualization for **irradiance probe volumes** (light probes / DDGI / radiance cascades). Here's how to read and validate it:

## What You Should See ✓

- **Smooth spatial gradients** — irradiance is a low-frequency signal, so neighboring probes should differ only slightly
- **Color tint matching the room's lighting** (warm near incandescent, cool near windows, neutral in shadowed areas)
- **Brighter probes near light sources / windows**, darker probes deep in occluded regions
- **Gradual falloff** through doorways and around occluders

## Red Flags 🚩

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Probe never integrated samples (zero ray hits, NaN accumulation, cleared but not written, or hysteresis stuck at 0). Check ray dispatch, miss shader sky contribution, and atomic/blend writes. |
| **Sharp grid steps** between adjacent probes | No temporal accumulation, wrong probe spacing vs. scene scale, probes stuck inside geometry (relocation failed), or per-probe normalization differs (divide-by-sample-count bug) |
| **Wrong colors** (e.g., magenta, oversaturated, gamma-wrong) | sRGB/linear mismatch on store or display, wrong texture format (R11G11B10F clamping), tonemap applied twice, or BRDF/π factor missing in the integrator |

## Quick Checks

1. **Isolate one probe** — dump its octahedral map; the average should equal the visualized texel.
2. **Disable temporal blending** — if gradient appears, hysteresis/feedback is the culprit; if still blocky, integration is wrong.
3. **Force a constant sky** (e.g., white miss shader) — every non-occluded probe should converge to the same gray. Black probes here = ray/dispatch bug.
4. **Check probe classification** — "inactive" or "inside geometry" probes are often skipped and read as 0.
5. **Verify units** — radiance vs. irradiance (factor of π), and whether you're storing pre- or post-cosine-weighted values.

If you can share a screenshot or describe which failure mode you're seeing, I can narrow it down further.

## Final Frame (from capture thumbnail)

# Artifact Analysis

Looking at this rendered frame of what appears to be a corridor-style scene:

## Identified Artifacts

1. **Heavy motion blur / temporal smearing** (right side) — The right wall and foreground show extreme blurring, likely from temporal accumulation lagging behind camera motion. Not strictly in your artifact list, but dominates the image.

2. **Outer-wall drift** — Visible along the ceiling and right wall, where brightness drifts monotonically toward the edges, with the right side blowing out into a bright orange/white smear.

3. **Color bleeding errors** — Strong orange/brown tint saturates surfaces that should likely be more neutral (ceiling tiles and right wall), suggesting indirect light is over-contributing warm bounce color across surfaces it shouldn't reach this strongly.

4. **Probe-grid banding** — Faint blocky luminance patches visible on the ceiling panels and along the floor reflections, consistent with low-resolution probe interpolation.

5. **Cascade boundary seams** — A subtle ring/transition is visible in the mid-distance where the corridor lighting shifts abruptly from warm orange to darker tones.

6. **Shadow acne** — Speckled dark dots scattered on the floor near the foreground.

## Quality Rating: **Poor**

The frame suffers from multiple compounding issues: severe smearing on the right half, oversaturated color bleed, and visible probe/cascade structure. At 54 FPS the renderer is performant, but visual fidelity is significantly compromised.
