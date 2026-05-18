# Visibility Acceleration Plan — Dot-Banding Fix & Performance

**Date:** 2026-05-12T18:17+08:00
**Context:** Dot banding on vertical surfaces after H6 probeVisible landed. Mode 3 (per-direction-bin shadow trace) fixes banding but costs ~32× more than binary mode.
**Root cause:** Binary `probeVisibility()` creates 8 hard on/off decisions per pixel at trilinear cell boundaries. On vertical walls, the axis-aligned probe grid asymmetrically splits corners (4 visible, 4 occluded), producing grid-aligned bright/dark dots.
**Prior art:** Original RC algorithm stores **transparency interval (α)** per direction bin during bake — visibility is free, no separate shadow trace needed.

---

## Why No Banding Without Our Code Changes

Before H6, `sampleDirectionalGI` did **plain trilinear interpolation of all 8 corners** with no visibility gating. Every probe always contributed nonzero irradiance. The blend weights `f.x, f.y, f.z` vary continuously with position → spatially smooth output. The result was *wrong* (light leaked through walls) but *continuous* — no discontinuities at cell boundaries.

After H6 with binary `probeVisible`, corners flip between real irradiance and `vec3(0)` at cell boundaries. When a surface point crosses from one probe cell to the next, a previously-visible probe suddenly becomes occluded → one corner drops to zero. The trilinear blend jumps discontinuously, producing grid-aligned dots.

Mode 3 (per-direction-bin) eliminates this by checking occlusion per direction bin, not per probe. The blend remains continuous across cell boundaries because a "behind the wall" probe still contributes its forward-facing bins.

---

## How Shadertoy / Original RC Handles Visibility

The original Radiance Cascades algorithm (Sannikov, Shadertoy Xdt3D8, Fad mtlBzX) uses **hierarchical interval merging with per-direction transparency intervals stored in α**:

- Each direction bin stores `(RGB=radiance_interval, A=transparency_interval)`
- α = 1.0 if the ray **missed** (transparent interval), α = 0.0 if the ray **hit** (occluded)
- Merge formula: `L_{a,c} = L_{a,b} + β_{a,b} * L_{b,c}`, `β_{a,c} = β_{a,b} * β_{b,c}`
- In code: `radiance = near.rgb + far.rgb * near.a; alpha = near.a * far.a`

This is per-direction occlusion baked into the data structure at **zero extra cost** — the same raymarch that computes radiance also computes hit/miss. No separate shadow trace, no banding, no over-darkening.

Our 3D implementation stores `hit.a` (distance) instead of `β`, then adds a post-hoc `probeVisibility()` shadow trace at render time — both expensive and banding-prone.

Additionally, the original Shadertoy `WeightedSample()` (`Image.glsl:21-41`) uses the **lower probe's own directional hit distances** as an approximate visibility proxy for upper probes. It reads `lProbeRayDist` from the stored atlas and checks `length(relVec) < lProbeRayDist * cos(theta)`. This is a "free" visibility check using already-baked data, based on a flatland/2D approximation.

---

## Acceleration Strategies

### Strategy 1 — Transparency Interval (α) in Bake (HIGHEST impact, architecturally correct)

**What**: Change the directional atlas from RGB-only to RGBA. Store `α = 0.0` for hit bins, `α = 1.0` for miss bins. At render time, use α to gate far-field contributions per-direction.

**Where**: `radiance_3d.comp` bake pass + `raymarch.frag` sample pass.

**Bake changes** (`radiance_3d.comp`):
- Per-direction loop already has hit/miss classification (lines 382-397):
  - Surface hit (`hit.a > 0.0`): store `α = 0.0` (occluded)
  - In-volume miss (`hit.a == 0.0`): store `α = 1.0` (transparent, inherits from upper)
  - Sky sentinel (`hit.a < 0.0`): store `α = 1.0` (transparent, sky fill)
- The smoothstep blend at far boundary (`rad = hit.rgb * l + upperDir * (1-l)`) needs to incorporate α:
  ```
  rad = hit.rgb * l + upperDir.rgb * upperDir.a * (1-l)
  ```
  This gates upper contributions by the near-field transparency per-direction.

