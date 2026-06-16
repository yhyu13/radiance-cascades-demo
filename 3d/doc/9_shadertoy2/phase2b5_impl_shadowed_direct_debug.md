# ShaderToy2 Phase 2B-5 Implementation — Shadowed Direct-Light Debug, No Feedback

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Binary shadow-to-light diagnostics and shadowed direct visualization for classified room-plane hits only. Unknown hits skipped. No feedback, no accumulation, no final GI lookup.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b5_plan_shadowed_direct_debug.md
```

The phase extends Phase 2B-4's unshadowed direct debug:

```text
classified hit point + light direction
  -> binary SDF shadow trace toward uLightPos
  -> shadow visibility debug
  -> shadowed direct debug
```

This remains diagnostic-only NEE. It does not feed a persistent atlas or final renderer.

---

## 2. C++ Changes

Updated:

```text
src/surface_rc.cpp
src/demo3d.cpp
```

Radiance debug mode clamp now supports:

```text
0..14
```

New labels:

```text
13 shadow visibility
14 shadowed direct
```

ImGui radiance mode list now includes:

```text
Shadow Visibility
Shadowed Direct
```

No CLI change was needed because numeric mode selection already works:

```text
--surface-radiance-debug-mode=13
--surface-radiance-debug-mode=14
```

---

## 3. Shader Changes

Updated:

```text
res/shaders/surface_radiance_debug.comp
```

### Binary shadow helper

Added:

```glsl
float shadowVisibility(vec3 hitPos, vec3 lightDir, float lightDist) {
    float t = max(uRayBias * 2.0, uHitEpsilon * 2.0);
    float tMax = max(lightDist - uRayBias * 2.0, t);
    for (int i = 0; i < uTraceSteps && t < tMax; ++i) {
        vec3 p = hitPos + lightDir * t;
        float d = sampleSDF(p);
        if (d >= INF * TRACE_ESCAPE_INF_FRACTION) return 1.0;
        if (d < uHitEpsilon) return 0.0;
        t += max(d, uHitEpsilon);
    }
    return 1.0;
}
```

This is a binary visibility diagnostic:

```text
1 = visible to point light
0 = blocked by SDF/UDF
```

### Direct computation update

For modes `>= 13`, visibility is computed after the existing unshadowed direct term:

```glsl
if (uDebugMode >= 13)
    visibility = shadowVisibility(tr.pos, lightDir, sqrt(dist2));
