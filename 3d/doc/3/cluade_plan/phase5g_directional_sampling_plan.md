# Phase 5g — Directional Atlas Sampling in Final Renderer

**Date:** 2026-04-29
**Branch:** 3d
**Snapshot as of:** b3b04ef
**Priority:** 3 (after Phase 5h)
**Prerequisite:** Phase 5h (shadow ray baseline — gives correctly shadowed direct term to compare against)
**Updated:** 2026-04-29 — revised after Codex review 11 (texture unit fix, spatial trilinear, claim downgrade)

---

## Problem Statement

`raymarch.frag` reads only the isotropic probe average (`uRadiance`) for indirect GI:

```glsl
vec3 indirect = texture(uRadiance, uvw).rgb;
surfaceColor += indirect;
```

The isotropic average is produced by `reduction_3d.comp`:

```glsl
avg += texelFetch(uAtlas, ...).rgb;
avg /= float(uDirRes * uDirRes);   // D=4: divide by 16
```

This is architecturally incorrect for two reasons:

1. **Shadow signal diluted ~8–16×**: the ceiling light occupies 1–2 direction bins out
   of D²=16. The shadow (difference between a lit probe and a shadowed probe) is spread
   uniformly across all 16 bins in the average. The signal is small.

2. **Subsurface contributions included**: the average includes radiance from directions
   below the surface (dot(binDir, normal) < 0). These directions cannot physically
   contribute to a surface's illumination. Including them reduces the shadow contrast
   further and introduces light-bleeding artifacts.

A better approximation is cosine-weighted hemisphere integration over the directional atlas:

```
E(p, n) ≈ (Σ L(p, ω_b) · max(0, n·ω_b)) / (Σ max(0, n·ω_b))
```

Summed over D² bins. Note: this is an approximation, not an exact irradiance integral —
equal `dx,dy` bins in an octahedral parameterization do not subtend equal solid angles.
A fully correct version would apply a per-bin solid-angle Jacobian. For D=4 the
distortion is small and the approximation is acceptable.

This approximation removes two sources of error present in the isotropic average:
1. Back-facing bins (dot(ω_b, n) < 0) are excluded — they physically cannot illuminate
   the surface and only dilute the signal.
2. Bins aligned with the surface normal receive higher weight, matching Lambert's law.

---

## Architecture Decision

Two options to expose the directional atlas to `raymarch.frag`:

### Option A — Normal-weighted reduction texture

Modify `reduction_3d.comp` to output two images: isotropic (current) and a surface
normal → hemisphere-weighted texture. `raymarch.frag` reads the second texture and uses
the surface normal to pick which texture to read.

**Problem:** the normal is not known at reduction time — it depends on where the primary
ray hits. The reduction pass runs over the probe grid, not over pixels. This option
requires the reduction to pre-compute 6 face-weighted textures (±X, ±Y, ±Z hemisphere
averages) or a SH-compressed representation. Significant complexity.

### Option B — Direct atlas read in `raymarch.frag` (preferred)

Pass the directional atlas texture (`probeAtlasTexture`) to `raymarch.frag`. At each
surface hit, compute the probe position in atlas space and sum D² bins with cosine
weights:

```glsl
// Fetch probe index for this surface point
vec3 uvwProbe = (pos - uGridOrigin) / uGridSize;
ivec3 probeCoord = ivec3(floor(uvwProbe * vec3(uVolumeSize)));

vec3 irrad = vec3(0.0); float wsum = 0.0;
for (int dy = 0; dy < uDirRes; ++dy)
    for (int dx = 0; dx < uDirRes; ++dx) {
        vec3 bdir = binToDir(ivec2(dx, dy), uDirRes);
        float w   = max(0.0, dot(bdir, normal));
        vec3 bin  = texelFetch(uAtlas, ivec3(probeCoord.x*uDirRes+dx,
                                              probeCoord.y*uDirRes+dy,
                                              probeCoord.z), 0).rgb;
        irrad += bin * w; wsum += w;
    }
irrad /= max(wsum, 1e-4);
```

**Concern:** D²=16 texelFetch calls per pixel. At 1280×720 with 30% surface coverage,
~9M fetches/frame. Fast in practice — these are coherent reads within one probe tile.

