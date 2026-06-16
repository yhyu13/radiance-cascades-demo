# ShaderToy2 Phase 2B-3 Implementation — Hit Surface Classification, No Lighting

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** Classify trace hits onto active Cornell room charts and output hit chart/UV diagnostics. No lighting, no NEE, no feedback.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b3_plan_hit_surface_classification.md
```

This phase extends the Phase 2B-2 trace skeleton:

```text
trace hit position
  -> analytic Cornell room-plane classification
  -> hit chart ID color / hit UV gradient
```

---

## 2. C++ Changes

Updated:

```text
src/surface_rc.cpp
src/demo3d.cpp
```

Radiance debug mode clamp now supports:

```text
0..7
```

New radiance mode labels:

```text
6 Hit Chart ID
7 Hit Chart UV
```

No new CLI was needed because existing numeric mode control works:

```text
--surface-radiance-debug-mode=6
--surface-radiance-debug-mode=7
```

---

## 3. Shader Changes

Updated:

```text
res/shaders/surface_radiance_debug.comp
```

### Trace result now carries position

```glsl
struct TraceResult {
    int state; // 0=max miss, 1=hit, 2=escaped volume
    float t;
    vec3 pos;
};
```

### Hit surface structure

```glsl
struct HitSurface {
    int chartId;
    vec2 uv;
    bool valid;
};
```

### Classification helper

Added:

```glsl
HitSurface classifyHitSurface(vec3 p)
```

It classifies against active open-front Cornell room planes:

```text
chart 1 floor   y = boundsMin.y, uv=(x,z)
chart 2 ceiling y = boundsMax.y, uv=(x,z)
chart 3 left    x = boundsMin.x, uv=(y,z)
chart 4 right   x = boundsMax.x, uv=(y,z)
chart 5 back    z = boundsMin.z, uv=(y,x)
chart 6 front   inactive / never classified
```

The UV mappings are inverse to the existing `chartToWorld()` equations.

### Classification epsilon

Initial epsilon:

```glsl
planeEps = max(0.035, 2*uHitEpsilon)
```

Self-critique showed unknown hits were higher than desired, so it was widened to:

```glsl
planeEps = max(0.06, 2*uHitEpsilon)
```

Reason:

```text
The mesh SDF/UDF hit point can be offset from exact analytic planes due to voxelization and conservative distance semantics.
```

---

## 4. New Debug Modes

### Mode 6 — Hit Chart ID

```text
classified hit: chartColor(chartId)
hit but unknown: yellow
escaped volume: dim blue
max miss: dim red
inactive: black / alpha 0
```

### Mode 7 — Hit Chart UV

```text
classified hit: RGB=(uv.x, uv.y, chartId/6)
hit but unknown: yellow
escaped/miss: low diagnostic color
inactive: black
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

Initial captures:

```text
tools/phase2b3_visual/hit_m6.png
tools/phase2b3_visual/hit_m7.png
```

After self-critique epsilon improvement:

```text
tools/phase2b3_visual/hit_m6_eps006.png
tools/phase2b3_visual/hit_m7_eps006.png
```

Commands:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=6 --screenshot=tools/phase2b3_visual/hit_m6_eps006.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=radiance --surface-radiance-debug-mode=7 --screenshot=tools/phase2b3_visual/hit_m7_eps006.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

Overlay split:

```text
active region:   x 0..335  (charts 1..5)
inactive region: x 336..383 (chart 6/front)
overlay y range: 193..767
```

Initial result:

```text
hit_m6.png activeUnique4=8    classified=8369 yellow=1885 red=1842 blue=0    inactiveBright=0 activeSamples=12096
hit_m7.png activeUnique4=1161 classified=6953 yellow=2059 red=108  blue=2144 inactiveBright=0 activeSamples=12096
```

After widening `planeEps` to `0.06`:

