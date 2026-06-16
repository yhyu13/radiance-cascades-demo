# ShaderToy2 Phase 2B-4 Implementation — Hit Normal + Unshadowed Direct-Light Debug, No Feedback

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Constrained debug-only NEE precursor. Classified room-plane hits only. Unknown hits skipped. No shadows, no feedback, no accumulation.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b4_plan_hit_normal_unshadowed_direct_debug.md
```

This phase follows Critique 05's constrained allowance:

```text
YES: hit normal debug, unshadowed direct-light debug, unknown hits skipped
NO: feedback, production NEE, treating unknown/box hits as valid room hits
```

All captures use:

```text
--surface-ray-bias=0.02
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

`SurfaceRC::dispatchRadianceDebug()` now accepts light data:

```cpp
void dispatchRadianceDebug(GLuint computeProgram,
                           GLuint sdfTexture,
                           const glm::vec3& gridOrigin,
                           const glm::vec3& gridSize,
                           const glm::vec3& lightPos,
                           const glm::vec3& lightColor);
```

`Demo3D` passes the same light convention used elsewhere:

```cpp
glm::vec3 surfaceLightPos = lightPosition;
if (useDirectionalLight) {
    glm::vec3 volCenter = volumeOrigin + 0.5f * volumeSize;
    surfaceLightPos = volCenter - glm::normalize(lightDirection) * 100.0f;
}
glm::vec3 surfaceLightColor = glm::vec3(1.0f, 0.95f, 0.85f) * lightIntensity;
```

### New radiance debug labels

Radiance debug mode clamp now supports `0..12`.

New labels:

```text
9  hit normal
10 unshadowed direct
11 ndotl
12 skip mask
```

---

## 3. Shader Changes

Updated:

```text
res/shaders/surface_radiance_debug.comp
```

### New uniforms

```glsl
uniform vec3 uLightPos;
uniform vec3 uLightColor;
```

### Normal estimation

Added:

```glsl
vec3 estimateNormal(vec3 p) {
    float e = max(0.01, uHitEpsilon * 4.0);
    vec3 g = vec3(
        sampleSDF(p + vec3(e, 0.0, 0.0)) - sampleSDF(p - vec3(e, 0.0, 0.0)),
        sampleSDF(p + vec3(0.0, e, 0.0)) - sampleSDF(p - vec3(0.0, e, 0.0)),
        sampleSDF(p + vec3(0.0, 0.0, e)) - sampleSDF(p - vec3(0.0, 0.0, e))
    );
    float lenG = length(g);
    if (lenG < 1e-6) return vec3(0.0, 1.0, 0.0);
    return normalize(g);
}
```

### Unshadowed direct-light term

For classified room-plane hits only:

```glsl
hitNormal = estimateNormal(tr.pos);
vec3 L = uLightPos - tr.pos;
float dist2 = max(dot(L, L), 1e-4);
vec3 lightDir = normalize(L);
ndotl = max(dot(hitNormal, lightDir), 0.0);
direct = uLightColor * ndotl / dist2;
```

Visualization:

```glsl
rgb = direct / (direct + vec3(1.0));
```

No albedo and no shadowing are applied.

---

## 4. New Debug Modes

### Mode 9 — Hit Normal

```text
classified hit: normal * 0.5 + 0.5
unknown hit:    yellow
miss/escape:    black/dim diagnostic alpha
inactive:       black
```

### Mode 10 — Unshadowed Direct

```text
classified hit: direct / (direct + 1)
unknown hit:    yellow
miss/escape:    black
inactive:       black
```

### Mode 11 — NdotL

```text
classified hit: grayscale NdotL
unknown hit:    yellow
miss/escape:    black
inactive:       black
```

### Mode 12 — Skip Mask

