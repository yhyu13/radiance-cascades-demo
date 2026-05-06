# Phase 5g Implementation Learnings — Directional Atlas Sampling

**Date:** 2026-04-29
**Branch:** 3d
**Files changed:**
- `res/shaders/raymarch.frag`
- `src/demo3d.h`
- `src/demo3d.cpp`

---

## What Was Done

Phase 5g replaces the isotropic probe average with a cosine-weighted directional integration
over the C0 atlas in `raymarch.frag`. The toggle `uUseDirectionalGI` lets the user A/B
compare the two paths. The cascade bake and reduction passes are untouched — this is
display-path only.

---

## Shader Changes (`raymarch.frag`)

### New uniforms

```glsl
uniform sampler3D uDirectionalAtlas;  // C0 per-direction atlas (probeAtlasTexture)
uniform ivec3     uAtlasVolumeSize;   // C0 probe grid dimensions
uniform vec3      uAtlasGridOrigin;   // world-space origin of C0 grid (= volumeOrigin)
uniform vec3      uAtlasGridSize;     // world-space extent of C0 grid (= volumeSize)
uniform int       uAtlasDirRes;       // D for C0 (cascadeDirRes[0])
uniform int       uUseDirectionalGI;  // 1=directional, 0=isotropic (default)
```

Placed after the Phase 5h `uUseShadowRay` uniform — chronological ordering by phase.

### `octToDir` and `binToDir` helpers

Identical to the functions in `radiance_3d.comp`. Copied rather than shared because
GLSL has no include mechanism in this project. `octToDir` maps the unit square [0,1]²
to the unit sphere via octahedral folding. `binToDir` returns the representative
direction for bin `(dx, dy)` using center-sample convention (`+ 0.5`).

### `sampleProbeDir` — one probe tile

```glsl
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
```

**Key decisions:**

- `max(0.0, dot(bdir, normal))`: back-facing bins (dot < 0) excluded with zero weight.
  These cannot physically illuminate the surface and only dilute the signal in the
  isotropic average — this exclusion is the primary correctness improvement over
  `texture(uRadiance, uvw).rgb`.

- `wsum` normalization: makes the result independent of how many bins face the surface
  (avoids dimming at grazing angles where few bins are in the hemisphere).

- `max(wsum, 1e-4)`: prevents divide-by-zero at a surface facing away from all bins
  (e.g., facing exactly into an octahedral fold gap), producing black instead of NaN.

- **Approximation caveat**: equal `dx,dy` bins in octahedral parameterization do not
  subtend equal solid angles. A fully correct integral would weight each bin by
  `cos(θ) × Ω_bin` where `Ω_bin` is the true solid-angle Jacobian. For D=4 this
  distortion is small and not corrected — documented in the plan as a known omission.

### `sampleDirectionalGI` — 8-probe trilinear blend

```glsl
vec3 sampleDirectionalGI(vec3 pos, vec3 normal) {
    vec3 uvw = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return vec3(0.0);

    vec3  pg   = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                       vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    ivec3 p000 = ivec3(floor(pg));
    vec3  f    = fract(pg);
    ivec3 hi   = uAtlasVolumeSize - ivec3(1);
    int   D    = uAtlasDirRes;
    // ... 8 sampleProbeDir calls + trilinear mix
}
```

**Why 8-probe trilinear is required (not single-probe snapping):**

The isotropic path `texture(uRadiance, uvw)` gets hardware trilinear for free —
8 probes blended by the GPU's texture unit. A naive replacement:
```glsl
ivec3 pc = ivec3(floor(uvw * vec3(uAtlasVolumeSize)));
texelFetch(uDirectionalAtlas, ivec3(pc.x*D+dx, pc.y*D+dy, pc.z), 0)
```
would snap to the nearest probe tile — effectively nearest-neighbor spatial
interpolation. This would be a spatial *regression* on top of the directional
*improvement*, likely making banding worse, not better.

The 8-probe trilinear exactly replicates what `texture()` does spatially but
applies the cosine-weighted directional integral per probe first. It matches the
pattern of `sampleUpperDirTrilinear()` in `radiance_3d.comp` (Phase 5d trilinear
for the bake pass).

**`-0.5` center-aligned offset:** Same invariant as Phase 5d trilinear and Phase 5f
directional bilinear. Probe k's center is at world position `origin + (k+0.5)*cellSize`.
Subtracting 0.5 before `floor`/`fract` maps probe k's center to integer k, so
`fract = 0` at the center (weight lands entirely on probe k) and `fract = 0.5`
halfway between probes (equal weight on both neighbors).

**clamp-before-floor/fract border invariant:** `clamp(pg, 0, N-1)` ensures that at
the grid boundary, `floor` gives a valid probe index and `fract = 0`, so the
`p000 + ivec3(1,...)` neighbor (also clamped to `hi`) has zero weight. No out-of-bounds
atlas access is possible.

