# ShaderToy2 Phase 2B-5 Plan — Shadowed Direct-Light Debug, No Feedback

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Add binary shadow-to-light diagnostics for classified room-plane hits only. Unknown/box hits are skipped. No feedback, no accumulation, no final GI lookup.

---

## 1. Goal

Extend Phase 2B-4 unshadowed direct-light debug with a binary SDF visibility trace to the current point light:

```text
classified hit point + light direction
  -> binary shadow trace to uLightPos
  -> shadow visibility mode
  -> shadowed direct-light debug mode
```

This is still diagnostic NEE only. It does not write persistent radiance or feed the final renderer.

---

## 2. Non-Goals

Do **not** implement:

```text
- persistent ping-pong feedback
- previous-frame atlas sampling
- final surface GI lookup
- upper cascade merge
- box/object charts
- production NEE claims
- EXR/PT quality metrics
```

Unknown hits remain skipped and visible as yellow diagnostics.

---

## 3. Implementation Plan

### 3.1 Add binary shadow helper

In `surface_radiance_debug.comp`:

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

Offset along `lightDir`, not normal, to avoid UDF normal ambiguity.

### 3.2 Add modes

Current modes end at 12. Add:

```text
13 shadow visibility
14 shadowed direct
```

Mode 13:

```text
classified visible: green
classified blocked: red
unknown hit: yellow
miss/escape: blue/black diagnostic
inactive: black
```

Mode 14:

```text
classified hit: direct * visibility, visualized as x/(x+1)
unknown hit: yellow
miss/escape: black
inactive: black
```

### 3.3 C++ updates

Update:

```text
SurfaceRC::setRadianceDebugMode clamp 0..14
SurfaceRC::radianceDebugModeName
ImGui radianceModes[]
```

No CLI change needed; numeric mode already works.

### 3.4 Verification

Build:

```powershell
cmake --build build --config Debug
```

Captures with safer bias:

```text
tools/phase2b5_visual/m13_shadow_visibility.png
tools/phase2b5_visual/m14_shadowed_direct.png
```

Commands use:

```text
--surface-ray-bias=0.02
```

Structure checks:

```text
Chart 6 inactive remains dark.
Mode 13 has classified-visible and/or blocked pixels; not all yellow/all black.
Mode 14 has nonzero direct pixels and fewer/equal bright pixels than unshadowed mode 10.
Unknown hits remain visible as yellow and skipped from direct.
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if shadow visibility is all blocked or all visible unexpectedly, stop and diagnose before feedback
```

---

## 5. Self-Critique of This Plan

### SC1 — Binary shadow over UDF may over-block

Accepted. Conservative UDF plus fixed epsilon may report blockers too aggressively.

Improvement: Keep this as binary debug only; do not use for production visibility without bias/epsilon analysis.

### SC2 — Light source geometry can be confused with blockers

Accepted. Cornell OBJ contains area-light geometry, while this phase uses a point light. If the ray reaches light/ceiling region, UDF may classify geometry near the point as blocking.

Improvement: Stop shadow trace at `lightDist - 2*bias` and document artifacts.

### SC3 — Unknown/box hits remain skipped

Accepted. This keeps diagnostics honest but means box direct-light contribution is absent.

### SC4 — Shadowed direct is still not feedback-ready

Accepted. This phase only verifies a debug visibility path. Feedback remains blocked by box/unknown handling and production angular sampling.

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Add shadowVisibility().
2. Add modes 13/14.
3. Update C++ labels/clamp.
4. Build and capture m13/m14 at bias 0.02.
5. Run structure checks against m10/m12 baselines.
6. Document implementation + self-critique.
```

Stop before feedback or accumulation.
