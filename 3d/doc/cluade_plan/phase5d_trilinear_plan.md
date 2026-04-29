# Phase 5d Trilinear Spatial Merge — Implementation Plan

**Date:** 2026-04-29
**Branch:** 3d
**Status:** Plan. Not yet implemented. Math corrected after Codex review 08.
**Prerequisite docs:** `phase5d_impl_learnings.md`, `doc/codex_plan/class/09_phase5d_trilinear_upper_lookup.md`

---

## Problem Statement

The current Phase 5d non-co-located merge reads one upper probe:

```glsl
ivec3 upperProbePos = probePos / uUpperToCurrentScale;  // probePos / 2
upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
```

Every C0 probe in the same 2×2×2 block reads the same single C1 parent, producing
blocky 2×2×2 steps in the merged radiance. The correct fix is **spatial trilinear
interpolation across the 8 surrounding upper probes** — addressing the same structural
gap as ShaderToy's `WeightedSample()`, but implementing only the spatial interpolation
half. Per-corner visibility weighting (the second half of `WeightedSample()`) is deferred:
the current visibility check is analytically inert and the per-corner version adds 8×
cost for zero measured benefit.

Additionally, the Phase 5d visibility block (`upperOccluded`) is a global zero — when
the check fires it discards contributions from all 8 neighbors at once. This is
structurally wrong. (The check is also proven analytically inert: `distToUpper < tMin_upper`
for all cascade pairs.) The block will be removed; the comment explaining why is preserved.

---

## What Changes

### 1. `res/shaders/radiance_3d.comp`

#### 1a. Two new uniforms (after `uUpperProbeCellSize`)

```glsl
// Phase 5d trilinear: upper cascade probe grid dimensions for 8-neighbor clamping.
// In co-located mode this equals uVolumeSize; in non-co-located it is halved per level.
uniform ivec3 uUpperVolumeSize;

// Phase 5d trilinear: 1=8-neighbor spatial trilinear (non-co-located only), 0=nearest-parent.
// Co-located mode ignores this; upperProbePos==probePos so trilinear is trivially correct.
uniform int   uUseSpatialTrilinear;
```

#### 1b. New function `sampleUpperDirTrilinear()` (after `sampleUpperDir()`)

```glsl
// Phase 5d trilinear: spatially blend 8 surrounding upper probes at the same direction bin.
// triP000/triF are pre-computed per probe (hoisted outside the direction loop).
// Each corner calls sampleUpperDir(), which already handles directional bilinear (Phase 5f).
// The +1 corners are clamped to [0, uUpperVolumeSize-1] to prevent OOB texelFetch.
// At the high spatial border triF.x/y/z == 0.0 so +1 samples have zero blend weight —
// clamping to the same border probe is correct (GL_CLAMP_TO_EDGE semantics).
vec3 sampleUpperDirTrilinear(ivec3 triP000, vec3 triF, vec3 rayDir, int Du) {
    ivec3 hi   = uUpperVolumeSize - ivec3(1);  // max valid probe index per axis
    ivec3 p100 = clamp(triP000 + ivec3(1,0,0), ivec3(0), hi);
    ivec3 p010 = clamp(triP000 + ivec3(0,1,0), ivec3(0), hi);
    ivec3 p110 = clamp(triP000 + ivec3(1,1,0), ivec3(0), hi);
    ivec3 p001 = clamp(triP000 + ivec3(0,0,1), ivec3(0), hi);
    ivec3 p101 = clamp(triP000 + ivec3(1,0,1), ivec3(0), hi);
    ivec3 p011 = clamp(triP000 + ivec3(0,1,1), ivec3(0), hi);
    ivec3 p111 = clamp(triP000 + ivec3(1,1,1), ivec3(0), hi);
    vec3 s000 = sampleUpperDir(triP000, rayDir, Du);
    vec3 s100 = sampleUpperDir(p100,   rayDir, Du);
    vec3 s010 = sampleUpperDir(p010,   rayDir, Du);
    vec3 s110 = sampleUpperDir(p110,   rayDir, Du);
    vec3 s001 = sampleUpperDir(p001,   rayDir, Du);
    vec3 s101 = sampleUpperDir(p101,   rayDir, Du);
    vec3 s011 = sampleUpperDir(p011,   rayDir, Du);
    vec3 s111 = sampleUpperDir(p111,   rayDir, Du);
    vec3 sx00 = mix(s000, s100, triF.x);
    vec3 sx10 = mix(s010, s110, triF.x);
    vec3 sx01 = mix(s001, s101, triF.x);
    vec3 sx11 = mix(s011, s111, triF.x);
    vec3 sxy0 = mix(sx00, sx10, triF.y);
    vec3 sxy1 = mix(sx01, sx11, triF.y);
    return mix(sxy0, sxy1, triF.z);
}
```

#### 1c. Replace the hoisted block in `main()` (lines 222–247)