```

Mode 14 uses:

```glsl
vec3 shadowedDirect = direct * visibility;
rgb = shadowedDirect / (shadowedDirect + vec3(1.0));
```

---

## 4. New Debug Modes

### Mode 13 — Shadow Visibility

```text
classified visible: green
classified blocked: red
unknown hit:        yellow
escape:             blue
miss:               dim red
inactive:           black
```

### Mode 14 — Shadowed Direct

```text
classified hit: shadowedDirect / (shadowedDirect + 1)
unknown hit:    yellow
miss/escape:    black
inactive:       black
```

---

## 5. Verification

### Build

Command:

```powershell
cmake --build build --config Debug
```

Result:

```text
RadianceCascades3D.exe built successfully
```

Warnings are pre-existing MSVC warnings.

### Captures

Captured with:

```text
--surface-ray-bias=0.02
```

Files:

```text
tools/phase2b5_visual/m13.png
tools/phase2b5_visual/m14.png
```

Commands:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=13 --surface-ray-bias=0.02 --screenshot=tools/phase2b5_visual/m13.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=14 --surface-ray-bias=0.02 --screenshot=tools/phase2b5_visual/m14.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

Baseline from Phase 2B-4 unshadowed direct:

```text
m10.png activeUnique4=44 nonzero=9796 bright=6563 green=0 yellow=2371 red=0 blue=0 inactiveBright=0 activeSamples=12096
```

Phase 2B-5 results:

```text
m13.png activeUnique4=5  nonzero=12096 bright=11523 green=5585 yellow=2371 red=2121 blue=1446 inactiveBright=0 activeSamples=12096
m14.png activeUnique4=42 nonzero=7940  bright=6164  green=0    yellow=2371 red=0    blue=0    inactiveBright=0 activeSamples=12096
```

Interpretation:

```text
Chart 6 remains inactive: inactiveBright=0.
Mode 13 has both visible and blocked classified hits: green=5585, red=2121.
Mode 13 preserves unknown/miss/escape diagnostics: yellow=2371, blue=1446.
Mode 14 has fewer nonzero pixels than unshadowed mode 10: 7940 vs 9796.
Mode 14 has fewer bright pixels than unshadowed mode 10: 6164 vs 6563.
```

This shows the binary shadow trace is actively suppressing some direct-light contribution.

---

## 6. Self-Critique

### SC1 — Binary shadow over conservative UDF may over-block

Accepted.

The shadow helper uses the same UDF-like field as trace classification. It can over-block near surfaces, around the area-light geometry, or when the SDF/UDF is conservative.

### SC2 — Point-light vs Cornell area-light mismatch remains

Accepted.

The scene has area-light geometry, but this debug mode uses the existing point-light convention. The shadow ray may interact with the ceiling/light geometry differently from a physically correct area-light NEE.

### SC3 — Unknown/box hits remain skipped

Accepted.

Mode 13/14 continue to show `yellow=2371`. These hits are not assigned direct light. This is intentional but means box surfaces still receive no direct/indirect contribution in the surface path.

### SC4 — Shadowed direct is still not feedback-ready

Accepted.

This phase only verifies binary visibility behavior. It does not justify persistent feedback because unknown/box handling and final angular mapping are still unresolved.

### SC5 — Shadow bias/epsilon sensitivity is not swept yet

Accepted.

The capture uses `--surface-ray-bias=0.02`, but there is no shadow-bias-specific sweep. If shadowed direct becomes unexpectedly dark, a shadow-bias sweep is required before tuning lighting.

---

## 7. Improvements Applied After Self-Critique

No code changes were applied after self-critique because the constrained debug gates passed:

```text
- both visible and blocked classified pixels exist
- shadowed direct is not all black/all white
- shadowed direct suppresses some unshadowed direct contribution
- Chart 6 remains inactive
- unknown hits remain explicit
```

The next improvement should be architectural: decide how to handle unknown/box hits before feedback.

---

## 8. Current Limitations

Still not implemented:

```text
- production NEE
- soft/cone shadows
- albedo-modulated direct radiance
- previous-frame atlas sampling
- persistent ping-pong feedback
- box/object charts
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT metrics
```

Known debug limitations:

```text
- Binary shadow may over-block due to UDF semantics.
- Unknown/box hits are skipped.
- Light is point-light debug, not physical area-light sampling.
- No shadow-bias sweep yet.
- No raw direct/visibility readback yet.
```

---

## 9. Next Implementation Decision

At this point, continuing incremental lighting without addressing unknown/box hits risks building a room-only surface RC path that fails on Cornell boxes.

Recommended next step is **not feedback yet**.

Choose one:

```text
Option A — Add basic box charts / object hit support
  Recommended if Cornell boxes matter for the target image.
  This unblocks more complete NEE and later feedback.

Option B — Explicit room-only direct radiance path
  Accept unknown/box hits as skipped.
  Document that box surfaces receive no surface RC radiance.
  Then proceed to a room-only single-frame direct atlas mode.
```

Do not implement persistent feedback until one of these choices is made and documented.

---

## 10. Files Changed In This Phase

```text
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2b5_plan_shadowed_direct_debug.md
doc/9_shadertoy2/phase2b5_impl_shadowed_direct_debug.md
tools/phase2b5_visual/m13.png
tools/phase2b5_visual/m14.png
```
