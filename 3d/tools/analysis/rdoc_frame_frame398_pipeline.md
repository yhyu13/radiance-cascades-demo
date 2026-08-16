# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame398.rdc`  
**Analyzed:** 2026-05-11T15:35:22  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 18989.2 |
| Cascade reduction | dispatch | 27.1 |
| Cascade bake | dispatch | 16077.8 |
| Cascade reduction | dispatch | 156.2 |
| Cascade bake | dispatch | 21142.7 |
| Cascade reduction | dispatch | 167.9 |
| Cascade bake | dispatch | 8624.2 |
| Cascade reduction | dispatch | 227.0 |
| Raymarching | draw | 7324.3 |
| GI blur | draw | 9051.1 |
| glDrawElements() | draw | 9.2 |
| **Total** | | **81796.7** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this image as a signed distance field visualization, I can identify several **significant issues**:

## 🔴 Critical Problems

**1. Lack of Smooth Gradient**
- A proper SDF should display a smooth radial falloff from surfaces
- This image shows **near-binary values** (saturated red vs. near-black) with almost no transitional gradient
- This suggests the SDF was either thresholded, quantized, or improperly normalized

**2. Hard Seams / Flat Regions**
- The large uniform red border region is suspiciously flat — distance values should continue increasing away from geometry
- The black interior shapes have **sharp, hard edges** with no isoband transitions
- Indicates voxelization artifacts or that only a narrow band was computed

**3. Possible Holes / Missing Geometry**
- The repeating black rectangular structures (appears to be a building/facade with windows) show **inconsistent thickness**
- Small bright red speckles inside the dark regions suggest **holes in the surface mesh** or incorrect inside/outside classification
- The thin horizontal band of small rectangles (windows?) may be **under-resolved** relative to voxel size

**4. Sign Errors**
- Bright red pixels appearing inside what should be solid geometry indicate **inverted sign** in localized regions
- This is typical of non-watertight input meshes causing flood-fill failures

## Recommended Fixes
- Verify mesh is **watertight** before SDF generation
- Increase **voxel resolution** — fine features (window mullions) appear undersampled
- Use a **wider narrow band** or full SDF computation
- Apply **generalized winding numbers** instead of ray-casting for inside/outside tests
- Re-normalize output and visualize with a proper diverging colormap to confirm gradient continuity

Would you like suggestions for specific SDF generation libraries that handle these cases robustly?

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Detected Issues

**❌ Left Wall - INCORRECT**
- Expected: Red
- Observed: Pale yellow/cream tones with brown/tan shading
- Status: **FLAGGED** — no red pigment present

**❌ Right Wall - INCORRECT**
- Expected: Green
- Observed: Cream/white with tan accents
- Status: **FLAGGED** — no green pigment present

**⚠️ Interior/Other Surfaces - PARTIAL**
- Expected: White
- Observed: Mostly off-white/cream, but contains multiple colored elements (purple, yellow, green, red dots — appears to be decorative/food items)
- Status: **FLAGGED** — colored inclusions present where white expected

**❌ Background - OUT OF SPEC**
- Observed: Brown (surrounding area)
- Not addressed in color spec

## Summary
| Region | Expected | Actual | Status |
|--------|----------|--------|--------|
| Left wall | Red | Cream/tan | ❌ Missing |
| Right wall | Green | Cream/tan | ❌ Missing |
| Center/other | White | White + multicolor specks | ⚠️ Contaminated |
| Background | (n/a) | Brown | — |

**Overall: Color scheme does not match specification.** The image appears to depict a decorated tray/platter rather than the described red/green/white surface configuration.

## C0 Probe Directional Atlas

I'll analyze this image based on the technical context you've provided about probe-baked tiles.

**Observations:**

- **Layout**: The image shows a grid of D×D tiles arranged in a rectangular atlas, typical of an irradiance/radiance probe bake (like DDGI or lightmap probe atlases).

- **Tile content**: Most tiles display structured, symmetric patterns (diamond/cross-like motifs) rather than uniform gray or random noise — this suggests the probes **are baked** and **not corrupted by merge errors**.

- **Smoothness between neighbors**: The patterns appear consistent and repeat coherently across adjacent tiles, indicating **smooth spatial variation** — a healthy sign for probe interpolation.

- **Border tiles**: The outer ring of tiles (top, bottom, left, right edges) shows denser, more uniform dash/dot patterns. These likely correspond to probes whose samples mostly **miss geometry** or hit the **sky** (negative alpha bins dominating).

