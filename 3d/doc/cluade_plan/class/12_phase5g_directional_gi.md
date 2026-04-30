# 12 — Phase 5g: Directional GI Sampling in the Renderer

**Purpose:** Understand why the isotropic probe average can contribute wrong light
directions to a surface, how cosine-weighted directional integration fixes it, and
why 8-probe trilinear blending is required rather than snapping to the nearest probe.

---

## The problem: isotropic probe average includes back-facing directions

Before Phase 5g, indirect GI in the renderer read the isotropic `probeGridTexture`:

```glsl
vec3 indirect = texture(uRadiance, uvw).rgb;
```

`probeGridTexture` is the result of `reduction_3d.comp`: the average of all D²=16
direction bins in the probe atlas, weighted equally. This average includes bins facing
**away from the surface** — bins whose direction `bdir` satisfies `dot(bdir, normal) < 0`.

Why is this a problem? A bin facing the floor (`bdir` pointing down) captures light
bouncing off the floor. But if you're shading the floor itself (normal pointing up),
that floor-facing bin's contribution is back-facing — adding floor-reflected light to a
floor surface that physically cannot receive it from below. The result is washed-out
indirect, reduced directional contrast, and incorrect color bleeding.

At D=4 (16 bins), this dilution is especially severe: only ≈8 of the 16 bins actually
face the surface being shaded. The other 8 are averaged in anyway.

---

## The fix: cosine-weighted directional integration

Phase 5g adds `sampleProbeDir()` — for a single probe tile, integrate only the bins
whose direction `bdir` faces the surface:

```glsl
vec3 sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3 irrad = vec3(0.0); float wsum = 0.0;
    for (int dy = 0; dy < D; ++dy)
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float w    = max(0.0, dot(bdir, normal));  // cosine weight; back-facing = 0
            irrad += texelFetch(uDirectionalAtlas, ivec3(pc.x*D+dx, pc.y*D+dy, pc.z), 0).rgb * w;
            wsum  += w;
        }
    return irrad / max(wsum, 1e-4);   // normalize; clamp denominator to avoid NaN
}
```

**Three key decisions:**

1. **Back-facing bins excluded** (`max(0.0, dot(bdir, normal))`): any bin with
   `dot(bdir, normal) < 0` contributes zero weight. Only the hemisphere facing the
   surface normal is integrated — matching the physical model where indirect light can
   only arrive at a surface from the forward hemisphere.

2. **wsum normalization**: dividing by `wsum` makes the result independent of how many
   bins face the surface. At grazing incidence (normal nearly parallel to the probe grid
   plane), fewer bins face the surface — without normalization the result would dim as the
   surface tilts away. The division by `wsum` removes this directional bias.

3. **`max(wsum, 1e-4)`**: if all bins happen to be back-facing (a surface pointing exactly
   into an octahedral fold gap), `wsum = 0`. The clamp prevents divide-by-zero, returning
   approximately `vec3(0.0)` (black) rather than NaN.

---

## 8-probe trilinear: why snapping to the nearest probe is wrong

A naive implementation would find the nearest probe and call `sampleProbeDir()` once:

```glsl
// WRONG — nearest-probe snap
ivec3 pc = ivec3(floor(uvw * vec3(uAtlasVolumeSize)));
vec3 indirect = sampleProbeDir(pc, normal, D);
```

This is a spatial regression. The isotropic path `texture(uRadiance, uvw)` gets free
**hardware trilinear interpolation**: the texture sampler blends 8 surrounding probes
weighted by the surface's fractional position in the grid cell. Replacing this with a
single snapped probe gives nearest-neighbor spatial quality — visible probe-grid banding
as the surface crosses from one probe's cell to another.

Phase 5g fixes this by implementing the same 8-probe trilinear blend manually:

```glsl
vec3 sampleDirectionalGI(vec3 pos, vec3 normal) {
    vec3 uvw = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return vec3(0.0);  // out of grid — no indirect

    // Center-aligned index: probe k's center maps to integer k
    vec3  pg   = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5, vec3(0.0), vec3(uAtlasVolumeSize - 1));
    ivec3 p000 = ivec3(floor(pg));
    vec3  f    = fract(pg);
    // ... 8 calls to sampleProbeDir, trilinearly mixed by f
}
```

The `-0.5` offset ensures that `fract = 0` when the surface is exactly at a probe center
(giving 100% weight to that probe) and `fract = 0.5` halfway between probe centers
(giving equal weight to both neighbors). This is center-to-center trilinear interpolation,
matching what `texture(uRadiance, uvw)` does for the isotropic case.

**Cost:** 8 probes × D² bins = 8 × 16 = 128 `texelFetch` calls per shaded pixel (D=4).
Hardware `texture()` does ~8 fetches. This is ~16× more per pixel, paid for better
directional quality. Acceptable for a static scene at 60fps; measure at runtime with
the `uUseDirectionalGI` toggle.

---

## Texture unit assignment

`sampleDirectionalGI()` reads `uDirectionalAtlas` — the C0 `probeAtlasTexture` bound
on texture unit 3:

| Unit | Uniform | Contents |
|---|---|---|
| 0 | `uSDF` | Signed distance field volume |
| 1 | `uRadiance` | Isotropic probe grid (selected cascade, `probeGridTexture`) |
| 2 | `uAlbedo` | Albedo volume |
| 3 | `uDirectionalAtlas` | C0 directional atlas (`probeAtlasTexture`) |

Only C0's atlas is read. Phase 5g always uses C0 regardless of which cascade level is
selected in the cascade dropdown — that dropdown controls unit 1 (`uRadiance`) only.

---

## A/B comparison: mode 6 as the diagnostic tool

Mode 6 ("GI only") shows indirect GI without direct light, tone mapping, or albedo:

```glsl
if (uRenderMode == 6) {
    vec3 indirect6 = (uUseDirectionalGI != 0 && uUseCascade != 0)
        ? sampleDirectionalGI(pos, normal)
        : texture(uRadiance, uvw).rgb;
    fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);
    return;
}
```

With `uUseDirectionalGI` toggled ON/OFF in mode 6, you can directly see:
- **OFF**: the isotropic average — often washed out with light from all directions
- **ON**: the cosine-weighted directional integral — more directional contrast

Mode 3 (Indirect×5) always uses the isotropic `uRadiance` path for diagnostic purposes
and does not respect `uUseDirectionalGI`.

---

## Known omission: octahedral solid-angle distortion

The D×D bin grid uses octahedral parameterization. Equal `dx,dy` steps in octahedral
space do **not** subtend equal solid angles on the sphere — bins near the octahedral fold
edges are stretched. A fully correct Lambertian integral would weight each bin by both
`dot(bdir, normal)` (cosine) and the bin's true solid-angle Jacobian `Ω_bin`.

At D=4, bins span roughly 36° and the distortion is moderate. Phase 5g does not correct
for it — the implementation uses equal-area bins implicitly (`w = max(0, dot)` with no
Jacobian factor). This is documented as a known approximation, not a bug.

---

## Summary

| Without Phase 5g | With Phase 5g |
|---|---|
| Isotropic average: 16 bins, all directions | Cosine-weighted: only forward-hemisphere bins |
| Back-facing radiance dilutes the result | Back-facing bins have zero weight |
| Single texture() call — hardware trilinear | 8× sampleProbeDir() — manual trilinear |
| Correct spatial blending, wrong directional filtering | Correct spatial blending + directional filtering |

Phase 5g is a display-path-only change. The atlas content (written by the bake passes)
is unchanged. Toggling `uUseDirectionalGI` costs no cascade rebuild.
