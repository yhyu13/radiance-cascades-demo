# ShaderToy2 Phase 2C Plan — Basic Box Charts / Object Hit Support

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Classify short_box and tall_box faces into valid surface charts. Give box hits chart IDs and UVs. Allow direct light on box surfaces. Feedback remains blocked.

---

## 1. Goal

Extend hit surface classification from room-only (5 charts) to include Cornell box geometry:

```text
hit position from SDF trace
  -> classify as room plane OR box face
  -> assign chart ID (1-17: 5 room + 12 box faces)
  -> compute chart UV for atlas mapping
  -> enable direct lighting on box surfaces
```

This unblocks complete Cornell rendering in the surface RC path. After this phase, unknown/yellow hit rate should drop from ~12% to < 2%.

---

## 2. Non-Goals

Do **not** implement:

```text
- persistent ping-pong feedback
- multi-bounce closure
- surface cascade merge
- final raymarch surface GI lookup
- EXR/PT quality metrics
- arbitrary mesh support (only Cornell boxes)
- dynamic object tracking
- box material/albedo variation (use uniform color per box)
```

Reason:

```text
Box chart support is prerequisite for feedback but not sufficient alone.
Must validate box classification and UV mapping before adding temporal accumulation.
```

---

## 3. Implementation Plan

### 3.1 Analyze Cornell OBJ box geometry

From `res/scene/cornell_box.obj`, extract box dimensions:

```text
short_box: typically smaller box inside Cornell room
tall_box:  typically taller box touching or near ceiling

Need exact bounds from OBJ file or scene definition.
```

Update C++ to parse or hardcode box bounds:

```cpp
struct BoxInfo {
    glm::vec3 bmin;
    glm::vec3 bmax;
    int baseChartId;  // Starting chart ID for this box's 6 faces
};

std::vector<BoxInfo> sceneBoxes;
```

For Cornell scene:

```cpp
// Example values - verify from actual OBJ
sceneBoxes.push_back({
    .bmin = glm::vec3(-0.5, -1.0, -0.5),  // Adjust to match scene
    .bmax = glm::vec3(0.0, 0.0, 0.0),
    .baseChartId = 7  // Charts 7-12 for short_box
});
sceneBoxes.push_back({
    .bmin = glm::vec3(0.0, -1.0, 0.0),
    .bmax = glm::vec3(0.5, 1.0, 0.5),
    .baseChartId = 13  // Charts 13-18 for tall_box
});
```

### 3.2 Extend chart system to 18 charts

Current: 6 charts (5 active + 1 reserved front wall)  
New: 18 charts (5 room + 12 box faces + 1 reserved)

Update shader constants:

```glsl
const int TOTAL_CHARTS = 18;
uniform int uChartActive[18];  // Expand array size
```

Update C++ SurfaceRC class:

```cpp
std::array<int, 18> chartActive;  // Was std::array<int, 6>
```

Chart ID assignment:

```text
Chart 1:  floor
Chart 2:  ceiling
Chart 3:  left wall
Chart 4:  right wall
Chart 5:  back wall
Chart 6:  front wall (reserved/inactive for open-front Cornell)
Chart 7:  short_box bottom
Chart 8:  short_box top
Chart 9:  short_box left
Chart 10: short_box right
Chart 11: short_box front
Chart 12: short_box back
Chart 13: tall_box bottom
Chart 14: tall_box top
Chart 15: tall_box left
Chart 16: tall_box right
Chart 17: tall_box front
Chart 18: tall_box back
```

### 3.3 Extend decodeChart() for box charts

Add box chart decoding logic:

