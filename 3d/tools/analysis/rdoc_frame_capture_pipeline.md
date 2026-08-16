# RenderDoc GPU Analysis

**Capture:** `D:/GitRepo-My/radiance-cascades-demo/3d/doc/cluade_plan/AI/captures/rdoc_frame_capture.rdc`  
**Analyzed:** 2026-05-05T02:18:22  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| glDispatchCompute() | dispatch | 11644.9 |
| glDispatchCompute() | dispatch | 193.6 |
| glDispatchCompute() | dispatch | 6912.0 |
| glDispatchCompute() | dispatch | 223.3 |
| glDrawArrays() | draw | 6008.5 |
| glDrawArrays() | draw | 1675.6 |
| glDrawElements() | draw | 11.3 |
| **Total** | | **26669.2** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this image as a signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

### 1. **Hard Seams / Sharp Discontinuities**
- The transition between the red shape and the black interior is abrupt — there's no smooth gradient falloff
- A proper SDF should show continuous value changes; here the boundaries look **binary/thresholded**

### 2. **Flat Regions (Voxelization Artifacts)**
- The black square in the center-right appears as a **uniform flat region** with no internal distance gradient
- The outer black border is also completely flat — indicating distance values were either clamped or never computed beyond a narrow band
- This is a classic sign of **narrow-band SDF truncation** or missing far-field computation

### 3. **Missing Geometry / Holes**
- The shape resembles a torii/gate or table structure, but the "legs" appear disconnected or incomplete
- There's a visible **hole/gap** between the top horizontal bar and the rectangular block on the right
- The left leg appears thinner/shorter than expected, suggesting **partial geometry loss during voxelization**

### 4. **Asymmetry Issues**
- If this shape is meant to be symmetric (gate-like), the right side has an extra solid block that breaks symmetry — possibly **incorrect distance computation** merging two separate primitives

### 5. **Resolution Problems**
- Heavy pixelation/blockiness along edges suggests the SDF was computed at **too low a resolution** for the feature size
- Fine details (thin bars) are being lost to the voxel grid

## Recommendations
- Increase voxel grid resolution
- Use **full-field** rather than narrow-band SDF computation
- Verify mesh is watertight before SDF generation
- Apply proper trilinear interpolation when sampling
- Check for inverted normals causing sign-flip errors in interior regions

## Albedo Volume

# Surface Color Inspection Report

## Detected Regions

