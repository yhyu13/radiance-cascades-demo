# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame444.rdc`  
**Analyzed:** 2026-05-11T15:25:56  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 166.7 |
| Cascade reduction | dispatch | 19.6 |
| Cascade bake | dispatch | 9095.2 |
| Cascade reduction | dispatch | 20.5 |
| Cascade bake | dispatch | 7035.6 |
| Cascade reduction | dispatch | 36.8 |
| Cascade bake | dispatch | 4130.1 |
| Cascade reduction | dispatch | 46.9 |
| Raymarching | draw | 2129.9 |
| GI blur | draw | 226.0 |
| glDrawElements() | draw | 10.2 |
| **Total** | | **22917.5** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

### 1. **Hard Seams / Sharp Discontinuities**
- The transition between the dark interior shapes and the red field is abrupt rather than smooth
- A proper SDF should show a continuous gradient radiating outward from surfaces
- The flat red regions surrounding the geometry indicate the distance values are **clamped or saturated**

### 2. **Flat Regions (Saturation)**
- Large uniform red areas suggest distance values exceed the visualization range
- This indicates either:
  - Truncated SDF (TSDF) without proper falloff
  - Insufficient bit depth in storage
  - Missing normalization

### 3. **Voxelization Artifacts**
- The interior geometry shows **blocky, stair-stepped edges** rather than smooth iso-contours
- Visible pixel-aligned rectangles inside the shape suggest the SDF was computed at too low a resolution
- The horizontal "teeth" pattern in the middle appears aliased

### 4. **Possible Holes / Missing Geometry**
- The two dark vertical bars on the left and right sides appear disconnected from the central structure
- Gaps between components may indicate **missing surface samples** or incorrect inside/outside classification
- The interior should show concentric distance bands, but instead shows solid black (suggesting incorrect sign or zero distance throughout interior)

### 5. **Incorrect Interior Distances**
- The interior should display a gradient growing more negative toward the medial axis
- Instead it appears uniformly dark, suggesting the **inside distances are not being computed** (only outside, or binary mask)

## Recommendations

- **Recompute** with higher voxel resolution
- Ensure **signed** computation (both interior negative and exterior positive distances)
- Use **unclamped** distance values or extend the visualization range
- Verify mesh is **watertight** before SDF generation
- Apply proper **narrow-band** vs **full-field** SDF based on use case

This SDF is **not suitable** for applications requiring smooth gradients (e.g., raymarching, collision detection, or neural network training).

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions Flagged:

**❌ Left Wall**
- Expected: Red
- Found: Tan/beige and white tones
- **FLAG: Missing red coloring**

**❌ Right Wall**
- Expected: Green
- Found: Tan/beige and white tones
- **FLAG: Missing green coloring**

**❌ Background/Surrounding Area**
- Expected: White
- Found: Dark brown
- **FLAG: Incorrect base color**

**⚠️ Interior Surface**
- Contains scattered multi-colored elements (purple, yellow, green, orange spots)
- Base appears off-white/cream rather than pure white
- **FLAG: Color contamination on white surface**

**✓ Partial Compliance**
- Some white/light regions present on the central platform structure

## Summary
The image does **not** conform to the specified color scheme. The expected red and green wall indicators are entirely absent, and the surrounding "elsewhere" region is brown rather than white. Recommend re-rendering with correct color assignments.

## C0 Probe Directional Atlas

# Probe Tile Diagnostics

When inspecting baked probe atlases, each probe contributes a **D × D tile** to the texture. Reading them correctly tells you whether your lighting bake is healthy.

## What to look for

**Healthy tiles**
- Neighboring tiles transition **smoothly** in color/intensity
- Gradients reflect spatial coherence of the lighting field
- Sharp discontinuities only at genuine occluders (walls, corners)

**Failure flags**

| Symptom | Likely cause |
|---|---|
| Uniform **gray tile** | Probe was never baked / skipped |
| **Random noise** in a tile | Merge/atlas packing error, stale data, or race in the bake job |
| Hard seams between tiles | Neighbor lookup or octahedral encoding mismatch |
| Banding across a row | Row-major write into column-major buffer (or vice versa) |

## Alpha channel = hit classification

The alpha (or distance) bin encodes what the probe ray actually intersected:

- **α > 0** → ray hit a **surface** (store distance / radiance)
- **α < 0** → ray escaped to **sky** (use skybox / distant light)
- **α == 0** → ray **missed** entirely (invalid sample, exclude from filtering)

## Quick debug pass

1. Visualize the atlas with a **false-color** view of α (red = surface, blue = sky, black = miss).
2. Scan for **flat-gray** tiles → re-queue those probe IDs for baking.
3. Scan for **high-frequency speckle** → check the merge/scatter step; verify barriers between dispatch passes.
4. Confirm tile borders match by sampling neighbor centers — they should differ by less than a small ε for nearby probes in open space.

If you share a screenshot of the atlas (or the probe layout), I can point at which failure mode you're hitting.

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, Post-Phase 14c)

## Methodology
Same protocol as C0 baseline:
- Per

## C0 Isotropic Probe Grid (reduction output)

This appears to be a debug visualization of **direction-averaged radiance per probe** from a light probe grid (likely irradiance probes for global illumination).

Looking at the image against your flagged criteria:

**Issues to flag:**

1. **All-black probes** ⚠️ — The image appears predominantly/entirely black. If probes are genuinely capturing room lighting, you'd expect to see varying gray/colored values reflecting bounced light. Possible causes:
   - Probes not being updated/baked
   - Radiance accumulation buffer not being written to
   - Incorrect exposure/tonemapping in the debug view
   - Probes placed inside geometry (occluded from all light)
   - Shader reading from wrong texture/mip

2. **Sharp grid steps** — Hard to assess without visible gradient, but if any structure is visible, check for:
   - Missing trilinear interpolation between probes
   - Probe spacing too large for scene lighting frequency

3. **Wrong colors** — Cannot evaluate without visible signal, but once probes light up, verify:
   - Color space (linear vs sRGB) is correct
   - No channel swizzling (BGR vs RGB)
   - White balance matches scene lights

**Recommended debugging steps:**
- Verify probe bake/update actually ran (check GPU capture)
- Boost debug visualization exposure significantly (×100 or more) to rule out very dim but non-zero values
- Confirm probe positions aren't all inside walls/geometry
- Check that the radiance integration isn't dividing by zero or clamping to black
- Render a single probe's full cubemap to confirm capture is working before averaging

Could you share more context — the renderer/engine, probe placement strategy, or whether bake completed successfully? That would help narrow down the root cause.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible on the floor as horizontal striped/blocky patterns in the indirect lighting, especially in the mid-foreground where the lighting transitions create regular grid-aligned discontinuities.

2. **Cascade boundary seams** — A ring-like brightness discontinuity is visible roughly mid-frame where the warm orange glow transitions to darker tones, suggesting a cascade level boundary.

3. **Outer-wall drift** — The ceiling and upper walls show monotonic darkening toward the edges/corners, with the top-right area appearing notably darker and patchy compared to the central illumination.

4. **Directional bin banding** — The orange/warm light on the back wall and floor shows hard angular falloff steps rather than smooth gradients, consistent with limited directional resolution in the cascade bins.

5. **Color bleeding errors** — The reddish-brown tint appears overly dominant across surfaces that should receive more neutral light (ceiling especially), suggesting indirect color is being misattributed.

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts — grid banding, directional steps, and edge drift are all simultaneously visible, making the indirect illumination appear blotchy and structurally incorrect rather than smoothly diffuse.
