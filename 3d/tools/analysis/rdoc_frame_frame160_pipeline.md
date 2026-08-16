# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame160.rdc`  
**Analyzed:** 2026-05-11T15:24:17  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 6043.6 |
| Cascade reduction | dispatch | 27.5 |
| Cascade bake | dispatch | 10066.1 |
| Cascade reduction | dispatch | 164.4 |
| Cascade bake | dispatch | 22525.0 |
| Cascade reduction | dispatch | 159.7 |
| Cascade bake | dispatch | 12201.0 |
| Cascade reduction | dispatch | 225.4 |
| Raymarching | draw | 42145.8 |
| GI blur | draw | 23512.6 |
| glDrawElements() | draw | 10.5 |
| **Total** | | **117081.7** |


## SDF Volume (Signed Distance Field)

Looking at this image as an SDF (Signed Distance Field) visualization, I can identify several issues:

## Problems Detected

**1. Hard Seams / Sharp Discontinuities**
- There's an abrupt transition between the bright red exterior and the dark interior region. A proper SDF should show a smooth gradient radiating outward from the surface, not a binary-like jump.

**2. Flat Regions (Constant Distance Values)**
- The large uniform red border area shows no gradient variation, suggesting the distance field is clamped or the values saturated. Distance should continuously increase with distance from the surface.
- The black interior also appears mostly flat, indicating the interior distances aren't being computed properly (should get progressively more negative toward the medial axis).

**3. Voxelization Artifacts**
- The geometry shows blocky, pixelated edges rather than smooth isocontours
- Vertical "pillar" structures on the left and right sides have stair-stepped edges
- The horizontal band of small rectangles in the middle shows aliasing consistent with low-resolution voxel sampling

**4. Possible Holes / Missing Geometry**
- The small repeating rectangular features in the center band look fragmented — they may represent either intentional thin features that are under-resolved, or holes where the SDF failed to capture continuous geometry
- Gaps between the central elements suggest the voxel resolution is too coarse to represent thin walls

**5. Missing Smooth Gradient**
- Overall, the field reads as nearly binary (inside/outside) rather than as a true distance function. A healthy SDF slice would show smooth color/intensity ramps perpendicular to surfaces.

## Recommendations
- **Increase voxel resolution** to better capture the thin central features
- **Recompute distances** using a proper method (e.g., fast sweeping, fast marching, or brute-force for ground truth) rather than what appears to be an occupancy-based approximation
- **Verify normalization** — values may be clamped to a narrow range, hiding the gradient
- **Check for watertightness** in the source mesh; non-manifold geometry can cause inside/outside classification failures producing the flat dark interior

## Albedo Volume

# Surface Color Inspection Report

## Expected Color Scheme
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings

Analyzing the image, the structure appears to be a small architectural form (possibly a shrine/box) with decorative elements on top.

### ❌ Incorrect / Missing Color Regions:

1. **Left wall — FAIL**
   - Expected: Red
   - Observed: Pale yellow/cream tone
   - Status: Missing red coloration

2. **Right wall — FAIL**
   - Expected: Green
   - Observed: Pale yellow/cream tone (matches left wall)
   - Status: Missing green coloration

3. **Front/interior surface — PARTIAL**
   - Expected: White
   - Observed: Light beige/off-white with multicolored debris (purple, green, orange, yellow spots)
   - Status: Base tone acceptable, but contains foreign color artifacts

4. **Top surface — FAIL**
   - Expected: White
   - Observed: Tan/brown with dark streaks
   - Status: Incorrect base color

### Summary
**No walls display the required red or green coloration.** The entire object renders in warm earth tones (tan, beige, cream) rather than the specified red/green/white palette. Recommend re-checking material assignments or lighting conditions, as the surfaces appear uniformly desaturated.

## C0 Probe Directional Atlas

## Reflection Probe Atlas Diagnostic

What you're looking at is a **reflection probe atlas** (or irradiance/light-probe atlas) — a 2D texture where each **D×D tile** stores the baked environment captured by one probe in the scene. At runtime, shaders sample the tile belonging to the nearest probe(s) to compute reflections or indirect lighting.

