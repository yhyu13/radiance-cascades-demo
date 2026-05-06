# 11 — Phase 5h: Binary Shadow Ray

**Purpose:** Understand how the direct lighting term acquires shadows and why the
renderer's shadow ray uses a different origin convention from the bake shader.

---

## The problem before Phase 5h

The `raymarch.frag` shader computes diffuse direct lighting as:

```glsl
float diff = max(dot(normal, lightDir), 0.0);
surfaceColor += albedo * diff * lightColor;
```

`diff` uses the surface normal and light direction — but it never asks *is there anything
between this point and the light?* Every surface received full N·L direct lighting
regardless of whether geometry blocked the path. Walls behind other walls were lit
identically to walls facing the light.

This was the single most visible defect in the pre-5h renderer: no hard shadows anywhere.

---

## The fix: SDF sphere march toward the light

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

Return convention: **0.0 = lit, 1.0 = in shadow**.

Integration at the call site:

```glsl
float shadow = (uUseShadowRay != 0) ? shadowRay(pos, normal, uLightPos) : 0.0;
float diff   = max(dot(normal, lightDir), 0.0) * (1.0 - shadow);
```

When `uUseShadowRay = 0`, `shadow = 0.0` and the path is identical to pre-5h.
Zero regression.

---

## Why the origin is not `hitPos` itself

Starting the march at the exact hit surface causes **self-intersection**: the surface SDF
value is ≈0 at the hit point, so `d < 0.002` triggers immediately, reporting *always in
shadow*. Every surface would be black.

The fix: offset the origin away from the surface before marching.

**Phase 5h uses a normal-offset origin:**

```
origin = hitPos + normal * 0.02 + ldir * 0.01
```

- `normal * 0.02`: move along the outward surface normal, guaranteed to be away from the
  surface regardless of the light direction.
- `ldir * 0.01`: additional offset in the light direction. Needed at **grazing incidence**:
  when the surface normal is nearly perpendicular to the light direction (`dot(normal, ldir) ≈ 0`),
  the normal offset does little to clear the surface in the direction of march. The `ldir`
  component ensures the origin is also slightly clear in the direction we're marching.

This is strictly better than the bake shader's approach (see below).

---

## Compare: renderer vs bake shader origin convention

The bake compute shader (`radiance_3d.comp`) also does shadow tests via `inShadow()`:

```glsl
bool inShadow(vec3 pos, vec3 lightPos) {
    // ...
    float t = 0.05;  // fixed bias — no surface normal available
    // ...
}
```

The bake shader does not have the surface normal at the probe hit point. Its march origin
is just `pos + dir * 0.05` — a fixed `t=0.05` step in the direction toward the light.
This is imprecise at grazing angles and can produce self-shadow artifacts if the fixed
bias is too small.

The final renderer does have the surface normal (computed by `estimateNormal(pos)` via
the SDF gradient). Using that normal in the offset is always geometrically sound.

| | renderer `shadowRay()` | bake `inShadow()` |
|---|---|---|
| Normal available? | Yes — `estimateNormal(pos)` | No |
| Origin offset | `normal * 0.02 + ldir * 0.01` | fixed `t = 0.05` |
| Quality at grazing | Better (two-axis offset) | Fixed bias may self-shadow |

---

## Volume exit: not-in-shadow

`sampleSDF()` returns `1e10` (defined as `INF`) when the sample point is outside the
scene volume. The light source is outside the scene volume. So: if the shadow ray travels
all the way to the volume boundary without hitting geometry, there is nothing between
the surface and the light — the point is lit.

`if (d >= 1e9) return 0.0;` — the `1e9` threshold catches both `INF = 1e10` and any
future slightly-below-INF sentinel.

---

## What Phase 5h changes and does not change

**Changes:**
- Mode 0 (final render): direct term now correctly darkens occluded surfaces.
- Mode 4 (direct-only debug): same shadow applied; mode 4 now shows the correctly
  shadowed direct scene in isolation. Useful as a reference when adding Phase 5g
  indirect on top.

**Does not change:**
- Cascade bake (`radiance_3d.comp`): probe radiance already uses `inShadow()` for baked
  direct shading. Phase 5h does not touch bake shaders — no `cascadeReady = false`.
- Mode 3, mode 5, mode 6: these read from the atlas or probe grid directly; no path through
  the direct lighting term.
- Shadow softness: this is a binary shadow (hit = fully dark, no hit = fully lit). Phase 5i
  adds SDF cone soft shadow as an optional replacement.

---

## Key invariant summary

| Scenario | Result |
|---|---|
| `uUseShadowRay = 0` | `shadow = 0.0` → identical to pre-5h |
| `uUseShadowRay = 1`, open path to light | `shadowRay()` returns 0.0, full direct |
| `uUseShadowRay = 1`, geometry occludes | `shadowRay()` returns 1.0, `diff = 0.0` |
| Ray exits scene volume | `d >= 1e9` → 0.0, not in shadow |
| Grazing incidence | `ldir * 0.01` offset prevents false self-shadow |
