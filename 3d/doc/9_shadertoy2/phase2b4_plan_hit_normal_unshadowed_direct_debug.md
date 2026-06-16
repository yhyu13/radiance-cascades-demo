# ShaderToy2 Phase 2B-4 Plan — Hit Normal + Unshadowed Direct-Light Debug, No Feedback

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Constrained debug-only NEE precursor. Estimate hit normal and evaluate unshadowed point-light direct term for classified room-plane hits only. Unknown/box hits are skipped. No shadows, no feedback, no accumulation.

---

## 1. Context / Gate From Critique 05

Critique 05 allowed Phase 2B-4 only under a constrained scope because:

```text
UV round-trip: PASS
Unknown/box-hit spatial alignment: only partially characterized
Feedback readiness: NO
Unshadowed direct-light debug readiness: YES, with unknown hits skipped
```

Operational rule:

```text
Use --surface-ray-bias=0.02 for captures.
```

This phase is **not** production NEE. It is a diagnostic to see whether classified room-plane hit points can produce plausible point-light direct values.

---

## 2. Goal

Add two/four debug-only outputs to `surface_radiance_debug.comp`:

```text
9  hit normal debug
10 unshadowed direct-light NEE debug
11 NdotL debug
12 unknown/skip mask debug
```

For traced hits:

```text
if hit is classified as active room chart:
    estimate normal
    compute unshadowed direct term from point light
else:
    skip / black or diagnostic color
```

---

## 3. Non-Goals

Do **not** implement:

```text
- shadow ray to light
- cone soft shadow
- albedo/modulated radiance accumulation
- previous-frame atlas sampling
- persistent ping-pong feedback
- upper cascade merge
- final raymarch surface GI lookup
- production NEE claims
- box chart support
```

Unknown hits must not be treated as valid room-plane hits.

---

## 4. Implementation Plan

### 4.1 Extend uniforms

Add to `surface_radiance_debug.comp`:

```glsl
uniform vec3 uLightPos;
uniform vec3 uLightColor;
```

Update `SurfaceRC::dispatchRadianceDebug()` signature:

```cpp
dispatchRadianceDebug(program, sdfTexture, gridOrigin, gridSize, lightPos, lightColor)
```

`Demo3D` passes current Cornell point-light position and scaled light color.

### 4.2 Add normal estimation helper

Use central differences over existing `sampleSDF()`:

```glsl
vec3 estimateNormal(vec3 p) {
    float e = max(0.01, uHitEpsilon * 4.0);
    vec3 g = vec3(
      sampleSDF(p + vec3(e,0,0)) - sampleSDF(p - vec3(e,0,0)),
      sampleSDF(p + vec3(0,e,0)) - sampleSDF(p - vec3(0,e,0)),
      sampleSDF(p + vec3(0,0,e)) - sampleSDF(p - vec3(0,0,e))
    );
    return normalize(g);
}
```

Caveat: existing field is conservative/UDF-like, so normal is debug-quality only.

### 4.3 Direct-light term

For classified room-plane hits only:

```glsl
vec3 L = uLightPos - hitPos;
float dist2 = max(dot(L, L), 1e-4);
vec3 lightDir = normalize(L);
float ndotl = max(dot(hitNormal, lightDir), 0.0);
vec3 direct = uLightColor * ndotl / dist2;
```

No albedo yet unless needed for debug later. No shadowing.

### 4.4 Debug modes

Add radiance debug modes:

```text
9  Hit normal              rgb = normal * 0.5 + 0.5
10 Unshadowed direct       rgb = tonemap/debug-scaled direct
11 NdotL                   grayscale ndotl
12 Skip/unknown mask        classified=green, unknown=yellow, miss=red, escape=blue, inactive=black
```

Mode 10 needs a stable visualization scale:

```glsl
rgb = direct / (direct + vec3(1.0))
```

This avoids choosing arbitrary exposure for point-light inverse-square spikes.

### 4.5 C++ labels / clamp

Update:

```text
SurfaceRC::setRadianceDebugMode clamp 0..12
SurfaceRC::radianceDebugModeName
ImGui radianceModes[]
```

CLI numeric mode already works.

### 4.6 Verification

Build:

```powershell
cmake --build build --config Debug
```

Captures with bias 0.02:

```text
tools/phase2b4_visual/normal_m9.png
tools/phase2b4_visual/direct_m10.png
tools/phase2b4_visual/ndotl_m11.png
tools/phase2b4_visual/mask_m12.png
```

Structure checks:

```text
Chart 6 inactive stays dark.
Mode 9 has multiple normal colors but no random noise-only pattern.
Mode 10 has nonzero direct pixels among classified hits.
Mode 11 has nonzero NdotL pixels and not all white/all black.
Mode 12 reports classified/unknown/miss/escape counts; unknown count remains documented.
```

---

## 5. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if direct mode is all black/all white or unknown dominates, stop before adding shadowing/feedback
```

---

## 6. Self-Critique of This Plan

### SC1 — SDF-gradient normals on UDF may be unreliable

Accepted. Conservative UDF gradients near corners/box geometry may be noisy.

Improvement:

```text
Use normals only for debug, not production lighting. Compare room-plane classified normal against known chart normal in a future diagnostic if needed.
```

### SC2 — Unshadowed direct is not physically complete

Accepted. Without shadowing, light can illuminate through geometry.

Improvement:

```text
Label mode 10 explicitly as unshadowed direct. Do not compare to PT or final visual quality.
```

### SC3 — Unknown/box hits are skipped

Accepted. This means box surfaces receive no debug direct value.

Improvement:

```text
Mode 12 must quantify skipped unknowns so this limitation stays visible.
```

### SC4 — Inverse-square direct can spike near light

Accepted. Point light direct can be high near ceiling/light.

Improvement:

```text
Use direct/(direct+1) visualization rather than raw direct. Keep raw values for later readback if needed.
```

### SC5 — Light position/color must match existing Demo3D conventions

Accepted. Use same `lightPosition` and `vec3(1.0,0.95,0.85)*lightIntensity` style used by raymarch pass.

---

## 7. Improved Final Plan

Implement exactly:

```text
1. Add light uniforms to surface_radiance_debug.comp.
2. Pass light position/color from Demo3D into SurfaceRC dispatch.
3. Add normal/direct/NdotL/skip-mask modes 9..12.
4. Update C++ clamp and UI labels.
5. Build and capture modes 9..12 with --surface-ray-bias=0.02.
6. Run structure checks and document classified/unknown/miss/escape counts.
7. Stop before shadows, feedback, or radiance accumulation.
```
