# Phase 4c Plan — Continuous Distance-Blend Merge

**Date:** 2026-04-24  
**Branch:** 3d  
**Depends on:** Phase 4b complete, Phase 4a complete (sky sentinel already wired)  
**Goal:** Replace the binary hit/miss switch at cascade interval boundaries with a linear blend, smoothing GI brightness bands at surfaces near interval edges.

---

## Problem

The current merge is binary at `tMax`:

```glsl
} else if (hit.a > 0.0) {
    totalRadiance += hit.rgb;        // surface hit — local data only
} else if (uHasUpperCascade != 0) {
    totalRadiance += upperSample;    // miss — upper cascade only
}
```

A ray hitting at `t = tMax - ε` uses 100% local data. A ray missing at `t = tMax + ε` uses 100% upper cascade. The hard switch can produce a visible GI brightness band when geometry sits near a cascade interval boundary.

---

## What This Fixes (and What It Does Not)

**Fixes:** The hard binary handoff at `tMax`. Surfaces near an interval edge get a smooth blend of local and upper-cascade data as `t → tMax`.

**Does not fix:** The isotropic merge mismatch. The upper cascade still contributes its directional average, not the radiance along the specific missed-ray direction. Phase 5 fixes that. If the banding visible in this scene is driven primarily by directional mismatch rather than the binary switch, 4c will have minimal visual effect. **A/B comparison is required and the result must be documented honestly — a no-op is an acceptable outcome.**

---

## Verified Pre-conditions

- `raymarchSDF()` currently returns `vec4(color, 1.0)` at surface hits (line 125) and `vec4(0.0)` for misses. Sky sentinel already returns `vec4(sky, -1.0)` from 4a. The `hit.a` sentinel convention is already established.
- `hit.a > 0.0` as the surface hit check still works when `a = t` because `t > tMin > 0` always. No branch logic changes.
- `hit.a == 0.0` miss and `hit.a < 0.0` sky paths are unaffected.
- The packed alpha `surfaceHits + skyHits * 255.0` written by `imageStore` increments `surfaceHits` on `hit.a > 0.0` — still correct when `.a` holds `t` rather than `1.0`. Debug stats are unaffected.
- `uBlendFraction` does not yet exist anywhere in the codebase (confirmed by grep).

---

## Files Touched

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | Return `t` in `.a` on surface hit; add `uBlendFraction`; blend in `main()` |
| `src/demo3d.h` | Add `float blendFraction` member |
| `src/demo3d.cpp` constructor | Initialize `blendFraction(0.5f)` |
| `src/demo3d.cpp updateSingleCascade()` | Push `uBlendFraction` uniform |
| `src/demo3d.cpp render()` | Add sentinel for `blendFraction` |
| `src/demo3d.cpp renderCascadePanel()` | Add slider + `(?)` help marker |

---

## Implementation

### `radiance_3d.comp` — return `t` on surface hit

Current (line 125):
```glsl
return vec4(color, 1.0);
```

Replace with:
```glsl
return vec4(color, t);
```

`t` is already in scope at the hit site. This is a one-character change. The sentinel convention is preserved: `a > 0` = surface hit, `a < 0` = sky exit, `a == 0` = miss.

### `radiance_3d.comp` — add uniform

```glsl
uniform float uBlendFraction;  // 0.0 = binary (Phase 3 behaviour), default 0.5
```

### `radiance_3d.comp` — blend in `main()`

Replace the surface-hit branch:

```glsl
vec3 upperSample = (uHasUpperCascade != 0)
                   ? texture(uUpperCascade, uvwProbe).rgb
                   : vec3(0.0);
float blendWidth = (tMax - tMin) * uBlendFraction;

if (hit.a < 0.0) {
    // Sky exit (4a) — unchanged
    totalRadiance += hit.rgb;
    ++skyHits;
} else if (hit.a > 0.0) {
    // Surface hit — blend toward upper cascade near tMax,
    // but only when an upper cascade exists (skip for C3)
    float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
        ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
        : 1.0;  // no upper cascade (C3) or binary mode: full local hit
    totalRadiance += hit.rgb * l + upperSample * (1.0 - l);
    ++surfaceHits;
} else {
    // In-volume miss — full upper cascade (zero for C3)
    totalRadiance += upperSample;
}
```

The `uHasUpperCascade != 0 && blendWidth > 0.0` guard serves two purposes:
- Prevents C3 surface hits from blending toward `vec3(0)` (there is no upper cascade to hand off to — full local data is always correct for C3)
- Prevents division by zero when `uBlendFraction == 0.0` (binary Phase 3 behaviour)

In binary mode (`l = 1.0`) the result is identical to Phase 3 behaviour. For C3 (`uHasUpperCascade == 0`), `l = 1.0` always regardless of `blendFraction`.

