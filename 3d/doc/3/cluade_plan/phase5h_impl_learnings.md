# Phase 5h Implementation Learnings — Shadow Ray in Final Renderer

**Date:** 2026-04-29
**Branch:** 3d
**Files changed:**
- `res/shaders/raymarch.frag`
- `src/demo3d.h`
- `src/demo3d.cpp`

---

## What Was Done

Phase 5h adds a binary shadow ray from each primary surface hit to the point light,
fixing the largest visual defect: the direct lighting term in `raymarch.frag` had no
shadow test — every surface received full unshadowed N·L.

---

## Shader Changes (`raymarch.frag`)

### New uniform

```glsl
uniform int uUseShadowRay;  // 5h: 1=shadow ray in direct path (default), 0=unshadowed
```

Placed after `uniform bool uUseCascade;` — easy to find in the Phase 5 uniform block.

### Shadow helper function

```glsl
float shadowRay(vec3 hitPos, vec3 normal, vec3 lightPos) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;   // exited volume — not occluded
        if (d < 0.002) return 1.0;  // hit geometry — in shadow
        t += max(d * 0.9, 0.01);
    }
    return 0.0;
}
```

**Key design decisions:**

1. **Normal-offset origin** (`normal * 0.02 + ldir * 0.01`): The final renderer has the
   surface normal available at the hit point. Using it to offset the origin guarantees
   we start outside the surface along the outward direction. The additional `ldir * 0.01`
   handles grazing incidence (normal ⊥ light direction). This is strictly better than
   the compute shader's `t = 0.05` fixed-bias approach (used in `inShadow()` in
   `radiance_3d.comp`), which was designed for when no normal is available.

2. **Volume exit early return** (`d >= 1e9`): `sampleSDF()` returns `INF = 1e10` when
   the ray exits the volume bounds. Treating this as not-in-shadow is correct: the
   light source is outside the scene volume, so if the ray reaches the boundary
   unobstructed, there is no geometry between the surface and the light.

3. **32 steps**: Same count as the bake shadow test (`inShadow()` in `radiance_3d.comp`).
   Sufficient for the Cornell Box geometry at the probe cell scale (~0.125m).
   *Manual runtime observation*: no false-positive shadow or acne visible in the Cornell
   Box scene during implementation session. This is empirical, not statically verifiable
   from code alone — different scenes or SDF configurations may require tuning.

4. **SDF march threshold 0.002**: Conservative enough to avoid false hits at surface
   reconstruction noise from the discretized SDF, but tight enough to catch real
   geometry before the ray terminates.

### Integration into mode 0 and mode 4

Both modes share the same pattern:

```glsl
float shadow  = (uUseShadowRay != 0) ? shadowRay(pos, normal, uLightPos) : 0.0;
float diff    = max(dot(normal, lightDir), 0.0) * (1.0 - shadow);
```

When `uUseShadowRay = 0`, `shadow = 0.0` and `(1.0 - shadow) = 1.0` — identical to
the pre-change path. Zero regression guaranteed at the GLSL level.

Mode 4 (direct-only debug) uses `shadow4`/`diff4` names to avoid shadowing mode 0
variables. Mode 4 now shows the correctly shadowed direct term in isolation, making it
a useful reference baseline when evaluating Phase 5g indirect GI.

---

## C++ Changes

| Location | Change |
|---|---|
| `demo3d.h` line ~699 | Added `bool useShadowRay;` field with doc comment |
| Constructor line ~111 | Added `, useShadowRay(true)` (default ON) |
| `render()` after `lastSpatialTrilinear` block | Tracking block: log toggle, no cascadeReady reset |
| `raymarchPass()` after albedo binding | `glUniform1i("uUseShadowRay", useShadowRay ? 1 : 0)` |
| `renderCascadePanel()` after spatial trilinear section | ImGui checkbox + HelpMarker (updated in review 13 fix) |

### HelpMarker accuracy (updated after review 13)

The original HelpMarker text said "Uses the same SDF-march as the cascade bake
`inShadow()`." This was inaccurate: the final renderer's `shadowRay()` uses a
normal-offset origin (`normal*0.02 + ldir*0.01`) which is *better* than the bake
shader's fixed `t=0.05` bias. The HelpMarker was updated to reflect this distinction:

> "Uses a normal-offset origin (normal\*0.02 + ldir\*0.01) — better than the bake
> shader's fixed t=0.05 bias because the surface normal is known in the final renderer."

### No cascade rebuild on toggle

`useShadowRay` controls `raymarch.frag` only — it does not affect any bake pass
(`radiance_3d.comp`, `reduction_3d.comp`, or voxelization). The tracking block
intentionally omits `cascadeReady = false`. Compare to `useSpatialTrilinear` which
DOES reset `cascadeReady` because it affects the bake shader path.

### Texture unit assignment unchanged

No new textures needed. `shadowRay()` calls `sampleSDF()` which reads `uSDF`
(unit 0, already bound). The texture unit assignment table remains:
- Unit 0: `uSDF`
- Unit 1: `uRadiance`
- Unit 2: `uAlbedo`
- (Unit 3 reserved for Phase 5g directional atlas)

---

## Correctness Invariants

| Scenario | Behaviour |
|---|---|
| `uUseShadowRay=0` | `shadow=0.0`, `diff` unchanged → identical to Phase 1-4. |
| `uUseShadowRay=1`, point fully lit | `shadowRay()` returns 0.0 → no change to `diff`. |
| `uUseShadowRay=1`, point fully occluded | `shadowRay()` returns 1.0 → `diff=0.0`. Only ambient 0.05 + cascade indirect. |
| Ray exits volume in shadow test | `d >= 1e9` → 0.0 (not shadowed). Correct: light is outside scene. |
| Grazing incidence | `ldir * 0.01` offset prevents self-intersection at near-perpendicular surface-light angles. |

---

## What This Does Not Do

- Does not soften the shadow edge (remains binary). Phase 5g's 8-probe trilinear GI
  will produce spatially smooth indirect that transitions near shadow boundaries, but
  the direct term shadow will always be hard (binary point-light result).
- Does not affect cascade baking — the shadow in probes is still governed by
  `inShadow()` in `radiance_3d.comp`, which uses the same SDF march but without
  the normal-offset improvement (no normal available during bake).
- Does not change mode 5 (step heatmap), mode 6 (GI-only raw), or mode 3 (indirect ×5).

---

## Follow-on: Phase 5g

With Phase 5h ON, mode 4 (direct-only) now shows the correct shadowed scene.
Phase 5g (directional atlas sampling) adds cosine-weighted hemisphere integration
over the C0 probe atlas, replacing the isotropic average. The combined result (5h + 5g)
gives correct direct shadow plus spatially smooth indirect GI.

---

## Related Bug Fix (2026-04-29)

Phase 5h shadow is applied outside the `if (uUseCascade != 0)` gate — it is always
computed in mode 0 regardless of cascade GI state. However, a bug was found where
`useCascadeGI` defaulted to `false`, which suppressed indirect GI entirely and made
Phase 5h appear to be the only active contribution. Fixed by changing the constructor
default to `useCascadeGI(true)`. See `phase5g_impl_learnings.md` bug-fix section for
full details.

The `uniform bool uUseCascade` declaration in `raymarch.frag` was also changed to
`uniform int` for consistency with Phase 5 conventions (`uUseShadowRay`,
`uUseDirectionalGI` are both `uniform int`). Usage site updated to
`if (uUseCascade != 0)`. Shadow ray is unaffected — it is gated by `uUseShadowRay`,
not `uUseCascade`.
