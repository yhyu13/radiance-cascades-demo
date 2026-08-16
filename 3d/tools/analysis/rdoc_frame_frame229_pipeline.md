# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame229.rdc`  
**Analyzed:** 2026-05-11T13:31:29  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 5842.9 |
| Cascade reduction | dispatch | 28.7 |
| Cascade bake | dispatch | 9934.2 |
| Cascade reduction | dispatch | 161.8 |
| Cascade bake | dispatch | 15062.9 |
| Cascade reduction | dispatch | 165.9 |
| Cascade bake | dispatch | 13882.2 |
| Cascade reduction | dispatch | 222.9 |
| Raymarching | draw | 26052.2 |
| GI blur | draw | 11442.2 |
| glDrawElements() | draw | 10.6 |
| **Total** | | **82806.4** |


## SDF Volume (Signed Distance Field)

# SDF Inspection Analysis

Looking at this signed distance field visualization, I can identify several **significant issues**:

## Problems Detected

### 1. **Hard Seams / Flat Regions** ❌
- The outer red region appears almost uniformly saturated with no visible gradient falloff
- A proper SDF should show smooth transitions from the surface outward, but here the exterior looks like a flat clamped value
- This suggests either **value clamping** or **truncation** of the distance field beyond a narrow band

### 2. **Voxelization Artifacts** ❌
- Sharp rectangular blockiness is visible along the dark structure edges
- The internal "teeth" or column-like features show stair-stepping rather than smooth distance contours
- Indicates the SDF was computed at low resolution or sampled from a binary voxel grid without proper sub-voxel refinement

### 3. **Holes / Missing Geometry** ⚠️
- The dark interior regions show inconsistent depths — some "slots" between the vertical bars appear disconnected
- The bottom edge has irregular notches that may indicate **missing surface samples** or boundary computation errors
- The horizontal dashed band near the top suggests broken/discontinuous geometry

### 4. **Incorrect Distance Encoding** ❌
- True SDFs should display a continuous gradient (typically a smooth radial falloff around features)
- Here the field is essentially **binary-looking** (deep red vs. near-black) with minimal intermediate values
- This indicates the distance values are not being normalized correctly, or the visualization is using a **sign-only** representation rather than true distance

## Recommendations

1. **Recompute with higher resolution** sampling to eliminate voxel stair-stepping
2. **Remove value clamping** or expand the visualization range to show the full gradient
3. **Use a proper SDF algorithm** (e.g., fast sweeping, fast marching, or jump flooding) instead of binary thresholding
4. **Verify mesh watertightness** — the holes/notches suggest the source geometry may have non-manifold edges
5. **Check normalization**: distances should scale smoothly with proximity to the surface

This SDF appears to need regeneration — it currently looks more like a **rasterized occupancy mask** than a true signed distance field.

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color requirements:

**Expected:**
- Left wall: Red
- Right wall: Green
- Other surfaces: White

**Findings — Flagged Issues:**

🚩 **Left wall:** INCORRECT
- Appears in tan/beige/yellowish tones, not red
- Missing required red coloration

🚩 **Right wall:** INCORRECT
- Appears in tan/beige tones with some greenish-yellow accents
- Does not display the required green color

🚩 **Other surfaces (floor/ceiling/back):** INCORRECT
- Floor appears brown/dark
- Ceiling/top shows tan and orange tones
- Background shows yellow/beige rather than white

**Summary:** All three color regions fail to match specifications. The scene appears rendered in a warm earth-tone palette (browns, tans, yellows) rather than the expected red/green/white Cornell box-style color scheme. Recommend re-checking material assignments or lighting conditions that may be tinting the surfaces.

## C0 Probe Directional Atlas

I'll analyze this image based on the technical description provided.

**Observations:**

1. **Layout**: The image appears to be a probe atlas/texture where each probe contributes a D×D tile arranged in a grid pattern.

2. **Color smoothness**: Most tiles show fine, varying patterns (dots/dithering) that transition between neighbors — this suggests the probes are **baked correctly** with spatially coherent irradiance/visibility data.

3. **Flags to check**:
   - ❌ **Uniform gray tiles (unbaked probes)**: I don't see large solid-gray blocks dominating the atlas — the patterns look populated.
   - ❌ **Random noise (merge error)**: The patterns appear structured and repeat coherently rather than looking like white noise.
   - ✅ **Alpha bins**: The dotted patterns suggest a mix of:
     - **Positive alpha** (surface hits) — the dense dot clusters in the central region
     - **Negative alpha** (sky) — the sparser/lighter border regions
     - **Zero alpha** (misses) — the near-white gaps between dot patterns

