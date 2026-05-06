# Phase 5 Gap Analysis & Feature Brainstorm

**Date:** 2026-04-29
**Snapshot as of:** b3b04ef (Phase 5d trilinear)
**Purpose:** Audit what Phase 5 has not yet done, diagnose the shadow penumbra banding,
and brainstorm next features.

---

## Part 1 — What Phase 5 Still Has Not Done

### Runtime validation (all pending)

| Feature | Implemented | Validated |
|---|---|---|
| 5a: Octahedral D=4 encoding | Yes | No — "0 errors" only |
| 5b: Per-direction atlas write | Yes | No |
| 5c: Directional upper-cascade merge | Yes | No |
| 5d: Non-co-located layout | Yes | No |
| 5d trilinear: 8-neighbor spatial blend | Yes (this session) | No |
| 5e: Per-cascade D scaling | Yes | No |
| 5f: Directional bilinear | Yes | No |

No phase has a recorded A/B image comparison against the previous baseline. All stop
conditions are structural/compile only. The project has been advancing on the
"if it builds and the logic is correct, it works" assumption since Phase 5a.

### Structural gaps vs ShaderToy

| Gap | Status |
|---|---|
| Per-neighbor visibility weighting in trilinear merge | Deferred — inert analytically; adds 8x cost |
| Directional sampling in final render (N-dot-L hemisphere) | **Not started** — see Part 2 |
| Per-cascade D scaling visual A/B | Implemented, never compared |

---

## Part 2 — Shadow Penumbra Banding: Root Cause Analysis

### Observed symptom

GI color interpolates smoothly (trilinear probe sampling visible in wall color bleed).
Shadow penumbra shows discrete bands at probe-grid boundaries (~0.125m steps for C0).

### Why colors are smooth but shadows band — three stacked reasons

#### Reason 1: the isotropic reduction dilutes the shadow signal

`reduction_3d.comp` averages all D² bins equally:

```glsl
avg += texelFetch(uAtlas, ...).rgb;
avg /= float(uDirRes * uDirRes);  // D=4: divide by 16
```

With D=4 (16 bins per probe), the ceiling light occupies at most 1–2 bins per probe
from below. The other 14–15 bins see unblocked ambient and GI bounces. After averaging,
the shadow contribution is diluted by **~8–16x** relative to the directional bin that
actually looks toward the light.

Color bleeding (red/green walls) comes from many bins simultaneously — the average
preserves it. Shadow (blocking of one light direction) affects 1–2 bins — the average
erases most of it.

#### Reason 2: the final renderer adds unshadowed direct light

`raymarch.frag` lines 285–288:

```glsl
float diff    = max(dot(normal, lightDir), 0.0);
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));  // NO shadow check
if (uUseCascade) surfaceColor += indirect;
```

Every surface receives full unshadowed direct N·L regardless of geometry. Shadow only
appears as a *reduction* in the added `indirect` term. The direct term dominates the
output in well-lit areas, making the small shadow signal in `indirect` even less visible.

#### Reason 3: trilinear interpolation across a binary shadow boundary

Even if Reasons 1 and 2 were fixed, a probe at grid spacing 0.125m cannot resolve a
shadow boundary narrower than ~0.125m. The `inShadow()` bake in `radiance_3d.comp`
returns a hard 0 or 1 per probe. A probe just inside the shadow and a probe just outside
have vastly different radiance. Trilinear interpolation creates a linear ramp across
exactly one probe cell — which the eye reads as a visible band.

Wall color varies at a scale much larger than 0.125m (walls are meters away), so
trilinear smoothing is imperceptible. Shadow edges can be sub-centimeter at the light
source boundary — the 0.125m probe grid is too coarse to resolve them.

### How to debug soft shadow quality

**Existing debug modes:**

| Mode | What it shows | Use for shadow debug |
|---|---|---|
| 4 (direct-only) | `albedo * (N·L * lightColor + 0.05)`, no shadow | Confirm direct path has NO shadow check — expected |
| 6 (GI-only) | `indirect * 2.0` from `uRadiance` (isotropic cascade) | Shows cascade shadow contribution in isolation |
| 3 (indirect × 5) | `texture(uRadiance, uvw) * 5` tone-mapped | Higher gain, exposes shadow boundary in cascade |

**Recommended debug workflow:**

1. Switch to mode 6 (GI-only). The shadow boundary from the cascade should be visible
   as a smooth gradient if the probe grid resolves it, or as banding if it does not.
2. Toggle `useSpatialTrilinear` ON↔OFF in non-co-located mode. With trilinear ON, the
   2×2×2 block stepping at the shadow boundary should soften.
3. Increase `cascadeC0Res` from 32³ to 64³. Shadow boundary sharpness should improve
   proportionally (probe spacing halves from 0.125m to 0.0625m).
4. **Proposed new mode 7**: sample the directional atlas bin closest to the surface
   normal (or the light direction) instead of the isotropic average. This exposes what
   the directional data actually captured before the reduction dilutes it.

---

## Part 3 — Brainstorm: Phase 5g and Beyond

### Phase 5g — Directional sampling in final render (HIGH IMPACT, medium effort)

**The fix for shadow banding.**

`raymarch.frag` currently reads `texture(uRadiance, uvw)` — the isotropic average.
Instead, it should sample the directional atlas weighted by the surface hemisphere:

```glsl
// Sample cascade with N·L hemisphere weighting
// Sum bins where dot(binDir, normal) > 0, weighted by N·L
// This replaces the isotropic average with a cosine-weighted irradiance estimate
```

