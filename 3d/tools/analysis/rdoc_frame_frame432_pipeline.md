# RenderDoc GPU Analysis

**Capture:** `D:/GitRepo-My/radiance-cascades-demo/3d/tools/captures/rdoc_frame_frame432.rdc`  
**Analyzed:** 2026-05-05T23:22:42  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 7601.5 |
| Cascade reduction | dispatch | 216.3 |
| Raymarching | draw | 4853.8 |
| GI blur | draw | 12878.0 |
| glDrawElements() | draw | 10.3 |
| **Total** | | **25559.9** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this image as a signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

**1. Hard Seams / Sharp Discontinuities**
- The boundary between the bright red interior shape and the black background is extremely abrupt
- A proper SDF should show a smooth gradient transitioning through intermediate values
- The near-binary appearance (bright red → black) suggests the distance values are either clamped, thresholded, or the field was rendered as a mask rather than a true distance field

**2. Flat Regions**
- The interior of the shape appears uniformly saturated red with no internal gradient
- In a correct SDF, interior distances should grow progressively more negative toward the medial axis, producing a visible gradient peak at the shape's center
- The flat fill indicates either:
  - Distance values saturated/clipped at the visualization range
  - Only the surface band was computed (narrow-band SDF truncation error)
  - Interior distances were not computed (unsigned DF mistakenly used)

**3. Possible Voxelization Artifacts**
- The shape edges appear blocky/aliased rather than smoothly anti-aliased
- The vertical legs and horizontal top of the gate-like (torii) shape show staircase patterns consistent with low-resolution voxel sampling

**4. Missing Geometry / Holes**
- The dark gap between the two "legs" of the shape is correct *if* intentional, but the transition there lacks the smooth falloff expected from a valid SDF
- No visible isocontour rings around the geometry

## Recommendations
- Re-render using a continuous colormap mapped to the **full signed range** (e.g., blue=interior, white=zero, red=exterior)
- Verify the SDF was computed with a proper algorithm (e.g., fast marching, 8SSEDT) rather than a binary mask
- Increase voxel resolution to reduce staircasing
- Confirm interior distances are signed negative, not zeroed out

## Albedo Volume

# Surface Color Inspection Report

## Observed Colors
- **Left region**: Red wall ✓ (matches expected)
- **Right region**: Green wall ✓ (matches expected)
- **Background/elsewhere**: **Gray** ✗ (expected white)

## Flagged Issues