```glsl
} else if (pInBand.x < 1152) {  // 128 * 9 more charts after room charts
    int boxChartId = 7 + (pInBand.x - 896) / 128;
    int faceIndex = (boxChartId - 7) % 6;
    int boxIndex = (boxChartId - 7) / 6;  // 0 = short_box, 1 = tall_box
    
    vec3 boxBmin = (boxIndex == 0) ? uShortBoxBmin : uTallBoxBmin;
    vec3 boxBmax = (boxIndex == 0) ? uShortBoxBmax : uTallBoxBmax;
    
    c.id = boxChartId;
    c.localPx = vec2(pInBand.x - 896 - (boxChartId - 7) * 128, pInBand.y);
    c.gRes = vec2(128.0, 256.0);
    
    // Set normal/tangent/bitangent based on faceIndex
    if (faceIndex == 0) {  // bottom (-Y)
        c.normal = vec3(0.0, -1.0, 0.0);
        c.tangent = vec3(1.0, 0.0, 0.0);
        c.bitangent = vec3(0.0, 0.0, 1.0);
        c.worldOrigin = vec3(boxBmin.x, boxBmin.y, boxBmin.z);
    } else if (faceIndex == 1) {  // top (+Y)
        c.normal = vec3(0.0, 1.0, 0.0);
        c.tangent = vec3(1.0, 0.0, 0.0);
        c.bitangent = vec3(0.0, 0.0, 1.0);
        c.worldOrigin = vec3(boxBmin.x, boxBmax.y, boxBmin.z);
    } else if (faceIndex == 2) {  // left (-X)
        c.normal = vec3(-1.0, 0.0, 0.0);
        c.tangent = vec3(0.0, 1.0, 0.0);
        c.bitangent = vec3(0.0, 0.0, 1.0);
        c.worldOrigin = vec3(boxBmin.x, boxBmin.y, boxBmin.z);
    } else if (faceIndex == 3) {  // right (+X)
        c.normal = vec3(1.0, 0.0, 0.0);
        c.tangent = vec3(0.0, 1.0, 0.0);
        c.bitangent = vec3(0.0, 0.0, 1.0);
        c.worldOrigin = vec3(boxBmax.x, boxBmin.y, boxBmin.z);
    } else if (faceIndex == 4) {  // front (-Z)
        c.normal = vec3(0.0, 0.0, -1.0);
        c.tangent = vec3(0.0, 1.0, 0.0);
        c.bitangent = vec3(1.0, 0.0, 0.0);
        c.worldOrigin = vec3(boxBmin.x, boxBmin.y, boxBmin.z);
    } else if (faceIndex == 5) {  // back (+Z)
        c.normal = vec3(0.0, 0.0, 1.0);
        c.tangent = vec3(0.0, 1.0, 0.0);
        c.bitangent = vec3(1.0, 0.0, 0.0);
        c.worldOrigin = vec3(boxBmin.x, boxBmin.y, boxBmax.z);
    }
    
    c.valid = true;
}
```

### 3.4 Add box bounds uniforms

Add shader uniforms:

```glsl
uniform vec3 uShortBoxBmin;
uniform vec3 uShortBoxBmax;
uniform vec3 uTallBoxBmin;
uniform vec3 uTallBoxBmax;
```

Update C++ dispatch to set these from scene data:

```cpp
glUniform3fv(loc, 1, &shortBoxBmin[0]);
glUniform3fv(loc, 1, &shortBoxBmax[0]);
glUniform3fv(loc, 1, &tallBoxBmin[0]);
glUniform3fv(loc, 1, &tallBoxBmax[0]);
```

### 3.5 Extend classifyHitSurface() for boxes

Add box classification after room plane checks:

