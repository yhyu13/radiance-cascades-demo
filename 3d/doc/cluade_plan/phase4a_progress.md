# Phase 4a Progress — Environment Fill Toggle

**Date:** 2026-04-23  
**Branch:** 3d  
**Status:** Implemented + debugged  
**Plan reference:** `phase4_plan.md §4a`

---

## What Was Implemented

### Shader (`res/shaders/radiance_3d.comp`)

Two new uniforms:
```glsl
uniform int   uUseEnvFill;   // 0 = honest miss (default), 1 = fill with sky
uniform vec3  uSkyColor;     // sky color when uUseEnvFill is on
```

`raymarchSDF()` now detects volume exit before the surface-hit check:
```glsl
if (dist >= INF * 0.5) {
    if (uUseEnvFill != 0)
        return vec4(uSkyColor, -1.0);  // sky sentinel
    else
        return vec4(0.0);              // honest miss
}
```

Return convention (`.a` encodes result type):
- `vec4(color,  1.0)` — direct surface hit
- `vec4(sky,   -1.0)` — exited simulation volume, env fill ON
- `vec4(0.0)`         — in-volume miss (or volume exit with fill OFF)

`main()` dispatches on `.a`:
```glsl
if      (hit.a < 0.0) totalRadiance += hit.rgb;            // sky
else if (hit.a > 0.0) { totalRadiance += hit.rgb; ++surfaceHits; }  // surface
else if (uHasUpperCascade != 0) totalRadiance += texture(uUpperCascade, uvwProbe).rgb;
```

Alpha channel of stored probe repurposed for debug stat:
```glsl
float surfaceFrac = float(surfaceHits) / float(uRaysPerProbe);
imageStore(oRadiance, probePos, vec4(totalRadiance, surfaceFrac));
```

The merge path reads only `.rgb` from the upper cascade texture — alpha repurposing does not affect the cascade chain.

### CPU (`src/demo3d.cpp` + `src/demo3d.h`)

New members:
```cpp
bool      useEnvFill;   // default false
glm::vec3 skyColor;     // default (0.02, 0.03, 0.05)
```

Sentinel in `render()` — triggers cascade rebuild only when the effective state changes:
```cpp
if (useEnvFill != lastEnvFill ||
    (useEnvFill && skyColor != lastSkyColor)) {  // sky color only matters when fill is ON
    lastEnvFill  = useEnvFill;
    lastSkyColor = skyColor;
    cascadeReady = false;
}
```

Uniforms pushed in `updateSingleCascade()`:
```cpp
glUniform1i(glGetUniformLocation(prog, "uUseEnvFill"), useEnvFill ? 1 : 0);
glUniform3fv(glGetUniformLocation(prog, "uSkyColor"),  1, glm::value_ptr(skyColor));
```

Probe readback tracks `probeSurfaceHit[]` separately from `probeNonZero[]`:
```cpp
float surfFrac = buf[i*4+3];  // surface-hit fraction stored in probe alpha
if (surfFrac > 0.01f) ++surfHit;
```

### UI (`renderCascadePanel()` + `renderTutorialPanel()`)

Env fill section in Cascades panel:
- Checkbox with hover tooltip explaining cascade propagation
- Color picker (only visible when fill is ON)
- `ImGuiColorEditFlags_Float` for sub-1.0 sky values

Per-cascade stats row now shows three columns:
```
C3 [2.00,8.00]: any=100.0%  surf=3.1%  sky*=96.9%  max=0.050  mean=0.0330
```

`sky* = any% - surf%` is derived at display time — no additional GPU data needed.  
It is **exact for C3** (C3 has no upper cascade, so any contribution not from a surface hit must be a direct sky exit).  
For C0-C2 it includes sky propagated via the merge chain.

Tutorial panel Phase 4 section shows live state: 4a label flips green/grey based on `useEnvFill`.

---

## Bugs Found and Fixed During Implementation

### Bug 1 — Unnecessary cascade rebuild on sky color change while fill is OFF

**Original sentinel:**
```cpp
if (useEnvFill != lastEnvFill || skyColor != lastSkyColor)
```
Tweaking the color picker while `useEnvFill = false` triggered a full cascade bake even though sky color has no effect when fill is off.

**Fix:** Gate the color check on the toggle being active:
```cpp
if (useEnvFill != lastEnvFill ||
    (useEnvFill && skyColor != lastSkyColor))
```

---

### Bug 2 — All cascade levels showed ~100% fill rate when env fill ON

**Root cause:** Sky color from C3 propagates through the merge chain to C0. Any C2/C1/C0 probe with a miss ray samples the level above, which now has sky color. Since sky luma (~0.033) is far above the `1e-4` non-zero threshold, every probe with any miss ray becomes non-zero.

This is **correct cascade physics** — sky ambient propagates through the hierarchy. But the `any%` stat alone was misleading.

**Fix:** Track surface hits separately (probe alpha = surfaceFrac from the shader), show both `surf%` and derived `sky*%` in the UI. `surf%` remains stable regardless of env fill.

**Key insight:** For C3 specifically (no upper cascade exists), `sky*% = any% - surf%` is exact — any non-surface contribution must be a direct sky exit. For C0-C2 it includes both direct exits and merge propagation.

---

### Bug 3 — Color-coding regressed after adding surf% stat

**Original:** Color-coded by `any%` (intuitive: how much data does this cascade have?)  
**Introduced error:** Switched to coloring by `surf%`, which permanently marks C3 red (~3%) even when everything is working correctly.

**Fix:** Reverted color coding to `any%`-based.

---

### Non-issue confirmed — alpha repurposing

Initially flagged as a potential critical bug: `raymarch.frag` and `radiance_debug.frag` might sample probe texture alpha.

**Result after inspection:**
- `raymarch.frag`: all cascade samples use `.rgb` only ✓
- `radiance_debug.frag`: uses `.rgb` for display; alpha is included in intermediate vec4 max/average computations but is never output or used for decisions ✓

No rendering bug. Alpha repurposing is safe.

---

## Validation Criteria (from plan)

| Test | Expected | Status |
|---|---|---|
| `useEnvFill = false` | C3 any% ~3%, surf% ~3% — baseline unchanged | Not yet verified at runtime |
| `useEnvFill = true` | C3 any% ~100%, sky*% ~97% — expected by construction | Not yet verified at runtime |
| Mode 6 with env fill ON | Visibly brighter indirect in dark areas | Not yet verified at runtime |
| Color picker change with fill OFF | No cascade rebuild triggered | Fixed (Bug 1) |
| All levels any% ~100% with fill ON | Expected; surf% stays stable | Explained (Bug 2) |

---

## Relation to Remaining Phase 4 Tasks

- **4b (per-cascade ray scaling):** The `.a = surfaceFrac` return convention in `raymarchSDF` is already in place. 4b has no shader impact — it only changes `raysPerProbe` uniforms and the `initCascades()` call.
- **4c (continuous blend):** Will add `hit.a = t` for surface hits (replacing `1.0`). The sky sentinel uses `-1.0` which is unambiguous. No conflict.
- **4a → 4c interaction:** The `hit.a < 0` sky branch in `main()` is already wired. When 4c lands, the `hit.a > 0` branch gets the blend logic. Sky branch is untouched.
