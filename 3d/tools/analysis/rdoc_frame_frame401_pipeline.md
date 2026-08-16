# RenderDoc GPU Analysis

**Capture:** `D:/GitRepo-My/radiance-cascades-demo/3d/tools/captures/rdoc_frame_frame401.rdc`  
**Analyzed:** 2026-05-06T10:25:44  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 5271.9 |
| Cascade reduction | dispatch | 27.2 |
| Cascade bake | dispatch | 6340.8 |
| Cascade reduction | dispatch | 193.2 |
| Cascade bake | dispatch | 10525.4 |
| Cascade reduction | dispatch | 198.7 |
| Cascade bake | dispatch | 6992.6 |
| Cascade reduction | dispatch | 223.0 |
| Raymarching | draw | 5916.7 |
| GI blur | draw | 1724.4 |
| glDrawElements() | draw | 11.9 |
| **Total** | | **37426.0** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as if it were an SDF (Signed Distance Field) visualization.

**Observations:**

1. **Hard seams / sharp transitions**: The image shows very abrupt transitions between the black interior, red shape, and the outer red glow. A proper SDF should exhibit smooth gradient falloff, but here the boundaries appear stepped/binary rather than continuous.

2. **Flat regions**: 
   - The solid black square frame area shows uniform color with no gradient — indicating either clamped distance values or a flat/zero-gradient region (likely an SDF computation error or aggressive thresholding).
   - The interior red shape (resembling a torii/arch glyph) has flat red fill rather than a smooth distance gradient toward its medial axis.

3. **Possible missing geometry**: The arch/torii shape appears asymmetric — the right "leg" looks shorter or partially cut compared to the left, suggesting missing voxels or truncated geometry near the bottom-right.

4. **Holes/discontinuities**: There appear to be small dark notches or gaps where the shape meets the frame, which could indicate holes in the SDF or incorrect inside/outside classification.

5. **Voxelization artifacts**: The blocky, pixelated edges (especially on the curved/diagonal parts of the glyph) strongly suggest the SDF was sampled at too low a resolution before visualization, producing stair-stepping instead of smooth isocontours.

**Recommendations:**
- Increase voxel resolution for the SDF grid.
- Verify distance computation isn't being clamped to a narrow band (which would explain the flat black/red regions).
- Re-check the source mesh for watertightness — the asymmetry in the arch suggests non-manifold or missing faces.
- Use a continuous colormap (e.g., signed gradient ramp) rather than thresholded colors to better diagnose distance accuracy.

## Albedo Volume

# Surface Color Inspection Report

## Detected Regions

| Region | Expected Color | Actual Color | Status |
|--------|---------------|--------------|--------|
| Left wall | Red | Red | ✅ OK |
| Right wall | Green | Green | ✅ OK |
| Background/elsewhere | White | **Light Gray** | ⚠️ FLAGGED |

## Issues Found

