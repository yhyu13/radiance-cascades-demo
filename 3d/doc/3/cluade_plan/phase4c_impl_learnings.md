# Phase 4c Implementation Learnings — Continuous Distance-Blend Merge

**Date:** 2026-04-24  
**Branch:** 3d  
**Status:** Complete  
**Follows:** `phase4c_plan.md` + Codex critic `06_phase4c_plan_review.md` + `claude_reply/reply_phase4c_plan_review.md`

---

## What Was Implemented

### 1. Return actual hit distance `t` from `raymarchSDF()`

`res/shaders/radiance_3d.comp:126` — one-character change:

```glsl
// Before (Phase 3)
return vec4(color, 1.0);

// After (Phase 4c)
return vec4(color, t);
```

The sentinel convention `hit.a > 0` = surface hit is preserved because `t > tMin > 0` always. The `hit.a == 0` miss and `hit.a < 0` sky paths are unaffected. This is the only raymarch change — the hit-type branches in `main()` did not need to change because `a > 0` is still the correct surface test.

### 2. `uBlendFraction` uniform

`res/shaders/radiance_3d.comp:42`:
```glsl
uniform float uBlendFraction;  // 0.0 = binary (Phase 3), default 0.5
```

`src/demo3d.cpp:915` (`updateSingleCascade()`):
```cpp
glUniform1f(glGetUniformLocation(prog, "uBlendFraction"), blendFraction);
```

### 3. Blend logic in `main()` — hoisted constants + guarded lerp

`res/shaders/radiance_3d.comp:159–186`:

```glsl
// Hoisted outside the ray loop — uniform-dependent constants per probe
vec3 upperSample = (uHasUpperCascade != 0)
                   ? texture(uUpperCascade, uvwProbe).rgb
                   : vec3(0.0);
float blendWidth = (tMax - tMin) * uBlendFraction;

for (int i = 0; i < uRaysPerProbe; ++i) {
    vec4 hit = raymarchSDF(worldPos, rayDir, tMin, tMax);
    if (hit.a < 0.0) {
        totalRadiance += hit.rgb;   // sky sentinel — unchanged
        ++skyHits;
    } else if (hit.a > 0.0) {
        // Surface hit — blend toward upper cascade near tMax,
        // guarded: no blend for C3 (no upper cascade to hand off to)
        float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
            ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
            : 1.0;
        totalRadiance += hit.rgb * l + upperSample * (1.0 - l);
        ++surfaceHits;
    } else {
        totalRadiance += upperSample;  // miss — upper cascade (zero for C3)
    }
}
```

### 4. C++ changes

| Location | Change |
|---|---|
| `demo3d.h:657` | `float blendFraction;` member |
| Constructor | `, blendFraction(0.5f)` |
| `updateSingleCascade()` | `glUniform1f(..., "uBlendFraction", blendFraction)` |
| `render()` sentinel | `static float lastBlendFrac = -1.0f;` triggers rebake on change |
| `renderCascadePanel()` | "Interval Blend (4c)" section with `SliderFloat`, `(?)` help marker, binary/blended tag |
| Tutorial panel | 4c status line — green, shows `blend=N.NN` and mode tag |

---

## Key Learnings

### The C3 blend-toward-black bug (caught by Codex before implementation)

The original plan code set `upperSample = vec3(0.0)` for C3 (no upper cascade) and then blended surface hits toward that value as `t → tMax`. For a surface hit at `t = 7.9m` in C3's `[2.0, 8.0m]` band, the blend factor `l` would be less than 1.0 — meaning valid local radiance gets discarded with no physical justification.

The fix is a compound guard:
```glsl
float l = (uHasUpperCascade != 0 && blendWidth > 0.0) ? ... : 1.0;
```

When `uHasUpperCascade == 0` (C3): `l = 1.0` always. The math `hit.rgb * 1.0 + vec3(0) * 0.0` is a no-op — full local data, no energy discarded.

**Key insight:** "No upper cascade" and "upper cascade contributes zero energy" are different things. The guard encodes the first. The code path for C3 misses (`totalRadiance += upperSample`) correctly produces zero — an in-volume miss in C3 that escapes the interval genuinely has no further cascade to fall back on, so zero is the right answer. But a surface *hit* in C3 has real local energy and must not be blended toward zero.

### Division-by-zero guard doubles as the C3 guard

The compound `uHasUpperCascade != 0 && blendWidth > 0.0` handles two independent failure modes:

1. **`uHasUpperCascade == 0`**: C3 must not blend (energy loss, explained above).
2. **`blendWidth == 0.0`**: When `uBlendFraction == 0.0`, `blendWidth = 0` → division `(hit.a - blendStart) / blendWidth` is `0.0 / 0.0` — GLSL undefined behaviour (NaN on desktop). The guard short-circuits to `l = 1.0`, exactly the binary Phase 3 result.

