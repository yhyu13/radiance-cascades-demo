# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame383.rdc`  
**Analyzed:** 2026-05-11T15:41:25  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 5506.3 |
| Cascade reduction | dispatch | 27.7 |
| Cascade bake | dispatch | 17356.8 |
| Cascade reduction | dispatch | 157.8 |
| Cascade bake | dispatch | 23236.6 |
| Cascade reduction | dispatch | 168.3 |
| Cascade bake | dispatch | 15584.2 |
| Cascade reduction | dispatch | 223.7 |
| Raymarching | draw | 9660.4 |
| GI blur | draw | 8477.5 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **80409.5** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

### 1. **Hard Seams / Flat Regions**
- The image shows large areas of **uniform red** with no gradient variation, particularly in the outer regions
- A proper SDF should display **smooth radial gradients** emanating from surfaces
- The flat red zones indicate the distance values are **clamped or saturated**, losing precision information

### 2. **Sharp Discontinuities**
- There are **abrupt black-to-red transitions** along the geometry edges with no transitional gradient band
- This is characteristic of **voxelization artifacts** — likely a low-resolution grid or nearest-neighbor sampling
- A correct SDF would show a smooth falloff over multiple pixels

### 3. **Holes / Missing Geometry**
- The dark central structure appears **fragmented** with disconnected black regions
- The horizontal bar of small dots/holes suggests **incomplete surface reconstruction** or aliasing
- Vertical black columns on the sides appear truncated and don't connect smoothly

### 4. **Inconsistent Sign Convention**
- The interior (black) regions don't show graduated negative distances — they appear binary
- Expected behavior: interior should darken progressively toward the medial axis

## Likely Causes

| Issue | Probable Cause |
|-------|---------------|
| Flat red exterior | Distance clamping at low max value |
| Hard seams | Voxel grid resolution too coarse |
| Fragmented geometry | Mesh has non-manifold edges or self-intersections |
| Binary interior | Unsigned distance computed instead of signed |

## Recommended Fixes

1. **Increase voxel resolution** for the SDF grid
2. **Remove distance clamping** or extend the clamp range
3. **Repair source mesh** — check for holes, flipped normals, non-manifold geometry
4. **Use proper signed distance algorithm** (e.g., generalized winding number or fast sweeping) rather than unsigned + flood fill
5. **Apply narrow-band refinement** near the surface for sub-voxel accuracy

This SDF is **not production-ready** and would cause artifacts in raymarching, collision detection, or mesh extraction (marching cubes).

## Albedo Volume

# Surface Color Inspection Report

## Expected Color Scheme
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings — Incorrect/Missing Color Regions

### ❌ Left Wall
- **Expected:** Red
- **Observed:** Pale yellow/cream tone with brown/orange streaks at top
- **Status:** FAIL — No red coloration detected

### ❌ Right Wall
- **Expected:** Green
- **Observed:** Pale yellow/cream tone, similar to left wall
- **Status:** FAIL — No green coloration detected

### ❌ Back/Other Surfaces
- **Expected:** White
- **Observed:** Yellowish interior with multi-colored spots (purple, yellow, green, red specks) on the floor
- **Status:** FAIL — Surface is not white; contains scattered chromatic noise

### ⚠️ Additional Anomalies
- **Top edge:** Brown/orange irregular border (likely ceiling bleed or texture artifact)
- **Outer frame:** Dark brown background dominates — not part of the Cornell-box-style surface set
- **Floor:** Contains colored point artifacts inconsistent with a uniform white diffuse surface

## Summary
**All three primary surface regions fail the expected color assignment.** The image does not exhibit the canonical red-left / green-right / white-remainder configuration. Recommend verifying:
1. Material assignments on wall geometry
2. Lighting white balance (scene appears warm-tinted)
3. Render/texture resolution — colored floor specks suggest noise or incorrect texture mapping

## C0 Probe Directional Atlas

I'll flag the issues per your spec:

**Observations on the lightmap atlas:**

- **Uniform gray tiles (probe not baked):** Several tiles along the **top row** and scattered along the **right edge** appear as flat gray patches with no detail — these probes failed to bake.
- **Random noise (merge error):** A few tiles near the **upper-left** and along the **bottom border** show speckled high-frequency noise inconsistent with neighbors — likely atlas merge/seam errors.
- **Smooth neighbors:** The central region tiles transition smoothly (good gradient continuity) ✅.