**Remove:** the `upperOccluded` bool and the entire Phase 5d visibility block.

**Replace with:** trilinear coordinate setup (still hoisted above the direction loop).

```glsl
    // Phase 5d: upper probe index for co-located path and isotropic fallback.
    // Co-located: upperProbePos == probePos (scale=1, displacement=0, exact).
    // Non-co-located: used only by isotropic texelFetch fallback; directional path uses trilinear.
    ivec3 upperProbePos = (uUpperToCurrentScale > 0)
        ? (probePos / uUpperToCurrentScale)
        : ivec3(0);

    // Phase 5d trilinear: pre-compute 8-neighbor base corner and blend weights.
    // Only computed for non-co-located + directional merge + trilinear ON.
    // The -0.5 converts edge-aligned grid coords to center-aligned probe-center coords
    // (same pattern as directional bilinear's -0.5 in sampleUpperDir).
    // Clamp to max(uUpperVolumeSize - 2, 0) so p000+1 is always within atlas bounds.
    // Note: Phase 5d visibility (inert — distToUpper < tMin_upper for all cascade pairs)
    // is intentionally removed. A per-neighbor weighted version would be correct but adds
    // 8x cost for a check that has never fired analytically.
    ivec3 triP000 = ivec3(0);
    vec3  triF    = vec3(0.0);
    if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0
            && uHasUpperCascade != 0 && uUseDirectionalMerge != 0) {
        vec3 upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5;
        // Clamp continuous coordinate to [0, upperRes-1] BEFORE floor/fract.
        // This is the spatial analogue of Phase 5f's:
        //   octScaled = clamp(dirToOct(dir)*D - 0.5, 0, D-1)
        // Without the clamp: a probe at the low edge gives upperGrid=-0.25,
        // fract(-0.25)=0.75 → 75% weight toward probe 1 even though it should
        // be 100% probe 0. The clamp pins both the base corner AND the fractional
        // weight to the boundary simultaneously.
        // At low border:  clamped=0   → floor=0,  fract=0 → 100% probe 0
        // At high border: clamped=N-1 → floor=N-1, fract=0 → 100% probe N-1
        vec3 upperGridClamped = clamp(upperGrid,
                                      vec3(0.0),
                                      vec3(uUpperVolumeSize - ivec3(1)));
        triP000 = ivec3(floor(upperGridClamped));  // guaranteed in [0, upperRes-1]
        triF    = fract(upperGridClamped);          // 0.0 at both spatial borders
    }
```

#### 1d. Replace `upperDir` assignment inside the direction loop (lines 264–275)

**Remove** `if (upperOccluded) upperDir = vec3(0.0);`.

**Replace** the `if (uHasUpperCascade != 0)` block with:

```glsl
            vec3 upperDir = vec3(0.0);
            if (uHasUpperCascade != 0) {
                if (uUseDirectionalMerge != 0) {
                    // Non-co-located + trilinear: blend 8 upper probes spatially
                    if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0)
                        upperDir = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
                    else
                        // Co-located (displacement=0, trilinear trivially correct) or trilinear OFF
                        upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
                } else if (uUseDirBilinear != 0) {
                    // Isotropic path: hardware trilinear from GL_LINEAR probeGridTexture
                    upperDir = texture(uUpperCascade, uvwProbe).rgb;
                } else {
                    // Isotropic nearest-probe
                    upperDir = texelFetch(uUpperCascade, upperProbePos, 0).rgb;
                }
            }
```

---

### 2. `src/demo3d.h`

Add after `bool useDirBilinear;`:

```cpp
/** Phase 5d trilinear: 8-neighbor spatial interpolation when reading upper cascade
 *  in non-co-located mode. true=trilinear (default), false=nearest-parent (Phase 5d baseline).
 *  No effect in co-located mode (upper probe is at same position; trilinear is trivially exact). */
bool useSpatialTrilinear;
```

---

### 3. `src/demo3d.cpp`

#### 3a. Constructor initializer list

Add `, useSpatialTrilinear(true)` after `, useDirBilinear(true)`.

#### 3b. `updateSingleCascade()` — two new uniforms (after `uUpperDirRes` push)

```cpp
    // Phase 5d trilinear: upper cascade probe grid dimensions for 8-neighbor clamping.
    int upperRes = hasUpper5d ? cascades[upperIdx5d].resolution : 1;
    glm::ivec3 upperVolRes(upperRes);
    glUniform3iv(glGetUniformLocation(prog, "uUpperVolumeSize"), 1, glm::value_ptr(upperVolRes));
    glUniform1i(glGetUniformLocation(prog, "uUseSpatialTrilinear"), useSpatialTrilinear ? 1 : 0);
```

#### 3c. `render()` — tracking block (after `lastDirBilinear` block)

