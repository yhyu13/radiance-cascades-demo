# Reply to Review 11 — Phase 5g Directional Sampling Plan

**Date:** 2026-04-29
**Reviewer document:** `11_phase5g_directional_sampling_plan_review.md`
**Status:** All five findings accepted. F1 and F2 are concrete implementation bugs
requiring plan fixes. F3 is a correctness claim downgrade. F4/F5 are scope corrections.

---

## Finding 1 — High: texture unit 2 collision with `uAlbedo`

**Accepted.** Verified at `src/demo3d.cpp:1226-1229`:

```cpp
glActiveTexture(GL_TEXTURE2);
glBindTexture(GL_TEXTURE_3D, albedoTexture);
glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 2);
```

The plan proposed binding `uDirectionalAtlas` on unit 2 — this would silently stomp
the albedo texture, breaking all surface shading.

**Fix:** bind the directional atlas on unit **3**:

```cpp
glActiveTexture(GL_TEXTURE3);
glBindTexture(GL_TEXTURE_3D, c0.probeAtlasTexture);
glUniform1i(glGetUniformLocation(prog, "uDirectionalAtlas"), 3);
```

In `raymarch.frag`, the sampler declaration and the `uniform int uDirectionalAtlas`
binding point change correspondingly. Units 0–2 (SDF, Radiance, Albedo) are untouched.

**Plan doc updated:** all references to unit 2 for the atlas changed to unit 3.

---

## Finding 2 — High: proposed path drops spatial interpolation → regression risk

**Accepted.** The current final renderer path:

```glsl
texture(uRadiance, uvw).rgb   // hardware trilinear — blends 8 surrounding probes
```

The plan's proposed directional path:

```glsl
ivec3 pc = ivec3(floor(uvw * vec3(uAtlasVolumeSize)));   // snaps to one probe tile
texelFetch(uDirectionalAtlas, ivec3(pc.x*D+dx, pc.y*D+dy, pc.z), 0)
```

This replaces "isotropic + spatially filtered" with "directional + nearest-probe
snapping." The spatial regression may be worse than the directional gain — the
reviewer's concern is correct.

**The fix:** extend `sampleDirectionalGI()` to trilinearly blend 8 surrounding probes,
each contributing their cosine-weighted directional sum. This mirrors exactly what
Phase 5d trilinear does in the compute shader (`sampleUpperDirTrilinear()`), but now
in the final renderer:

```glsl
// Helper: cosine-weighted irradiance from one probe's atlas tile
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

// Trilinear spatial blend across 8 surrounding probes — same -0.5 offset as Phase 5d
vec3 sampleDirectionalGI(vec3 pos, vec3 normal) {
    vec3 uvw  = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return vec3(0.0);

    // Center-aligned probe-grid coordinates (-0.5 maps probe-k center to integer k)
    vec3 pg     = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                        vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    ivec3 p000  = ivec3(floor(pg));
    vec3  f     = fract(pg);
    ivec3 hi    = uAtlasVolumeSize - ivec3(1);
    ivec3 p100  = clamp(p000 + ivec3(1,0,0), ivec3(0), hi);
    ivec3 p010  = clamp(p000 + ivec3(0,1,0), ivec3(0), hi);
    ivec3 p110  = clamp(p000 + ivec3(1,1,0), ivec3(0), hi);
    ivec3 p001  = clamp(p000 + ivec3(0,0,1), ivec3(0), hi);
    ivec3 p101  = clamp(p000 + ivec3(1,0,1), ivec3(0), hi);
    ivec3 p011  = clamp(p000 + ivec3(0,1,1), ivec3(0), hi);
    ivec3 p111  = clamp(p000 + ivec3(1,1,1), ivec3(0), hi);

    int D = uAtlasDirRes;
    vec3 s000 = sampleProbeDir(p000, normal, D);
    vec3 s100 = sampleProbeDir(p100, normal, D);
    vec3 s010 = sampleProbeDir(p010, normal, D);
    vec3 s110 = sampleProbeDir(p110, normal, D);
    vec3 s001 = sampleProbeDir(p001, normal, D);
    vec3 s101 = sampleProbeDir(p101, normal, D);
    vec3 s011 = sampleProbeDir(p011, normal, D);
    vec3 s111 = sampleProbeDir(p111, normal, D);
    vec3 sx00 = mix(s000, s100, f.x); vec3 sx10 = mix(s010, s110, f.x);
    vec3 sx01 = mix(s001, s101, f.x); vec3 sx11 = mix(s011, s111, f.x);
    vec3 sxy0 = mix(sx00, sx10, f.y); vec3 sxy1 = mix(sx01, sx11, f.y);
    return mix(sxy0, sxy1, f.z);
}
```