**Alpha channel summary:**
- **Positive α (surface hits):** dominant central block — valid probe coverage.
- **Negative α (sky):** thin band along the outer frame — expected skybox samples.
- **Zero α (misses):** the gray/unbaked tiles correlate with miss regions — probes likely placed inside geometry or outside the scene bounds.

**Recommended actions:**
1. Re-bake the gray tiles (check probe placement isn't inside collision).
2. Re-run atlas merge for the noisy tiles (verify tile stride = D and no off-by-one in UV packing).
3. Validate sky probes' negative-α sign convention is consistent across the border.

Want me to estimate tile coordinates (row, col) for the bad probes so you can target a partial re-bake?

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 14c)

## Visual Assessment

Looking at the

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✓
- **Smooth spatial gradients** across the probe grid
- Brighter probes near light sources (windows, lamps, emissive surfaces)
- Darker probes in occluded areas (under furniture, in corners)
- Color tints matching nearby bounce surfaces (warm near wood, cool near sky-lit areas)
- Gradual falloff with distance from emitters

## Red Flags 🚩

### All-Black Probes
**Likely causes:**
- Probes failed to gather/integrate radiance (zero ray hits or all rays missed scene)
- Probes spawned **inside geometry** → backface culled or self-occluded
- Missing emissive contribution / sky not sampled
- Integration divided by wrong normalization (e.g., divide-by-zero → NaN → clamped to 0)
- Atlas/texture not bound or wrong mip level read

### Sharp Grid Steps
**Likely causes:**
- **No trilinear interpolation** between probes (nearest-neighbor lookup)
- Probe spacing too large for scene's lighting frequency
- Visibility/Chebyshev weights snapping hard between probes
- Missing or incorrect probe **blending weights** (normals/backface test rejecting all but one probe)
- Quantization artifacts from low-bit storage (e.g., RGB8 without proper encoding)

### Wrong Colors
**Likely causes:**
- **Color space mismatch** (sRGB stored as linear or vice versa)
- Channel swap (RGB ↔ BGR) in upload or readback
- HDR values clipped or tone-mapped before storage
- Wrong encoding format (RGBM/RGBE decode mismatch)
- Gamma applied twice
- Albedo/irradiance confusion (surface color leaking into radiance)

## Quick Triage Checklist
1. Visualize **single probe** → is its octahedral/cubemap data sane?
2. Disable interpolation → confirm per-probe values look right in isolation
3. Check probe **placement** vs. scene geometry (offset probes out of walls)
4. Verify **ray hit rate** > 0 for suspect probes
5. Compare against a **reference path-traced** irradiance sample at probe location

Want me to dig into a specific failure mode you're seeing?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible as blocky/blurry rectangular patches across the ceiling and upper walls, particularly the brown/red smeared blocks on the ceiling. The indirect lighting shows clear grid-aligned interpolation artifacts.

2. **Color bleeding errors** — The right wall shows excessive red/brown bleeding that appears too saturated and extends unnaturally far from any red source. The left wall shows the opposite — washed-out white bleeding dominates where some color contribution would be expected.

3. **Outer-wall drift** — The right wall darkens monotonically toward the upper-right corner, and the left wall brightens toward the camera, suggesting cascade contribution falloff at frame edges.

4. **Directional bin banding** — The streaky white highlights on the floor (the elongated dash-like specular/light shapes) suggest hard angular steps in the directional bins rather than smooth light distribution. The light reflections are fragmented into discrete directional slivers.

5. **Cascade boundary seams** — Faint ring-like transition visible mid-corridor where the brown wall coloration shifts in intensity bands along the right wall depth.

6. **Missing/incorrect shadows** — The ceiling fixtures appear to float with broken occlusion; their shadows don't ground them properly to the ceiling geometry.

**Quality Rating: Poor**

The frame suffers from heavy probe interpolation blur, fragmented directional sampling on the floor, and unbalanced color bleeding that obscures the underlying Cornell Box geometry. The scene reads more as a smeared painterly blur than as coherent global illumination.
