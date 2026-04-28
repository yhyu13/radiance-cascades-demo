# Phase 5b / 5b-1 / 5c Implementation Learnings -- Per-Direction Atlas & Directional Merge

**Date:** 2026-04-28  
**Branch:** 3d  
**Status:** Implemented, compiled (0 errors). Runtime visual validation pending.  
**Follows:** `phase5a_impl_learnings.md` + Codex reflection review `codex_critic_phase5/02_reflection_2026-04-26_review.md`

---

## What Was Implemented

### Phase 5b -- Per-Direction Atlas Texture

**New texture per cascade:** `(32*D) x (32*D) x 32` RGBA16F.  
Probe `(px, py, pz)` occupies atlas tile at x:[px·D, px·D+D), y:[py·D, py·D+D), z:pz.  
Direction bin (dx, dy) for that probe -> atlas texel `ivec3(px·D+dx, py·D+dy, pz)`.  
RGB = per-direction radiance. W = hit sentinel: `>0` surface, `<0` sky, `=0` miss.

**`initCascades()` changes:**
```cpp
int D = dirRes;   // 4
int atlasXY = probeRes * D;   // 128 for 32³ grid
cascades[i].probeAtlasTexture = gl::createTexture3D(
    atlasXY, atlasXY, probeRes, GL_RGBA16F, nullptr, GL_CLAMP_TO_EDGE);
// GL_NEAREST override -- must come immediately after, before any render
glBindTexture(GL_TEXTURE_3D, cascades[i].probeAtlasTexture);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glBindTexture(GL_TEXTURE_3D, 0);
```

**`updateSingleCascade()` image binding change:**  
Old: `glBindImageTexture(0, c.probeGridTexture, ...)` -> wrote isotropic value directly.  
New: `glBindImageTexture(0, c.probeAtlasTexture, ...)` -> writes per-direction atlas. `oRadiance` image unit removed from the compute shader entirely.

**Shader inner loop write:**
```glsl
ivec3 atlasTxl = ivec3(probePos.x * uDirRes + dx,
                       probePos.y * uDirRes + dy,
                       probePos.z);
imageStore(oAtlas, atlasTxl, vec4(rad, hit.a));
```

### Phase 5b-1 -- Atlas Reduction Pass

New shader `reduction_3d.comp`. One thread per probe. Reads atlas as sampler3D, averages D^2 bins, writes `probeGridTexture` to keep `raymarch.frag` valid.

```glsl
vec3 avg = vec3(0.0);
for (int dy = 0; dy < uDirRes; ++dy)
    for (int dx = 0; dx < uDirRes; ++dx)
        avg += texelFetch(uAtlas,
            ivec3(probePos.x*uDirRes+dx, probePos.y*uDirRes+dy, probePos.z), 0).rgb;
avg /= float(uDirRes * uDirRes);
imageStore(oRadiance, probePos, vec4(avg, 0.0));   // alpha intentionally zeroed
```

Alpha `0.0` in `probeGridTexture` is intentional: the packed hit count convention (`surfH + skyH*255`) is retired. Hit classification now lives in atlas alpha per bin. This has downstream consequences -- see Learnings.

**Dispatch order per cascade (C3 first, C0 last):**
```
1. Dispatch radiance_3d.comp  (atlas bake)
2. glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT)
3. Dispatch reduction_3d.comp (atlas -> isotropic)
4. glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT)
```
Barrier 4 ensures the next cascade's atlas bake reads a complete, reduced probeGridTexture from the cascade above (used by the isotropic fallback path). Barrier 2 ensures the reduction reads a complete atlas.

### Phase 5c -- Directional Upper Cascade Merge

Replaces `texture(uUpperCascade, uvwProbe).rgb` (one isotropic probe average for all directions) with `texelFetch(uUpperCascadeAtlas, atlasTxl, 0).rgb` (exact bin for the current ray direction).

**Key insight -- co-located grids:** All cascades use the same 32³ world-space probe positions. The upper probe IS at the same position as the current probe. Therefore the upper atlas texel for direction bin (dx, dy) is `ivec3(probePos.x*D+dx, probePos.y*D+dy, probePos.z)` -- the same index as the current atlas write. No spatial interpolation across neighboring probes is needed.

