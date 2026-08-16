# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame350.rdc`  
**Analyzed:** 2026-05-11T15:36:51  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 69589.1 |
| Cascade reduction | dispatch | 27.4 |
| Cascade bake | dispatch | 64483.2 |
| Cascade reduction | dispatch | 159.6 |
| Cascade bake | dispatch | 63399.9 |
| Cascade reduction | dispatch | 170.8 |
| Cascade bake | dispatch | 75441.1 |
| Cascade reduction | dispatch | 226.8 |
| Raymarching | draw | 46117.1 |
| GI blur | draw | 114191.0 |
| glDrawElements() | draw | 11.7 |
| **Total** | | **433817.8** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a Signed Distance Field (SDF) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: There's a very abrupt transition between the dark (interior/negative) region and the bright red (exterior/positive) region. A proper SDF should show a smooth gradient radiating outward from the surface, but here the red appears nearly uniform in saturation.

2. **Flat regions**: The bright red background area looks largely flat/saturated rather than showing distance falloff. This suggests either:
   - Distance values are clamped too aggressively
   - The visualization range is compressed
   - The SDF wasn't computed beyond a narrow band

3. **Voxelization artifacts**: The interior shows blocky, pixelated structures (the column-like shapes and horizontal bar) with hard edges rather than smooth distance gradients. True SDF interiors should darken smoothly toward the medial axis.

4. **Missing geometry / holes**: 
   - The horizontal bright red band cutting through the middle of the dark region looks suspicious — it may indicate a hole or incorrectly classified inside/outside region
   - The small bright dots inside the dark columns suggest isolated misclassified voxels (holes in the geometry)

5. **Inside/outside inconsistency**: The thin red strips appearing inside what should be solid interior regions suggest sign errors in the SDF computation — likely from non-watertight input geometry or incorrect winding.

**Recommended fixes:**
- Verify the input mesh is watertight and has consistent normals
- Increase SDF computation bandwidth or remove clamping
- Use higher voxel resolution to reduce blockiness
- Apply flood-fill to fix isolated sign errors
- Check the visualization's value range maps appropriately to the actual distance values

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color requirements:

**Expected:**
- Left wall: Red
- Right wall: Green
- Other surfaces: White

**Findings — Issues Flagged:**

🚩 **Left wall**: INCORRECT — Appears in pale yellow/cream tones rather than red.

🚩 **Right wall**: INCORRECT — Appears in similar yellow/beige tones, not green.

🚩 **Other surfaces (floor/back)**: INCORRECT — The interior shows mixed colors (yellow base with scattered purple, green, and orange spots) rather than white.

**Additional observations:**
- The surrounding/background area is brown (appears to be outside the target inspection zone).
- The top edge shows irregular tan/yellow protrusions.
- No surface in the structure matches the required color specification.

**Conclusion:** All three color regions fail the specification. The structure appears to use a warm yellow/cream palette with multicolored interior detailing instead of the required red/green/white scheme. Recommend full re-coloring or verification that the correct reference image was provided.

## C0 Probe Directional Atlas

I can't actually see fine detail in that thumbnail, but based on your description, here's how to interpret what you're looking at:

**What you're inspecting:** An irradiance/radiance probe atlas (likely DDGI or similar), where each probe's spherical irradiance is encoded into a small DxD tile (often octah

## C1 Probe Directional Atlas

# Cascade 1 Analys

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe: Diagnostic Guide

## What You Should See

A **direction-averaged radiance** visualization collapses each probe's directional irradiance into a single RGB value (the spherical average), then displays one colored dot/cell per probe position. Expected characteristics:

- **Smooth spatial gradients** — radiance varies continuously between neighboring probes since lighting is a low-frequency signal
- **Color tints matching the room** — warm near incandescent lights, cool near windows, neutral in shadowed interior regions
- **Brightness falloff** — probes near light sources are brighter; occluded/under-furniture probes are dimmer
- **Soft transitions across walls** — only sharp where probes straddle an opaque boundary

## Red Flags & Likely Causes

### 🚩 All-black probes
| Cause | Check |
|---|---|
| Probes never received gather/trace pass | Verify probe update kernel dispatched; check probe count > 0 |
| Sky/emissive missing from gather | Confirm emissive surfaces and skybox sampled in ray hits |
| Wrong buffer bound for readback | Check SRV/UAV binding and frame index (read-after-write hazard) |
| HDR clamped to zero | Inspect for `max(0, x)` on negative NaN/Inf inputs |
| Probes inside geometry | Backface hits returning zero — add relocation/offset pass |

### 🚩 Sharp grid steps (visible cell boundaries)
| Cause | Check |
|---|---|
| No interpolation in viz shader | This may be *correct* for per-probe display; only an issue if you expected trilinear sampling |
| Probe spacing too coarse vs. lighting frequency | Increase probe density or filter radius |
| Per-probe normalization differs | Ensure consistent sample count / weight accumulation |
| Hysteresis blending broken | Some probes converged, others not — check temporal alpha |

### 🚩 Wrong colors
| Symptom | Likely cause |
|---|---|
| Magenta/green dominant | Channel swizzle (BGR vs RGB) in storage or display |
| Washed out / oversaturated | Missing tonemap, or double gamma applied |
| Inverted (dark where bright should be) | Cosine weight sign error, or octahedral decode flipped |
| Single-channel only | sRGB view on linear texture, or alpha written to color |
| Uniform gray | Averaging over wrong axis (all directions including back hemisphere with sign error) |

## Quick Validation Steps

1. **Place one bright colored emitter** in an empty room → confirm gradient points away from it with correct hue
2. **Compare a single probe** against a CPU-computed reference for a known direction
3. **Toggle direction average vs. full octahedral** — averaged should equal the mean of the octahedral texels
4. **Disable temporal blending** to isolate per-frame correctness from convergence issues

Want me to go deeper on any specific failure mode (e.g

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Highly visible across the ceiling and upper walls as horizontal stripe/dash patterns (the brown segmented marks). The regular spacing reveals the underlying probe grid sampling.

2. **Probe-grid banding (floor)** — The dashed white/bright streaks running down the center of the floor toward the vanishing point are aligned probe samples bleeding through, not a continuous specular highlight.

3. **Color bleeding errors** — The right wall and far-right region show excessive warm brown saturation that doesn't correspond to a clear colored-wall source; indirect light appears to be smeared/oversampled from the wrong direction.

4. **Outer-wall drift** — The left wall shows monotonic darkening/brightening toward the edges with a soft gradient that doesn't match expected Cornell box illumination.

5. **Cascade boundary seam** — A visible discontinuity in the mid-distance on the left wall where blur/resolution character changes abruptly (sharp pixelated region transitioning to smooth blur).

6. **Missing/broken occlusion** — The dark blob in the center-left (near the doorway/opening) has hard black edges with no soft shadow falloff, suggesting occlusion sampling failure rather than a proper shadow.

**Quality rating: Poor**

The frame suffers from severe probe aliasing on nearly every surface, with the dashed-line pattern on ceiling and floor being especially diagnostic of undersampled cascade probes. Indirect lighting is also incorrectly tinted across large regions.