```glsl
HitSurface classifyHitSurface(vec3 p) {
    HitSurface h;
    h.chartId = 0;
    h.uv = vec2(0.0);
    h.valid = false;

    vec3 bmin = uSceneBoundsMin;
    vec3 bmax = uSceneBoundsMax;
    float planeEps = max(0.06, uHitEpsilon * 2.0);
    
    // ... existing room plane classification ...
    
    // If no room plane matched, try box classification
    if (!h.valid) {
        // Check short_box
        if (isPointNearBox(p, uShortBoxBmin, uShortBoxBmax, planeEps)) {
            h = classifyBoxFace(p, uShortBoxBmin, uShortBoxBmax, 7);
        }
        // Check tall_box
        else if (isPointNearBox(p, uTallBoxBmin, uTallBoxBmax, planeEps)) {
            h = classifyBoxFace(p, uTallBoxBmin, uTallBoxBmax, 13);
        }
    }
    
    return h;
}

bool isPointNearBox(vec3 p, vec3 bmin, vec3 bmax, float eps) {
    // Point is near box if it's within eps of any face
    bool nearX = (abs(p.x - bmin.x) < eps || abs(p.x - bmax.x) < eps);
    bool nearY = (abs(p.y - bmin.y) < eps || abs(p.y - bmax.y) < eps);
    bool nearZ = (abs(p.z - bmin.z) < eps || abs(p.z - bmax.z) < eps);
    
    // Must be within box bounds (with eps tolerance)
    bool inX = (p.x >= bmin.x - eps && p.x <= bmax.x + eps);
    bool inY = (p.y >= bmin.y - eps && p.y <= bmax.y + eps);
    bool inZ = (p.z >= bmin.z - eps && p.z <= bmax.z + eps);
    
    return (nearX && inY && inZ) || (inX && nearY && inZ) || (inX && inY && nearZ);
}

HitSurface classifyBoxFace(vec3 p, vec3 bmin, vec3 bmax, int baseChartId) {
    HitSurface h;
    float eps = max(0.06, uHitEpsilon * 2.0);
    
    float dBottom = abs(p.y - bmin.y);
    float dTop    = abs(p.y - bmax.y);
    float dLeft   = abs(p.x - bmin.x);
    float dRight  = abs(p.x - bmax.x);
    float dFront  = abs(p.z - bmin.z);
    float dBack   = abs(p.z - bmax.z);
    
    float best = dBottom;
    h.chartId = baseChartId;  // bottom face
    h.uv = vec2(remap01(p.x, bmin.x, bmax.x), remap01(p.z, bmin.z, bmax.z));
    
    if (dTop < best) {
        best = dTop;
        h.chartId = baseChartId + 1;  // top face
        h.uv = vec2(remap01(p.x, bmin.x, bmax.x), remap01(p.z, bmin.z, bmax.z));
    }
    if (dLeft < best) {
        best = dLeft;
        h.chartId = baseChartId + 2;  // left face
        h.uv = vec2(remap01(p.z, bmin.z, bmax.z), remap01(p.y, bmin.y, bmax.y));
    }
    if (dRight < best) {
        best = dRight;
        h.chartId = baseChartId + 3;  // right face
        h.uv = vec2(remap01(p.z, bmin.z, bmax.z), remap01(p.y, bmin.y, bmax.y));
    }
    if (dFront < best) {
        best = dFront;
        h.chartId = baseChartId + 4;  // front face
        h.uv = vec2(remap01(p.x, bmin.x, bmax.x), remap01(p.y, bmin.y, bmax.y));
    }
    if (dBack < best) {
        best = dBack;
        h.chartId = baseChartId + 5;  // back face
        h.uv = vec2(remap01(p.x, bmin.x, bmax.x), remap01(p.y, bmin.y, bmax.y));
    }
    
    h.valid = (best <= eps);
    return h;
}
```

### 3.6 Update chartColor() for box charts

Extend color function:

```glsl
vec3 chartColor(int id) {
    if (id == 1) return vec3(0.85, 0.85, 0.85);  // floor
    if (id == 2) return vec3(0.55, 0.70, 1.00);  // ceiling
    if (id == 3) return vec3(0.95, 0.15, 0.12);  // left wall
    if (id == 4) return vec3(0.10, 0.85, 0.18);  // right wall
    if (id == 5) return vec3(0.95, 0.95, 0.95);  // back wall
    if (id == 6) return vec3(0.65, 0.65, 0.75);  // front wall (inactive)
    
    // Box charts
    if (id >= 7 && id <= 12) {
        // short_box: brown/orange tones
        vec3 baseColor = vec3(0.75, 0.50, 0.25);
        float shade = 0.8 + 0.2 * float(id - 7) / 5.0;
        return baseColor * shade;
    }
    if (id >= 13 && id <= 18) {
        // tall_box: blue/purple tones
        vec3 baseColor = vec3(0.30, 0.40, 0.80);
        float shade = 0.8 + 0.2 * float(id - 13) / 5.0;
        return baseColor * shade;
    }
    
    return vec3(0.0);
}
```

### 3.7 Update chartToWorld() for box charts

Extend world position computation:

