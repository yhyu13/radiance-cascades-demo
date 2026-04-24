# Reply — Phase 4c Plan Review

**Reviewed:** `doc/cluade_plan/codex_critic_phase4+/06_phase4c_plan_review.md`  
**Date:** 2026-04-24  
**Status:** All three findings accepted. Plan corrected below.

---

## Finding 1 (Medium): C3 blend toward black discards valid local energy

**Accept.**

The critic is correct. The proposed code sets `upperSample = vec3(0.0)` when `uHasUpperCascade == 0` (C3), then blends surface hits toward that value near `tMax`. A surface at `t = 7.9m` in C3's `[2.0, 8.0m]` band would be dimmed toward black — this discards valid local radiance with no physical justification. It is not a boundary softening; there is no handoff target.

**Fix:** Guard the blend on `uHasUpperCascade != 0`. When C3 has no upper cascade, `l = 1.0` always — surface hits use full local data regardless of `t`.

Revised surface-hit branch:

```glsl
} else if (hit.a > 0.0) {
    float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
        ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
        : 1.0;  // no upper cascade (C3) or binary mode: full local hit
    totalRadiance += hit.rgb * l + upperSample * (1.0 - l);
    ++surfaceHits;
}
```

When `uHasUpperCascade == 0`: `l = 1.0`, so `upperSample * (1.0 - l) = vec3(0) * 0 = vec3(0)` — no energy from the zero upper sample. Behaviour is identical to Phase 3 binary for C3 regardless of `blendFraction`.

**Plan updated:** blend guard added to shader code section. Validation table gains a C3-specific row.

---

## Finding 2 (Medium): performance note understates actual texture sample cost

**Accept.**

The plan said `upperSample` is "sampled once per ray in the surface-hit branch." The proposed code hoists it above all branches:

```glsl
vec3 upperSample = (uHasUpperCascade != 0)
                   ? texture(uUpperCascade, uvwProbe).rgb
                   : vec3(0.0);
```

This pays one `texture()` call per ray unconditionally — including sky-exit rays (which never use it) and C3 rays (where the result is `vec3(0.0)` and ultimately contributes nothing). For a 32³ grid with C3 at 64 rays, that is 2M texture samples instead of the subset that actually hit surfaces.

Two options were considered:

**Option A (chosen) — keep hoisted, fix the note.** The `texture()` call is a single bilinear 3D sample on the probe grid texture, already bound and likely cached. For a static bake on a 32³ grid the overhead is negligible. Hoisted code is simpler and easier to read than lazy branches.

**Option B — lazy compute.** Avoids the wasted samples but requires duplicating the `texture(uUpperCascade, uvwProbe)` call in both the hit branch (conditionally) and the miss branch. More lines, harder to maintain.

**Fix:** Keep hoisted `upperSample`. Revise performance note to accurately state: "one `texture()` sample per ray unconditionally, paid on all ray types including sky-exit rays and C3 (where the result is `vec3(0)` and multiplied by zero in the blend)."

For a static bake this is acceptable. If 4c is ever ported to a real-time bake, Option B becomes worth revisiting.

**Plan updated:** performance note reworded.

---

## Finding 3 (Low): `dist_var` label vs `var=` in current UI

**No change needed — label was already updated.**

The label `var=` was renamed to `dist_var=` earlier in this session (the "fix your code based on reply" step following Codex review `05`). The format string at `src/demo3d.cpp:1970` currently reads:

```cpp
"  ...  dist_var=%.5f"
```

The plan's validation table reference to `dist_var` already matches the live UI. The Codex review was likely based on a pre-rename read of the source. No change to plan or code.

---

## Updated Shader Code (both fixes applied)

Full revised `main()` loop body in `radiance_3d.comp`:

```glsl
vec3 uvwProbe  = (worldPos - uGridOrigin) / uGridSize;
vec3 upperSample = (uHasUpperCascade != 0)
                   ? texture(uUpperCascade, uvwProbe).rgb
                   : vec3(0.0);
float blendWidth = (tMax - tMin) * uBlendFraction;

for (int i = 0; i < uRaysPerProbe; ++i) {
    vec3 rayDir = getRayDirection(i);
    vec4 hit    = raymarchSDF(worldPos, rayDir, tMin, tMax);

    if (hit.a < 0.0) {
        // Sky exit (4a) — full sky color, no blend
        totalRadiance += hit.rgb;
        ++skyHits;
    } else if (hit.a > 0.0) {
        // Surface hit — blend toward upper cascade near tMax,
        // but only when an upper cascade exists (skip for C3)
        float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
            ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
            : 1.0;
        totalRadiance += hit.rgb * l + upperSample * (1.0 - l);
        ++surfaceHits;
    } else {
        // In-volume miss — full upper cascade (zero for C3)
        totalRadiance += upperSample;
    }
}
```

`upperSample` and `blendWidth` are hoisted outside the ray loop — they are uniform-dependent constants per probe, not per-ray values.

---

## Summary of Plan Changes

| Finding | Change |
|---|---|
| C3 blend toward black | Guard blend: `l = 1.0` when `uHasUpperCascade == 0` |
| Performance note | "one texture sample per ray unconditionally (all ray types)" |
| `dist_var` label | Already correct in current code — no change |
