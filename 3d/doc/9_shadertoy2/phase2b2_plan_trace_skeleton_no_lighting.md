# ShaderToy2 Phase 2B-2 Plan — Surface Trace Skeleton, No Lighting

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Add hit/miss tracing diagnostics to the surface radiance skeleton path. No lighting, no point-light NEE, no feedback, no final GI lookup.

---

## 1. Goal

Extend the Phase 2B-1 radiance skeleton from geometry-only output to **ray hit/miss classification**:

```text
rayOrigin + worldDir + existing SDF texture
  -> short sphere trace
  -> output hit/miss/escaped/inactive debug color
```

This verifies that the surface-attached probe rays can actually intersect the existing scene representation before adding any light transport.

---

## 2. Non-Goals

Do **not** implement:

```text
- point-light NEE
- direct lighting at hit point
- albedo sampling for radiance
- hit normal estimation except optional debug-only if cheap
- persistent ping-pong feedback
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT quality metrics
```

Reason:

```text
If hit/miss classification is wrong, NEE and feedback would only amplify a broken geometric path.
```

---

## 3. Implementation Plan

### 3.1 Extend `surface_radiance_debug.comp`

Add uniforms:

```glsl
uniform sampler3D uSDF;
uniform vec3 uGridOrigin;
uniform vec3 uGridSize;
uniform int uTraceSteps;
uniform float uTraceMaxDist;
uniform float uHitEpsilon;
```

Add helper copied from existing shader convention:

```glsl
float sampleSDF(vec3 worldPos) {
    vec3 uvw = (worldPos - uGridOrigin) / uGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return INF;
    return texture(uSDF, uvw).r;
}
```

Add simple trace:

```glsl
TraceResult traceSDF(vec3 origin, vec3 dir) {
    float t = uRayBias * 2.0;
    for i in 0..uTraceSteps:
        pos = origin + dir * t
        d = sampleSDF(pos)
        if d >= INF*0.5: escaped
        if d < uHitEpsilon: hit
        t += max(d, uHitEpsilon)
        if t > uTraceMaxDist: miss
}
```

### 3.2 Add radiance debug modes

Current modes:

```text
0 ray origin
1 hemisphere direction
2 normal
3 active/chart mask
```

Add:

```text
4 trace hit/miss classification
5 normalized hit distance
```

Mode 4 color convention:

```text
inactive/invalid: black, alpha 0
hit:              green, alpha normalized hit distance
escaped volume:   blue, alpha 1
max-distance miss:red, alpha 1
```

Mode 5:

```text
grayscale = clamp(hitDistance / uTraceMaxDist, 0, 1)
alpha = 1 for hit, 0 for inactive, 1 for miss/escape if useful
```

### 3.3 Bind SDF texture in C++

Extend:

```cpp
SurfaceRC::dispatchRadianceDebug(...)
```

or add parameters to it:

```cpp
dispatchRadianceDebug(GLuint computeProgram,
                      GLuint sdfTexture,
                      glm::vec3 gridOrigin,
                      glm::vec3 gridSize)
```

Bind:

```cpp
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_3D, sdfTexture);
glUniform1i(uSDF, 0);
glUniform3fv(uGridOrigin, ...);
glUniform3fv(uGridSize, ...);
```

Trace defaults:

```text
uTraceSteps = 96
uTraceMaxDist = length(gridSize)
uHitEpsilon = 0.002
```

These mirror existing `radiance_3d.comp` conventions where possible.

### 3.4 CLI/UI

Update radiance debug mode clamp and labels to 0..5.

No new CLI needed:

```text
--surface-radiance-debug-mode=4
--surface-radiance-debug-mode=5
```

### 3.5 Verification

Build:

```powershell
cmake --build build --config Debug
```

Smoke/captures:

```text
tools/phase2b2_visual/trace_m4_classification.png
tools/phase2b2_visual/trace_m5_distance.png
```

Structure checks:

```text
inactive chart 6 remains dark
mode 4 has nonzero green hit pixels in active charts
mode 4 has at least one non-hit class if present; if all active rays hit, document why
mode 5 has nontrivial active-region variation
```

Optional numeric summary using `System.Drawing`:

```text
activeUnique4
inactiveBright
hitGreenCount
missRedCount
escapeBlueCount
distanceUnique4
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if trace mode still produces all misses/all hits unexpectedly, stop and diagnose before adding lighting
```

---

## 5. Self-Critique of This Plan

### SC1 — Existing SDF is a conservative UDF, not signed SDF

Accepted. The mesh path writes a conservative unsigned distance field, so sphere tracing from surface-biased origins may behave differently than a true signed field.

Improvement:

```text
Use a positive normal bias and start t at 2*bias to avoid immediate self-hit.
Treat this as hit/miss classification only, not physically correct visibility yet.
```

### SC2 — Surface probes start on surfaces, so self-hit is likely

Accepted. This is the most likely failure source.

Improvement:

```text
Expose uRayBias and start trace t above the bias. If everything self-hits, increase bias diagnostically before changing topology.
```

### SC3 — Debug hemisphere direction is still not final ShaderToy angular distribution

Accepted. The trace classification validates data path and TBN orientation, not final angular sampling distribution.

Improvement:

```text
Keep this caveat in implementation doc. Do not tune radiance quality from this mapping.
```

### SC4 — Mode 4 could show all hits in a closed room and still be valid

Accepted. In Cornell, many hemisphere rays from surfaces should hit other surfaces. The pass/fail is not simply "mixed colors".

Improvement:

```text
Require nonzero hits and nontrivial distance variation; do not require red/blue misses if scene geometry naturally contains rays.
```

### SC5 — Binding SDF texture into SurfaceRC increases coupling to Demo3D

Accepted. This is necessary for trace diagnostics.

Improvement:

```text
Pass only texture handle and grid bounds; keep scene-specific logic in SurfaceRC shader path minimal.
```

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Add trace uniforms and SDF sampler to surface_radiance_debug.comp.
2. Add modes 4/5 for classification and distance.
3. Pass sdfTexture + volumeOrigin + volumeSize into SurfaceRC::dispatchRadianceDebug.
4. Update CLI/UI clamp and labels.
5. Build + capture modes 4/5.
6. Run structure checks.
7. Document implementation + self-critique.
```

Stop before lighting, NEE, feedback, or final surface GI consumption.