**Render changes** (`raymarch.frag`):
- `sampleProbeDir` reads RGBA instead of RGB. The α channel is the per-direction transparency.
- `sampleDirectionalGI` uses α as the visibility weight:
  ```glsl
  // Per-corner: sampleProbeDir returns (irradiance, avgTransparency)
  vec4 probe = sampleProbeDirRGBA(pc, normal, D);
  float wCorner = w[i] * probe.a;  // α gates the corner
  num += probe.rgb * wCorner;
  wsum += wCorner;
  ```
- Remove `probeVisibility()` entirely. Remove `uVisibilityMode` uniform and all 4 modes.
- The trilinear interpolation is now safe because we're interpolating **intervals** (radiance + transparency), not full radiance. The paper's penumbra condition guarantees linear interpolateability of intervals.

**Cost at render time**: **Free** — the α was computed during bake, no extra SDF traces.

**Fixes**: Banding (yes), over-darkening (yes), light leaking (yes).

**Implementation complexity**: Medium — requires changing atlas format from RGB to RGBA (GL_RGB8 → GL_RGBA8), updating all texelFetch sites, and modifying the bake-time inheritance merge logic.

**Caveats**:
- α is binary per-bin (0 or 1) in the current bake. For smooth transitions, we'd need a soft α (0..1) based on SDF proximity, similar to mode 2's smoothstep. This could be added later.
- The inheritance merge `rad = hit.rgb * l + upperDir * (1-l)` smoothstep currently blends blindly. With α, it becomes `rad = hit.rgb * l + upperDir * upperAlpha * (1-l)` — the upper contribution is gated by its transparency, which is correct.
- Need to handle the case where ALL bins in a probe have α=0 (fully occluded probe) → the trilinear blend of α values across 8 corners would still be continuous, unlike the binary vec3(0) gate.

---

### Strategy 2 — WeightedSample (Lower Hit Distance Proxy) (MEDIUM impact, quick win)

**What**: Reuse the already-baked `hit.a` (distance) from each probe's directional atlas as an approximate visibility check. Instead of tracing a new shadow ray, read the stored hit distance and check whether the probe-to-surface direction is occluded.

**Where**: `raymarch.frag` `sampleDirectionalGI`.

**Implementation**:
```glsl
float approxVisibility(ivec3 pc, vec3 surfacePos) {
    vec3 probeCenter = uAtlasGridOrigin + (vec3(pc) + 0.5) * cellSize;
    vec3 toProbe = probeCenter - surfacePos;
    float dist = length(toProbe);
    vec3 dir = toProbe / dist;
    // Read the hit distance from the probe's atlas at the direction
    // closest to dir (approximate — use nearest bin)
    ivec2 binIdx = dirToNearestBin(dir, D);
    float hitDist = texelFetch(uDirectionalAtlas,
                                ivec3(pc.x*D+binIdx.x, pc.y*D+binIdx.y, pc.z), 0).a;
    // If hit.a > 0 and hit.a < dist, the probe hit geometry before
    // reaching the surface → occluded
    if (hitDist > 0.0 && hitDist < dist) return 0.0;
    return 1.0;
}
```

**Cost at render time**: 1 texelFetch per corner (read existing hit.a) instead of 16 SDF traces. ~128× cheaper than mode 3.

**Fixes**: Banding partially (flatland approximation works for planar walls, degrades for complex 3D occluders like columns where hit direction ≠ probe-to-surface direction). Over-darkening partially.

**Implementation complexity**: Low — reads existing data, no format changes.

**Caveats**:
- Uses a **flatland/2D approximation**: the stored hit distance is for the ray's own bake direction, not the probe-to-surface direction. Accurate for axis-aligned walls, inaccurate for columns/arches.
- The hit.a distance is relative to the **probe center**, not the surface point. Need to account for the offset between probe center and surface position.
- This could be combined with Strategy 4 (cached bitmask) to avoid per-pixel texelFetch overhead.

---

### Strategy 3 — Coarse SDF for Visibility (MEDIUM impact)

**What**: Trace `probeVisibility` against a lower-resolution SDF (e.g., 32³ instead of 128³). Coarse SDF can't resolve thin walls but detects thick occluders (walls, columns) — the main banding sources.