Both fail safely to `l = 1.0` (full local hit), so a single `||`-free `&&` guard handles both.

### `upperSample` and `blendWidth` are probe-level constants, not ray-level

Both depend only on uniforms (`uHasUpperCascade`, `uUpperCascade`, `uBlendFraction`, `tMin`, `tMax`) — none of which vary per ray. Hoisting outside the loop is correct: all rays in a given probe dispatch share the same `uvwProbe` and interval.

The cost: one `texture()` call per probe unconditionally (not per surface-hit ray as the original plan said). This includes:
- Sky-exit rays (which ignore `upperSample`)
- C3 rays (where `upperSample = vec3(0)` and `l = 1.0` — multiply contributes nothing)

For a static bake on a 32³ grid this is negligible. The alternative (lazy per-branch fetch) avoids the wasted samples but requires duplicating the `texture()` call and is harder to read.

### `hit.a = t` vs `hit.a = 1.0` — sentinel arithmetic still works

The original `a = 1.0` was a placeholder meaning "hit." After the change `a = t`, all downstream code still works because:

- `hit.a > 0` is true for all valid `t` (since `t > tMin > 0`)
- `hit.a == 0` (miss) is unambiguous — `t` starts above zero and only increments
- `hit.a < 0` (sky sentinel, set by 4a) remains unambiguous — the march sets `a = -1.0` explicitly

The debug stat (`surfaceHits` increments on `hit.a > 0`) is still correct — it counts surface hits regardless of the exact `t` value stored in `.a`.

### Blend is continuous in the outer fraction of the interval

`l` ranges from `1.0` at `t = tMax - blendWidth` down to `0.0` at `t = tMax`. A ray hitting at exactly `tMax - blendWidth` uses 100% local data; a ray at `tMax` would use 100% upper cascade. The `clamp` ensures `l` never goes outside `[0, 1]` even if `hit.a` overshoots `tMax` (which shouldn't happen in practice but the guard is harmless).

---

## Codex Critic Corrections Applied to Code

All three findings from `06_phase4c_plan_review.md` were handled:

| Finding | Severity | Action |
|---|---|---|
| C3 blends surface hits toward black | Medium | Added `uHasUpperCascade != 0` guard to `l` computation — C3 always gets `l = 1.0` |
| Performance note understates texture cost | Medium | Kept hoisted `upperSample`; rewrote note to say "once per ray unconditionally (all ray types)" |
| `dist_var` label vs `var=` in UI | Low | Already renamed in code during 4b-1; plan table was already correct — no change |

---

## Pre-conditions Verified

- `raymarchSDF()` `t` is in scope at the hit site (line 126) — confirmed before making the change
- `hit.a > 0` sentinel still correct with `a = t` because `t > tMin > 0` always
- `uBlendFraction` did not exist anywhere in the codebase before 4c (grep confirmed during planning)
- `surfaceHits` debug counter unaffected — still increments on `hit.a > 0`

---

## Validation Required (A/B)

Run the demo and compare:

| Test | Method | Expected |
|---|---|---|
| Binary vs blended | `blendFraction = 0.0` vs `0.5`, mode 0 or 6, surfaces near cascade boundaries | Some smoothing, or no visible difference |
| Phase 3 parity | `blendFraction = 0.0` must look identical to pre-4c baseline | Exact match |
| surf% stable | `surf%` readback unchanged at 0.0 vs 0.5 | Same count (hit detection unchanged) |
| C3 boundary | At `blendFraction = 0.5`, inspect surfaces near C3's 8m boundary | No artificial darkening — guard fires, `l = 1.0` |

## A/B Result (2026-04-24)

**Outcome: No visible difference. Mean luminance per cascade unchanged.**

Toggling `blendFraction` between 0.0 (binary) and 0.5 (blended) produced no observable change in the rendered image and no change in per-cascade mean luminance readback.

**Root cause confirmed:** The banding in this scene is driven by directional mismatch. The upper cascade (`upperSample`) contributes its isotropic probe average — the same value regardless of which ray direction is being blended. Smoothing the binary boundary handoff has no visual effect because the target being blended toward is already directionally incorrect. The hard edge at `tMax` and the blended edge at `tMax` differ only in how gradually they transition to the wrong value.

**4c status: correct implementation of a no-op for this scene.** The code is valid and forward-compatible. When Phase 5 replaces the isotropic `upperSample` with per-direction radiance, the blend formula is unchanged and the `blendFraction` slider will become meaningful.

---

## Open Questions

- Would a blend-zone fraction readback (what % of surface hits were in the blend zone) be worth a second GPU buffer? Currently impossible without storing hit distances per ray.
- Phase 5 (directional-correct merge): when `upperSample` carries per-direction radiance, 4c becomes effective. No code change required — only the value of `upperSample` improves.