**Cost:** 8 probes × D² bins = 128 `texelFetch` per shaded pixel at D=4. Hardware
`texture()` trilinear is ~8 fetches under the hood, so this is ~16× more. In a static
Cornell Box at 60fps this is expected acceptable — confirm at runtime with the
toggle.

### Replace indirect block in mode 0

```glsl
if (uUseCascade) {
    vec3 indirect = (uUseDirectionalGI != 0)
        ? sampleDirectionalGI(pos, normal)
        : texture(uRadiance, uvw).rgb;
    surfaceColor += indirect;
}
```

When `uUseDirectionalGI = 0`, the path is identical to pre-5g (same `texture()` call,
same `uvw` coordinate). Zero regression guaranteed at the GLSL level.

---

## C++ Changes

| Location | Change |
|---|---|
| `demo3d.h` line ~703 | Added `bool useDirectionalGI;` field with doc comment |
| Constructor line ~112 | Added `, useDirectionalGI(false)` (default OFF — isotropic baseline preserved) |
| `render()` after `lastShadowRay` block | Tracking block; log toggle; no `cascadeReady` reset |
| `raymarchPass()` after shadow ray push | Bind `cascades[0].probeAtlasTexture` on unit 3; push 5 uniforms |
| `renderCascadePanel()` after Phase 5h section | ImGui checkbox + HelpMarker |

### Texture unit 3 for the atlas

Unit assignment in `raymarchPass()`:
- Unit 0: `uSDF`
- Unit 1: `uRadiance` (isotropic probeGridTexture, selected cascade)
- Unit 2: `uAlbedo`
- Unit 3: `uDirectionalAtlas` (C0 probeAtlasTexture, Phase 5g)

The plan originally proposed unit 2 — this would have silently stomped `uAlbedo`,
breaking all surface shading. Caught in review 11 (F1). Final implementation uses
unit 3, confirmed safe.

### `cascadeCount > 0` guard vs `.empty()`

`cascades` is a fixed array `RadianceCascade3D cascades[MAX_CASCADES]`, not a
`std::vector`. The `.empty()` check in the first draft caused a compile error
(`C2228: ".empty" left side must have class/struct/union`). Fixed to use
`cascadeCount > 0 && cascades[0].active && cascades[0].probeAtlasTexture != 0` —
the same pattern used in the existing cascade-conditional code in `raymarchPass()`.

### Default OFF

`useDirectionalGI(false)` preserves the pre-5g isotropic baseline as the default.
The feature must be explicitly enabled in the UI. This makes the zero-regression
condition (OFF path = identical image) trivially testable at startup.

---

## Correctness Invariants

| Scenario | Behaviour |
|---|---|
| `uUseDirectionalGI=0` | `texture(uRadiance, uvw)` — identical to pre-5g. |
| `uUseDirectionalGI=1`, surface facing light | Back-half bins excluded; front bins weighted by N·L cosine. |
| `uUseDirectionalGI=1`, surface at grid boundary | `uvw` out-of-range check returns `vec3(0.0)`. |
| `uUseDirectionalGI=1`, all bins back-facing | `wsum < 1e-4` → denominator clamped; result ≈ `vec3(0.0)`. |
| C0 atlas not yet allocated | `atlasAvailable=false` → binding skipped AND `uUseDirectionalGI` forced to 0 regardless of UI state. Both halves are enforced in C++ (review 12 fix). |

---

## What This Does Not Do

- Does not modify the cascade bake or reduction passes — the atlas content is unchanged.
- Does not apply directional sampling to debug mode 3 (Indirect×5 always reads the
  isotropic `uRadiance` grid — its diagnostic purpose is unchanged).
- ~~Mode 6 (GI-only) cannot be used to validate the directional improvement.~~
  **Corrected (2026-04-30)**: Mode 6 now respects `uUseDirectionalGI` — see Bug 3 in
  "Post-Implementation Bug Fixes" below. Use mode 6 to A/B compare directional vs
  isotropic in isolation without direct light or tone mapping.
- Does not correct for octahedral solid-angle distortion — equal dx,dy bins are not
  equal-area. Documented as known omission.
- The cascade panel's "selected cascade" dropdown controls which `probeGridTexture` is
  bound to `uRadiance`. It still applies to: the isotropic path in mode 0, mode 3, and
  mode 6 when directional GI is **OFF**. It does **not** control mode 6 when directional
  GI is **ON** — that path calls `sampleDirectionalGI()` which always reads the C0 atlas
  (`cascades[0].probeAtlasTexture`) regardless of the dropdown selection.

---

## Expected Visual Outcome

| Config | Result |
|---|---|
| 5g OFF, 5h OFF | Unshadowed direct, isotropic indirect — pre-5 baseline |
| 5g OFF, 5h ON | Hard shadow in direct; isotropic indirect with probe-grid banding |
| 5g ON, 5h OFF | Directional GI with smoother indirect; unshadowed direct |
| 5g ON, 5h ON | Hard shadow in direct + smoother indirect GI — target configuration |