**Where**: `raymarch.frag` `probeVisibility`.

**Implementation**: Either a second `uSDFLo` texture at 32³, or `textureLod(uSDFVolume, uvw, 2.0)` with LOD bias.

**Cost**: 32³ = 4× fewer voxels. Faster convergence (wider conservative band → fewer steps). ~4× cheaper than current mode 3.

**Fixes**: Banding for thick walls (yes). Thin walls that coarse SDF misses → still leak, but no banding (coarse SDF never marks them occluded → same behavior as no-visibility).

**Implementation complexity**: Low — LOD bias or second texture.

---

### Strategy 4 — Cached Per-Probe Visibility Bitmask (HIGH impact, moderate complexity)

**What**: Pre-compute a per-probe visibility bitmask (8 bits per probe, one per trilinear neighbor direction). Store as R8 texture at probe-grid resolution. Update every N frames or on scene change.

**Where**: New compute shader + `raymarch.frag` read.

**Bake (compute pass)**:
```glsl
// For each probe (x,y,z), check visibility to 8 neighbors
for (int i = 0; i < 8; ++i) {
    ivec3 neighbor = probeIdx + offsets[i];
    vec3 neighborPos = probeCenter(neighbor);
    bool vis = probeVisibilityBinary(probeIdx, neighborPos);
    bitmask |= (vis ? 1 : 0) << i;
}
imageStore(uVisBitmask, probeIdx, bitmask);
```

**Render**:
```glsl
float vis = float((texelFetch(uVisBitmask, pc, 0).r >> cornerIdx) & 1);
```

**Cost at render time**: 1 texelFetch per probe (R8) instead of 16 SDF traces. ~128× cheaper than mode 3.

**Cost at bake time**: 8 × 16 = 128 SDF traces per probe, but amortized over N frames (e.g., every 10 frames → 12.8 traces/frame equivalent). Computed on GPU in a separate compute pass.

**Fixes**: Banding for thick walls (yes). Cell-boundary surfaces may have stale visibility (cached from probe center, not surface point). For thick walls spanning multiple cells, accurate.

**Implementation complexity**: Medium — new compute shader, new R8 texture, new uniform binding.

---

### Strategy 5 — Hierarchical Skip (Far Probes Assumed Visible) (LOW impact, easy)

**What**: If a probe is far from the surface (> 2× wall thickness) and the SDF shows no nearby geometry, skip the visibility trace (assume visible). Only trace probes within 1–2 cells of the surface.

**Where**: `raymarch.frag` `probeVisibility`.

**Implementation**: Check SDF at surface first. If `sampleSDF(surfacePos) > cellSize * 2`, the surface is far from any occluder → all probes visible, skip all 8 traces.

**Cost**: Eliminates traces for ~6 of 8 corners (far from surface). ~4× cheaper in open areas. No savings in dense scenes (Sponza corridors).

**Fixes**: None — this is a pure optimization, doesn't change banding behavior.

**Implementation complexity**: Low — one SDF sample check before the 8-corner loop.

---

### Strategy 6 — Half-Resolution Visibility (MEDIUM impact, easy win)

**What**: Compute visibility at half resolution (640×360 instead of 1280×720). The banding pattern is at probe-cell granularity (~8 pixels wide), so half-res doesn't lose meaningful detail. Upsample bilinearly.

**Where**: `raymarch.frag` — compute visibility in a separate half-res pass, store in a texture, read in the full-res pass.

**Implementation**: Render visibility weights to a `RG16F` half-res texture (weights for the 8 corners packed into 2 channels). Full-res pass reads via `texture(uVisHalfRes, uv)`.

**Cost**: ~4× cheaper (half pixel count).

**Fixes**: Banding (yes, at half-res granularity which is still sub-cell). Over-darkening (yes).

**Implementation complexity**: Low-medium — new half-res render pass + texture.

---

### Strategy 7 — Temporal Amortization (1 Probe Per Frame) (LOW impact, introduces lag)

**What**: Each frame, trace visibility for only 1 of the 8 trilinear corners (rotating over 8 frames). Accumulate in a per-probe visibility texture. After 8 frames, all corners valid.