This cannot directly read the directional atlas from `raymarch.frag` (it reads
`probeGridTexture`). Options:

**Option A — Cosine-weighted reduction pass**: modify `reduction_3d.comp` to output
not just the flat average but also a separate "upper hemisphere" texture per cascade,
sampled at runtime with surface normal. Requires a second output texture and a normal
input. Cost: doubles reduction memory and compute.

**Option B — Normal-direction atlas sample at surface**: pass the directional atlas
to `raymarch.frag` and sample it at the correct bin for the surface normal. This is
the architecturally correct approach — it's what the atlas was built for. Cost:
requires atlas texture binding in `raymarch.frag` and a `dirToBin(normal, D)` lookup.

**Option B is preferred** — it uses the already-baked directional data. The isotropic
path remains as fallback. Expected result: soft shadows from the cascade with angular
resolution proportional to D (D=4: 16 bins × 1 atlas sample; D=16: 256 bins, sharper).

### Phase 5h — Shadow ray in final renderer (MEDIUM IMPACT, low effort)

Add a binary shadow check to `raymarch.frag` directly:

```glsl
// After hitting surface, march from pos toward uLightPos
float shadowFactor = 0.0;
vec3 toLight = normalize(uLightPos - pos);
float distToLight = length(uLightPos - pos);
float st = 0.05;
for (int s = 0; s < 32 && st < distToLight; ++s) {
    float sd = sampleSDF(pos + toLight * st);
    if (sd < 0.002) { shadowFactor = 1.0; break; }
    st += max(sd * 0.9, 0.01);
}
float diff = max(dot(normal, lightDir), 0.0) * (1.0 - shadowFactor);
```

This gives the final renderer its own hard shadow. Combined with the cascade indirect
(which also bakes shadow), the result is:
- Hard shadow boundary from the direct ray (sharp, cost ~32 SDF samples/pixel)
- Soft shadow gradient from the cascade indirect (smooth, already computed)
- Combined: soft-shadow penumbra with a hard core — visually accurate

Compared to Phase 5g: this is simpler to implement but adds a per-pixel shadow ray in
the display path. Phase 5g makes the cascade itself provide soft shadows; Phase 5h adds
a hard shadow to the direct term.

### Phase 5i — N·L hemisphere irradiance from directional atlas (ARCHITECTURAL)

The probe's directional atlas stores the full sphere. For opaque surfaces, only the
upper hemisphere (relative to the surface normal) contributes. The correct irradiance
estimate is:

```
E(p, n) = (1/π) ∫ L(p, ω) max(0, n·ω) dω
```

Approximated over D² bins:
```glsl
float wsum = 0.0;
vec3 irrad = vec3(0.0);
for each bin b:
    vec3 bdir = binToDir(b, D);
    float w   = max(0.0, dot(bdir, normal));
    irrad    += sampleAtlas(probePos, b) * w;
    wsum     += w;
irrad /= max(wsum, 1e-4);
```

This is more expensive (D² texture fetches in `raymarch.frag`) but architecturally
correct. Can be pre-computed into a second reduction texture (like Option A above).

### Phase 5j — Probe density adaptation (LOW PRIORITY)

Non-co-located already halves probe density per level. A further optimization: reduce
C0 density in regions far from surfaces (open air). Cost: complexity of adaptive
data structures. Likely not worth it for a Cornell Box demo.

### Debug Mode 7 — Directional atlas shadow slice

New render mode: display the cascade's directional atlas bin for the light direction,
not the isotropic average. This shows the raw shadow data before the reduction dilutes
it. Useful for diagnosing whether the banding is a probe-grid or a reduction artifact.

```glsl
// Mode 7: directional shadow diagnostic
vec3 toLightDir = normalize(uLightPos - pos);
ivec2 lightBin = dirToBin(toLightDir, D);
// read atlas at (probePos * D + lightBin)
// compare with mode 6 (isotropic) to see dilution effect
```

---

## Part 4 — Priority Order

| Priority | Feature | Expected impact | Effort |
|---|---|---|---|
| 1 | Phase 5h: shadow ray in `raymarch.frag` | Immediate hard shadow | Low (1 day) |
| 2 | Debug mode 7: directional atlas light-dir slice | Diagnose shadow quality | Low (half day) |
| 3 | Phase 5g Option B: directional atlas sample in final render | Soft shadow from cascade | Medium (2 days) |
| 4 | Runtime validation: 5a–5f visual A/B | Confirm no regressions | Medium (1 day) |
| 5 | Phase 5i: cosine-weighted irradiance from atlas | Architecturally correct GI | High (3 days) |

### Why Phase 5h before Phase 5g?

Phase 5h (shadow ray in `raymarch.frag`) is a 32-step SDF march per pixel — the exact
same code already in `radiance_3d.comp`'s `inShadow()`. It immediately makes the
shadow visible as a hard binary. This creates a reference ground truth for what the
soft shadow from Phase 5g should converge to. Implementing Phase 5g without Phase 5h
means there is no ground truth to compare against.

### Why debug mode 7 before phase 5g?

Before fixing the shadow, confirm what the directional atlas actually captured.
If mode 7 shows sharp shadow boundaries in the atlas, the banding is a reduction
artifact (Phase 5g will fix it). If mode 7 also shows banding, the banding is a
probe-grid-resolution issue (only finer probes or Phase 5h will help).
