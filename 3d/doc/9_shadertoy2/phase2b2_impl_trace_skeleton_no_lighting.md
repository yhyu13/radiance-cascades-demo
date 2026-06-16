# ShaderToy2 Phase 2B-2 Implementation — Surface Trace Skeleton, No Lighting

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Surface probe SDF hit/miss diagnostics only. No lighting, no point-light NEE, no feedback, no final GI lookup.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b2_plan_trace_skeleton_no_lighting.md
```

The phase extends the Phase 2B-1 radiance skeleton path with trace diagnostics:

```text
rayOrigin + worldDir + existing sdfTexture
  -> short SDF/UDF trace
  -> classification / distance debug output
```

---

## 2. C++ Changes

Updated:

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
```

### Dispatch signature

`SurfaceRC::dispatchRadianceDebug()` now takes the existing SDF texture and volume bounds:

```cpp
void dispatchRadianceDebug(GLuint computeProgram,
                           GLuint sdfTexture,
                           const glm::vec3& gridOrigin,
                           const glm::vec3& gridSize);
```

`Demo3D` calls it as:

```cpp
surfaceRC->dispatchRadianceDebug(radit->second, sdfTexture, volumeOrigin, volumeSize);
```

### Uploaded uniforms

New uniforms:

```text
uSDF sampler3D on texture unit 0
uGridOrigin
uGridSize
uTraceSteps = 96
uTraceMaxDist = length(gridSize)
uHitEpsilon = 0.002
```

Radiance debug modes now clamp to `0..5`.

UI labels now include:

```text
Trace Classification
Trace Distance
```

---

## 3. Shader Changes

Updated:

```text
res/shaders/surface_radiance_debug.comp
```

### New uniforms

```glsl
uniform sampler3D uSDF;
uniform vec3 uGridOrigin;
uniform vec3 uGridSize;
uniform int uTraceSteps;
uniform float uTraceMaxDist;
uniform float uHitEpsilon;
```

### SDF sampling

Uses the same convention as `radiance_3d.comp`:

```glsl
float sampleSDF(vec3 worldPos) {
    vec3 uvw = (worldPos - uGridOrigin) / uGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return INF;
    return texture(uSDF, uvw).r;
}
```

### Trace function

```glsl
TraceResult traceSDF(vec3 origin, vec3 dir) {
    float t = max(uRayBias * 2.0, uHitEpsilon * 2.0);
    for (int i = 0; i < uTraceSteps && t < uTraceMaxDist; ++i) {
        vec3 pos = origin + dir * t;
        float d = sampleSDF(pos);
        if (d >= INF * 0.5) return TraceResult(2, t); // escaped volume
        if (d < uHitEpsilon) return TraceResult(1, t); // hit
        t += max(d, uHitEpsilon);
    }
    return TraceResult(0, min(t, uTraceMaxDist)); // max-distance miss
}
```

### New modes

```text
4 trace classification
5 normalized trace distance
```

Mode 4 color convention:

```text
hit:            green
escaped volume: blue
max miss:       red
inactive:       black / alpha 0
```

Mode 5:

```text
grayscale = trace distance / max trace distance
alpha = max(distanceNorm, 0.001) for hits, 1 for miss/escape, 0 for inactive
```

The small hit alpha floor was added during self-critique so near hits do not become invisible in display paths that multiply by alpha.

---

## 4. Verification

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

Captured:

```text
tools/phase2b2_visual/trace_m4.png
tools/phase2b2_visual/trace_m5.png
```

Commands:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=4 --screenshot=tools/phase2b2_visual/trace_m4.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=5 --screenshot=tools/phase2b2_visual/trace_m5.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

A `System.Drawing` check split the visible overlay into active and inactive chart regions:

```text
active region:   x 0..335  (charts 1..5)
inactive region: x 336..383 (chart 6/front)
overlay y range: 193..767
```

Result:

```text
trace_m4.png activeUnique4=3  inactiveUnique4=2 inactiveBright=0 hit=11264 miss=18 escape=814 activeSamples=12096
trace_m5.png activeUnique4=14 inactiveUnique4=2 inactiveBright=0 hit=0     miss=0  escape=0   activeSamples=12096
```

Interpretation:

```text
Chart 6 remains inactive: inactiveBright=0.
Mode 4 shows substantial green hit population: hit=11264.
Mode 4 also shows non-hit classes: miss=18, escape=814.
Mode 5 has nontrivial distance variation: activeUnique4=14.
```