**Option B is preferred.** It uses the already-baked directional data directly and
requires no changes to the bake or reduction passes.

---

## What Changes

### `res/shaders/raymarch.frag`

#### New uniforms

```glsl
uniform sampler3D uDirectionalAtlas;  // 5g: per-direction atlas (probeAtlasTexture of C0)
uniform ivec3     uAtlasVolumeSize;   // probe grid dims (uVolumeSize of C0)
uniform vec3      uAtlasGridOrigin;   // uGridOrigin of C0
uniform vec3      uAtlasGridSize;     // uGridSize of C0
uniform int       uAtlasDirRes;       // D for C0 atlas
uniform int       uUseDirectionalGI;  // 5g: 1=cosine-weighted atlas, 0=isotropic (default)
```

#### `octToDir` / `binToDir` helpers (copy from radiance_3d.comp)

```glsl
vec3 octToDir(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec3 d = vec3(uv, 1.0 - abs(uv.x) - abs(uv.y));
    if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * sign(d.xy);
    return normalize(d);
}
vec3 binToDir(ivec2 bin, int D) {
    return octToDir((vec2(bin) + 0.5) / float(D));
}
```

#### New `sampleProbeDir()` + `sampleDirectionalGI()` functions

`sampleProbeDir` computes the cosine-weighted sum for one probe tile.
`sampleDirectionalGI` trilinearly blends 8 surrounding probes — same -0.5 pattern as
Phase 5d trilinear and Phase 5f directional bilinear. Preserves spatial interpolation.

```glsl
// Cosine-weighted irradiance from one probe's D×D atlas tile
vec3 sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3 irrad = vec3(0.0); float wsum = 0.0;
    for (int dy = 0; dy < D; ++dy)
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float w    = max(0.0, dot(bdir, normal));
            irrad += texelFetch(uDirectionalAtlas,
                         ivec3(pc.x*D+dx, pc.y*D+dy, pc.z), 0).rgb * w;
            wsum  += w;
        }
    return irrad / max(wsum, 1e-4);
}

// Trilinear spatial blend across 8 probes, each cosine-weighted.
// -0.5 offset: same center-aligned probe-grid convention as Phase 5d/5f.
// clamp-before-floor/fract: same border invariant as Phase 5d trilinear.
// Cost: 8 probes × D² bins = 128 texelFetch/pixel at D=4.
vec3 sampleDirectionalGI(vec3 pos, vec3 normal) {
    vec3 uvw = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return vec3(0.0);
    vec3 pg    = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                       vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    ivec3 p000 = ivec3(floor(pg));
    vec3  f    = fract(pg);
    ivec3 hi   = uAtlasVolumeSize - ivec3(1);
    int D      = uAtlasDirRes;
    vec3 s000 = sampleProbeDir(p000,                              normal, D);
    vec3 s100 = sampleProbeDir(clamp(p000+ivec3(1,0,0),ivec3(0),hi), normal, D);
    vec3 s010 = sampleProbeDir(clamp(p000+ivec3(0,1,0),ivec3(0),hi), normal, D);
    vec3 s110 = sampleProbeDir(clamp(p000+ivec3(1,1,0),ivec3(0),hi), normal, D);
    vec3 s001 = sampleProbeDir(clamp(p000+ivec3(0,0,1),ivec3(0),hi), normal, D);
    vec3 s101 = sampleProbeDir(clamp(p000+ivec3(1,0,1),ivec3(0),hi), normal, D);
    vec3 s011 = sampleProbeDir(clamp(p000+ivec3(0,1,1),ivec3(0),hi), normal, D);
    vec3 s111 = sampleProbeDir(clamp(p000+ivec3(1,1,1),ivec3(0),hi), normal, D);
    vec3 sx00 = mix(s000,s100,f.x); vec3 sx10 = mix(s010,s110,f.x);
    vec3 sx01 = mix(s001,s101,f.x); vec3 sx11 = mix(s011,s111,f.x);
    vec3 sxy0 = mix(sx00,sx10,f.y); vec3 sxy1 = mix(sx01,sx11,f.y);
    return mix(sxy0, sxy1, f.z);
}
```