### How to read it

- **Tiles = probes.** Each cell in the grid is one probe's encoded environment (often an octahedral or dual-paraboloid unwrap of a cubemap).
- **Neighboring tiles ≈ neighboring probe positions in space.** Because nearby probes see nearly the same world, their tiles should look **almost identical**, producing a smooth gradient across the atlas.
- **Bin alpha channel** carries hit classification:

## C1 Probe Directional Atlas

# Cascade 1 Analysis (tMax ~1.0wu, Post-Phase 14c)

## Surface Coverage Assessment

**Estimated coverage: ~92-94%** — below the expected near-100% threshold.

## Dead Tile Detection (all-zero alpha)

Scanning the tile grid for zero-alpha regions:

| Region | Approx. Coords (

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What You Should See
A **smooth, low-frequency spatial gradient** across the probe grid that mirrors the room's lighting distribution:

- **Bright probes** near windows, light fixtures, or strong emitters
- **Dimmer probes** in occluded corners, under furniture, behind walls
- **Gradual falloff** between bright and dark regions (since you're averaging over all directions, high-frequency directional detail collapses into a soft scalar field)
- **Color tint** consistent with dominant light sources (warm near incandescent, cool near skylights, neutral in mixed lighting)

## Red Flags & Likely Causes

| Symptom | Likely Cause |
|---|---|
| **All-black probes** | Probes never received any rays / capture pass didn't run; cleared but never written; HDR values clamped or NaN-flushed to zero; wrong texture bound; missing emissive surfaces in scene |
| **Sharp grid steps / blocky discontinuities** | Probes inside geometry (leaking into solid walls return zero); no neighbor blending or trilinear sampling; visibility/occlusion test killing valid contributions; probe placement straddling a wall |
| **Wrong colors** (e.g., magenta, green-only, inverted) | Channel swizzle (RGB↔BGR); sRGB vs linear mismatch on read or write; wrong texture format (e.g., RG16F instead of RGBA16F); uninitialized memory; tonemap applied twice or not at all |
| **Checkerboard pattern** | Atomic write race / interleaved update missing half the probes per frame |
| **Single bright probe, rest dark** | Indexing bug — all rays accumulating to probe 0 |
| **Uniform gray everywhere** | Averaging denominator wrong (dividing by total probes instead of per-probe sample count); or fallback constant being returned |

## Quick Validation Steps
1. **Log min/max/mean** of the probe buffer — should be > 0 and < some sane HDR ceiling (e.g., < 100).
2. **Check for NaN/Inf** explicitly before averaging.
3. **Visualize a single slice** of the 3D probe grid as a 2D image; gradient should be visually continuous.
4. **Compare to a reference**: render the scene with path tracing at a few probe locations and confirm the averaged radiance roughly matches.
5. **Toggle occlusion off** — if blockiness disappears, the visibility term is the culprit.

Want me to look at the actual probe visualization or the capture/averaging shader code?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Directional bin banding** — Strong angular color steps are visible on the ceiling and right wall, appearing as hard-edged stripe/chevron patterns of brown/orange against the lighter base color. These ~36° angular discontinuities are the dominant artifact.

2. **Probe-grid banding** — The floor shows a regular pattern of bright white pixel clusters arranged in a roughly grid-aligned sequence receding into the distance, indicating per-probe sample discontinuities in the indirect lighting.

3. **Color bleeding errors** — The right wall and ceiling are saturated with brown/red tones that appear too strong and incorrectly distributed (bleeding onto the ceiling in striped patches rather than as a smooth gradient). The left wall also shows unexpected warm tints near the floor edge.

4. **Outer-wall drift** — The left wall progressively brightens toward the camera and washes out to near-white, while the right wall darkens into deep brown — a monotonic luminance drift along the wall length.

5. **Cascade boundary seams** — Faint ring-like transitions are visible mid-corridor where the ceiling pattern density changes, suggesting a cascade level transition.

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts: the directional binning is severely undersampled, probe interpolation is failing on the floor, and the overall energy distribution between walls is incorrect. The scene is barely readable as a Cornell Box corridor.
