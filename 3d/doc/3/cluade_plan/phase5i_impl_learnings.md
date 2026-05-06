# Phase 5i Implementation Learnings — SDF Cone Soft Shadow (Display + Bake)

**Date:** 2026-04-29
**Branch:** 3d
**Files changed:**
- `res/shaders/raymarch.frag`
- `res/shaders/radiance_3d.comp`
- `src/demo3d.h`
- `src/demo3d.cpp`

---

## What Was Done

Phase 5i implements Strategy A and Strategy B1 from `phase5_banding_analysis.md`.

**Strategy A**: SDF cone soft shadow in the final renderer (`raymarch.frag`), replacing the
binary `shadowRay()` when toggled ON. This is a display-path-only change — no cascade rebuild.

**Strategy B1**: SDF cone soft shadow in the bake shader (`radiance_3d.comp`), replacing the
binary `inShadow()` for the direct shading baked into probe radiance. This is a bake-path
change — toggling or changing `k` requires a cascade rebuild.

Both paths share the same `k` parameter (penumbra width). This reduces UI clutter and
keeps the two approximations roughly visually aligned, but does **not** make them
technically consistent — the two paths differ in origin convention (normal-offset vs fixed
bias) and in how the shadow signal propagates (direct term vs baked into probe radiance
then filtered through the cascade). Shared `k` is an authoring convenience, not a
physically meaningful coupling.

---

## Strategy A — Display Path (`raymarch.frag`)

### New uniforms

```glsl
uniform int   uUseSoftShadow;  // 5i: 1=cone soft shadow, 0=binary shadowRay() (Phase 5h)
uniform float uSoftShadowK;    // penumbra width k [1,16]; lower=wider, default 8
```

Placed immediately after `uniform int uUseShadowRay;` (Phase 5h uniform).

### `softShadow()` function

```glsl
float softShadow(vec3 hitPos, vec3 normal, vec3 lightPos, float k) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    float res       = 1.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;
        if (d < 0.002) return 1.0;
        res = min(res, k * d / max(t, 0.001));
        t  += max(d * 0.9, 0.01);
    }
    return 1.0 - clamp(res, 0.0, 1.0);
}
```

The same normal-offset origin as `shadowRay()` (`normal * 0.02 + ldir * 0.01`) avoids
self-intersection without a fixed bias.

Return convention: 0.0 = lit, 1.0 = fully in shadow (matches `shadowRay()`'s convention so
the two are interchangeable in the call site).

### Call site pattern

Both mode 4 (direct only) and mode 0 (final) use the same nested ternary:

```glsl
float shadow = (uUseShadowRay != 0)
    ? ((uUseSoftShadow != 0) ? softShadow(pos, normal, uLightPos, uSoftShadowK)
                             : shadowRay(pos, normal, uLightPos))
    : 0.0;
```

Guard order: `uUseShadowRay` outer, `uUseSoftShadow` inner. With `useShadowRay=false`,
neither shadow function is called regardless of `uUseSoftShadow`. This is correct: soft
shadow is a replacement for the shadow ray, not an addition to unshadowed direct.

### Zero-regression guarantee

With both defaults (`useSoftShadow=false`, `useShadowRay=true`), the ternary evaluates to
`shadowRay(...)` — identical to Phase 5h behaviour. No cascade rebuild needed on toggle.

---

## Strategy B1 — Bake Path (`radiance_3d.comp`)

### New uniforms

```glsl
uniform int   uUseSoftShadowBake;  // 0=binary inShadow() (default), 1=SDF cone soft shadow
uniform float uSoftShadowK;        // penumbra width, shared with display shader
```

### `softShadowBake()` function

```glsl
float softShadowBake(vec3 hitPos, vec3 lightPos, float k) {
    vec3  toLight = lightPos - hitPos;
    float dist    = length(toLight);
    vec3  dir     = toLight / dist;
    float t       = 0.05;   // fixed bias — no surface normal in bake path
    float res     = 1.0;
    for (int i = 0; i < 32 && t < dist; ++i) {
        float d = sampleSDF(hitPos + dir * t);
        if (d >= INF * 0.5) return 0.0;
        if (d < 0.002)      return 1.0;
        res = min(res, k * d / max(t, 0.001));
        t  += max(d * 0.9, 0.01);
    }
    return 1.0 - clamp(res, 0.0, 1.0);
}
```

