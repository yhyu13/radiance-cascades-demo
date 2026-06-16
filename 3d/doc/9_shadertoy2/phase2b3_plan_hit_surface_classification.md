# ShaderToy2 Phase 2B-3 Plan — Hit Surface Classification, No Lighting

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Classify SDF trace hit positions back onto Cornell surface charts and output hit chart/UV diagnostics. No lighting, no NEE, no feedback.

---

## 1. Goal

Extend the Phase 2B-2 trace skeleton from generic hit/miss to semantic surface hit classification:

```text
rayOrigin + worldDir -> trace hit position
  -> classify hit as Cornell chart ID + chart UV
  -> output hit chart color and hit UV gradient
```

This is the last geometry-only step before point-light NEE/direct lighting. Surface feedback requires knowing where a ray hit in the surface atlas; this phase validates that mapping.

---

## 2. Non-Goals

Do **not** implement:

```text
- point-light NEE
- direct lighting at hit point
- albedo/radiance accumulation
- previous-frame/persistent feedback
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT quality metrics
```

Reason:

```text
If hit chart/UV classification is wrong, NEE and feedback will sample/write the wrong surface texels.
```

---

## 3. Implementation Plan

### 3.1 Add hit position to trace result

Extend shader struct:

```glsl
struct TraceResult {
    int state; // 0=max miss, 1=hit, 2=escaped volume
    float t;
    vec3 pos;
};
```

Return `pos` on hit and also on miss/escape for debugging.

### 3.2 Add hit chart classification helper

Add:

```glsl
struct HitSurface {
    int chartId;
    vec2 uv;
    bool valid;
};
```

Function:

```glsl
HitSurface classifyHitSurface(vec3 p)
```

For open-front Cornell:

```text
floor:   abs(p.y - boundsMin.y) < eps -> chart 1, uv=(x,z)
ceiling: abs(p.y - boundsMax.y) < eps -> chart 2, uv=(x,z)
left:    abs(p.x - boundsMin.x) < eps -> chart 3, uv=(y,z)
right:   abs(p.x - boundsMax.x) < eps -> chart 4, uv=(y,z)
back:    abs(p.z - boundsMin.z) < eps -> chart 5, uv=(y,x) or equivalent matching chartToWorld
front:   inactive for cornell_box.obj, do not classify as chart 6
```

Because SDF hits may land slightly inside/beside plane due to UDF discretization, do not use exact equality. Choose nearest active plane by minimum normalized distance, with a classification epsilon:

```text
planeEps = max(0.035, 2 * uHitEpsilon)
```

If no active plane is within `planeEps`, output invalid/unknown.

### 3.3 Add debug modes

Current radiance debug modes:

```text
0 origin
1 direction
2 normal
3 active/chart mask
4 trace classification
5 trace distance
```

Add:

```text
6 hit chart ID
7 hit chart UV
```

Mode 6:

```text
if hit and classified: chartColor(hit.chartId)
if hit but unknown: yellow
if miss/escape: existing red/blue class color dimmed
inactive: black
```

Mode 7:

```text
if hit and classified: RGB=(uv.x, uv.y, chartId/6)
if hit but unknown: yellow
if miss/escape: black/red/blue low intensity
inactive: black
```

Alpha:

```text
1 for classified hits
0.5 for hit-unknown
0 for inactive
```

### 3.4 C++ updates

Update:

```cpp
SurfaceRC::setRadianceDebugMode clamp 0..7
SurfaceRC::radianceDebugModeName
ImGui radianceModes[]
```

CLI already supports numeric mode.

### 3.5 Verification

Build:

```powershell
cmake --build build --config Debug
```

Captures:

```text
tools/phase2b3_visual/hit_m6_chart.png
tools/phase2b3_visual/hit_m7_uv.png
```

Structure checks:

```text
inactive chart 6 remains dark
mode 6 has multiple classified chart colors in active region
unknown/yellow count should be tracked; acceptable only if small/moderate and documented
mode 7 has nontrivial UV variation
```

Suggested pixel-count buckets for mode 6:

```text
classified colors count
unknown yellow count
miss red count
escape blue count
inactive bright count
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if hit classification is mostly unknown or only one chart color, stop and diagnose before adding NEE
```

---

## 5. Self-Critique of This Plan

### SC1 — Plane classification ignores Cornell boxes/interior geometry

Accepted. The Cornell OBJ includes short/tall boxes. This phase only classifies room planes. Rays hitting boxes may become unknown/yellow.

Improvement:

```text
Treat unknown count as diagnostic, not immediate failure. If unknown dominates, boxes need chart support before NEE/feedback.
```

### SC2 — Nearest-plane classification can misclassify box hits as room walls

Accepted. If a hit on a box happens near a wall plane, the nearest active plane heuristic may assign it incorrectly.

Improvement:

```text
Use planeEps small enough to avoid broad misclassification. Report unknown count. Do not use this classifier for production feedback without object charts.
```

### SC3 — SDF/UDF hit point may be offset from actual plane

Accepted. Conservative UDF and finite hit epsilon mean hit points may not lie exactly on planes.

Improvement:

```text
Use normalized plane-distance ranking and document planeEps. If classification fails too often, add hit normal estimation before changing topology.
```

### SC4 — UV orientation must match chartToWorld exactly

Accepted. This is the main risk.

Improvement:

```text
Derive UV mappings as inverse of existing chartToWorld equations and document them in shader comments.
```

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Extend TraceResult with hit position.
2. Add classifyHitSurface() for active room planes only.
3. Add modes 6/7 for hit chart ID and hit UV.
4. Update C++ clamp/labels.
5. Build + capture m6/m7.
6. Run structure checks with unknown/yellow count.
7. Document implementation + self-critique.
```

Stop before lighting, NEE, feedback, or surface GI consumption.