Note: `upperSample` and `blendWidth` are hoisted above the ray loop — they are uniform-dependent constants per probe, not per-ray values.

### `demo3d.h` — new member

```cpp
float blendFraction;  // default 0.5 — 4c blend zone as fraction of interval width
```

### Constructor

```cpp
, blendFraction(0.5f)
```

### `updateSingleCascade()` — push uniform

After the existing `uUseEnvFill` / `uSkyColor` pushes:

```cpp
glUniform1f(glGetUniformLocation(prog, "uBlendFraction"), blendFraction);
```

### `render()` — sentinel

Alongside the other sentinels (env fill, base rays):

```cpp
static float lastBlendFrac = -1.0f;
if (blendFraction != lastBlendFrac) {
    lastBlendFrac = blendFraction;
    cascadeReady  = false;
}
```

### `renderCascadePanel()` — slider + help marker

After the ray count scaling section:

```cpp
ImGui::Separator();
ImGui::Text("Interval Blend (4c):");
HelpMarker(
    "Controls how sharply the cascade hands off at its interval boundary (tMax).\n\n"
    "0.0 = binary (Phase 3 behaviour): surface hit uses local data only;\n"
    "      miss jumps immediately to upper cascade.\n\n"
    "0.5 = default: blend zone covers the outer 50%% of the interval.\n"
    "      A hit at tMax-epsilon lerps smoothly toward the upper cascade.\n\n"
    "If changing this has no visible effect, the banding is driven by\n"
    "directional mismatch (Phase 5 fix), not the binary switch.");
ImGui::SliderFloat("Blend fraction", &blendFraction, 0.0f, 1.0f);
ImGui::SameLine();
ImGui::TextDisabled(blendFraction < 0.01f ? "(binary — Phase 3)" : "(blended)");
```

---

## Validation (A/B Required)

| Test | Method | Expected |
|---|---|---|
| Binary vs blended | `blendFraction = 0.0` vs `0.5`, mode 0 or 6, look at surfaces near cascade boundaries | Some smoothing, or no visible difference |
| Phase 3 parity | `blendFraction = 0.0` must look identical to the pre-4c baseline | Exact match |
| surf% stable | Readback `surf%` unchanged between 0.0 and 0.5 (surfaceHits still increments on `hit.a > 0`) | Same count |
| dist_var heuristic | Compare cascade panel `dist_var` at 0.0 vs 0.5 | May decrease slightly at boundaries |
| C3 boundary (no upper) | At `blendFraction = 0.5`, C3 surface hits near 8m boundary | `l = 1.0` always (guard fires) — no darkening, full local data |

**Document the result either way.** If A/B shows no visual difference, record it explicitly — the banding is then attributed to directional mismatch and 4c is a confirmed no-op for this scene.

---

## Division-by-Zero Guard

When `uBlendFraction == 0.0`: `blendWidth = 0.0`. Without the guard, `(hit.a - blendStart) / blendWidth` is `0.0 / 0.0` — GLSL undefined behaviour (typically NaN on desktop, varies on mobile). The guard:

```glsl
float l = (blendWidth > 0.0)
    ? 1.0 - clamp((hit.a - blendStart) / blendWidth, 0.0, 1.0)
    : 1.0;
```

produces `l = 1.0` (full local hit, no blend) when `blendFraction == 0.0`, which is exactly binary Phase 3 behaviour. No branch divergence in the common case since `blendWidth` is uniform.

---

## Performance Note

- `upperSample` is hoisted above the ray loop and sampled **once per ray unconditionally** — including sky-exit rays (which never use it) and C3 rays (where the result is `vec3(0)` and multiplied by zero in the blend). Previously `upperSample` was only evaluated in the miss branch.
- At base=8 default: C0 fires 8 rays, C3 fires 64. All rays pay one bilinear 3D sample on the probe grid texture, already bound and likely cached.
- For a static bake this is negligible. If 4c is ever ported to a real-time bake, a lazy per-branch fetch (Option B from Codex reply) becomes worth revisiting.

---

## Relation to 4a and 4d

- **4a (env fill):** The sky sentinel `hit.a < 0.0` path is unchanged. C3 out-of-volume rays still return sky color directly without blending — the blend only applies to confirmed surface hits (`hit.a > 0.0`).
- **4d (filter verify):** Independent. Can run at any time.

---

## Relation to Phase 5

Phase 5 fixes the directional mismatch — the upper cascade will store per-direction radiance rather than an isotropic average. When Phase 5 lands, `upperSample` in the blend formula will carry directionally correct data, making 4c's smooth blend more meaningful. In the current Phase 3/4 isotropic cascade, the blend is softening a boundary but the upper sample it blends toward is still directionally wrong.