```text
classified hit: green
unknown hit:    yellow
escape:         blue
miss:           red
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

Captured:

```text
tools/phase2b4_visual/m9.png
tools/phase2b4_visual/m10.png
tools/phase2b4_visual/m11.png
tools/phase2b4_visual/m12.png
```

Commands used:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=9  --surface-ray-bias=0.02 --screenshot=tools/phase2b4_visual/m9.png  --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=10 --surface-ray-bias=0.02 --screenshot=tools/phase2b4_visual/m10.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=11 --surface-ray-bias=0.02 --screenshot=tools/phase2b4_visual/m11.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=12 --surface-ray-bias=0.02 --screenshot=tools/phase2b4_visual/m12.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

Overlay split:

```text
active region:   x 0..335  (charts 1..5)
inactive region: x 336..383 (chart 6/front)
overlay y range: 193..767
```

Results:

```text
m9.png  activeUnique4=244 nonzero=10077 green=6    yellow=2378 red=60  blue=45   inactiveBright=0 activeSamples=12096
m10.png activeUnique4=44  nonzero=9796  green=0    yellow=2371 red=0   blue=0    inactiveBright=0 activeSamples=12096
m11.png activeUnique4=18  nonzero=9847  green=0    yellow=2371 red=0   blue=0    inactiveBright=0 activeSamples=12096
m12.png activeUnique4=4   nonzero=12096 green=7706 yellow=2371 red=573 blue=1446 inactiveBright=0 activeSamples=12096
```

Interpretation:

```text
Chart 6 remains inactive in all modes: inactiveBright=0.
Mode 9 normal debug has broad variation: activeUnique4=244.
Mode 10 direct debug has nonzero classified direct pixels: nonzero=9796.
Mode 11 NdotL debug is not all black/all white: activeUnique4=18, nonzero=9847.
Mode 12 skip mask explicitly reports classified/unknown/miss/escape populations.
Unknown hits are skipped for direct/normal/NdotL and remain visible as yellow.
```

Mode 12 counts at bias 0.02:

```text
classified=7706
unknown=2371
miss=573
escape=1446
```

These differ from earlier mode 6 counts because the pixel-analysis thresholds and visualization modes differ, but the result confirms a substantial unknown/skip population remains.

---

## 6. Self-Critique

### SC1 — SDF-gradient normal is debug-only

Accepted.

The field is conservative/UDF-like. Normals near boxes, edges, and corners may be noisy. Mode 9 should not be treated as production normal quality.

### SC2 — Direct mode is unshadowed and not physically complete

Accepted.

Mode 10 intentionally allows light through geometry. It only verifies that classified hit points can produce a finite point-light direct term.

### SC3 — Unknown/box hits remain substantial

Accepted.

Mode 12 reports `yellow=2371` under current thresholding. Unknown hits are skipped and displayed, not hidden.

This confirms Critique 05's warning remains active:

```text
Do not enable feedback or production NEE until box/unknown handling is resolved.
```

### SC4 — Direct visualization is tonemapped/compressed

Accepted.

`direct/(direct+1)` is useful for finite visualization but cannot be used for energy validation. Raw direct values need readback before any quantitative comparison.

### SC5 — Normal direction may not match analytic chart normals

Accepted.

The current normal comes from SDF gradient, not chart normal. This is intentional for debugging, but a later direct-light path may need a switch to use classified chart normals for stable room-plane NEE.

---

## 7. Improvements Applied After Self-Critique

No additional code changes were applied after self-critique because the constrained phase passed its debug gates:

```text
- finite normal output
- finite unshadowed direct output
- finite NdotL output
- explicit skip mask
- Chart 6 inactive
```

The main follow-up is scope control, not a code tweak.

---

## 8. Current Limitations

Still not implemented:

```text
- shadow ray to light
- shadowed/direct production NEE
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
- Unknown/box hits are skipped.
- Mode 10 is unshadowed.
- Mode 9 normals are UDF-gradient debug normals.
- Direction mapping is still debug-oriented.
- No raw direct-value readback yet.
```

---

## 9. Next Implementation Slice

Proceed only to **Phase 2B-5: Shadowed Direct-Light Debug, No Feedback** if staying incremental.

Recommended scope:

```text
1. Add a binary shadow trace from classified hit point to uLightPos.
2. Keep unknown hits skipped.
3. Compare mode 10 unshadowed direct vs new shadowed direct mode.
4. Add raw/direct or NdotL readback only if needed.
5. No feedback.
6. No accumulation.
```

Do not implement persistent feedback until either:

```text
A. box/object charts are added, or
B. unknown/box hits are intentionally excluded from the feedback design and documented as a known visual limitation.
```

---

## 10. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2b4_plan_hit_normal_unshadowed_direct_debug.md
doc/9_shadertoy2/phase2b4_impl_hit_normal_unshadowed_direct_debug.md
tools/phase2b4_visual/m9.png
tools/phase2b4_visual/m10.png
tools/phase2b4_visual/m11.png
tools/phase2b4_visual/m12.png
```
