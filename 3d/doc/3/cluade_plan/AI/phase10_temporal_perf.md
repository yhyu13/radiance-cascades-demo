# Phase 10 — Temporal Accumulation Performance

**Date:** 2026-05-02
**Trigger:** User request after Phase 9d — optimize the temporal accumulation pipeline,
which was the dominant per-frame GPU cost at D=8, res=32, 4 cascades.

---

## Summary of Changes

| Change | Files |
|---|---|
| Staggered cascade updates (configurable interval) | `src/demo3d.h`, `src/demo3d.cpp` |
| Fused atlas EMA in bake shader (eliminates atlas temporal_blend dispatch) | `res/shaders/radiance_3d.comp`, `src/demo3d.cpp` |
| AABB history clamping in bake shader (ghost rejection preserved) | `res/shaders/radiance_3d.comp` |
| Handle-swap for grid (eliminates grid temporal_blend dispatch) | `src/demo3d.cpp` |
| Upper cascade binding updated for fused path | `src/demo3d.cpp` |
| Stagger UI (radio buttons 1/2/4/8) | `src/demo3d.cpp` |
| Larger workgroup for reduction (4×4×4 → 8×8×4) | `res/shaders/reduction_3d.comp`, `src/demo3d.cpp` |

---

## Bottleneck Analysis

### Before Phase 10 (D=8, res=32, 4 co-located cascades, temporal ON)

Per-frame dispatches:

| Dispatch | Threads | Image ops per thread | Total image ops |
|---|---|---|---|
| `radiance_3d.comp` × 4 | 32 768 × 4 | 64 stores | 8 M stores |
| `reduction_3d.comp` × 4 | 32 768 × 4 | 64 reads + 1 store | 10 M |
| `temporal_blend.comp` (atlas) × 4 | **2 097 152 × 4** | 7 loads + 1 store | **67 M** |
| `temporal_blend.comp` (grid) × 4 | 32 768 × 4 | 7 loads + 1 store | 1 M |

The atlas temporal blend (256×256×32 per cascade × 4 cascades × 8 image ops/texel) was
the dominant cost by ~5×. The AABB clamping (6 neighbor reads + 1 center read) made each
texel cost 7 imageLoads rather than 1.

---

## Part A — Staggered Cascade Updates

### Motivation

All 4 cascades were rebuilt every frame. Coarser cascades (C1–C3) represent larger
world-space regions that change slowly. They can safely be rebuilt less frequently.

### Implementation

New members in `demo3d.h`:
```cpp
uint32_t renderFrameIndex;   // monotonic counter, incremented after each full cascade cycle
int      staggerMaxInterval; // max interval: 1=none, 2/4/8=stagger (default 8)
```

Loop in `updateRadianceCascades()`:
```cpp
for (int i = cascadeCount - 1; i >= 0; --i) {
    if (!cascades[i].active) continue;
    int interval = std::min(1 << i, staggerMaxInterval);
    if ((renderFrameIndex % interval) != 0) continue;
    updateSingleCascade(i);
}
++renderFrameIndex;
```

Update schedule at default `staggerMaxInterval=8`:
- C0 (finest): every frame
- C1: every 2nd frame
- C2: every 4th frame
- C3: every 8th frame

Average cascade rebuilds per frame: 1 + 0.5 + 0.25 + 0.125 = **1.875** (vs 4) → ~2.1× speedup.

### Upper cascade correctness with stagger

When C0 is updated and C1 was skipped this frame, C0's bake reads C1's atlas from the
last time C1 was updated. This is correct — coarser cascades provide slowly-varying
far-field radiance. The stale C1 data decays toward the true value over subsequent C1
updates.

### UI

In the Temporal section of the Settings panel:
```
Stagger max interval:  [1] [2] [4] [8]
```
Setting 1 disables staggering entirely (matches pre-Phase-10 behavior).

---

## Part B — Fused Atlas EMA in Bake Shader

### Motivation

The atlas `temporal_blend.comp` dispatch was 64× more expensive than the grid blend
(2M vs 32K texels per cascade). Eliminating it while preserving AABB ghost rejection
is the largest single performance gain.

### Pipeline change

**Before:**
```
bake → probeAtlasTexture
temporal_blend.comp: clamp(history, currentNeighborAABB) + mix → probeAtlasHistory
reduction: probeAtlasTexture → probeGridTexture
temporal_blend.comp: clamp(history, currentNeighborAABB) + mix → probeGridHistory
```

