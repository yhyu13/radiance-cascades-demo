# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame192.rdc`  
**Analyzed:** 2026-05-12T11:34:44  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 47888.1 |
| Cascade reduction | dispatch | 27.9 |
| Cascade bake | dispatch | 55787.5 |
| Cascade reduction | dispatch | 156.4 |
| Cascade bake | dispatch | 119214.9 |
| Cascade reduction | dispatch | 167.7 |
| Cascade bake | dispatch | 64521.3 |
| Cascade reduction | dispatch | 218.4 |
| Raymarching | draw | 64467.6 |
| GI blur | draw | 58032.7 |
| glDrawElements() | draw | 12.0 |
| **Total** | | **410494.5** |


## SDF Volume (Signed Distance Field)

Looking at this image, which appears to be a signed distance field (SDF) visualization:

**Issues I can identify:**

1. **Hard seams/sharp transitions**: There's a very abrupt transition between the black (interior/negative) regions and the red (exterior/positive) regions. A proper SDF should show smooth gradient falloff, but here the boundaries appear binary-like.

2. **Flat regions**: Large areas of uniform red color indicate the SDF is saturated or not computing true distance values—it looks more like a binary mask than a distance field. True SDF values should continuously increase with distance from the surface.

3. **Lack of gradient**: I don't see the expected smooth color gradient radiating outward from the geometry. The red appears mostly uniform in intensity rather than getting progressively brighter/darker with distance.

4. **Possible voxelization artifacts**: The black geometry features show blocky, stair-stepped edges (visible in the pillar-like structures and the horizontal band of small rectangles), suggesting the source mesh was voxelized at low resolution before SDF computation.

5. **Holes/missing geometry concerns**: The pattern of small black rectangles in the middle could either be intentional detail (like railing/fence geometry) or holes/gaps from incomplete surface reconstruction. Without reference geometry it's hard to tell, but the regular spacing suggests it may be intentional.

**Recommendations:**
- Recompute the SDF with proper unsigned distance propagation (e.g., fast marching or brute-force distance to nearest surface)
- Increase voxel resolution to reduce stair-stepping
- Verify the visualization isn't clamping values too aggressively—try remapping the value range to see the full gradient
- Check that the source mesh is watertight before SDF generation

## Albedo Volume

# Surface Color Inspection

Analyzing the image for the specified color scheme (red left wall, green right wall, white elsewhere):

## Findings - Multiple Incorrect/Missing Regions

**❌ Left Wall - FAIL**
- Expected: Red
- Actual: Tan/beige and white tones
- **Flag:** Missing red coloration entirely

**❌ Right Wall - FAIL**
- Expected: Green
- Actual: Tan/beige and white tones
- **Flag:** Missing green coloration entirely

**⚠️ Other Surfaces - PARTIAL**
- Expected: White
- Actual: Mixed white, yellow/gold, with scattered colored elements (purple, green, orange, red spots)
- **Flag:** Contamination from decorative/object colors on surfaces that should read as white

## Summary
The image does not conform to the specified color scheme. The dominant palette is **brown/tan (background), cream/white, and gold/yellow**, with small multicolored accents (likely depicting food items or decorations). Neither the red left wall nor green right wall requirement is met.