This verifies the trace skeleton is not all-black, not all-inactive, and not a single-class output.

---

## 5. Self-Critique

### SC1 — The trace uses a conservative UDF, not a true signed SDF

Accepted.

The existing mesh SDF path is documented as conservative/UDF-like. This means the trace is useful for hit/miss diagnostics, but not yet a physically exact visibility test.

Implication:

```text
Do not tune lighting or energy from this trace yet.
Use it only to verify surface-probe rays can intersect scene geometry.
```

### SC2 — Self-hit remains the most likely hidden issue

Accepted.

Surface probes start on surface planes. The implementation uses:

```text
rayOrigin = probeWorldPos + normal * 0.01
t start = max(2*bias, 2*epsilon)
```

This reduces immediate self-hit, but does not prove all self-hit artifacts are gone.

Future requirement:

```text
Before NEE/feedback, add a bias sweep or sampled readback for representative atlas texels.
```

### SC3 — Mode 5 hit alpha could be too low for near hits

Accepted and fixed.

Initial mode 5 used:

```glsl
alpha = dNorm for hits
```

Near hits can have very small `dNorm`, causing them to disappear if the display path uses alpha. Fixed to:

```glsl
alpha = max(dNorm, 0.001) for hits
```

### SC4 — Trace classification is not semantic surface identity

Accepted.

Mode 4 says hit/miss/escape. It does not classify which wall/object was hit. That is enough for this phase, but not enough for surface feedback, which will need hit chart/UV lookup.

### SC5 — Direction distribution remains debug-only

Accepted.

This phase still uses the debug hemisphere mapping from Phase 2B-0/2B-1. It validates tracing through the data path, not final ShaderToy angular sampling quality.

---

## 6. Improvements Applied After Self-Critique

1. Added hit alpha floor for mode 5:

```glsl
a = (tr.state == 1) ? max(dNorm, 0.001) : 1.0;
```

2. Rebuilt successfully after the fix.
3. Verified trace captures with structure-aware checks.

---

## 6.1 Critique 04 Follow-up — Bias Sweep

Critique 04 (`doc/9_shadertoy2/critic/04_critique_phase2b2_trace_skeleton.md`) correctly identified self-hit/bias sensitivity as a blocker before trusting hit classification. The follow-up reply is documented in:

```text
doc/9_shadertoy2/critic/reply/04_reply_to_phase2b2_trace_skeleton_critique.md
```

Implemented diagnostic controls:

```text
--surface-ray-bias=<float>
SurfaceRC::setRayBias/getRayBias
Surface ray bias UI slider
```

Bias sweep summary:

```text
bias 0.005: hits=11815 miss=14  escape=267
bias 0.010: hits=11264 miss=18  escape=814
bias 0.015: hits=10310 miss=616 escape=1170
bias 0.020: hits=10077 miss=573 escape=1446
bias 0.030: hits=9708  miss=782 escape=1606
bias 0.050: hits=9633  miss=628 escape=1835
```

Key result:

```text
0.01 -> 0.03 hit drop = 13.81%
```

Decision:

```text
Proceed with caution, not a hard stop.
Use --surface-ray-bias=0.02 for Phase 2B-3/2B-4 diagnostic captures unless explicitly running a bias test.
```

---

## 7. Current Limitations

Still not implemented:

```text
- hit chart/UV classification
- hit normal estimation
- hit albedo sampling
- point-light NEE
- direct lighting at hit point
- persistent ping-pong feedback
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT metrics
```

Known debug limitations:

```text
- Uses conservative UDF semantics.
- Bias is fixed at 0.01.
- Direction mapping is debug-oriented, not final ShaderToy angular mapping.
- Trace result does not identify which surface was hit.
```

---

## 8. Next Implementation Slice

Proceed to **Phase 2B-3: Hit Surface Classification, No Lighting**.

Recommended scope:

```text
1. At trace hit point, classify hit chart/UV analytically for Cornell planes.
2. Output hit chart ID color and hit UV gradient.
3. Keep Chart 6 inactive.
4. No NEE, no direct lighting, no feedback.
5. Verify that floor rays hit walls/ceiling/back/etc. with plausible chart colors.
```

Do not add point-light NEE until hit chart/UV classification is sane.

---

## 9. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2b2_plan_trace_skeleton_no_lighting.md
doc/9_shadertoy2/phase2b2_impl_trace_skeleton_no_lighting.md
tools/phase2b2_visual/trace_m4.png
tools/phase2b2_visual/trace_m5.png
```