The -0.5 center-aligned offset and clamp-before-floor/fract pattern are the same
invariants as Phase 5d trilinear and Phase 5f directional bilinear — consistent
across the codebase.

**Cost:** 8 probes × D² bins = 8 × 16 = **128 texelFetch per shaded pixel** with D=4.
Hardware `texture()` trilinear is ~8 texelFetch behind the scenes, so this is ~16×
more fetches than the isotropic baseline. Expected acceptable for a static baked scene
at 60fps; verify at runtime.

**Plan doc updated:** `sampleDirectionalGI()` replaced with the two-function version
above. Single-probe snapping removed. The -0.5 trilinear pattern documented as
consistent with Phase 5d/5f.

---

## Finding 3 — High: "architecturally correct" claim overstates octahedral accuracy

**Accepted.** Equal `dx,dy` bins in an octahedral parameterization do not subtend equal
solid angles. The bin at the octahedral pole covers more sphere area than an equatorial
bin. The cosine-weighted sum with `wsum` normalization treats all bins as equal-area,
which is an approximation.

The correct weight would be `cos(θ) × Ω_bin` where `Ω_bin` is the true solid angle of
each bin (derived from the octahedral Jacobian). Computing `Ω_bin` per bin is non-trivial
and not clearly worth the complexity for a 4×4 atlas.

**Corrected claim:** "better directional irradiance approximation — removes the
below-surface contamination and weights by N·L, but does not correct for octahedral
solid-angle distortion. A fully correct implementation would apply a per-bin Jacobian."

**Plan doc updated:** "architecturally correct" replaced with "improved approximation."
The limitation is documented as a known simplification.

---

## Finding 4 — Medium: debug mode 7 prerequisite does not exist

**Accepted.** The current branch has modes 0–6 in `raymarch.frag` and 0–6 in the
radiance debug UI. Debug mode 7 was listed in the brainstorm doc as a planned feature
but is not implemented.

**Fix:** remove "debug mode 7 (atlas quality confirmed)" from the prerequisites. The
prerequisite list now reads only "Phase 5h (shadow ray ground truth)."

Debug mode 7 remains a useful future diagnostic but is not a blocker for Phase 5g.
The existing mode 6 (GI-only) already exposes the isotropic cascade shadow quality
and is sufficient to compare against the Phase 5g directional result.

**Plan doc updated:** prerequisite line corrected.

---

## Finding 5 — Medium: "hard core + soft penumbra" framing too strong for point light

**Accepted**, same reasoning as Review 10 F1. With a point light, the only physically
correct penumbra comes from an area light or from GI propagation smoothing.

The Phase 5g + 5h combined result is better described as:
- Phase 5h: correct binary shadow in the direct term
- Phase 5g: smoother indirect GI with directional weighting (reduced probe-grid
  banding, not physically correct direct-light penumbra)

The "soft penumbra" emerges from spatial trilinear blending across 8 probes, not from
any area-light model. This is still a meaningful improvement — it just should not be
sold as physics-correct direct-light softening.

**Plan doc updated:** expected-outcome table revised. "Soft shadow from cascade" →
"smoother indirect GI, reduced probe-grid banding." "Hard core + soft penumbra" →
"correct direct shadow + smoother GI transition."

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Texture unit 2 collision with `uAlbedo` | High | **Fixed**: atlas binds on unit 3 |
| Spatial interpolation dropped → regression | High | **Fixed**: `sampleDirectionalGI` trilinearly blends 8 probes; 128 texelFetch/pixel with D=4 |
| "Architecturally correct" overstates octahedral accuracy | High | **Downgraded**: "improved approximation"; Jacobian correction documented as known omission |
| Debug mode 7 prerequisite nonexistent | Medium | **Removed**: prerequisite now only Phase 5h |
| "Hard core + soft penumbra" too strong | Medium | **Reframed**: "correct direct shadow + smoother GI transition" |

The core plan — directional atlas sampling toggle in `raymarch.frag`, display-path only,
no cascade rebuild — is unchanged. The implementation changes are significant (8-probe
trilinear in the display shader) but follow the exact same pattern already proven in
Phase 5d trilinear.
