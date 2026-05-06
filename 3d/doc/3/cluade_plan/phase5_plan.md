# Phase 5 Plan — Directional-Correct Merge

**Date:** 2026-04-24  
**Branch:** 3d  
**Status:** Next  
**Prerequisite:** Phase 4 complete. ✅  
**Revised:** After Codex review `codex_critic_phase5/01_phase5_plan_review.md` — all six findings accepted.

---

## Goal

Replace the isotropic `upperSample` with per-direction radiance lookup. Each ray that misses its interval should pull from the upper cascade's stored value for *that specific direction*, not the probe's omnidirectional average.

**Why:** The 4c A/B test confirmed that all remaining GI banding at cascade boundaries is directional mismatch. The isotropic `texture(uUpperCascade, uvwProbe).rgb` returns one averaged value for all directions. A red wall behind the probe looks the same as a blue ceiling — the color bleeds incorrectly across direction bins. No further work within the isotropic model can fix this.

---

## Architecture Change

| Aspect | Phase 4 (current) | Phase 5 (target) |
|---|---|---|
| Storage per probe | 1 × RGBA16F value (rgb=avg radiance, a=packed stats) | D×D × RGBA16F values (rgb=dir radiance, w=hit dist) + reduced isotropic grid |
| Atlas texture per cascade | none | (32·D)×(32·D)×32 RGBA16F |
| Isotropic grid per cascade | 32×32×32 RGBA16F (baked directly) | 32×32×32 RGBA16F (written by reduction pass after atlas) |
| Direction scheme | Fibonacci sphere (not invertible) | Octahedral binning D×D (analytically invertible) |
| Merge lookup | `texture(uUpperCascade, uvwProbe).rgb` | `texelFetch(uUpperCascadeAtlas, ivec3(...))` at exact direction bin |
| Rays per probe (start) | variable per cascade (C0=8 … C3=64) | D²=16 fixed (D=4); per-cascade scaling deferred to Phase 5e A/B |
| Final image display path | `raymarch.frag` reads `probeGridTexture` | unchanged — reduction pass keeps `probeGridTexture` populated |

**3D adaptation note (not ShaderToy parity):** The ShaderToy's 4-neighbor spatial interpolation during merge exists because its probe grid is surface-attached with different spatial densities per cascade. This implementation uses co-located 32³ grids across all cascades, which eliminates spatial probe-position misalignment by construction. There is no need to interpolate between upper-cascade probe neighbors — the upper probe is at the exact same world position as the current probe. This is a deliberate 3D volumetric architecture choice, not a faithful port of the ShaderToy merge.

---

## Phase 5a — Octahedral Direction Encoding

**Goal:** Replace the non-invertible Fibonacci sphere with a D×D octahedral bin grid. This makes direction ↔ bin index bijective, which is required for the atlas write and upper-cascade read paths.

**D = 4 (16 directional bins per probe) for Phase 5.** Per-cascade D scaling is evaluated in Phase 5e after quality A/B.

**Direction ↔ bin math:**

```glsl
// Standard octahedral parameterization: full sphere → [0,1]²
vec2 dirToOct(vec3 dir) {
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));
    if (dir.z < 0.0) {
        vec2 s = sign(dir.xy) * (1.0 - abs(dir.yx));
        dir.xy = s;
    }
    return dir.xy * 0.5 + 0.5;
}

vec3 octToDir(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec3 d = vec3(uv, 1.0 - abs(uv.x) - abs(uv.y));
    if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * sign(d.xy);
    return normalize(d);
}

ivec2 dirToBin(vec3 dir, int D) {
    vec2 oct = dirToOct(dir);
    return clamp(ivec2(floor(oct * float(D))), ivec2(0), ivec2(D - 1));
}

vec3 binToDir(ivec2 bin, int D) {
    vec2 oct = (vec2(bin) + 0.5) / float(D);
    return octToDir(oct);
}
```

**Changes to `radiance_3d.comp`:**
- Remove `getRayDirection(int idx)` (Fibonacci)
- Add `dirToBin()`, `binToDir()`, `dirToOct()`, `octToDir()`
- Main ray loop: `for dy in [0,D), for dx in [0,D)` → `rayDir = binToDir(ivec2(dx,dy), D)`
- Add `uniform int uDirRes;` (= 4)

**Stop conditions:**
- Shader compiles with no errors
- `dirToBin(binToDir(b, D), D) == b` holds for all valid b (bin round-trip stable)
- Full sphere coverage: no direction returns an out-of-range bin
- Visual quality at D=4 shows directional color separation in GI output; no obvious pole artifacts

---

## Phase 5b — Per-Direction Atlas Texture Storage

**Goal:** Allocate a D×D tile atlas per cascade and write each ray's radiance + hit distance into its directional bin.

**New texture layout:**