**Recommendation:** Re-render or recolor the left vertical surface to red (#FF0000 range) and right vertical surface to green (#00FF00 range), and desaturate non-wall regions to pure white.

## C0 Probe Directional Atlas

I'll analyze this image based on the technical description you've provided about probe tiles and baking artifacts.

**Observations:**

1. **Overall structure**: The image shows a grid-like arrangement of tiles, consistent with a probe atlas layout (likely irradiance/radiance probes for global illumination).

2. **Tile content**: Most tiles contain detailed, varying patterns (diamond/cross-hatch motifs) rather than uniform gray — this suggests the probes **have been baked** successfully. No obvious "unbaked" gray tiles are visible.

3. **Smoothness check**: Neighboring tiles appear to share similar patterns and structure, indicating reasonable spatial coherence between adjacent probes. I don't see obvious random noise discontinuities that would flag merge errors.

4. **Border regions**: The top and bottom edges show repetitive smaller tile patterns — likely border/padding texels used for hardware bilinear filtering across tile boundaries.

5. **Alpha interpretation**: Per your legend:
   - Positive alpha (surface hits) — would dominate interior probes seeing geometry
   - Negative alpha (sky) — likely the lighter/whiter regions
   - Zero alpha (miss) — would appear as gaps

**Verdict**: This atlas looks **healthy** — no uniform gray (unbaked) tiles and no high-frequency noise patches indicating merge corruption. The smooth variation between neighbors suggests the probe bake completed and merged correctly.

Is there a specific tile region you'd like me to examine more closely, or are you checking for a particular failure mode?

## C1 Probe Directional Atlas

I can't actually analyze the pixel data of the image you've shared — I can only see it as a rendered picture, not access its raw alpha channel or per-tile values. So I can't programmatically flag dead tiles (all-zero alpha) or compute exact surface coverage for cascade 1 at tMax ~1.0wu post-Phase 14c.

That said, here's what I can offer from a **visual inspection**:

### Qualitative Observations (Cascade 1, tMax ~1.0wu)
- **Top band (orange/copper tiles):** Appears mostly populated, but several rows show gaps — particularly the top 1–2 rows, where tiles look sparse or partially transparent. Possible candidates for dead/low-alpha tiles.
- **Middle band (gray lattice region):** Coverage looks high but **not** near-100%. There are visible voids in the central diamond/mesh pattern, especially toward the left-center and right-center edges. This is **inconsistent with the expected near-100% coverage** for cascade 1 at this timestep.
- **Bottom band (gray square tiles):** Bottom 2 rows show clear dropouts — multiple tiles missing or fully transparent. Likely dead tiles.

### Recommendation
If you can provide either:
1. The raw tile array / alpha buffer (e.g., as a NumPy array, JSON, or CSV), or
2. The tile grid dimensions and a way to access per-tile alpha,

…I can run an actual dead-tile flagging pass and compute true coverage % to compare against the C0 baseline. Would also help to know the expected tile grid resolution (looks roughly 32×32 tiles? hard to tell from the render).

Do you want to share the underlying data, or should I treat this as a qualitative review only?

## C0 Isotropic Probe Grid (reduction output)

# Direction-Averaged Radiance Per Probe — Diagnostic Notes

## What it should show
- **Smooth spatial gradients** across the probe grid, reflecting how room lighting varies in space
- Brighter values near light sources and windows
- Gradual falloff into shadowed corners and under occluders
- Color tints consistent with the dominant light sources (warm near tungsten, cool near sky/windows, neutral in mixed areas)
- Continuity across neighboring probes (no abrupt jumps unless a wall/occluder lies between them)

## Red flags to check for

### 🚩 All-black probes
Likely causes:
- Probe spawned **inside geometry** (rays terminate immediately on backfaces)
- **Ray budget = 0** or sampling loop skipped
- **Accumulator not initialized** / divided by zero sample count
- Probe outside scene bounds → all rays miss and miss-shader returns black
- Visibility/validity mask marking probe as invalid but renderer still showing it

### 🚩 Sharp grid steps
Indicates discretization or update issues:
- **No interpolation** between probes (nearest-neighbor lookup instead of trilinear)
- **Hysteresis / temporal blend** too aggressive on some probes, stale on others
- **Partial update** — only a subset of probes refreshed this frame, creating checkerboard
- **Per-probe normalization** mismatch (different sample counts not accounted for)
- Quantization in storage format (e.g., RGBA8 without proper encoding)

### 🚩 Wrong colors
Likely causes:
- **Color space mismatch** (linear vs sRGB double-conversion)
- **Channel swizzle** (BGR vs RGB)
- **Tone-mapping applied twice** or applied to debug view incorrectly
- **HDR encoding** (RGBE/RGB9E5) decoded wrong
- Emissive/albedo leaking into radiance (missing demodulation)
- Wrong **cosine weighting** during averaging — should integrate L(ω) over the sphere with uniform weight (4π) for direction-averaged irradiance proxy, not cosine-weighted

## Quick triage steps
1. Render **probe positions as points** colored by validity flag — confirms placement
2. Render **sample count per probe** as heatmap — reveals update issues
3. Compare against a **single reference probe** path-traced offline
4. Toggle **interpolation off/on** to isolate filtering vs data issues
5. Check a **uniform white-furnace test** — all probes should converge to constant value

Want me to focus on a specific GI system (DDGI, irradiance volumes, light probes in Unity/Unreal, etc.)?

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Observed artifacts:**

1. **Probe-grid banding** — Strongly visible across the right wall and ceiling. The brown/tan surfaces show a clear stair-stepped, blocky pattern of light intensity that betrays the underlying probe sampling grid rather than smooth illumination.

2. **Cascade boundary seams** — Visible as concentric arc-like brightness discontinuities sweeping across the right wall and ceiling, particularly noticeable in the upper-right where the lighting transitions in rings.

3. **Directional bin banding** — The ceiling lights and their spill exhibit hard angular steps (the jagged "sawtooth" silhouettes of the light fixtures and the chevron-like floor reflections), consistent with too few directional bins resolving the high-frequency emitters.

4. **Outer-wall drift** — The right wall darkens monotonically toward the far end and the left wall shows brightening drift near the window region, inconsistent with a uniform corridor.

5. **Color bleeding errors** — The orange/brown tint on the right wall appears to be leaking onto the ceiling and floor edge in places where geometry should not transport that radiance directly.

6. **Shadow acne / speckling** — Black dotted speckles are visible on the floor's center-line markings and along the ceiling fixture edges.

**Quality rating: Poor**

The frame suffers from multiple compounding artifacts (grid banding, cascade rings, directional aliasing) that dominate the indirect lighting and make the corridor look smeared and stepped rather than smoothly lit.