| Region | Expected Color | Detected Color | Status |
|--------|---------------|----------------|--------|
| Left wall | Red | Red ✓ | OK |
| Right wall | Green | Green ✓ | OK |
| Background (top/bottom/center) | White | **Light Gray** (~#BFBFBF) | ⚠️ FLAGGED |

## Issues Found

### ⚠️ Incorrect Background Color
- **Expected:** White (#FFFFFF)
- **Observed:** Light gray (approximately #BFBFBF)
- **Affected areas:** All non-wall regions (top, bottom, and central pinch area between the two arrow/triangle shapes)

### Shape Observations
- Left red shape: Right-pointing arrow/chevron — color valid
- Right green shape: Left-pointing arrow/chevron — color valid
- The two shapes meet at the center forming an "X" or bowtie silhouette against the gray field

## Summary
**1 issue flagged:** The background fails the "white elsewhere" specification — it is rendered in light gray rather than pure white. Red and green wall regions pass inspection.

## C0 Probe Directional Atlas

I'll analyze this debug visualization of what appears to be a light probe / irradiance volume baking system.

## Visual Analysis

**Structure observed:**
- A grid of D×D tiles arranged in a larger grid pattern (probes in a volume)
- Outer ring of small tiles = boundary probes
- Inner region shows the main probe field
- Color-coded directional bins per probe (octahedral or cube-mapped encoding)

**Color interpretation:**
- 🔴 **Red tints** (left/upper-left probes): likely encoding one hemisphere direction or surface hits with warm bounce lighting
- 🟢 **Green tints** (right/upper-right probes): opposing hemisphere or different surface material contribution
- ⚪ **White/near-white** (lower probes): sky or high-luminance miss bins
- The smooth gradient red→green across the upper interior suggests **probes are baked correctly** and interpolating smoothly

## Health Check Against Your Flags

✅ **No uniform gray tiles** — every probe shows directional variation, so baking completed for all probes shown.

✅ **No random noise** — neighboring tiles vary smoothly (red gradually shifts to green left→right), indicating **merge/blend across probe boundaries is consistent**. No salt-and-pepper artifacts.

⚠️ **Lower half asymmetry** — the bottom rows are noticeably washed out / near-white compared to the colored upper region. This could mean:
- Those probes mostly see **sky (negative alpha)** — consistent if the volume's lower probes are above an open area
- Or surface hits there are very low-albedo
- Worth confirming alpha sign matches expected geometry

⚠️ **Inner "M" or arch-shaped dark cavity** — appears to be an occluder (wall/arch) correctly shadowing nearby probes. The dark bins around it likely represent **miss bins (alpha = 0)** inside solid geometry — expected.

## Verdict
Probe volume looks **healthy and baked**. Smooth neighbor coherence + directional variation + plausible occlusion shadow = good bake. 

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, post-Phase 14c)

## Observations

Looking at the rendered tile grid, I can assess coverage as follows:

**Coverage Estimate: ~78-82%** ⚠️ *Below near-100% expectation*

### Active Tiles
- **Red/pink channel** dominates the left and upper-left qu

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅

A **smooth spatial gradient** across the probe grid that reflects the room's lighting distribution:

- **Brighter probes** near light sources, windows, or emissive surfaces
- **Dimmer probes** in corners, under furniture, or in occluded regions
- **Gradual falloff** between lit and shadowed areas
- **Color tinting** consistent with nearby surfaces (e.g., warm near incandescent, cool near sky light, color bleeding from walls)

The result is essentially a low-frequency "irradiance cloud" — like a blurry 3D photograph of the room's light energy.

---

## Failure Flags 🚩

### 1. All-Black Probes
**Symptom:** Probes render as zero/black radiance everywhere.

**Likely causes:**
- Probe rays not hitting any geometry (miss shader returning 0)
- SH/octahedral coefficients not being written (buffer binding issue)
- Integration weight = 0 (e.g., divided by sample count before samples accumulated)
- Wrong texture format (e.g., reading from uninitialized atlas)
- Sky/environment radiance contribution disabled

### 2. Sharp Grid Steps
**Symptom:** Visible blocky discontinuities between adjacent probes; no smooth interpolation.

**Likely causes:**
- Trilinear interpolation disabled or broken in the shading path
- Probe positions misaligned with sampling coordinates (off-by-half-texel)
- Visibility/Chebyshev weights collapsing to a single probe
- Using nearest-neighbor lookup instead of weighted blend
- Cascade boundaries not blended (in DDGI/cascaded setups)

### 3. Wrong Colors
**Symptom:** Probes show colors that don't match the scene (e.g., green in a red room, oversaturated, NaN/inf artifacts).

**Likely causes:**
- **Channel swap** (RGB ↔ BGR) on upload or readback
- **Color space mismatch** (linear vs. sRGB double-conversion)
- **Tonemap applied twice** or applied before averaging
- **HDR clamping** before integration (clipping bright bounces)
- **NaN propagation** from divide-by-zero in BRDF or PDF
- Wrong cosine weighting / missing Jacobian in octahedral mapping

---

## Quick Debug Checklist

| Check | Action |
|---|---|
| Probe ray hits | Visualize first-bounce hit positions |
| Radiance accumulation | Print min/max/mean of probe atlas |
| Interpolation weights | Force single-probe lookup, compare |
| Color space | Inspect raw float values before display transform |
| Sample count | Ensure ≥ N samples before normalization |

Want me to dig into one of these failure modes in more detail (e.g., DDGI-specific, or a specific renderer)?

## Final Frame (from capture thumbnail)

# Radiance Cascades Render Analysis

## Artifacts Detected

**1. Outer-wall drift (prominent)**
The exterior faces of the Cornell Box are visible as ghosted/translucent slabs around the box silhouette — gray bars on top/bottom, red on the left exterior, green on the right exterior. The outer walls appear to be leaking light or rendered semi-transparently, suggesting either ray escape from the volume or incorrect handling of back-face/outer geometry.

**2. Color bleeding deficiency**
The interior shows surprisingly weak color bleeding given the saturated red/green walls. The white floor and ceiling near the colored walls should pick up noticeably more red/green tint. Indirect color transport appears under-contributed.

**3. Mild probe-grid softness / low-frequency blotching**
The ceiling and back wall show a soft, uneven luminance falloff (brighter center, dimmer corners) that looks slightly lobed rather than smoothly radiometric — consistent with sparse probe interpolation.

**4. Shadow softness inconsistency**
The two box shadows on the floor are present but very soft and low-contrast, while the contact regions under the boxes lack a strong darkening — borderline **missing contact shadow** / ambient occlusion under the cubes.

## Not Observed
- No cascade ring seams
- No directional bin banding (no ~36° hard angular steps)
- No shadow acne speckling
- No hard probe-grid lattice

## Quality Rating: **Fair**

The interior lighting is plausible and stable, but the **outer-wall drift** is a significant structural artifact, and indirect color bleeding is weaker than expected for a Cornell Box reference.