```
Atlas dimensions: (32 * D) × (32 * D) × 32 RGBA16F
Probe (px, py, pz) occupies tile at x:[px*D, px*D+D), y:[py*D, py*D+D), z:pz

Direction bin (dx, dy) for probe (px, py, pz):
    atlas_texel = ivec3(px * D + dx,  py * D + dy,  pz)
    RGB = radiance for that direction
    W   = ray hit distance (>0 surface, <0 sky, =0 miss placeholder)
```

With D=4: 128×128×32 RGBA16F = **4 MB per cascade**, **16 MB total**.

**Changes to `demo3d.h`:**
```cpp
GLuint probeAtlasTexture[4];   // per-direction atlas, one per cascade
int    dirRes;                  // D = 4
```

**Changes to `demo3d.cpp`:**
- `initCascades()`: allocate `probeAtlasTexture[ci]` at `(res*D, res*D, res)` RGBA16F with `GL_NEAREST` filtering (no bin bleed)
- `updateSingleCascade()`: bind atlas as `layout(rgba16f) image3D oAtlas`; push `uniform int uDirRes`
- Keep `probeGridTexture[ci]` — the reduction pass (5b-1) populates it after the atlas bake

**Changes to `radiance_3d.comp` write path:**
```glsl
for (int dy = 0; dy < uDirRes; ++dy) {
    for (int dx = 0; dx < uDirRes; ++dx) {
        vec3  rayDir = binToDir(ivec2(dx, dy), uDirRes);
        vec4  hit    = raymarchSDF(worldPos, rayDir, tMin, tMax);
        vec3  rad;
        float dist;
        if      (hit.a < 0.0) { rad = hit.rgb; dist = -1.0; }  // sky
        else if (hit.a > 0.0) { rad = hit.rgb; dist =  hit.a; } // surface
        else                  { rad = vec3(0.0); dist = 0.0; }  // miss placeholder
        ivec3 txl = ivec3(probePos.x * uDirRes + dx,
                          probePos.y * uDirRes + dy,
                          probePos.z);
        imageStore(oAtlas, txl, vec4(rad, dist));
    }
}
```

**Stop conditions:**
- Atlas allocates without GL error
- Shader compiles with `oAtlas` binding
- Debug mode: sample atlas at fixed bin (dx=0, dy=0) across all probes → shows directional radiance slice

---

## Phase 5b-1 — Atlas Reduction Pass (required for final image)

**Goal:** Keep `probeGridTexture` valid for `raymarch.frag` by averaging the D² atlas bins per probe after the atlas bake.

**Why this approach:** `raymarch.frag` reads `texture(uRadiance, uvw).rgb` from the isotropic `probeGridTexture`. Modifying the display shader is out of scope for Phase 5. The reduction pass writes a direction-averaged value back into `probeGridTexture`, keeping the display path unchanged. The averaged value is directionally less precise than the atlas but it represents the same energy — and the display shader only uses it for the final GI contribution, not for cascade merge.

**Implementation:** Second compute dispatch, one thread per probe:
```glsl
// reduction_3d.comp (new shader, or second entry point in radiance_3d.comp)
ivec3 probePos = ivec3(gl_GlobalInvocationID);
vec3 avg = vec3(0.0);
for (int dy = 0; dy < uDirRes; ++dy)
    for (int dx = 0; dx < uDirRes; ++dx)
        avg += texelFetch(uAtlas,
                          ivec3(probePos.x * uDirRes + dx,
                                probePos.y * uDirRes + dy,
                                probePos.z), 0).rgb;
avg /= float(uDirRes * uDirRes);
// Preserve packed hit count from Phase 4 alpha convention
imageStore(oRadiance, probePos, vec4(avg, 0.0));
```

**Dispatch:** one `(32/4)³` dispatch per cascade, after the atlas bake dispatch, in the existing `updateSingleCascade()` order.

**Stop conditions:**
- Isotropic grid reads (final image) look at least as good as Phase 4 baseline with merge ON
- No regression in GI energy level visible in mean-lum readback

---

## Phase 5c — Directional Upper Cascade Merge

**Goal:** Replace `texture(uUpperCascade, uvwProbe).rgb` with `texelFetch` from the upper cascade atlas at the exact direction bin for the current ray. Miss rays and blend-zone surface hits both use the directional lookup.

**Critical detail: use `texelFetch`, not `texture`.** The atlas is packed with GL_NEAREST. Even so, use `texelFetch` with integer coordinates throughout the merge path — it eliminates any possibility of interpolation across bin or probe boundaries.