#### Replace `indirect` sampling in mode 0

```glsl
if (uUseCascade) {
    vec3 indirect = (uUseDirectionalGI != 0)
        ? sampleDirectionalGI(pos, normal)
        : texture(uRadiance, uvw).rgb;
    surfaceColor += indirect;
}
```

---

### `src/demo3d.h`

```cpp
bool useDirectionalGI;   // 5g: true=cosine-weighted atlas (default OFF until validated), false=isotropic
```

### `src/demo3d.cpp`

#### Constructor
`, useDirectionalGI(false)` — default OFF so isotropic path is preserved as baseline.

#### `raymarchPass()` — bind atlas and push uniforms

```cpp
// 5g: bind C0 directional atlas on unit 3 (units 0-2 = SDF/Radiance/Albedo — do not stomp)
auto& c0 = cascades[0];
glActiveTexture(GL_TEXTURE3);
glBindTexture(GL_TEXTURE_3D, c0.probeAtlasTexture);
glUniform1i(glGetUniformLocation(raymarchShader, "uDirectionalAtlas"), 3);
glUniform3iv(glGetUniformLocation(raymarchShader, "uAtlasVolumeSize"), 1,
             glm::value_ptr(glm::ivec3(c0.resolution)));
glUniform3fv(glGetUniformLocation(raymarchShader, "uAtlasGridOrigin"), 1,
             glm::value_ptr(volumeOrigin));
glUniform3fv(glGetUniformLocation(raymarchShader, "uAtlasGridSize"),   1,
             glm::value_ptr(volumeSize));
glUniform1i(glGetUniformLocation(raymarchShader, "uAtlasDirRes"),      cascadeDirRes[0]);
glUniform1i(glGetUniformLocation(raymarchShader, "uUseDirectionalGI"), useDirectionalGI ? 1 : 0);
```

#### `renderCascadePanel()` — checkbox

```cpp
ImGui::Checkbox("Directional GI sampling (Phase 5g)", &useDirectionalGI);
HelpMarker(
    "OFF (default): reads the isotropic average probeGridTexture.\n"
    "     Shadow signal diluted ~8x by direction averaging.\n\n"
    "ON: samples C0 directional atlas with cosine-weighted hemisphere\n"
    "    integration over surface normal. D^2 texelFetch per pixel.\n"
    "    Shadow quality depends on D and probe spacing -- soft boundary\n"
    "    where probe shadow transitions between lit and occluded probes.\n\n"
    "Combine with Phase 5h (shadow ray) for correct direct shadow + smoother GI.\n"
    "Toggle does NOT re-bake cascades -- display path only.");
ImGui::SameLine();
ImGui::TextDisabled(useDirectionalGI ? "(directional)" : "(isotropic)");
```

---

## Expected Outcome

| Config | Result |
|---|---|
| Baseline (no 5g, no 5h) | Shadow weak; banding at probe grid spacing in indirect |
| 5h only | Correct hard shadow in direct; isotropic indirect still banded |
| 5g only | Better directional GI with spatial trilinear; direct still unshadowed |
| 5h + 5g | Correct direct shadow + smoother GI transition across probe boundaries |

The 5h + 5g combination is the target. GI smoothness is bounded by C0 probe spacing
(0.125m for cascadeC0Res=32) — finer grids give smoother indirect transitions.
Note: this is not a physically correct area-light penumbra — the light is a point light.
The "soft" quality comes from probe-grid spatial blending, not from light-source extent.

---

## Stop Conditions

| Test | Expected |
|---|---|
| Build: 0 errors | Both modes |
| `uUseDirectionalGI=0`: identical image to pre-change | Zero regression |
| Mode 6 (GI-only) + directional ON: shadow boundary smoother than isotropic | Confirmed |
| Mode 6 + directional ON: wall color bleeding preserved (cosine weight not zero for diffuse dirs) | Confirmed |
| Combined 5h + 5g: visible penumbra zone between hard shadow and full light | Primary acceptance |
| No GL errors (atlas not bound at frame start) | Guard with `c0.active` check |