```glsl
vec3 chartToWorld(ChartInfo c, vec2 probeUVChart) {
    // ... existing room chart cases ...
    
    // Box charts
    if (c.id >= 7 && c.id <= 12) {
        int boxIndex = 0;  // short_box
        vec3 bmin = uShortBoxBmin;
        vec3 bmax = uShortBoxBmax;
        int faceIndex = c.id - 7;
        
        if (faceIndex == 0) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), bmin.y, mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 1) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), bmax.y, mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 2) return vec3(bmin.x, mix(bmin.y, bmax.y, probeUVChart.x), mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 3) return vec3(bmax.x, mix(bmin.y, bmax.y, probeUVChart.x), mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 4) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), mix(bmin.y, bmax.y, probeUVChart.y), bmin.z);
        if (faceIndex == 5) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), mix(bmin.y, bmax.y, probeUVChart.y), bmax.z);
    }
    
    if (c.id >= 13 && c.id <= 18) {
        int boxIndex = 1;  // tall_box
        vec3 bmin = uTallBoxBmin;
        vec3 bmax = uTallBoxBmax;
        int faceIndex = c.id - 13;
        
        if (faceIndex == 0) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), bmin.y, mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 1) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), bmax.y, mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 2) return vec3(bmin.x, mix(bmin.y, bmax.y, probeUVChart.x), mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 3) return vec3(bmax.x, mix(bmin.y, bmax.y, probeUVChart.x), mix(bmin.z, bmax.z, probeUVChart.y));
        if (faceIndex == 4) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), mix(bmin.y, bmax.y, probeUVChart.y), bmin.z);
        if (faceIndex == 5) return vec3(mix(bmin.x, bmax.x, probeUVChart.x), mix(bmin.y, bmax.y, probeUVChart.y), bmax.z);
    }
    
    return vec3(0.0);
}
```

### 3.8 Update C++ chart initialization

Update `SurfaceRC::updateScene()` to activate box charts:

```cpp
if (sceneKey == "cornell" || sceneKey == "cornell_orig" || sceneKey == "cornell_orig_alcove") {
    // Activate room charts + box charts
    chartActive = {1, 1, 1, 1, 1, 0,  // 1-6: room (front inactive)
                   1, 1, 1, 1, 1, 1,  // 7-12: short_box
                   1, 1, 1, 1, 1, 1}; // 13-18: tall_box
}
```

Expand array size:

```cpp
std::array<int, 18> chartActive;  // Was std::array<int, 6>
```

### 3.9 Verification

Build:

```powershell
cmake --build build --config Debug
```

Captures:

```text
tools/phase2c_visual/m6_with_boxes.png  (hit chart ID with box support)
tools/phase2c_visual/m10_direct_boxes.png  (unshadowed direct on boxes)
tools/phase2c_visual/m15_atlas_boxes.png  (direct radiance atlas with boxes)
```

Structure checks:

```text
- Chart 6 remains inactive (inactiveBright=0)
- Unknown/yellow count drops from ~12% to < 2%
- Box charts show distinct colors in mode 6
- Mode 10 shows direct lighting on box surfaces
- Mode 15 writes radiance to box chart regions
- All 18 charts have nonzero pixel counts when visible
```

Pixel-count comparison:

```text
Before (Phase 2B-5):
  classified=8633, unknown=1498, miss=1965
  
After (Phase 2C):
  classified=10100+, unknown=<250, miss=1965
  
Improvement: ~1467 previously-unknown hits now classified as box faces
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic round
if unknown rate doesn't drop significantly (< 5%), stop and diagnose classification logic
if box charts show incorrect UV mapping (mode 7 fails), stop and fix inverse mappings
if performance degrades > 20%, consider reducing box chart resolution (128x256 -> 64x128)
```

---

## 5. Self-Critique of This Plan

### SC1 — Box bounds must match OBJ geometry exactly

Risk: Hardcoded box bounds may not match actual OBJ vertex positions, causing misclassification.

Mitigation:

```text
Parse box bounds from OBJ file at load time, or provide CLI override:
--short-box-bounds=xmin,ymin,zmin,xmax,ymax,zmax
--tall-box-bounds=xmin,ymin,zmin,xmax,ymax,zmax

Default to hardcoded values but allow override for debugging.
```

### SC2 — Nearest-face heuristic may misclassify box corners/edges

Accepted. Same issue as room planes. Box corners where 3 faces meet may classify arbitrarily.

Mitigation:

```text
Use same planeEps approach. Track per-box-face unknown rate separately.
If one face has > 20% unknown, investigate that face's UV mapping.
```