### ⚠️ Incorrect Background Color
- **Expected:** White (#FFFFFF)
- **Observed:** Light gray (~#C0C0C0 / #BFBFBF)
- **Location:** All non-wall areas (top, bottom, center gap between shapes)
- **Severity:** Color mismatch across entire background region

### Shape Observations
- Left red region: Arrow/triangle pointing right (inward) — color correct
- Right green region: Arrow/triangle pointing left (inward) — color correct
- Both colored shapes appear properly saturated and correctly positioned

## Recommendation
Replace the gray background fill with pure white (#FFFFFF) to meet specification. The red and green wall regions pass inspection and require no correction.

## C0 Probe Directional Atlas

I'll analyze this image which appears to be a debug visualization of a probe-based lighting system (like DDGI - Dynamic Diffuse Global Illumination).

## Observations

**Structure:**
- A grid of probes, each owning a D×D tile of directional samples
- Outer border tiles appear mostly white/empty (likely sky or miss bins)
- Inner region shows colored tiles with red/green/gray patterns

**Color analysis per tile:**
- **Red-dominant arrows/bins** (upper-left inner area): surface hits with red albedo
- **Green-dominant arrows/bins** (right inner area): surface hits with green albedo  
- **Gray bins**: could be properly baked neutral surfaces *or* the "not baked" flag — need to check uniformity
- **White/empty borders**: sky or miss bins (negative/zero alpha)

## Diagnosis

**✅ Healthy signs:**
- Neighboring tiles in the red region transition smoothly red→red
- Neighboring tiles in the green region transition smoothly green→green
- The red/green split looks like a coherent scene boundary (e.g., a Cornell-box-style red wall on left, green wall on right)
- No high-frequency random noise within tiles → **no merge errors**

**⚠️ Potential issues:**
- The **uniform gray tiles** scattered in the lower-middle and along certain rows/columns look suspiciously flat — these may be **probes that were never baked** (the flag condition you described)
- The sharp transition zone in the middle where red meets green is expected (geometry boundary), but verify it's not a seam artifact

## Recommended actions

1. **Re-bake the gray uniform tiles** — list probe IDs whose tile variance ≈ 0 with positive alpha and queue them for baking
2. **Validate alpha channel**: confirm border whites are negative-alpha (sky) vs zero-alpha (miss) — misclassified misses can poison interpolation
3. **Check probe placement** in the gray-tile region — they may be stuck inside geometry (backface hits only) rather than truly "not baked"
4. Smooth gradients elsewhere → **merge/atomic accumulation is working correctly**

Want me to focus on a specific tile coordinate or suggest a shader-side validation pass?

## C1 Probe Directional Atlas

# Cascade 1 Surface Coverage Analysis (tMax ~1.0wu, post-Phase 14c)

## Observations

Looking at the rendered tile grid, I can assess coverage against the expected near-100% surface fill:

### Coverage Estimate
- **Active tiles (non-zero alpha):** ~70–75%
- **Expected:** ≥98%
- **Deficit:** ~25% — **fails spec**

### Dead Tile Map (all-zero alpha)
Scanning the interior grid (excluding the bezel dot ring), I flag the following clusters:

| Region | Approx. Grid Coords | Notes |
|---|---|---|
| Central void

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Guide

## What You Should See ✅

A **smooth spatial gradient** across the probe grid reflecting the room's lighting distribution:

- **Brighter probes** near light sources (windows, lamps, emissive surfaces)
- **Dimmer probes** in occluded regions (under furniture, in corners, behind walls)
- **Gradual transitions** between neighboring probes
- **Color tints** matching local illumination (warm near incandescent, cool near sky light, colored bounces near saturated walls)

## Red Flags 🚩

### 1. All-Black Probes
**Symptom:** One or more probes render as pure black (or near-zero radiance).

**Likely causes:**
- Probe placed inside geometry (rays hit backfaces / self-occlusion)
- Ray tracing failed (missing BLAS/TLAS entry, bad transform)
- Integration accumulator never written (dispatch size mismatch, wrong probe index)
- Sky/environment contribution disabled and no direct lights reach probe
- NaN/Inf clamped to zero in the resolve pass

### 2. Sharp Grid Steps
**Symptom:** Discrete brightness jumps between adjacent probes — visible "blockiness."

**Likely causes:**
- Insufficient ray count per probe (high variance)
- Missing temporal accumulation / hysteresis
- Probes in different visibility clusters without proper relocation
- Cosine weighting incorrect (uneven hemisphere coverage)
- One probe stuck on a stale frame (update scheduling bug)

### 3. Wrong Colors
**Symptom:** Tints that don't match the scene (e.g., green probes in a red room, magenta artifacts).

**Likely causes:**
- Channel swizzle (RGB↔BGR) in the probe atlas write or read
- sRGB/linear mismatch (gamma applied twice or not at all)
- Material albedo sampled in wrong color space
- Negative radiance from MIS weights or denoiser
- Texture format mismatch (e.g., R11G11B10F losing blue precision)

## Quick Debug Checklist

| Check | How |
|---|---|
| Probe positions valid | Overlay probe centers on scene geometry |
| Ray hits register | Visualize hit distance / first-bounce albedo per probe |
| Accumulation alive | Toggle temporal blend; black probes should "fill in" over frames |
| Color space | Compare a probe near a known white surface to reference |
| Sample count | Temporarily 10× the rays/probe — banding should soften |

## Reference Look

A correct visualization resembles a **low-frequency blurred version of the room's irradiance** — like squinting at the scene. If it looks like noise, a checkerboard, or a debug grid, something upstream is broken.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Outer-wall drift / leakage** — The red and green side walls are clearly visible *outside* the box silhouette (left and right edges), and the dark gray top/bottom slabs extend beyond the box bounds. Indirect/probe contributions are bleeding past the wall geometry, producing the colored halos around the box exterior.

2. **Color bleeding errors (mild)** — The red wall's color appears slightly too saturated and uniform across its full face, and the floor near the red wall doesn't show a corresponding strong red bounce (asymmetric with the green side, where some greenish tint reaches the right box). Indirect bounce distribution looks uneven.

3. **Cascade/probe leakage under the tall box** — The shadow cast by the taller block on the left appears to detach slightly from the base of the box (light leak at the contact point), suggesting probe interpolation across the occluder.

4. **Soft probe-grid smoothing on back wall** — The central bright lobe on the back wall is smooth, but the falloff has a faintly blocky/low-frequency character consistent with a coarse probe grid, though no hard banding is visible.

**Not observed:** No directional bin banding (~36°), no ring-shaped cascade seams, no shadow acne, shadows are present.

**Quality rating: Fair**

The interior lighting and soft shadows are convincing, but the wall-edge drift/leakage outside the box silhouette is a prominent and immediately noticeable artifact that breaks the Cornell Box illusion.