**After (fused, temporal ON):**
```
bake: clamp(history, histNeighborAABB) + mix(clampedHist, freshBake, α) → probeAtlasTexture
reduction: probeAtlasTexture (blended) → probeGridTexture
swap(probeAtlasTexture ↔ probeAtlasHistory)   — O(1) GLuint swap
swap(probeGridTexture  ↔ probeGridHistory)    — O(1) GLuint swap
```

No `temporal_blend.comp` dispatches for atlas or grid.

The grid is derived from the EMA-blended atlas via reduction, so it is already temporally
stable. Running a separate grid temporal blend would double-accumulate (add lag). The
handle swap makes `probeAtlasHistory` and `probeGridHistory` point to the fresh accumulated
data, which is what the display pass reads.

### AABB clamping in the bake shader

The original `temporal_blend.comp` built its AABB from the **current-frame bake** neighborhood.
In the fused bake, neighboring probes' current outputs are unavailable within a single dispatch
(all threads run simultaneously, no cross-workgroup sync). Instead, the AABB is built from
the **history neighborhood** — the same 6 adjacent probes' same-direction bin read from
`uAtlasHistory`.

Ghost rejection is still attempted via this history-neighborhood AABB, but the clamp source
changed from current-bake neighbors to history neighbors. These are not the same algorithm.
Quality equivalence is a **hypothesis to validate**, not a proven invariant: the assumption
is that stable history neighbors define a valid expected range, but a scene change or
aggressive jitter can make history neighbors stale simultaneously, weakening the clamp.
Validate visually at representative alpha and jitter settings before treating it as equivalent.

Neighbor offsets in the atlas (for direction bin `(dx, dy)` at probe `probePos`):
```glsl
ivec3 nbOff[6] = ivec3[6](
    ivec3( uDirRes, 0, 0), ivec3(-uDirRes, 0, 0),  // ±1 probe in x, same bin
    ivec3( 0, uDirRes, 0), ivec3( 0,-uDirRes, 0),  // ±1 probe in y, same bin
    ivec3( 0, 0,  1),      ivec3( 0, 0, -1)         // ±1 probe in z, same bin
);
```
`±uDirRes` in x/y keeps bin identity: `(coord ± D) % D == coord % D`.

### Seeding on temporal enable

Previously: `glCopyImageSubData` copied the current bake to history on the first frame.

Now: `historyNeedsSeed = true` → `fusedAlpha = 1.0f`. The blend becomes
`mix(stale_history, fresh_bake, 1.0) = fresh_bake`, automatically overwriting the
stale history. No GPU copy needed.

**Stagger interaction:** Seeding only works if all cascades rebuild on the same frame.
With staggering active, this is not guaranteed unless `renderFrameIndex = 0`, because
only when `n = 0` does `n % min(2^i, staggerMaxInterval) == 0` hold for every i.
For this reason, `renderFrameIndex` is reset to 0 whenever temporal accumulation is
enabled. This guarantees a full-cascade rebuild (all levels, `fusedAlpha=1.0`) on the
first post-enable frame, before `historyNeedsSeed` is cleared.

### Shader additions (`res/shaders/radiance_3d.comp`)

```glsl
layout(rgba16f, binding=1) readonly uniform image3D uAtlasHistory;
uniform float uTemporalAlpha;
uniform int   uTemporalActive;
uniform int   uClampHistory;
```

The final `imageStore` in the per-direction loop:
```glsl
if (uTemporalActive != 0) {
    vec4 hist = imageLoad(uAtlasHistory, atlasTxl);
    if (uClampHistory != 0) {
        ivec3 atlasMax = ivec3(uVolumeSize.x * uDirRes,
                               uVolumeSize.y * uDirRes, uVolumeSize.z);
        ivec3 nbOff[6] = ivec3[6](...);  // see above
        vec4 nMin = hist, nMax = hist;
        for (int n = 0; n < 6; ++n) {
            ivec3 nc = atlasTxl + nbOff[n];
            if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, atlasMax))) continue;
            vec4 nh = imageLoad(uAtlasHistory, nc);
            nMin = min(nMin, nh); nMax = max(nMax, nh);
        }
        hist = clamp(hist, nMin, nMax);
    }
    imageStore(oAtlas, atlasTxl, mix(hist, vec4(rad, hit.a), uTemporalAlpha));
} else {
    imageStore(oAtlas, atlasTxl, vec4(rad, hit.a));
}
```

### C++ additions (`src/demo3d.cpp`, `updateSingleCascade()`)