```text
hit_m6_eps006.png activeUnique4=8    classified=8633 yellow=1498 red=1965 blue=0    inactiveBright=0 activeSamples=12096
hit_m7_eps006.png activeUnique4=1209 classified=7203 yellow=1689 red=108  blue=2264 inactiveBright=0 activeSamples=12096
```

Interpretation:

```text
Chart 6 remains inactive: inactiveBright=0.
Mode 6 has multiple chart colors: activeUnique4=8.
Mode 7 has strong UV variation: activeUnique4=1209.
Unknown/yellow hits decreased after epsilon improvement:
  mode 6 yellow 1885 -> 1498
  mode 7 yellow 2059 -> 1689
Remaining yellow likely includes box/interior hits and UDF-offset plane hits.
```

---

## 6. Self-Critique

### SC1 — Room-plane classifier ignores Cornell boxes

Accepted.

`cornell_box.obj` includes short/tall boxes. Rays hitting those boxes cannot be correctly classified by a room-plane-only classifier and appear as unknown/yellow or can be misclassified if near a room plane.

Implication:

```text
Do not use this classifier for final feedback until object charts or another hit-surface strategy exists.
```

### SC2 — Nearest-plane heuristic can misclassify box hits

Accepted.

The classifier chooses nearest active room plane within `planeEps`. This is useful for room surfaces but not object surfaces.

Mitigation applied:

```text
planeEps widened only moderately to 0.06, not arbitrarily large.
Unknown count remains tracked.
```

### SC3 — Classification epsilon needed tuning

Accepted and improved.

Initial `planeEps=0.035` produced a higher unknown/yellow count. Widening to `0.06` reduced unknown hits while preserving inactive Chart 6 and maintaining multiple chart colors.

### SC4 — Mode 7 classification counts include miss/escape diagnostics

Accepted.

Mode 7's blue count is high because escape diagnostics are included in the same image. This is acceptable for debug, but future structure checks should separate classified-hit pixels from escape/miss pixels more precisely.

### SC5 — Hit chart classification is still not enough for feedback

Accepted.

Feedback needs:

```text
hit chart ID
hit UV
hit normal
hit albedo/direct lighting
sample previous atlas at hit chart/UV
```

This phase implements only chart/UV diagnostics.

---

## 7. Improvements Applied After Self-Critique

1. Widened classification epsilon:

```glsl
float planeEps = max(0.06, uHitEpsilon * 2.0);
```

2. Rebuilt after epsilon change.
3. Recaptured modes 6/7.
4. Reran structure-aware checks and documented before/after counts.

---

## 8. Current Limitations

Still not implemented:

```text
- object/box surface charts
- hit normal estimation
- hit albedo sampling
- point-light NEE
- direct lighting at hit point
- previous-frame atlas sampling
- persistent ping-pong feedback
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT metrics
```

Known debug limitations:

```text
- Room-plane classifier cannot classify box surfaces.
- Uses nearest-plane heuristic over UDF hit positions.
- Direction mapping is still debug-oriented.
- Mode 7 mixes classified hit UV and miss/escape diagnostics.
```

---

## 9. Next Implementation Slice

Proceed to **Phase 2B-4: Hit Normal + Direct-Light NEE Debug, No Feedback**.

Recommended scope:

```text
1. Estimate hit normal from SDF gradient at trace hit point.
2. Evaluate point-light NEE at hit point:
   lightDir, distance attenuation, max(dot(n, lightDir), 0)
3. Add binary shadow trace to light only if direct unshadowed NEE is sane first.
4. Output direct-light debug value, not persistent radiance.
5. Keep feedback disabled.
```

Do not add ping-pong feedback until hit direct-light debug is sane.

---

## 10. Files Changed In This Phase

```text
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2b3_plan_hit_surface_classification.md
doc/9_shadertoy2/phase2b3_impl_hit_surface_classification.md
tools/phase2b3_visual/hit_m6.png
tools/phase2b3_visual/hit_m7.png
tools/phase2b3_visual/hit_m6_eps006.png
tools/phase2b3_visual/hit_m7_eps006.png
```