GI smoothness is bounded by C0 probe spacing (0.125m at cascadeC0Res=32). The "soft"
quality at shadow transitions comes from 8-probe trilinear spatial blending, not from
any area-light model. This is not a physically correct penumbra — the light is a point.

---

## Post-Implementation Bug Fixes (2026-04-29)

### Bug 1 — Directional GI toggle had no visible effect

**Symptom:** Enabling the "Directional GI" checkbox in the UI produced no change in
the rendered image.

**Root cause:** `useCascadeGI` defaulted to `false` in the `Demo3D` constructor
(line ~100: `, useCascadeGI(false)`). This pushed `uUseCascade = 0` to the shader.
The entire indirect GI block in `raymarch.frag` is gated on `if (uUseCascade != 0)` —
when that gate was closed, `sampleDirectionalGI()` was never called regardless of
`uUseDirectionalGI`.

**Fix:** Changed constructor default to `, useCascadeGI(true)`. Cascade GI is now
ON by default, consistent with the intent of Phase 5g as the production path.

### Bug 2 — Mode 0 showed no indirect GI added to direct lighting

**Symptom:** Mode 0 (final render) showed only direct + ambient shading with no
cascade-sourced indirect contribution.

**Root cause:** Same as Bug 1 — `useCascadeGI(false)` default suppressed the entire
`if (uUseCascade != 0)` block in `raymarch.frag`.

**Fix:** Same constructor change. Both bugs had the same single root cause.

### Consistency fix — `uniform bool` → `uniform int` for `uUseCascade`

`uUseCascade` was declared `uniform bool` in `raymarch.frag` while all Phase 5
additions (`uUseShadowRay`, `uUseDirectionalGI`) use `uniform int`. GLSL drivers
are spec'd to accept `glUniform1i` into a `bool` uniform, but mixing the conventions
creates silent risk (driver-dependent behavior at the boundary). Changed to
`uniform int uUseCascade` and updated the one usage site to `if (uUseCascade != 0)`,
matching the Phase 5 convention.

### Bug 3 — Directional GI toggle appeared to have no effect (Mode 6 regression)

**Symptom (2026-04-30):** After Phase 5i was implemented, enabling "Directional GI
sampling" appeared to have no visible effect on the rendered image.

**Root cause:** Mode 6 ("GI only") in `raymarch.frag` was hardcoded to always read
from the isotropic `uRadiance` texture:

```glsl
// BEFORE — hardcoded isotropic, ignores uUseDirectionalGI
if (uRenderMode == 6) {
    vec3 indirect6 = texture(uRadiance, uvw).rgb;
    fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);
    return;
}
```

The user was testing the directional GI toggle in mode 6. Toggling `uUseDirectionalGI`
had no effect on the mode 6 output, making it appear broken even though mode 0 was
correctly using `sampleDirectionalGI`.

**Why Phase 5i surfaced it**: Phase 5i did not break directional GI — mode 0 was
already correct. The user naturally switched to mode 6 (GI-only, no direct light) to
isolate the indirect contribution and confirm the toggle was working. Mode 6 had always
been isotropic-only; this was just the first session where the isolation diagnostic was
needed.

**Fix (`raymarch.frag`):**

```glsl
// AFTER — respects uUseDirectionalGI toggle
if (uRenderMode == 6) {
    vec3 indirect6 = (uUseDirectionalGI != 0 && uUseCascade != 0)
        ? sampleDirectionalGI(pos, normal)
        : texture(uRadiance, uvw).rgb;
    fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);
    return;
}
```

`normal` is already computed before mode 6 runs (`estimateNormal(pos)` is called once
before any debug-mode early-return), so no additional cost.

**Also updated:**
- HelpMarker for the directional GI checkbox now says "Mode 6 respects this toggle —
  use it to A/B compare directional vs isotropic in isolation."
- Tooltip added to the mode 6 radio button in the settings panel.
- Phase 5i impl learnings doc (`phase5i_impl_learnings.md`) documents this fix in its
  "Directional GI Mode 6 Fix" section.

---

### Lesson

Default `false` for the cascade GI toggle was set to allow the renderer to start in
a lightweight state during development. Once Phase 5g was merged, that default became
wrong — it hid all indirect GI by default, making the Phase 5g and 5h improvements
invisible to a first-time user. Feature toggles that gate major display paths should
default to `true` once the feature is stable.

**Second lesson (Bug 3):** Debug modes that are supposed to isolate a feature must
respect all feature toggles relevant to that feature. Mode 6 was intended to show GI
in isolation, but by ignoring `uUseDirectionalGI` it became useless as a diagnostic
for Phase 5g. Any debug mode that shows the output of a toggleable path should be
wired to that path's toggle.
