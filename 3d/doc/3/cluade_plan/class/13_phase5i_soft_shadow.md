# 13 — Phase 5i: SDF Cone Soft Shadow

**Purpose:** Understand the IQ cone soft shadow formula, why the display and bake paths
use different origin conventions, and why sharing one `k` slider is UI convenience rather
than physical consistency.

---

## The artifact being fixed: hard shadow boundary banding

Phase 5h's binary shadow ray produces a perfectly sharp shadow edge — correct for a
mathematical point light, but visually harsh and prone to accentuating cascade-level
spatial banding at probe cell boundaries. The cascade indirect GI (from Phase 5g) has
smooth spatial blending, but the direct shadow cuts are pixel-sharp, making the two
contributions visually inconsistent.

Phase 5i replaces the binary shadow test with an **SDF cone soft shadow** that
accumulates a penumbra factor as the ray marches toward the light.

---

## The IQ cone soft shadow formula

The idea: as a shadow ray marches toward the light, at each step we have:
- `d` = SDF value at the current point (how far we are from the nearest surface)
- `t` = how far we have marched from the origin

The ratio `d/t` is roughly proportional to the **angular half-width of the unoccluded
cone** at that step. If geometry nearly clips the ray (small `d` for a given `t`), the
cone is narrow — the surface is nearly in hard shadow. If the ray passes far from geometry
(`d` large relative to `t`), the cone is wide — the surface is in open light.

```glsl
float softShadow(vec3 hitPos, vec3 normal, vec3 lightPos, float k) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;  // same normal-offset as Phase 5h
    float t   = 0.0;
    float res = 1.0;  // 1.0 = fully lit; approaches 0 as penumbra narrows
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9)  return 0.0;   // exited volume — not in shadow
        if (d < 0.002) return 1.0;   // hit geometry — full shadow
        res = min(res, k * d / max(t, 0.001));  // accumulate narrowest cone seen so far
        t  += max(d * 0.9, 0.01);
    }
    return 1.0 - clamp(res, 0.0, 1.0);  // invert: 0=lit, 1=shadowed
}
```

The `min(res, k*d/t)` keeps only the **narrowest cone** seen across all march steps —
a single near-miss anywhere on the path produces a shadow even if the rest of the path
is clear. This gives a physically plausible penumbra that narrows as geometry
approaches the ray.

**`k` parameter:** scales how quickly the shadow darkens.
- Low `k` (e.g. 1): wide, soft penumbra — shadow fades over a large region.
- High `k` (e.g. 16): approaches binary — very narrow penumbra, nearly hard edge.

Return convention: 0.0 = lit, 1.0 = full shadow — same as Phase 5h's `shadowRay()`.
The two are interchangeable at the call site via a ternary:

```glsl
float shadow = (uUseShadowRay != 0)
    ? ((uUseSoftShadow != 0) ? softShadow(pos, normal, uLightPos, uSoftShadowK)
                             : shadowRay(pos, normal, uLightPos))
    : 0.0;
```

With `useSoftShadow = false` (default): falls through to `shadowRay()` — identical to
Phase 5h. No regression.

---

## Two paths: display (Strategy A) and bake (Strategy B1)

Phase 5i implements soft shadow in two independent places:

### Strategy A — display path (`raymarch.frag`)

`softShadow()` (above) runs per shaded pixel in the renderer. It:
- Uses the surface normal for origin offset (`normal * 0.02 + ldir * 0.01`)
- Modifies the direct-light term the user sees directly on screen
- Does **not** require a cascade rebuild (display-path only, same as Phase 5h)

Toggling `useSoftShadow` shows/hides the soft penumbra in direct light immediately.

### Strategy B1 — bake path (`radiance_3d.comp`)

`softShadowBake()` runs in the compute shader that writes probe radiance:

```glsl
float softShadowBake(vec3 hitPos, vec3 lightPos, float k) {
    // ...
    float t = 0.05;  // fixed bias — no surface normal in compute shader
    // ...
    // same k*d/t accumulation
}
```