```cpp
    static bool lastSpatialTrilinear = true;
    if (useSpatialTrilinear != lastSpatialTrilinear) {
        lastSpatialTrilinear = useSpatialTrilinear;
        cascadeReady = false;
        std::cout << "[5d] spatial trilinear: "
                  << (useSpatialTrilinear ? "ON (8-neighbor)" : "OFF (nearest-parent)")
                  << std::endl;
    }
```

No atlas rebuild — only the uniform value changes.

#### 3d. `renderCascadePanel()` — checkbox under Phase 5d section

After the co-located checkbox block (after the C0 resolution combo):

```cpp
    // Phase 5d trilinear: only meaningful in non-co-located mode
    {
        bool disabled = useColocatedCascades;
        if (disabled) ImGui::BeginDisabled();
        ImGui::Checkbox("Spatial trilinear merge (Phase 5d)", &useSpatialTrilinear);
        HelpMarker(
            "Non-co-located mode only. Has no effect when co-located is ON.\n\n"
            "ON  (default): when a lower cascade misses, blends the 8 surrounding\n"
            "     upper probes using trilinear interpolation weighted by the lower\n"
            "     probe's fractional position within the upper cell.\n"
            "     Cost: 8x directional reads per miss vs 1x (static bake only).\n\n"
            "OFF: reads only the single nearest upper probe (Phase 5d baseline).\n"
            "     All 8 lower probes in a 2x2x2 block read the same parent -- blocky.\n\n"
            "The -0.5 offset in the trilinear formula maps upper probe centers to\n"
            "integers, matching the same center-to-center convention as Phase 5f\n"
            "directional bilinear (sampleUpperDir's octScaled = oct*D - 0.5).");
        if (disabled) ImGui::EndDisabled();
        ImGui::SameLine();
        if (useColocatedCascades)
            ImGui::TextDisabled("(co-located: no effect)");
        else
            ImGui::TextDisabled(useSpatialTrilinear ? "(8-neighbor)" : "(nearest-parent)");
    }
```

---

## Correctness Invariants

| Scenario | Behaviour |
|---|---|
| Co-located (`uUpperToCurrentScale==1`) | `triP000`/`triF` never computed; `sampleUpperDir(upperProbePos,...)` used; upper probe at same world position; displacement=0; identical to Phase 5c/5f baseline. Zero regression. |
| Non-co-located, trilinear OFF | `sampleUpperDir(probePos/2, ...)` — same as original Phase 5d. Zero regression. |
| Non-co-located, trilinear ON, co-located checkbox ON | Guarded by `uUpperToCurrentScale == 2`; co-located sets scale=1; trilinear branch not taken. |
| Upper cascade resolution = 1 (degenerate C3=1³ at C0Res=8 non-co-located) | `max(ivec3(1) - ivec3(2), ivec3(0)) = ivec3(0)`; all 8 corners collapse to p000=ivec3(0); trilinear returns s000 × 1.0. Correct degenerate fallback. |
| Atlas bounds (non-co-located C1=16³, trilinear ON) | `clamp(p000, 0, 14)` → p000.z ≤ 14; p000.z+1 = 15 < 16 (atlas depth). Safe by construction. |
| `uUseSpatialTrilinear` uniform when no upper | Guarded by `uHasUpperCascade != 0 && uUseDirectionalMerge != 0`; branch not entered. |

---

## Performance Analysis

`sampleUpperDir` with bilinear direction ON (Phase 5f default) does 4 texelFetch/call.

| Config | Fetches per miss per direction bin |
|---|---|
| Co-located, any | 4 (Phase 5f bilinear, single probe) |
| Non-co-located, trilinear OFF | 4 (nearest-parent, single probe) |
| Non-co-located, trilinear ON | 8 corners × 4 = **32** fetches |

At D=4 (16 bins), non-co-located 32³ probes, 3 merging cascades:
- 32,768 × 16 × 32 × 3 ≈ **50M texelFetch** for the full bake

Acceptable for a static-scene bake. All reads are within each upper probe's tile
(GL_NEAREST guaranteed — `sampleUpperDir` uses clamped texelFetch).

---

## Stop Conditions

| Test | Expected |
|---|---|
| Build: 0 errors | Both modes |
| Co-located, trilinear ON: identical GI to pre-change | Zero regression |
| Non-co-located, trilinear OFF: identical GI to current Phase 5d | Zero regression |
| Non-co-located, trilinear ON: blocky 2×2×2 stepping removed | Smooth probe transitions |
| Log: `[5d] spatial trilinear: ON (8-neighbor)` on toggle | Confirmed |
| Degenerate (C0Res=8, non-co-located): no crash / GL error | Graceful fallback |
| Non-co-located, trilinear ON vs OFF: blocky 2x2x2 stepping removed in final GI image | Smooth spatial gradient |
| Avg/MaxProj debug (mode 2/1): no hard probe-grid boundaries visible with trilinear ON | Confirmed |
| Note: Mode 3 (Atlas) is NOT a valid acceptance test — it shows the current cascade's own tile layout, not the upper-cascade read quality |