### ⚠️ Incorrect Background Color
- **Expected**: White (#FFFFFF)
- **Actual**: Light gray (~#BFBFBF / #C0C0C0)
- **Location**: All non-wall areas (top, bottom, and central "X" intersection zone)

### Shape Notes
- Red left shape: Arrow/chevron pointing right (inward) — color correct
- Green right shape: Arrow/chevron pointing left (inward) — color correct
- The two shapes form an hourglass/bowtie "X" pattern with gray (not white) filling the negative space

## Summary
| Region | Expected | Actual | Status |
|--------|----------|--------|--------|
| Left wall | Red | Red | ✅ Pass |
| Right wall | Green | Green | ✅ Pass |
| Background | White | Gray | ❌ Fail |

**Recommendation**: Repaint background regions to true white to meet specification.

## C0 Probe Directional Atlas

# Probe Tile Analysis

Looking at this image, I can identify it as a **probe atlas visualization** (likely an irradiance/radiance probe volume baked into D×D tiles per probe):

## Observations

**Overall structure:** A grid of probe tiles arranged in a 2D atlas, with each probe storing directional radiance samples in its D×D footprint.

**Color/Quality assessment:**
- **Center region:** Shows coherent **red-to-green gradients** transitioning smoothly across neighboring tiles — this indicates probes are **properly baked** with valid directional lighting data (red likely = +X hemisphere hits, green = +Y hemisphere hits).
- **Border tiles:** Appear as **uniform gray/white** — these probes are **not baked** (flagged condition #1). Likely they fall outside the bake volume or were culled.
- **No visible random noise** — so no merge errors detected. ✅

**Alpha interpretation per your spec:**
- **Positive α (surface hit):** The colored bins in the center — geometry was struck, radiance recorded.
- **Negative α (sky):** Likely the lighter/whitish bins at tile edges — rays escaped to sky.
- **Zero α (miss):** The flat gray border tiles — no rays cast or probe inact

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu,

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅

A **smooth spatial gradient** across the probe grid, where each probe's color reflects the average incoming light from all directions at that point in space:

- **Brighter probes** near windows, lamps, or emissive surfaces
- **Dimmer probes** in corners, under furniture, behind occluders
- **Gradual color shifts** matching the room's lighting tint (warm near incandescent, cool near skylights)
- **Soft falloff** with distance from light sources

## Red Flags 🚩

### 1. All-Black Probes
**Symptoms:** Entire grid (or large regions) renders pure black.

**Likely causes:**
- Probes never received any radiance samples (raygen not dispatched, or 0 rays/probe)
- Accumulation buffer cleared every frame without integration
- Wrong texture binding / sampling from uninitialized memory
- Light sources not in BVH, or emission missing from material eval
- NaN/Inf from a bad BRDF eval being clamped to 0

**Check:** Log a single probe's accumulated sum; verify ray hits return non-zero `Le + direct`.

### 2. Sharp Grid Steps
**Symptoms:** Visible blocky discontinuities between adjacent probes; lighting "pops" at probe boundaries.

**Likely causes:**
- No trilinear interpolation between probes during shading
- Probe spacing too large for the scene's lighting frequency
- Visibility/occlusion weighting missing → leaking probes get full weight
- Per-probe sample count too low (high variance → neighbor mismatch)
- Wrong probe-index → world-position mapping (off-by-one in grid coords)

**Check:** Render with 8× the probe density; if discontinuities shrink proportionally, it's spacing/interpolation. If they persist, it's an indexing bug.

### 3. Wrong Colors
**Symptoms:** Tints that don't match scene lights (e.g., magenta, green where white expected).

**Likely causes:**
- **Channel swap** (RGB↔BGR) on upload or readback
- **Color space mismatch** — sRGB texture sampled as linear, or vice versa
- **Tonemap applied twice** (or not at all when comparing to reference)
- **HDR clamp** losing chromaticity on bright pixels
- SH/octahedral encoding band coefficients in wrong order
- Missing division by sample count (raw sum, not average)

**Check:** Place a probe directly in front of a known pure-white emitter; the readout should be neutral (R=G=B) within noise.

## Quick Triage Order

1. Pick one probe at a known location → dump its raw accumulated value
2. Verify `radiance / sampleCount` is finite and non-zero
3. Compare against a path-traced reference at the probe's position
4. Toggle interpolation off → should see flat per-probe cells (sanity check)

Want me to look at the actual capture/integration shader code, or the

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Outer-wall drift / light leaking** — The most prominent issue. The red and green wall colors are visibly bleeding *outside* the box silhouette (red halo on the left exterior, green halo on the right exterior), and the gray top/bottom outer walls extend past the box bounds. This indicates probe radiance leaking beyond geometry boundaries.

2. **Color bleeding errors** — The red and green wall tints appear weaker than expected on the floor/ceiling near those walls, and there's minimal colored bounce onto the white boxes. The colored light seems to escape outward (see #1) rather than bouncing inward.

3. **Mild probe-grid softness** — The ceiling light falloff and the gradient on the back wall show slightly blotchy, low-frequency variation consistent with sparse probe interpolation, though no hard grid lines are visible.

4. **Shadow softness/missing contact** — Contact shadows under the two boxes are present but very soft; the taller box's shadow on the floor is somewhat detached, suggesting limited near-field occlusion resolution.

**Not observed:** No cascade ring seams, no directional bin banding (~36°) steps, no shadow acne speckles.

**Quality rating: Fair**

The interior lighting is plausible and smooth, but the significant outer-wall radiance drift (colored leakage outside the Cornell Box silhouette) is a clear correctness bug that drags the overall quality down from Good.
