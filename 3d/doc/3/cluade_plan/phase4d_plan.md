# Phase 4d — Filter Verification

**Date:** 2026-04-24  
**Branch:** 3d  
**Independent of:** Phase 4c (no dependency — can be verified at any point in Phase 4)  
**Status: Verification complete — no code changes required.**  
**Goal:** Verify that all 3D cascade probe textures have correct filter and wrap-mode settings, specifically that `GL_TEXTURE_WRAP_R` (the depth axis) is not accidentally omitted, and that `GL_LINEAR` interpolation is used consistently during upper-cascade sampling.

---

## Why This Matters

When `uUpperCascade` is sampled in `radiance_3d.comp`:

```glsl
vec3 upperSample = texture(uUpperCascade, uvwProbe).rgb;
```

Three texture parameters must be correct for probes to read cleanly:

| Parameter | Required | Reason |
|---|---|---|
| `GL_TEXTURE_WRAP_S` | `GL_CLAMP_TO_EDGE` | Probes at X boundary must not wrap to opposite side |
| `GL_TEXTURE_WRAP_T` | `GL_CLAMP_TO_EDGE` | Same for Y boundary |
| `GL_TEXTURE_WRAP_R` | `GL_CLAMP_TO_EDGE` | Z-axis — **the 3D-specific axis, easy to omit** |
| `GL_TEXTURE_MIN_FILTER` | `GL_LINEAR` | Smooth trilinear interpolation between adjacent probes |
| `GL_TEXTURE_MAG_FILTER` | `GL_LINEAR` | Consistent with min filter |

Missing `WRAP_R` is a common 3D texture bug: a probe at the Z-far boundary samples from the Z-near boundary instead, injecting light from the opposite side of the volume into the merge.

---

## Files to Verify

| File | What to Check |
|---|---|
| `src/gl_helpers.cpp` | `setTexture3DParameters()` — does it set all three wrap axes? |
| `include/gl_helpers.h` | Function signature / default arguments |
| `src/demo3d.cpp` | `RadianceCascade3D::initialize()` — does it call the helper for all cascade textures? |
| `res/shaders/radiance_3d.comp` | `uvwProbe` range — are coordinates normalized to `[0, 1]³`? |
| `res/shaders/radiance_debug.frag` | Slice / projection sampling — same normalization? |

---

## Verification Results

### `src/gl_helpers.cpp` — `setTexture3DParameters()`

```cpp
void setTexture3DParameters(
    GLuint texture,
    GLenum minFilter = GL_LINEAR,
    GLenum magFilter = GL_LINEAR,
    GLenum wrapMode  = GL_CLAMP_TO_EDGE
) {
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrapMode);   // ✓ present
    glBindTexture(GL_TEXTURE_3D, 0);
}
```

All three wrap axes are set. The default `GL_CLAMP_TO_EDGE` is applied to every 3D texture created via `gl::createTexture3D()`, which calls `setTexture3DParameters(texture)` automatically.

### Texture inventory

| Texture | Format | Filter | Wrap (S/T/R) |
|---|---|---|---|
| Cascade probes C0–C3 | `GL_RGBA16F` | `GL_LINEAR` | `GL_CLAMP_TO_EDGE` ✓ |
| SDF volume | `GL_R32F` | `GL_LINEAR` | `GL_CLAMP_TO_EDGE` ✓ |
| Albedo volume | `GL_RGBA8` | `GL_LINEAR` | `GL_CLAMP_TO_EDGE` ✓ |

### Shader coordinate range

`radiance_3d.comp`:
```glsl
vec3 uvwProbe = (worldPos - uGridOrigin) / uGridSize;
```
`worldPos` is a probe center computed from integer probe coordinates; `uGridOrigin` and `uGridSize` span the full volume. Result is in `[0, 1]³` by construction.

`radiance_debug.frag` slice and projection modes: all `texCoord` components are computed from normalised UV inputs in `[0, 1]`. Max/average projections use `t = (float(i) + 0.5) / float(samples)` — also `[0, 1]`.

No unnormalized coordinates or `texelFetch` calls found.

---

## Outcome — No Code Changes Required

**Phase 4d is a confirmed no-op.**

All filter and wrap settings were already correct when 4d was scoped. The risk (missing `WRAP_R`) does not exist in this codebase because `gl_helpers.cpp` uses a single `wrapMode` parameter that is applied to all three axes via a single helper function. Any future 3D texture creation that calls `gl::createTexture3D()` inherits the correct settings automatically.

---

## Implication for Phase 5

Phase 5 will add per-direction radiance storage to the upper cascade. The probe texture format may change (e.g., a texture array or a wider RGBA layout per direction). When that texture is introduced, `setTexture3DParameters()` (or its array equivalent) must be called with the same `GL_CLAMP_TO_EDGE` defaults — the helper makes this the path of least resistance.

---

## Status

| Check | Result |
|---|---|
| `WRAP_R` present in helper | ✓ Confirmed |
| All cascade textures use helper | ✓ Confirmed |
| Shader UVW in `[0, 1]³` | ✓ Confirmed |
| No `texelFetch` misuse | ✓ Confirmed |
| Code changes required | **None** |