**`radiance_3d.comp` merge inside the per-direction loop:**
```glsl
vec3 upperDir = vec3(0.0);
if (uHasUpperCascade != 0) {
    if (uUseDirectionalMerge != 0)
        upperDir = texelFetch(uUpperCascadeAtlas, atlasTxl, 0).rgb;
    else
        upperDir = texture(uUpperCascade, uvwProbe).rgb;   // isotropic fallback
}
```

`uvwProbe = (worldPos - uGridOrigin) / uGridSize` -- computed once before the loop, used only by the fallback path.

**Upper cascade binding in `updateSingleCascade()`:**
```cpp
// Unit 2: upper atlas (for directional merge)
glActiveTexture(GL_TEXTURE2);
glBindTexture(GL_TEXTURE_3D, cascades[upperIdx].probeAtlasTexture);
glUniform1i(..., "uUpperCascadeAtlas", 2);

// Unit 3: upper probeGridTexture (for isotropic fallback / A-B comparison)
glActiveTexture(GL_TEXTURE3);
glBindTexture(GL_TEXTURE_3D, cascades[upperIdx].probeGridTexture);
glUniform1i(..., "uUpperCascade", 3);

glUniform1i(..., "uUseDirectionalMerge", useDirectionalMerge ? 1 : 0);
```

**`useDirectionalMerge` toggle tracking in `render()`, inside the `cascadeReady` gating block (`src/demo3d.cpp:384`):**
```cpp
static bool lastDirectionalMerge = true;
if (useDirectionalMerge != lastDirectionalMerge) {
    lastDirectionalMerge = useDirectionalMerge;
    cascadeReady = false;   // triggers full rebake
}
```

---

## Key Learnings

### GL_NEAREST is not optional for atlas textures

`gl::createTexture3D()` defaults to `GL_LINEAR`. With D=4, each atlas probe tile is 4x4 texels. At a tile edge, bilinear sampling blends probe (px,py) with probe (px+1,py) -- cross-probe contamination that silently corrupts directional data.

The fix is to override immediately after `createTexture3D()` with raw GL calls. The correctness risk of forgetting this override is high and produces no error -- only a slightly-blurred atlas that looks plausible.

Even with `GL_NEAREST` set, `texelFetch` is used in the merge path rather than `texture()` -- eliminates any possibility of interpolation regardless of sampler state.

### Atlas alpha zeroing broke packed hit count everywhere

`reduction_3d.comp` writes `vec4(avg, 0.0)` -- alpha always 0. The old decode `surfH = int(alpha)` -> always returned 0. Two places silently broke:

1. **Mode 4 (HitType heatmap) in `radiance_debug.frag`** -- read `probeGridTexture` alpha expecting packed hit counts, always got 0 -> showed 100% miss (all red). Fix: mode 4 now reads atlas alpha directly per bin (`texelFetch(uAtlasTexture, ...)`) and counts `a > 0` (surface) / `a < 0` (sky).

2. **Probe fill rate readback in `demo3d.cpp`** -- `glGetTexImage` on `probeGridTexture`, decoded `buf[i*4+3]` -> always 0 -> fill bars showed 0%. Fix: dual-read pattern -- read `probeGridTexture` for RGB luminance only, read `probeAtlasTexture` separately and walk all D^2 bins per probe checking `a > 0` / `a < 0`.

**Lesson:** Any data stored in `probeGridTexture` alpha is fragile -- the reduction pass owns that texture entirely and writes what it wants. If packed hit counts are needed per-probe in the future, they belong in a separate texture or in the atlas alpha (already used for per-bin hit distance).

### Barrier ordering is per-cascade, not per-frame