**Cost**: 1/8 of mode 3 per frame. 8-frame convergence delay.

**Fixes**: Banding (yes after convergence). Over-darkening (yes after convergence).

**Caveats**: 8-frame lag for static scenes. For dynamic scenes (moving camera/objects), stale visibility → ghosting artifacts. Not suitable for real-time with camera movement.

**Implementation complexity**: Low — frame counter + accumulation texture.

---

## Strategy Comparison

| # | Strategy | Render cost vs mode 3 | Banding fixed? | Over-darkening fixed? | Light leak fixed? | Complexity |
|---|---|---|---|---|---|---|
| 1 | Transparency α in bake | **Free** | Yes | Yes | Yes | Medium |
| 2 | WeightedSample (hit.a proxy) | ~1/128 | Partial (flatland) | Partial | Partial | Low |
| 3 | Coarse SDF | ~1/4 | Thick walls yes | Partial | Thick walls yes | Low |
| 4 | Cached visibility bitmask | ~1/128 | Thick walls yes | Partial (cell stale) | Thick walls yes | Medium |
| 5 | Hierarchical skip | ~1/4 in open areas | N/A (opt only) | N/A | N/A | Low |
| 6 | Half-res visibility | ~1/4 | Yes | Yes | Yes | Low-Medium |
| 7 | Temporal amortization | ~1/8 | Yes (8f delay) | Yes (8f delay) | Yes (8f delay) | Low |

---

## Recommended Implementation Order

### Phase A — Quick wins (can land independently)

1. **Strategy 6** (half-res visibility): Easiest 4× speedup for mode 3. Doesn't require bake changes. Can land as a standalone optimization.
2. **Strategy 5** (hierarchical skip): Simple early-exit for open areas. Complements any other strategy.
3. **Strategy 3** (coarse SDF): Easy LOD-based fallback for mode 1/2 when thick-wall banding is the primary concern.

### Phase B — Architectural fix (requires bake format change)

4. **Strategy 1** (transparency α in bake): The proper fix. Requires:
   - Change atlas format: `GL_RGB8` → `GL_RGBA8` (or `GL_RGBA16F` for soft α)
   - Modify `radiance_3d.comp` bake: store α per direction bin
   - Modify inheritance merge: `rad = hit.rgb * l + upperDir.rgb * upperAlpha * (1-l)`
   - Modify `raymarch.frag` `sampleProbeDir`: read RGBA, use α as visibility weight
   - Remove `probeVisibility()` and `uVisibilityMode` entirely
   - This is ~1 day of work per the H5/H6 scope assessment

### Phase C — Combined acceleration (after Phase B or alongside)

5. **Strategy 2 + 4** (WeightedSample + cached bitmask): Use hit.a distances as approximate visibility, cache in a per-probe bitmask texture. This eliminates all SDF traces at render time while maintaining reasonable accuracy for axis-aligned walls. For non-axis-aligned geometry, fall back to the α-based merge (Strategy 1).

---

## Key Insight: Why Strategy 1 Is The Right Answer

The paper states (Section 1): "classic radiance probes indeed require special handling of disocclusion exactly *because* they attempt to encode full radiance instead of encoding radiance intervals. [...] the search for the optimal answer to this conundrum has been one of the major motivations for this work and prompted partitioning radiance fields into individual components (further referred to as *radiance intervals*) that can be interpolated without introducing light leaks."

Our `probeVisibility()` is solving a problem the original algorithm doesn't have. The original RC never interpolates full radiance across walls — it interpolates **intervals** and lets the transparency term gate the far-field per-direction. Our implementation should do the same.

Strategy 1 is the architecturally correct fix because:
- It matches the paper's design intent (intervals are linearly interpolateable)
- It eliminates banding, over-darkening, and light leaking at zero render-time cost
- It removes the need for the entire `probeVisibility` / `uVisibilityMode` system
- It makes the trilinear interpolation safe by definition (interpolating intervals, not full radiance)

The other strategies are optimizations for the **wrong abstraction** (post-hoc visibility tracing). They can serve as transitional fixes while Strategy 1 is implemented, but the long-term architecture should converge to α-based interval merging.