It replaces `inShadow()` for the direct shading stored per-probe. Changes here affect the
baked indirect GI, not the direct display term. Because probe radiance changes, a cascade
rebuild is required:

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

`k` changes only trigger a rebuild when bake soft shadow is ON (changing `k` while bake
soft shadow is OFF does not change bake output — no rebuild needed).

---

## Why the two paths use different origin offsets

| | display `softShadow()` | bake `softShadowBake()` |
|---|---|---|
| Normal available? | Yes — `estimateNormal()` in renderer | No — probe hit via ray march in compute |
| Origin | `hitPos + normal * 0.02 + ldir * 0.01` | `hitPos + dir * 0.05` (fixed bias) |
| Same as Phase 5h/bake | Normal-offset (same as `shadowRay()`) | Fixed-bias (same as `inShadow()`) |

The bake path inherits the same `t=0.05` fixed-bias approach as the binary `inShadow()` because
the surface normal is not available during bake-time probe ray marching.

---

## Shared `k`: UI convenience, not physical consistency

Both `softShadow()` (display) and `softShadowBake()` (bake) read from the same
`uSoftShadowK` uniform. This is intentional — one slider rather than two — but does
**not** mean the same `k` produces the same apparent penumbra width in both paths.

Two reasons they differ:

1. **Origin convention**: display path uses normal-offset (tighter surface clearance),
   bake path uses fixed `t=0.05` bias. The same `k` value starts accumulating `k*d/t`
   at a different geometric clearance — different apparent penumbra at the same `k`.

2. **Signal path**: display soft shadow modifies the direct-light term the user sees directly.
   Bake soft shadow modifies probe-hit shading, which then propagates through the cascade
   hierarchy (angular averaging in reduction, directional merging across cascade levels)
   before appearing on screen. The cascade pipeline smooths and redistributes the baked
   penumbra spatially. The user sees a filtered version, not the raw `k*d/t` result.

The correct mental model: shared `k` keeps the two approximations in a roughly similar
penumbra range so the UI is simple. Matching the two paths visually requires empirical
tuning, not deriving `k` from first principles.

---

## What this does and does not fix

**Fixes (display path):**
- Hard binary shadow edge in direct lighting → smooth penumbra with width controlled by `k`
- Reduced contrast between direct shadow and the Phase 5g smoothly-interpolated indirect GI

**Fixes (bake path):**
- Binary shadow boundaries stored in probe radiance → softer probe-baked shadows
- Reduces Source 2/3 banding (probe grid artifacts at shadow edges in the indirect channel)

**Does not fix:**
- This is not a physically correct area-light model. A point light has no penumbra;
  the cone soft shadow is an artistic approximation.
- Bake soft shadow and display soft shadow will not match visually at the same `k` (see above).
- Mode 3 (Indirect×5) does not separate baked shadow from probe radiance — you see the
  combined effect of the bake changes only by comparing before/after a rebuild.

---

## Debugging soft shadow

Use mode 4 (direct-only debug) to isolate the display soft shadow:
1. Enable `useShadowRay` (Phase 5h)
2. Enable `useSoftShadow` (Phase 5i display)
3. Switch to mode 4 — shows direct lighting with the soft penumbra, no indirect
4. Adjust `k`: low = wide soft shadow, high = binary-like

For bake soft shadow:
1. Enable `useSoftShadowBake`, wait for rebuild
2. Switch to mode 6 (GI-only) — shows indirect GI baked with soft shadows at probe level
3. Compare mode 6 with `uUseDirectionalGI` ON and OFF to see how baked penumbra interacts
   with the directional filtering

---

## Phase 5 shadow summary

| Phase | What | Where | Rebuild? |
|---|---|---|---|
| 5h | Binary shadow ray | Display direct term | No |
| 5i-A | Cone soft shadow | Display direct term | No |
| 5i-B1 | Cone soft shadow | Bake probe shading | Yes — probe radiance changes |
