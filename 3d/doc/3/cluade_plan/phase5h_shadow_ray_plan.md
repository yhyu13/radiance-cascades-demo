# Phase 5h — Shadow Ray in Final Renderer

**Date:** 2026-04-29
**Branch:** 3d
**Snapshot as of:** b3b04ef
**Priority:** 1 (implement before Phase 5g)
**Prerequisite:** Phase 5d trilinear (b3b04ef) complete

---

## Problem Statement

`raymarch.frag` computes direct lighting with no shadow check (lines 285–288):

```glsl
vec3 lightDir     = normalize(uLightPos - pos);
float diff        = max(dot(normal, lightDir), 0.0);
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));
```

Every surface receives full unshadowed N·L. Shadow only appears as a *reduction* in
the cascade indirect term (`uRadiance`), which is already diluted ~8–16× by the
isotropic averaging in `reduction_3d.comp`. The result: the shadow penumbra is weak and
shows discrete bands at the probe grid spacing (~0.125m).

Adding a shadow ray to `raymarch.frag` gives the direct term its own hard binary
shadow, fixing the primary display defect. It also provides a correct direct-light
reference in mode 4 so Phase 5g's indirect contribution can be judged against a
properly shadowed scene — not as a penumbra ground truth, but as a baseline that
makes the indirect contribution's spatial smoothing visible in isolation.

---

## What Changes

### `res/shaders/raymarch.frag`

#### New uniform

```glsl
uniform int uUseShadowRay;   // 5h: 1=shadow ray in direct path (default), 0=unshadowed (Phase 1-4 behaviour)
```

#### Shadow helper function (after `estimateNormal`)

```glsl
// normal: outward surface normal at hitPos (already computed before this call)
float shadowRay(vec3 hitPos, vec3 normal, vec3 lightPos) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    // Normal-offset origin: analytically outside the surface along the outward normal.
    // Additional ldir offset handles grazing incidence (normal ⊥ light).
    // Better than fixed t=0.05 — uses the normal the final renderer already has.
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;        // exited volume — not shadowed
        if (d < 0.002) return 1.0;       // hit geometry — in shadow
        t += max(d * 0.9, 0.01);
    }
    return 0.0;
}
```

#### Replace direct lighting block in `main()` (mode 0 and mode 4)

Mode 0 (final), replace lines 285–288:
```glsl
// Before:
float diff    = max(dot(normal, lightDir), 0.0);
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));

// After:
float shadow      = (uUseShadowRay != 0) ? shadowRay(pos, normal, uLightPos) : 0.0;
float diff        = max(dot(normal, lightDir), 0.0) * (1.0 - shadow);
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));
```

Mode 4 (direct-only debug), apply the same shadow factor to `diff4`:
```glsl
float shadow4  = (uUseShadowRay != 0) ? shadowRay(pos, normal, uLightPos) : 0.0;
float diff4    = max(dot(normal, lightDir4), 0.0) * (1.0 - shadow4);
```

---

### `src/demo3d.h`

Add after `bool useSpatialTrilinear;`:
```cpp
bool useShadowRay;  // 5h: true=shadow ray in direct path (default); false=unshadowed (Phase 1-4)
```

### `src/demo3d.cpp`

#### Constructor
Add `, useShadowRay(true)` after `, useSpatialTrilinear(true)`.

#### `raymarchPass()` — push uniform
```cpp
glUniform1i(glGetUniformLocation(raymarchShader, "uUseShadowRay"), useShadowRay ? 1 : 0);
```

#### `render()` — tracking block
```cpp
static bool lastShadowRay = true;
if (useShadowRay != lastShadowRay) {
    lastShadowRay = useShadowRay;
    // No cascadeReady reset — this is a display-path-only change
    std::cout << "[5h] shadow ray: " << (useShadowRay ? "ON" : "OFF (unshadowed)") << std::endl;
}
```

No cascade rebuild needed — this only affects `raymarch.frag`, not the bake passes.

#### `renderCascadePanel()` — checkbox

In the Phase 5 section, after the cascade GI checkbox:
```cpp
ImGui::Checkbox("Shadow ray in direct path (Phase 5h)", &useShadowRay);
HelpMarker(
    "ON  (default): casts a 32-step shadow ray from each surface hit toward the\n"
    "     light. Uses the same SDF-march as the cascade bake inShadow(). Gives\n"
    "     hard binary shadow in the direct term.\n\n"
    "OFF (Phase 1-4 baseline): no shadow check in direct path. Shadow only\n"
    "     appears via the cascade indirect contribution, which is diluted ~8x\n"
    "     by the isotropic reduction.\n\n"
    "Toggle does NOT re-bake cascades -- display path only.");
ImGui::SameLine();
ImGui::TextDisabled(useShadowRay ? "(shadow ON)" : "(unshadowed)");
```

---

## Correctness Invariants

| Scenario | Behaviour |
|---|---|
| `uUseShadowRay=0` | `shadow=0.0`, `diff` unchanged → identical to Phase 1-4. Zero regression. |
| `uUseShadowRay=1`, point in full light | `shadowRay()` returns 0.0 → `diff` unchanged. Same as before. |
| `uUseShadowRay=1`, point fully occluded | `shadowRay()` returns 1.0 → `diff=0`. Only ambient 0.05 + cascade indirect. |
| `uUseShadowRay=1`, point at penumbra | Binary: either 0 or 1. Hard edge. Phase 5g will soften this. |
| Volume exit in shadow ray | `d >= INF*0.5` → return 0.0 (not shadowed). Light is outside scene bounds. |

The shadow ray uses the same `sampleSDF()` already in `raymarch.frag` — no new texture
bindings required. `uLightPos` is already a uniform in `raymarch.frag`.

---

## Performance

32 SDF texture samples per pixel for shaded surfaces. At 1280×720 with ~30% surface
coverage, ~8M texture fetches per frame. These are 3D texture reads — fast on GPU.
Expected acceptable; verify actual frame time with shadow ray ON vs OFF at runtime.

---

## Why Before Phase 5g

Phase 5h fixes the direct term — the larger and more obvious error. With Phase 5h ON,
mode 4 (direct-only) shows the correctly shadowed scene as a visual reference. Phase 5g
then adds directional GI on top of that correct baseline, making the indirect
contribution's spatial smoothing visible in isolation.

Without Phase 5h, the unshadowed direct term overwhelms the cascade indirect and
makes any Phase 5g improvement hard to judge at cascade boundaries.

Note: Phase 5h does not validate Phase 5g. Direct-light occlusion and diffuse probe
irradiance are different quantities. The combined result (5h + 5g) shows correct direct
shadow plus smoother GI transitions — not a physically correct area-light penumbra.

---

## Stop Conditions

| Test | Expected |
|---|---|
| Build: 0 errors | Both modes |
| Shadow ray OFF: identical image to pre-change | Zero regression |
| Shadow ray ON: hard shadow visible under ceiling geometry | Confirmed |
| Mode 4 (direct-only) + shadow ON: shadow visible without GI | Confirmed |
| Shadow boundary: hard edge (no softening yet) | Expected — Phase 5g will add penumbra |
| Frame rate: ≥ 60fps with shadow ray ON | Acceptable cost |
| Log: `[5h] shadow ray: ON` on toggle | Confirmed |