The cascade update loop is C3 -> C2 -> C1 -> C0 (highest to lowest). Each cascade requires:
- Barrier after its own atlas bake (so the reduction reads a complete atlas)  
- Barrier after its own reduction (so the NEXT cascade's bake reads a complete isotropic grid -- used by isotropic fallback path)

A single `glMemoryBarrier(GL_ALL_BARRIER_BITS)` at end-of-frame is not sufficient: C2's atlas bake starts before C3's reduction is done.

### The `upperTxl` and `atlasTxl` are the same index

This was initially surprising. In Phase 4, the upper sample was a single `texture()` call at `uvwProbe` -- one lookup outside the loop. In Phase 5c, the lookup is inside the per-direction loop and the texel address happens to be `ivec3(probePos.x*D+dx, probePos.y*D+dy, probePos.z)` -- the same formula as the write target `atlasTxl`. This is correct by construction: co-located grids mean the upper cascade's bin (dx,dy) for this world position is stored at the same tile offset as the current cascade's bin. No separate `upperTxl` variable is needed; the code uses `atlasTxl` for both read and write.

### `radiance_3d.comp` retained both `uUpperCascade` and `uUpperCascadeAtlas`

Rather than removing the old isotropic sampler, both are bound. `uUseDirectionalMerge` selects between them at runtime. This enables A/B comparison in a live session without shader reload:
- `uUseDirectionalMerge=1` -> directional merge (Phase 5c), `texelFetch` from atlas
- `uUseDirectionalMerge=0` -> isotropic fallback (Phase 4 equivalent), `texture()` from probeGridTexture

The performance cost of binding an extra texture unit is negligible. The A/B value during visual validation is high.

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | Added `oAtlas` image3D write; added `uUpperCascadeAtlas`, `uUpperCascade`, `uUseDirectionalMerge`; removed `oRadiance` image write; added `uvwProbe` before loop; per-direction merge logic |
| `res/shaders/reduction_3d.comp` | **New file.** Atlas -> isotropic average per probe; writes `probeGridTexture` alpha=0 |
| `src/demo3d.h` | Added `GLuint probeAtlasTexture` to `RadianceCascade3D`; added `bool useDirectionalMerge`, `int atlasBinDx`, `int atlasBinDy` to `Demo3D` |
| `src/demo3d.cpp` | Atlas allocation (GL_NEAREST); image unit 0 -> atlas; unit 2 = upper atlas, unit 3 = upper isotropic; reduction dispatch + barriers; `useDirectionalMerge` tracking in update loop; `destroy()` atlas cleanup |

---

## Validation Status

| Test | Status |
|---|---|
| Build: 0 errors | ✅ |
| Atlas allocation: GL error check | Pending runtime |
| Mode 3 (Atlas raw): 4x4 tile blocks visible | Pending runtime |
| Mode 4 (HitType): surf/sky fractions non-trivial | Pending runtime |
| Mode 5 (Bin viewer): directional color separation near red wall | Pending runtime |
| A/B: directional vs isotropic merge visual difference | Pending runtime |
| GI image energy stable vs Phase 4 | Pending runtime |
| C3 guard: `uHasUpperCascade==0` -> zero upper, no NaN | Pending runtime |

---

## Pre-conditions for Phase 5e

- Phase 5c A/B shows visibly better directional color separation -- confirms merge is working
- If quality is acceptable at D=4 for all cascades, Phase 5e becomes lower priority
- **Precondition before deferring 5e:** the stale `baseRaysPerProbe` slider and per-cascade `raysPerProbe` UI must be cleaned up (hidden or relabeled) so the instrumentation matches the actual fixed-D^2 dispatch. Until then the UI actively misleads: it shows per-cascade ray counts that do not affect the shader.
- If C3 shows angular aliasing, revisit per-cascade D scaling: C0=2, C1=4, C2=8, C3=16 (atlas C3 becomes 512x512x32 = 64 MB -- memory headroom on RTX 2080 SUPER is ~7 GB, acceptable)

## Known Debug-Path Debt (as of 2026-04-28)

- **Mode 5 (Bin) was unreachable via [F] key** -- the cycle used `% 5` instead of `% 6`. Fixed in the same session (`src/demo3d.cpp:313`). Log labels also updated from stale "Direct"/"HitType" to "Atlas"/"HitType"/"Bin".
- **`baseRaysPerProbe` slider and `raysPerProbe` per-cascade** -- still live in UI and struct; do not affect the shader (D^2=16 is the actual dispatch). Cleanup deferred until after Phase 5c visual validation.