### SC3 — Atlas width expansion may break existing layout

Current atlas: 1024 wide × (6 × 256) high  
New atlas: Need 18 charts × 128 wide = 2304 wide (or reorganize)

Risk: Breaking existing ring-packed layout or exceeding texture size limits.

Mitigation:

```text
Option A: Keep 1024-wide layout, wrap box charts to next rows
  Row 0: Charts 1-8 (room + partial short_box)
  Row 1: Charts 9-16 (rest of short_box + partial tall_box)
  Row 2: Charts 17-18 (rest of tall_box)

Option B: Increase atlas width to 2048 or 2304
  Verify GPU supports this width (should be fine for modern GPUs)

Option C: Reduce box chart resolution to 64×128
  Fits all 18 charts in 1024-wide layout with some empty space

Recommended: Option A for minimal disruption, Option C if performance concern.
```

### SC4 — Box material properties not modeled

Accepted. Boxes use uniform color, no albedo texture or material variation.

Future improvement:

```text
Add per-chart albedo uniform or texture lookup.
For now, use constant diffuse color per box.
```

### SC5 — No validation that box UVs map correctly to world positions

Risk: Inverse mapping errors cause feedback to write to wrong texels.

Mitigation:

```text
Run mode 8 (UV round-trip test) for box charts specifically.
Test 100 random UVs per box face, verify chartToWorld(classifyHitSurface(pos)) returns original UV.
Acceptance: 0 errors out of 1200 tests (100 UVs × 12 box faces).
```

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Parse/hardcode short_box and tall_box bounds from Cornell OBJ.
2. Extend chart system to 18 charts (5 room + 12 box + 1 reserved).
3. Update decodeChart(), chartToWorld(), chartColor() for box charts.
4. Add isPointNearBox() and classifyBoxFace() helpers.
5. Extend classifyHitSurface() to check boxes after room planes.
6. Update C++ chartActive array to 18 elements, activate box charts.
7. Add box bounds uniforms and set from scene data.
8. Build + capture m6/m10/m15 with box support.
9. Verify unknown rate drops from ~12% to < 2%.
10. Run UV round-trip test for box charts (mode 8).
11. Document implementation + self-critique.
```

Stop before feedback, accumulation, or cascade merge.

---

## 7. Success Criteria

Phase 2C succeeds if:

```text
✓ Unknown/yellow hit rate drops from ~12% to < 2%
✓ All 12 box charts show nonzero pixel counts in mode 6
✓ Box charts display distinct colors (not all same color)
✓ Mode 8 UV round-trip test passes for box charts (0 errors)
✓ Direct lighting (mode 10) appears on box surfaces
✓ Atlas write (mode 15) includes box chart regions
✓ Chart 6 remains inactive (no regression)
✓ Performance impact < 15% (profile dispatch time)
```

Phase 2C fails if:

```text
✗ Unknown rate remains > 5% (classification still broken)
✗ Box charts show all-black or all-white (UV mapping error)
✗ Mode 8 shows > 5% UV round-trip errors for boxes
✗ Direct lighting doesn't appear on boxes (normal/lighting bug)
✗ Performance degrades > 20% (optimization needed)
```

If failure occurs, diagnose and fix before proceeding to feedback implementation.

---

## 8. Post-Phase 2C Decision

After Phase 2C completes successfully, can proceed with:

```text
Option A: Persistent feedback loop (temporal accumulation)
  - Enable ping-pong between atlas and history
  - Add EMA blending (alpha = 0.1 typical)
  - Validate convergence over 100+ frames
  
Option B: Surface cascade merge (C1, C2, etc.)
  - Implement coarser cascade levels
  - Merge fine-to-coarse or coarse-to-fine
  - Validate multi-scale radiance propagation

Recommendation: Start with Option A (feedback) since box charts now unblock complete Cornell support.
Cascade merge can wait until single-bounce feedback is stable.
```

---

## 9. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_radiance_debug.comp
doc/9_shadertoy2/phase2c_plan_box_charts.md
doc/9_shadertoy2/phase2c_impl_box_charts.md
tools/phase2c_visual/m6_with_boxes.png
tools/phase2c_visual/m10_direct_boxes.png
tools/phase2c_visual/m15_atlas_boxes.png
```