**Key difference from display `softShadow()`**: uses a fixed `t=0.05` offset (no surface
normal available in the compute shader's ray-march context). This matches the existing
`inShadow()` function's fixed bias.

Return convention: 0.0 = lit, 1.0 = shadowed — matches `inShadow()` which returns a bool
(implicit 0/1). Both drop into the same expression:

```glsl
float shadowFact = (uUseSoftShadowBake != 0)
    ? softShadowBake(pos, uLightPos, uSoftShadowK)
    : (inShadow(pos, uLightPos) ? 1.0 : 0.0);
float diff = max(dot(n, lightDir), 0.0) * (1.0 - shadowFact);
```

### Rebuild trigger

Bake-path shadow changes the probe radiance, so the cascade must be rebuilt:

```cpp
static bool  lastSoftShadowBake = false;
static float lastSoftShadowK    = 8.0f;
if (useSoftShadowBake != lastSoftShadowBake ||
        (useSoftShadowBake && softShadowK != lastSoftShadowK)) {
    lastSoftShadowBake = useSoftShadowBake;
    lastSoftShadowK    = softShadowK;
    cascadeReady = false;
}
```

The `k` change only triggers a rebuild when `useSoftShadowBake` is ON. Changing `k` while
bake soft shadow is OFF does not trigger a rebuild (the bake path does not use `k` in that
state).

---

## Shared `k` Parameter Design

`softShadowK` is a single float shared by both display (`uSoftShadowK` in `raymarch.frag`)
and bake (`uSoftShadowK` in `radiance_3d.comp`). The rationale is UI simplicity and rough
visual alignment — one slider to tune rather than two.

The two paths are **not** truly consistent, however:
- Display path uses a normal-offset origin (`normal * 0.02 + ldir * 0.01`) and applies to
  the final direct-light term in the renderer.
- Bake path uses a fixed `t=0.05` bias and applies to probe-hit shading, whose result then
  propagates through the cascade hierarchy before reaching the display.

Shared `k` is an authoring convenience, not a physically meaningful coupling. A given `k`
produces different apparent penumbra widths in the two paths because the geometric scale
and signal path differ. The `k` value must be tuned by visual inspection, not derived.

The slider uses `[1.0, 16.0]`. Lower k → wider, softer penumbra. Higher k → approaches
binary. There is no analytically correct k — it is an artistic parameter.

---

## Debug Modes

- **Mode 4 (Direct only)**: shows the display soft shadow in isolation without indirect GI.
  Enable `useSoftShadow` and compare with binary shadow ray.
- **Mode 3 (Indirect*5)** and **Mode 6 (GI only)**: indirect radiance baked with
  `softShadowBake` shows softer shadow transitions at probe level.

To test bake soft shadow: enable `useSoftShadowBake`, wait for rebuild, switch to mode 3 or
6. Compare the baked indirect at shadow edges vs binary bake.

---

## Physical Accuracy Note

Neither Strategy A nor B1 is physically equivalent to a point light's correct binary shadow:

- Strategy A introduces a smooth penumbra that the light source (a point) does not have.
- Strategy B1 bakes a soft shadow into probes, reducing Source 2/3 banding but introducing
  penumbra mismatch between direct and indirect.

This is an appearance approximation, not a physically accurate improvement. The tradeoff
(visual smoothness vs physical correctness) is documented in the banding analysis.

---

## Bug Fixed in This Session — `uniform bool` → `uniform int`

During Phase 5i, `uniform bool uUseCascade` was changed to `uniform int uUseCascade` in
`raymarch.frag` (consistent with Phase 5 convention of using `int` for all toggles).

The cascade GI block was also updated from `if (uUseCascade)` to `if (uUseCascade != 0)`.
This is spec-legal and semantically equivalent, but aligns with the rest of the Phase 5
uniform conventions.

---

## Directional GI Mode 6 Fix (same session)

After Phase 5i, the user observed that the Phase 5g "Directional GI" toggle appeared to
have no effect. Investigation confirmed: **mode 6 (GI only) was always reading from the
isotropic `uRadiance` texture regardless of the `uUseDirectionalGI` toggle**. The user
was testing in mode 6 and seeing no change.

**Fix**: mode 6 in `raymarch.frag` now respects `uUseDirectionalGI`:

```glsl
if (uRenderMode == 6) {
    vec3 indirect6 = (uUseDirectionalGI != 0 && uUseCascade != 0)
        ? sampleDirectionalGI(pos, normal)
        : texture(uRadiance, uvw).rgb;
    fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);
    return;
}
```

Use mode 6 to A/B compare directional vs isotropic GI in isolation (no direct light, no
tone mapping). The HelpMarker for the directional GI checkbox was updated to mention this.

Mode 0 (final render) was already correctly using `sampleDirectionalGI` when the toggle is
ON. Mode 3 (Indirect*5) continues to read the isotropic probe grid (diagnostic purpose
unchanged).