**Before bake dispatch:**
```cpp
auto tb = shaders.find("temporal_blend.comp");
const bool doFusedEMA = useTemporalAccum && tb != shaders.end() &&
                        c.probeAtlasHistory != 0 && c.probeGridHistory != 0;
const float fusedAlpha = historyNeedsSeed ? 1.0f : temporalAlpha;
glUniform1i(..., "uTemporalActive", doFusedEMA ? 1 : 0);
glUniform1f(..., "uTemporalAlpha",  fusedAlpha);
glUniform1i(..., "uClampHistory",   useHistoryClamp ? 1 : 0);
if (doFusedEMA)
    glBindImageTexture(1, c.probeAtlasHistory, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
```

**After reduction:**
```cpp
if (doFusedEMA) {
    std::swap(c.probeAtlasTexture, c.probeAtlasHistory);
    std::swap(c.probeGridTexture,  c.probeGridHistory);
    if (cascadeIndex == 0) ++temporalRebuildCount;
} else if (useTemporalAccum && ...) {
    // original temporal_blend.comp path (fallback)
}
```

### Upper cascade binding

After the handle swap, `probeAtlasHistory` holds the fresh accumulated atlas. When C0
reads from C1's atlas for upper cascade merge, it must use `probeAtlasHistory`:

```cpp
GLuint upperAtlas = (doFusedEMA && cascades[upperIdx].probeAtlasHistory != 0)
                    ? cascades[upperIdx].probeAtlasHistory
                    : cascades[upperIdx].probeAtlasTexture;
GLuint upperGrid  = (doFusedEMA && cascades[upperIdx].probeGridHistory != 0)
                    ? cascades[upperIdx].probeGridHistory
                    : cascades[upperIdx].probeGridTexture;
```

---

## Part C — Larger Workgroup for Reduction

`reduction_3d.comp` local size: `4×4×4` (64 threads) → `8×8×4` (256 threads).

Each thread does D²=64 texture reads then 1 write. 256-thread workgroups improve GPU
occupancy and amortize dispatch overhead over more work. Dispatch adjusted:

```cpp
glm::ivec3 wgRed((c.resolution + 7) / 8, (c.resolution + 7) / 8, (c.resolution + 3) / 4);
glDispatchCompute(wgRed.x, wgRed.y, wgRed.z);
```

---

## Expected Performance Profile (operation-count model — not measured GPU results)

The numbers below are derived from dispatch/thread accounting, not GPU profiler output.
Actual speedup depends on GPU architecture, driver occupancy, and memory access patterns.
Key caveats: the larger reduction workgroup benefit is architecture-dependent; the fused
bake adds AABB image loads that partially offset the atlas blend elimination; staggering
reduces average cost but does not change per-updated-cascade cost.

At D=8, res=32, 4 cascades (co-located), staggerMaxInterval=8:

| Metric | Before Phase 10 | After Phase 10 |
|---|---|---|
| Cascade rebuilds / frame | 4 | ~1.875 (avg) |
| Atlas temporal_blend dispatches | 4 (2M threads each) | 0 |
| Grid temporal_blend dispatches | 4 | 0 |
| Extra imageLoads in bake (AABB) | 0 | 6×D²=384 per probe × 32768 = 12.5 M (per cascade updated) |
| Net image ops saved (atlas blend eliminated) | — | ~67 M → ~12.5 M overhead → **−54 M ops/frame at full stagger** |

---

## Backward Compatibility

- **Temporal OFF**: `uTemporalActive = 0`, no history binding, no swap. Identical to pre-Phase-10.
- **Stagger = 1**: all cascades update every frame. Identical to pre-Phase-10.
- **Non-fused fallback**: `doFusedEMA = false` if history textures missing; falls back to original `temporal_blend.comp` path.

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | 4 new uniforms/bindings; conditional EMA + AABB imageStore in per-direction loop |
| `res/shaders/reduction_3d.comp` | `local_size` 4×4×4 → 8×8×4; dispatch uses `wgRed` |
| `src/demo3d.h` | + `renderFrameIndex`, `staggerMaxInterval` |
| `src/demo3d.cpp` | Staggered loop; fused EMA uniforms + binding; handle swap; upper cascade binding fix; stagger UI radio buttons |

---

## Verification Checklist

| Check | Procedure |
|---|---|
| Stagger correctness | Enable temporal + stagger=8. After 8+ frames GI should match stagger=1 |
| Stagger=1 baseline | Matches pre-Phase-10 visual output exactly |
| Fused startup | Enable temporal (first time): GI at full brightness on frame 1 |
| Fused quality | Mode 6 at α=0.3: convergence speed matches old path |
| Fused + clamp | Ghost rejection: enable jitter, wait 20 frames, GI smooth and stable |
| Temporal off | Disabling temporal: fresh single-frame bake, no handle confusion |
| Performance | `cascadeTimeMs` display: lower cascade cost; raymarch unchanged |