**Diagnosis**: The atlas looks **healthy overall** — probes appear baked, neighbors vary smoothly, and there's a sensible distribution of surface-hit vs. sky vs. miss bins (interior surfaces dominate, with sky/miss along the borders consistent with an open-top scene).

**Suggested next checks**:
- Verify the border tiles aren't entirely zero-alpha (could indicate probes placed outside the bake volume).
- Confirm the symmetry along the vertical axis is intentional (it suggests a mirrored scene or a symmetric probe placement).
- Sample a few neighbor tile pairs and compute SH/luminance deltas to numerically confirm "smoothly varying."

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analys

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe: Diagnostic Guide

## What You Should See

A **direction-averaged radiance** visualization reduces each probe's full directional radiance distribution to a single RGB value (the spherical mean), then displays one colored sample per probe location. Healthy output exhibits:

- **Smooth spatial gradients** — radiance varies gradually between neighboring probes as light falls off, bounces, and mixes
- **Color tinting consistent with the scene** — warm near incandescent sources, cool near skylights/windows, neutral in mixed zones
- **Brightness falloff with distance** from emitters and through occluders
- **Subtle directional cues** even after averaging (probes near a colored wall pick up its bounce)

## Red Flags

### 🔴 All-black probes
Likely causes:
- **No light accumulation**: ray budget = 0, or rays missing emissive surfaces
- **Skybox/emissive not contributing** to probe gather pass
- **Tonemap/exposure** clipping HDR values to zero, or sRGB-encoding zero-mean buffers incorrectly
- **Probe placement inside geometry** — all rays hit backfaces immediately
- **Cleared but never written** — gather kernel not dispatched, or wrong resource bound

### 🔴 Sharp grid steps (visible probe boundaries)
Indicates the *visualization* or *sampling* is wrong, not necessarily the data:
- **Nearest-neighbor sampling** instead of trilinear between probes
- **Per-probe quantization** (e.g., storing as 8-bit without dithering)
- **Insufficient probe density** for the scene's lighting frequency
- **Leaking mitigation too aggressive** — depth/visibility weights snapping to 0/1
- **Cascade/volume seams** if using nested probe grids without blend regions

### 🔴 Wrong colors
- **Channel swizzle** (BGR vs RGB) — try swapping R/B
- **Color space mismatch** — linear data displayed as sRGB or vice versa
- **Gamma applied twice** (washed out) or **not at all** (overly dark/saturated)
- **Wrong texture format** — e.g., `R11G11B10F` interpreted as `RGBA8`
- **Energy conservation broken** — albedo > 1, or emissive scaled incorrectly
- **Octahedral decode bug** — averaging is correct but per-direction radiance is rotated/flipped

## Quick Triage Steps

1. **Override one probe** to a known constant (e.g., red) → confirm display path
2. **Disable bounces**, use direct-only → verify primary lighting reaches probes
3. **Compare a single probe's** averaged value against a CPU reference (sample the env map / nearby light)
4. **Toggle filtering** in the visualizer to distinguish data vs sampling artifacts
5. **Inspect HDR range** with a min/max readback before tonemapping

Want me to drill into a specific failure mode (DDGI, ir

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Visible artifacts:**

1. **Probe-grid banding** — Strong horizontal/grid-aligned streaks visible across the ceiling and upper walls, appearing as repeating dark brown chevron-like patterns. These are the radiance probe samples showing through as discrete lit cells.

2. **Directional bin banding** — The brown/orange angular streaks on the ceiling and right wall show hard directional steps consistent with too few angular bins per probe, producing zebra-like directional artifacts rather than smooth indirect lighting.

3. **Outer-wall drift** — The left wall exhibits monotonic brightening toward the foreground (washed-out near white), while the right wall darkens toward brown, suggesting energy isn't being properly bounded at wall edges.

4. **Color bleeding errors** — Heavy brown/orange tint dominates surfaces that in a Cornell Box should remain neutral (ceiling, floor center). The orange light appears to be smeared onto the ceiling where it shouldn't reach with such intensity.

5. **Cascade boundary seams** — Subtle ring-like brightness transitions visible mid-corridor where the floor lighting changes character (the bright speckled streak down the floor center has an abrupt falloff).

6. **Shadow acne / floor speckling** — The bright white dashed/speckled pattern running down the floor center is characteristic shadow acne or undersampled direct lighting hitting the floor.

**Quality rating: Poor**

The image suffers from compounding artifacts across nearly every category. Probe resolution and angular binning are both clearly insufficient, and the scene reads more as a blurry impressionistic smear than a coherent Cornell Box render.