**Merge inside the per-direction loop:**
```glsl
for (int dy = 0; dy < uDirRes; ++dy) {
    for (int dx = 0; dx < uDirRes; ++dx) {
        vec3 rayDir = binToDir(ivec2(dx, dy), uDirRes);
        vec4 hit    = raymarchSDF(worldPos, rayDir, tMin, tMax);
        vec3 rad;

        // Precompute upper atlas texel for this bin (same probe position, co-located grids)
        ivec3 upperTxl = ivec3(probePos.x * uDirRes + dx,
                               probePos.y * uDirRes + dy,
                               probePos.z);

        if (hit.a < 0.0) {
            rad = hit.rgb;   // sky
        } else if (hit.a > 0.0) {
            // Surface hit — 4c blend zone uses directional upper sample
            rad = hit.rgb;
            if (uHasUpperCascade != 0 && blendWidth > 0.0) {
                float l = 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
                if (l < 1.0) {
                    vec3 upperDir = texelFetch(uUpperCascadeAtlas, upperTxl, 0).rgb;
                    rad = rad * l + upperDir * (1.0 - l);
                }
            }
        } else {
            // Miss — fetch upper cascade at THIS direction bin
            rad = (uHasUpperCascade != 0)
                ? texelFetch(uUpperCascadeAtlas, upperTxl, 0).rgb
                : vec3(0.0);
        }

        ivec3 atlasTxl = ivec3(probePos.x * uDirRes + dx,
                               probePos.y * uDirRes + dy,
                               probePos.z);
        imageStore(oAtlas, atlasTxl, vec4(rad, hit.a));
    }
}
```

**New uniform:**
```glsl
uniform sampler3D uUpperCascadeAtlas;  // replaces uUpperCascade
```

**Changes to `demo3d.cpp`:**
- `updateSingleCascade()`: bind `probeAtlasTexture[ci+1]` as `uUpperCascadeAtlas` for cascade ci
- Remove `uUpperCascade` / `probeGridTexture` binding from compute pass (atlas only)
- `uHasUpperCascade` logic unchanged

**Stop conditions:**
- Renders without GL error or NaN
- GI near a red wall: leftward bins show red, rightward bins show the opposite wall's color
- Cascade boundary banding visibly reduced vs Phase 4 baseline
- C3 (`uHasUpperCascade == 0`) produces zero upper contribution, no NaN

---

## Phase 5e — Per-Cascade D Scaling A/B (quality gate)

**Goal:** Determine whether D=4 (16 bins) for all cascades is visually sufficient, or whether upper cascades need more directional bins to avoid angular aliasing.

**After Phase 5c is working**, run this A/B:

| Configuration | C0 | C1 | C2 | C3 |
|---|---|---|---|---|
| Fixed D=4 (starting point) | 16 bins | 16 bins | 16 bins | 16 bins |
| ShaderToy-scaled D | 4 bins | 16 bins | 64 bins | 256 bins |

**Expected outcome:** If C3 banding/aliasing is visible with D=4, adopt per-cascade scaling `D_ci = 2^(ci+1)`. Memory at full scaling: C0=0.25MB, C1=1MB, C2=4MB, C3=16MB → ~21MB total (acceptable on RTX 2080 SUPER).

**If A/B shows no visible difference:** lock in D=4 for all cascades.

---

## Architecture Note: Probe Visibility Weighting (5d — deferred indefinitely)

The ShaderToy `WeightedSample()` fetches the upper probe's stored ray distance in the direction toward the current probe, and zeros out the contribution if the upper probe's ray was occluded before reaching the current probe.

In this implementation, all cascades share the same 32³ grid at the same world origin. The upper cascade probe IS the current probe — same world position. Therefore `probeDist == 0` always, and the visibility check trivially passes. Implementing this check produces no visual change.

**This becomes relevant only if cascades adopt different spatial resolutions.** Until then it is not implemented.

---

## Validation

| Test | Method | Expected |
|---|---|---|
| Bin round-trip | CPU or shader assert: `dirToBin(binToDir(b,D),D)==b` | Exact match for all 16 bins |
| Full sphere coverage | All bin indices reachable from unique directions | 16 distinct bins populated |
| Red wall isolation | GI-only mode, probe near red wall | Red in leftward bins; opposite wall color in rightward bins |
| Cascade boundary banding | Full GI, compare 4c vs 5c at C0/C1, C1/C2 borders | Visibly smoother transition |
| Final image parity | Mode 0 (full GI) vs Phase 4 baseline at same settings | Energy level stable, GI quality ≥ Phase 4 |
| C3 guard | C3 merge path | `uHasUpperCascade==0` → zero upper sample, no NaN |
| Mean-lum stability | Readback panel after Phase 5c bake | Mean lum per cascade comparable to Phase 4 (within 20%) |

---

## Files to Touch

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | Octahedral bins; per-direction atlas write; directional merge via `texelFetch` |
| `res/shaders/reduction_3d.comp` (new) | Atlas → isotropic average per probe; writes `probeGridTexture` |
| `src/demo3d.h` | Add `GLuint probeAtlasTexture[4]`; add `int dirRes` |
| `src/demo3d.cpp` | Allocate atlas (GL_NEAREST); bind for write/read; push `uDirRes`, `uUpperCascadeAtlas`; dispatch reduction pass |
| `res/shaders/radiance_debug.frag` | Add atlas direction-bin debug mode (sample fixed bin across all probes) |

---

## Definition of Done

Phase 5 done when:
- GI banding at cascade interval boundaries is visibly reduced vs Phase 4 baseline with per-direction merge active
- Red and green walls show distinct directional color separation in probe atlas debug mode
- Final image (mode 0) energy level is stable and not regressed vs Phase 4
