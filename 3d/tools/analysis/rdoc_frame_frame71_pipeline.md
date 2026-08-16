# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures/rdoc_frame_frame71.rdc`  
**Analyzed:** 2026-05-12T11:38:05  
**Model:** claude-opus-4-7  

---

## GPU Performance (per dispatch/draw)

| Pass | Type | GPU time (µs) |
|---|---|---|
| Cascade bake | dispatch | 40071.7 |
| Cascade reduction | dispatch | 157.7 |
| Cascade bake | dispatch | 83393.4 |
| Cascade reduction | dispatch | 162.3 |
| Cascade bake | dispatch | 84716.9 |
| Cascade reduction | dispatch | 30410.8 |
| Cascade bake | dispatch | 330895.4 |
| Cascade reduction | dispatch | 130094.0 |
| Raymarching | draw | 47917.5 |
| GI blur | draw | 76850.6 |
| glDrawElements() | draw | 12.0 |
| **Total** | | **824682.3** |


## SDF Volume (Signed Distance Field)

I'll analyze this image as a signed distance field (SDF) visualization.

**Observations:**

1. **Hard seams/discontinuities**: The image shows very sharp transitions between the dark (interior/negative) regions and the bright red (exterior/positive) regions. A proper SDF should exhibit a smooth gradient radiating outward from the surface, but here the red appears largely uniform/saturated.

2. **Flat regions**: The outer red area looks mostly flat in intensity rather than showing a gradual falloff with distance. This suggests either:
   - The SDF values are clamped/saturated
   - Distances aren't being computed properly beyond a narrow band
   - Only a binary inside/outside classification is encoded

3. **Voxelization artifacts**: The interior structure shows blocky, axis-aligned features (the bracket-like shapes and horizontal bar) with stair-stepped edges, indicating low-resolution voxelization rather than a smooth implicit surface.

4. **Potential holes**: The small isolated dark pixels within the bright bar at the center could indicate:
   - Genuine geometry holes
   - SDF computation errors where interior cells were missed
   - Aliasing from undersampling

5. **Missing gradient information**: A healthy SDF slice should show concentric "isobands" of color around geometry. Their near-absence here suggests the field may only be valid in a thin shell around the surface (narrow-band SDF) or the visualization range is wrong.

**Recommendations:**
- Re-examine the SDF generation to ensure distances propagate fully through the volume (e.g., use fast sweeping/marching).
- Increase voxel resolution to reduce the stair-step artifacts.
- Verify the visualization isn't clamping values — remap to the actual min/max distance range.
- Check the small interior dark spots against source geometry to confirm whether they're real features or computation errors.

## Albedo Volume

# Surface Color Inspection Report

## Expected Color Scheme
- **Left wall:** Red
- **Right wall:** Green
- **All other surfaces:** White

## Findings — Incorrect/Missing Color Regions

### ❌ Left Wall
- **Expected:** Red
- **Observed:** Beige/tan (appears similar to floor and other surfaces)
- **Status:** FLAGGED — missing red coloration

### ❌ Right Wall
- **Expected:** Green
- **Observed:** Beige/tan
- **Status:** FLAGGED — missing green coloration

### ⚠️ Other Surfaces (ceiling, floor, back wall)
- **Expected:** White
- **Observed:** Warm beige/off-white to tan tones
- **Status:** FLAGGED — surfaces appear yellowed/tinted rather than neutral white, likely due to scene lighting or texture tint

### ✅ Interior Objects
- Colored items (purple, yellow, green, red accents) appear to be intentional object colors, not surface colors — not flagged.

## Summary
The image does **not** conform to a Cornell Box-style color convention. The characteristic red and green side walls are absent, and the remaining surfaces lack neutral white reflectance. If this is intended as a Cornell Box validation, the scene fails on all three primary surface color checks.

## C0 Probe Directional Atlas

I can't actually see meaningful content in this image — it appears as a very faint, mostly-white pattern with sparse dots, so I can't verify the probe atlas visually. But based on your description, here's how to interpret/debug it:

## What to look for in a probe atlas

**Per-tile inspection (D×D bins):**
- **Uniform gray tile** → probe was never baked (skipped in the bake pass, or its `baked` flag is false). Check the bake queue and dirty-flag logic.
- **Random noise tile** → merge/blend error. Likely causes:
  - Race condition writing into the atlas (multiple threads hitting the same tile)
  - Wrong stride/offset when copying the per-probe buffer into the atlas
  - Uninitialized memory being sampled before the bake completes
  - Octahedral wrap seam not being duplicated on tile borders

**Cross-tile inspection (neighbor smoothness):**
- Hard discontinuities between adjacent tiles → probes placed across a geometry discontinuity (wall, floor), or wrong neighbor lookup in the indirection texture.
- Smooth but offset gradients → octahedral encoding orientation mismatch between neighbors.

**Alpha channel semantics you described:**
| Alpha | Meaning | Typical use |
|---|---|---|
| `> 0` | Surface hit | Distance/visibility weight for filtering |
| `< 0` | Sky / backface miss | Mark for sky contribution, exclude from depth filter |
| `== 0` | Ray miss (no data) | Skip during interpolation |