- **Interior tiles**: Show the rich diamond patterns — these probes have many **surface hits** (positive alpha), capturing nearby geometry detail.

- **Flags status**:
  - ❌ No uniform gray tiles visible → all probes appear baked
  - ❌ No random noise patches → no merge errors detected
  - ✅ Smooth neighbor-to-neighbor variation maintained

**Diagnosis**: The probe atlas looks **healthy**. The transition from sky/miss-dominated border probes to surface-hit interior probes is consistent with a scene bounded by open space around a central geometry cluster.

## C1 Probe Directional Atlas

# Cascade 1 Analysis (tMax ~1.0wu, Post-Phase 14c)

## Coverage Assessment

Scanning the tile grid for surface coverage and alpha integrity:

**Overall Coverage: ~94-96%** (below the expected near-100% threshold)

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What You Should See
A **smooth spatial gradient** across the probe grid that reflects the room's lighting distribution:
- Brighter probes near light sources (windows, lamps, emissive surfaces)
- Gradual falloff into shadowed areas (under furniture, corners)
- Color tint matching dominant illumination (warm near incandescent, cool near skylights)
- Soft transitions between adjacent probes (since irradiance is low-frequency)

## Red Flags & Likely Causes

### 🚩 All-Black Probes
| Symptom | Likely Cause |
|---|---|
| Entire grid black | Probes never received any ray hits / capture pass not dispatched |
| Isolated black probes | Probe inside geometry (failed visibility test, no valid samples) |
| Black after first frame only | Accumulation buffer cleared but not refilled; temporal blend writing zeros |
| Black in specific regions | Sky/emissive missing from BVH; miss shader returns 0 |

### 🚩 Sharp Grid Steps
| Symptom | Likely Cause |
|---|---|
| Visible blocky pattern | No trilinear interpolation when sampling probe volume |
| Hard seams between cascades | Cascade blending weights wrong or missing |
| Stair-stepping along axes | Probe spacing too large vs. lighting frequency |
| Discontinuities at probe boundaries | Backface/visibility weights not normalized; missing Chebyshev/depth test |

### 🚩 Wrong Colors
| Symptom | Likely Cause |
|---|---|
| Magenta/green tint | Channel swizzle (RGB↔BGR), or sRGB↔linear mismatch |
| Over-saturated | Tonemapping applied twice, or HDR not clamped before encoding |
| Washed out / gray | Octahedral encoding averaging across hemispheres incorrectly |
| Color leaks through walls | Missing visibility/depth probes; no occlusion in irradiance lookup |
| NaN sparkles → black | Division by zero in solid-angle weighting; unguarded `radiance / pdf` |

## Quick Checks
1. **Dump raw probe texture** — confirm data is non-zero before lookup
2. **Visualize one probe's full octahedral map** — verify it captures scene correctly
3. **Disable interpolation** — see actual stored values vs. sampled
4. **Check sample count per probe** — uneven counts cause brightness banding
5. **Verify world-space probe positions** — off-by-one in grid indexing is common

Want me to go deeper on any specific symptom or the encoding/sampling math?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Severe artifacts detected:**

1. **Massive black void (center-frame)** — A large unshaded/unsampled region dominates the center of the image. This appears to be a catastrophic ray-march failure or probe lookup miss, possibly NaN/uninitialized radiance values bleeding through cascade interpolation.

2. **Probe-grid banding (ceiling, upper-center)** — Visible stair-stepped/zigzag patterns along the ceiling edge where probes are sparsely sampled. The jagged horizontal bands at the top reveal the underlying probe grid resolution.

3. **Cascade boundary seams** — The hard transitions between the black void region and surrounding lit areas suggest discontinuities at cascade interval boundaries, particularly visible around the central black blob's silhouette.

4. **Outer-wall drift** — The right wall shows a strong monotonic darkening gradient toward the back, and the left wall shows excessive brightening toward the camera. The brightness falloff doesn't match expected Cornell Box light transport.

5. **Color bleeding errors** — The warm orange/brown tint dominates surfaces that should likely be neutral (right wall, ceiling reflections), suggesting misdirected indirect contributions.

6. **Stray bright speckles (floor, lower-center)** — Small white fragments along the floor resemble fireflies or shadow-acne inverse artifacts.

**Quality rating: Poor**

The render is largely unusable — the central void alone obscures most of the scene geometry, and multiple compounding cascade/probe failures are evident throughout.