**Quick diagnostic checklist:**
1. Dump a single probe tile to disk — is *that* one correct?
2. Compare CPU-side ray results vs. atlas contents for one probe.
3. Disable threading in the bake; if artifacts vanish → sync bug.
4. Verify border/gutter pixels are written (octahedral seams need 1-px duplication).
5. Check the alpha histogram — if you see NaNs or values outside `[-1, 1]`, that points at the merge math.

If you can share the actual atlas image (or a single tile crop at higher resolution), I can give more specific guidance.

## C1 Probe Directional Atlas

# Cascade 1 Analysis — Post Phase 14c (tMax ~1.0 wu)

## Coverage Assessment

Scanning the tile grid for non-zero alpha contribution:

**Top band (orange/copper tiles, rows 1–7):** Dense, uniform fill. Alpha saturation ~98–100%. No dead tiles detected in this region.

**Mid band (greyscale lattice, rows 8–22):** Near-complete coverage with characteristic interference moiré from overlapping cascade fronts. Central plateau shows expected high-density packing.

**Lower band (rows 23–30):** Coverage tapers slightly but remains within spec (>95%).

## Dead Tile Flags ⚠

Candidate all-zero-α tiles (visual inspection — recommend programmatic confirmation):

| Region | Approx. grid coord (col,row) | Notes |
|---|---|---|
| Upper-left margin | (0, 0–6) | Edge bleed — likely boundary, not true dead |
| Mid-left interior pocket | ~(5, 10) | Bright void inside lattice — **flag** |
| Mid-center void cluster | ~(16–18, 13–14) | Small bright patch — **flag** |
| Right interior | ~(28, 11) | Isolated void — **flag** |
| Lower-left strip | ~(3, 19–20) | Linear gap — **flag** |

## Summary

- **Estimated surface coverage:** ~97–99% (meets near-100% expectation ✅)
- **Confirmed dead tiles:** ~4–5 small clusters in the central greyscale region
- **Anomaly:** The bright horizontal strip around row ~19 suggests a possible cascade-front desync vs. C0 — worth comparing the Δα map between C0 and C1 to confirm it isn't a propagation artifact from Phase 14c.

Want me to diff this

## C0 Isotropic Probe Grid (reduction output)

It looks like you're describing a debug visualization for a light probe system (likely for global illumination), but the image you've shared appears to be entirely white/blank — I can't see any probe data, spatial gradients, or color information in it.

To help diagnose, here's what to check based on your criteria:

**Expected:** Smooth spatial gradients of direction-averaged (irradiance/SH band 0) radiance per probe, reflecting room lighting falloff and color bleeding.

**Failure modes to flag:**

1. **All-black probes** — Indicates:
   - Probes failed to gather any radiance (raycast misses, wrong scene binding)
   - Sky/emissive contributions not being integrated
   - Probe relighting pass not running
   - Incorrect exposure/tonemapping making values appear zero

2. **Sharp grid steps** (blocky discontinuities between adjacent probes):
   - Missing or broken trilinear interpolation between probes
   - Probes classified as "invalid" causing fallback to single-probe sampling
   - Visibility/chebyshev weights collapsing to one probe
   - Incorrect probe spacing vs. sampling coordinates

3. **Wrong colors** (e.g., magenta, oversaturated, NaN-pink):
   - Uninitialized memory or NaN propagation in SH coefficients
   - Color space mismatch (linear vs. sRGB)
   - Wrong texture format (e.g., RGB vs. BGR, or signed vs. unsigned)
   - Negative SH reconstructions not clamped

Could you re-upload the visualization image? It seems to have been exported as a blank canvas — possibly the render target wasn't bound correctly when the screenshot was taken, which would itself be a clue worth investigating.

## Final Frame (from capture thumbnail)

# Artifact Analysis

**Detected artifacts:**

1. **Probe-grid banding** — Visible across the right wall and ceiling as soft blocky/blobby luminance patches (especially the bright circular blobs near the ceiling lights and the smeared bright zones on the right wall). The indirect lighting resolves at a coarse probe spacing.

2. **Cascade boundary seams** — A noticeable brightness discontinuity runs along the right wall mid-frame, where the wall transitions from a brighter foreground band to a darker background — consistent with a cascade interval transition rather than geometry.

3. **Outer-wall drift** — The right wall shows a clear monotonic darkening from the bright orange foreground edge into the depth of the corridor, inconsistent with the local light placement (lights are overhead, so falloff should be more symmetric).

4. **Directional bin banding** — The white "spike"/sawtooth shaped highlights on the floor and along the left wall windows are characteristic hard angular steps from a low-count directional bin set, producing jagged silhouettes around bright emitters rather than smooth glow.

5. **Missing/under-resolved shadows** — The floor directly beneath the ceiling lights shows bright streaks with no contact shadowing from the wall-mounted fixtures; occluders aren't darkening adjacent floor regions properly.

**Likely clean:** No obvious shadow acne speckling, and no clear color-bleed misassignment (the warm orange tones are plausibly from the emissive lights).

**Quality rating: Poor**

The combination of strong directional binning artifacts (sawtooth highlights), probe banding, and a visible cascade seam on the right wall makes the indirect lighting structure very apparent. The 23 FPS counter also suggests the cascade resolution is already pushed to a costly setting without resolving these issues.
