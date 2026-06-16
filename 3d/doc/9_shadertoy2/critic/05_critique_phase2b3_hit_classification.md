# Phase 2B-3 Critique — Hit Surface Classification, No Lighting

**Date:** 2026-05-29  
**Reviewer:** AI Code Reviewer  
**Phase Status:** Implemented ✅ (with known limitations)  
**Confidence for Next Phase:** 70% (recommend box chart support or unknown-hit analysis before NEE)

---

## Executive Summary

Phase 2B-3 successfully implements hit surface classification for Cornell room planes, extending the trace skeleton from Phase 2B-2. The implementation adds two debug modes (hit chart ID and UV visualization) with analytic plane classification. Verification shows:

- **71% classified hit rate** (8,633 classified out of 12,096 active samples after epsilon tuning)
- **12% unknown/yellow hits** (1,498 unclassifiable - likely boxes/UDF artifacts)
- **16% miss/escape** (1,965 red = max-distance misses, 0 blue escapes in mode 6)
- **Chart 6 remains inactive** ✓
- **Strong UV variation** (1,209 unique values in mode 7) ✓

**However**, the 12% unknown hit rate and lack of box/object chart support present significant risks for Phase 2B-4 (NEE). If unknown hits are misclassified as room planes, direct lighting will write to wrong atlas locations.

---

## What Was Done Well ✓

### 1. Clean Implementation Architecture
- Extended `TraceResult` struct to carry hit position (required for classification)
- Added `HitSurface` struct with clear semantics (chartId, uv, valid)
- `classifyHitSurface()` function is well-isolated and testable
- UV mappings documented as inverse of `chartToWorld()` equations

### 2. Iterative Self-Critique Applied
- Identified high unknown/yellow count (1,885 → 1,498 after tuning)
- Widened `planeEps` from 0.035 to 0.06 based on empirical evidence
- Rebuilt, recaptured, and re-verified after improvement
- Documented before/after comparison showing 20% reduction in unknowns

### 3. Verification Rigor Maintained
- Structure-aware pixel counting with multiple metrics:
  - Chart 6 inactive confirmation (inactiveBright=0) ✓
  - Multiple chart colors present (activeUnique4=8) ✓
  - Strong UV variation (activeUnique4=1,209 in mode 7) ✓
  - Unknown/yellow count tracked and improved ✓
- Both initial and improved captures provided
- Clear interpretation of what remaining yellow represents

### 4. Documentation Quality
- Plan document explicitly acknowledges limitations (boxes, UDF offset, UV orientation risk)
- Implementation doc tracks all changes systematically
- Self-critique sections identify 5 specific concerns with mitigations
- Next phase recommendation includes clear scope boundaries

### 5. Code Quality
- Mode clamp correctly updated to 0..7 range
- Labels added: "hit chart id", "hit chart uv"
- Alpha handling appropriate (1.0 for classified, 0.5 for unknown, diagnostic values for miss/escape)
- Nearest-plane heuristic cleanly implemented with distance ranking

---

## Critical Concerns & Risks ⚠️

### 🔴 HIGH RISK: 12% Unknown Hit Rate Not Analyzed

**Issue**: After epsilon tuning, 1,498 out of 12,096 active samples (12%) remain unclassifiable as yellow/unknown.

**Possible Causes**:
1. **Cornell box geometry**: Short/tall boxes inside room are not represented in classifier
2. **UDF offset artifacts**: Conservative SDF causes hit points to land > 0.06 units from true plane
3. **Grazing angle edge cases**: Rays hitting near corners/edges where multiple planes compete
4. **Misclassification**: Some box hits may be incorrectly assigned to nearest room plane

**Why This Matters for Phase 2B-4**:
- If 12% of hits have wrong chart/UV, NEE will sample incorrect light visibility
- Direct lighting writes will corrupt wrong atlas texels
- Feedback loop will amplify classification errors

**Missing Analysis**:
- ❌ No spatial distribution map of unknown hits (are they concentrated on box regions?)
- ❌ No per-chart unknown rate (does floor have more unknowns than ceiling?)
- ❌ No correlation with trace distance (do long-distance rays have more unknowns?)
- ❌ No breakdown by cascade level (higher cascades = coarser probes = more offset?)

**Recommendation** (Priority 1 Blocker):
```powershell
# Create spatial heatmap of unknown hits
# Overlay on Cornell box wireframe to see if they cluster around box geometry
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 `
    --surface-debug-target=radiance --surface-radiance-debug-mode=6 `
    --exit-frames=2 --screenshot="tools/phase2b3_visual/unknown_heatmap.png"

# Then manually inspect or use image analysis to check if yellow pixels 
# align with box positions in Cornell scene
```

**Acceptance Criteria for Phase 2B-4**:
- If > 80% of unknowns align with box geometry: **PROCEED** but document that boxes need charts before production feedback
- If unknowns are randomly distributed: **STOP** and investigate UDF quality or classification logic
- If one room plane has > 25% unknown rate: **STOP** and investigate that plane's TBN/UV mapping

---

### 🟠 MEDIUM-HIGH RISK: Box Geometry Completely Unsupported

**Issue**: Cornell OBJ includes short and tall boxes, but classifier only handles 5 room planes.

**Current Behavior**:
- Rays hitting boxes → classified as unknown/yellow (12% of hits)
- Some box hits near room walls → may be misclassified as nearest room plane
- No way to distinguish "legitimate unknown" from "classification error"

**Impact on Phase 2B-4**:
- NEE from box surfaces cannot be computed (no chart/UV)
- If box hits are misclassified as room planes, lighting writes go to wrong location
- Visual artifacts when feedback is enabled (wrong surfaces receive indirect light)

**Self-Critique Acknowledgment**: SC1 correctly identifies this limitation but treats it as acceptable.

**Recommendation**:
Before Phase 2B-4, choose one:
1. **Option A (Recommended)**: Add basic box chart support (2 boxes × 6 faces = 12 additional charts)
2. **Option B**: Quantify box hit rate and accept 12% unknown as temporary limitation
3. **Option C**: Use fallback strategy (e.g., classify box hits by dominant axis + distance)

**If Option B chosen**, document clearly:
```text
"Phase 2B-4 NEE will only work for room planes. Box surfaces will produce 
no indirect lighting until object charts are added in future phase."
```

---

### 🟡 MEDIUM RISK: Nearest-Plane Heuristic Can Misclassify

**Issue**: Classifier uses nearest-plane-by-distance within `planeEps=0.06`. This can cause:

1. **Box-to-wall misclassification**: Box hit 0.05 units from wall → classified as wall
2. **Corner ambiguity**: Hit near room corner equidistant from 2-3 planes → arbitrary assignment
3. **UDF-induced drift**: Conservative SDF pushes hit point toward interior → wrong plane selected

**Evidence Gap**: No quantitative analysis of misclassification rate.

**Quick Diagnostic**:
For rays that should analytically hit a specific plane, verify classification matches:
```cpp
// Pseudo-code for validation
for each probe in floor chart:
    ray_dir = downward hemisphere sample
    analytic_hit_plane = intersectRayWithPlanes(ray_origin, ray_dir)
    sdf_hit_pos = traceSDF(ray_origin, ray_dir).pos
    classified_chart = classifyHitSurface(sdf_hit_pos).chartId
    
    if (analytic_hit_plane != classified_chart):
        misclassification_count++
```

**Recommendation**: Run this test for 100 random probes per chart. If misclassification rate > 5%, investigate:
- Reduce `planeEps` and accept higher unknown rate
- Add secondary validation (e.g., check if hit normal aligns with chart normal)
- Use analytical intersection instead of SDF hit position for classification

---

### 🟡 MEDIUM RISK: UV Orientation Verification Missing

**Issue**: UV mappings must be exact inverses of `chartToWorld()` for feedback to work correctly.

**Current UV Mappings** (from shader):
```glsl
// Chart 1 (floor):   uv = (remap(x), remap(z))
// Chart 2 (ceiling): uv = (remap(x), remap(z))
// Chart 3 (left):    uv = (remap(y), remap(z))
// Chart 4 (right):   uv = (remap(y), remap(z))
// Chart 5 (back):    uv = (remap(y), remap(x))
```

**Verification Needed**: Confirm these match inverse of `chartToWorld()` in `decodeChart()`:
```glsl
// From decodeChart():
// Chart 1: worldOrigin = (bmin.x, bmin.y, bmin.z), tangent=(1,0,0), bitangent=(0,0,1)
//   chartToWorld: pos = origin + uv.x * extent.x * tangent + uv.y * extent.z * bitangent
//               = (bmin.x + uv.x*(bmax.x-bmin.x), bmin.y, bmin.z + uv.y*(bmax.z-bmin.z))
// Inverse: uv.x = (p.x - bmin.x) / (bmax.x - bmin.x) = remap(p.x, bmin.x, bmax.x) ✓
//          uv.y = (p.z - bmin.z) / (bmax.z - bmin.z) = remap(p.z, bmin.z, bmax.z) ✓
```

**Status**: Manual verification suggests mappings are correct, but no automated test exists.

**Recommendation**: Add round-trip test:
```glsl
// For random uv in [0,1]:
vec3 world_pos = chartToWorld(chart_id, uv);
HitSurface hs = classifyHitSurface(world_pos);
assert(hs.chartId == chart_id);
assert(distance(hs.uv, uv) < 0.01);  // Allow small numerical error
```

Run this test for 100 random UVs per chart. If any fail, UV mapping is broken.

---

### 🟢 LOW-MEDIUM RISK: Mode 7 Mixes Hit UV with Miss/Escape Diagnostics

**Issue**: Mode 7 output combines:
- Classified hits: RGB=(uv.x, uv.y, chartId/6)
- Unknown hits: yellow (1,1,0)
- Escapes: low blue
- Misses: black

This makes structure checks harder to interpret. The "blue=2,264" count in mode 7 includes escape diagnostics, not just hit-related data.

**Current Interpretation Challenge**:
```text
hit_m7_eps006.png: classified=7203, yellow=1689, red=108, blue=2264
```
- Are the 2,264 blue pixels all escapes? Or do some represent something else?
- Cannot easily extract "classified hit UV quality" from mixed output

**Recommendation**: Consider separating into dedicated modes:
- Mode 7a: Hit UV only (black for non-hits)
- Mode 7b: Trace state only (reuse mode 4/5 logic)

Or add structure check that filters by trace state before counting UV pixels.

---

### 🟢 LOW RISK: Epsilon Tuning May Be Scene-Specific

**Issue**: `planeEps=0.06` was tuned for Cornell box with current SDF resolution/UDF characteristics.

**Risk**: This value may not generalize to:
- Different scene scales (larger rooms need larger eps?)
- Different SDF resolutions (coarser voxels need larger eps?)
- True signed SDF vs conservative UDF (signed may need smaller eps)

**Current Mitigation**: Epsilon uses `max(0.06, 2*uHitEpsilon)` so it scales with hit epsilon.

**Recommendation**: Document epsilon sensitivity:
```text
"planeEps=0.06 tuned for Cornell scene at current SDF resolution. 
If changing scene scale or SDF generation, re-tune by minimizing 
unknown hit rate while maintaining chart separation."
```

Consider exposing as uniform for runtime tuning:
```glsl
uniform float uPlaneClassifyEps = 0.06;  // Tunable via CLI
```

---

## Documentation Quality Assessment

### Strengths ✓
- ✅ Plan explicitly lists 4 self-critique points before implementation
- ✅ Implementation doc tracks epsilon tuning iteration (0.035 → 0.06)
- ✅ Before/after verification counts clearly show improvement
- ✅ Current limitations section honestly lists 11 missing features
- ✅ Next phase recommendation includes clear stop conditions

### Gaps ❌
- ❌ **No performance measurements**: GPU dispatch time for classification not reported
- ❌ **No memory impact**: Additional structs/uniforms cost not quantified
- ❌ **Missing spatial analysis**: Where are unknown hits located in scene?
- ❌ **No misclassification rate**: How often does nearest-plane heuristic fail?
- ❌ **No UV round-trip test**: Verified correctness of inverse mappings?
- ❌ **No per-chart breakdown**: Does floor have different unknown rate than ceiling?

### Recommendations for Future Phases
1. Add "Spatial Distribution Analysis" section showing heatmaps of unknown hits
2. Include misclassification rate from analytical comparison test
3. Document UV round-trip verification results
4. Add per-chart statistics table (hits, unknowns, misclassifications per chart)
5. Measure GPU cost of `classifyHitSurface()` (branching, texture reads, etc.)

---

## Code Quality Observations

### Positive Patterns ✓
1. **Clean struct design**: `HitSurface` encapsulates classification result cleanly
2. **Defensive coding**: `h.valid` flag prevents using uninitialized chartId/uv
3. **Distance ranking**: Nearest-plane heuristic robustly handles edge cases
4. **Comment documentation**: UV mappings annotated as inverse of chartToWorld
5. **Alpha differentiation**: Classified (1.0) vs unknown (0.5) alpha aids visual debugging

### Areas for Improvement 🔧

#### 1. Magic Number in Epsilon
Current code:
```glsl
float planeEps = max(0.06, uHitEpsilon * 2.0);
```

**Issue**: `0.06` is hardcoded magic number tuned empirically.

**Recommendation**: Define as named constant or expose as uniform:
```glsl
const float PLANE_CLASSIFY_EPS_BASE = 0.06;  // Tuned for Cornell at current SDF res
uniform float uPlaneClassifyEpsScale = 1.0;   // Runtime tuning factor

float planeEps = max(PLANE_CLASSIFY_EPS_BASE * uPlaneClassifyEpsScale, uHitEpsilon * 2.0);
```

#### 2. Hardcoded Chart ID Range
Current code:
```glsl
h.valid = (best <= planeEps) && (h.chartId >= 1) && (h.chartId <= 5);
```

**Issue**: Assumes exactly 5 active charts. If chart 6 becomes active or new charts added, this breaks.

**Recommendation**: Use symbolic constants:
```glsl
const int MIN_ACTIVE_CHART = 1;
const int MAX_ACTIVE_CHART = 5;

h.valid = (best <= planeEps) && (h.chartId >= MIN_ACTIVE_CHART) && (h.chartId <= MAX_ACTIVE_CHART);
```

#### 3. No Early Exit Optimization
Current classifier checks all 5 planes even if first plane is very close:
```glsl
float dFloor = abs(p.y - bmin.y);  // Check floor
// ... continues checking all planes even if dFloor < 0.001
```

**Optimization**: Could add early exit if distance is very small:
```glsl
if (dFloor < 0.001) {
    h.chartId = 1;
    h.uv = vec2(remap01(p.x, bmin.x, bmax.x), remap01(p.z, bmin.z, bmax.z));
    h.valid = true;
    return h;
}
```

**Trade-off**: Saves ~4 distance calculations for ~50% of hits (those very close to floor), but adds branching complexity. Profile before optimizing.

#### 4. Mode 7 Blue Count Ambiguity
Current mode 7 escape color:
```glsl
} else {
    rgb = vec3(0.0);  // black for miss
    a = (tr.state == 2) ? 1.0 : 0.5;  // alpha distinguishes escape from miss
}
```

**Issue**: Structure check reports "blue=2,264" but mode 7 doesn't use blue for escapes. This suggests the pixel counting script may be using different color thresholds than expected.

**Recommendation**: Verify structure check script matches actual shader output colors. If escapes should be blue in mode 7, update shader:
```glsl
} else if (tr.state == 2) {
    rgb = vec3(0.0, 0.08, 0.35);  // dim blue for escape
    a = 1.0;
} else {
    rgb = vec3(0.0);  // black for miss
    a = 0.5;
}
```

---

## Recommended Actions Before Phase 2B-4

### Priority 1 (Blockers - Must Complete)

1. **Analyze Spatial Distribution of Unknown Hits**
   ```powershell
   # Capture mode 6 with Cornell wireframe overlay
   # Manually inspect if yellow pixels cluster around box geometry
   
   # OR use automated analysis:
   python analyze_unknown_distribution.py hit_m6_eps006.png cornell_box_wireframe.obj
   ```
   
   **Acceptance Criteria**:
   - If > 80% of unknowns align with box positions: ✅ PROCEED (document box limitation)
   - If unknowns randomly distributed: ❌ STOP (investigate UDF/classification bug)
   - If one chart has > 25% unknown rate: ❌ STOP (investigate that chart's mapping)

2. **Quantify Box Hit Rate**
   Estimate what fraction of 12% unknowns are actually box hits vs UDF artifacts:
   ```text
   Method: Compare trace distance distribution for unknowns vs classified hits
   - If unknowns have similar distance distribution: likely box hits (different geometry)
   - If unknowns have much shorter distances: likely UDF offset artifacts
   - If unknowns have much longer distances: likely grazing angle edge cases
   ```
   
   **Acceptance Criteria**: Document box hit percentage. If > 10% of ALL hits are boxes, strongly consider adding box charts before NEE.

3. **Verify UV Round-Trip Correctness**
   Add test shader or CPU-side verification:
   ```glsl
   // Test: for 100 random UVs per chart
   for (int i = 0; i < 100; i++) {
       vec2 test_uv = vec2(random(), random());
       vec3 world_pos = chartToWorld(chart_id, test_uv);
       HitSurface hs = classifyHitSurface(world_pos);
       
       if (hs.chartId != chart_id || distance(hs.uv, test_uv) > 0.01) {
           error_count++;
           print("UV round-trip failed:", chart_id, test_uv, "->", hs.chartId, hs.uv);
       }
   }
   ```
   
   **Acceptance Criteria**: 0 errors out of 500 tests (100 UVs × 5 charts)

### Priority 2 (Important - Should Complete)

4. **Measure Misclassification Rate**
   For 100 random probes per chart, compare SDF classification with analytical plane intersection:
   ```cpp
   int misclassifications = 0;
   for (int i = 0; i < 500; i++) {
       pick random probe on chart N
       ray_dir = random hemisphere direction
       
       analytic_plane = intersectRayWithRoomPlanes(ray_origin, ray_dir)
       sdf_hit_pos = traceSDF(ray_origin, ray_dir).pos
       classified_chart = classifyHitSurface(sdf_hit_pos).chartId
       
       if (analytic_plane != classified_chart && classified_chart != 0) {
           misclassifications++;
       }
   }
   float misclass_rate = misclassifications / 500.0f;
   ```
   
   **Acceptance Criteria**: Misclassification rate < 5%. If higher, reduce `planeEps` or add secondary validation.

5. **Add Per-Chart Statistics**
   Extend structure check to report:
   ```text
   Chart 1 (floor):   classified=1800, unknown=250, misclassified=30, total=2080
   Chart 2 (ceiling): classified=1750, unknown=280, misclassified=25, total=2055
   Chart 3 (left):    classified=1600, unknown=320, misclassified=40, total=1960
   Chart 4 (right):   classified=1650, unknown=310, misclassified=35, total=1995
   Chart 5 (back):    classified=1833, unknown=338, misclassified=28, total=2199
   ```
   
   **Acceptance Criteria**: No single chart has unknown rate > 20% or misclassification rate > 5%.

6. **Profile GPU Cost of Classification**
   Measure dispatch time with/without `classifyHitSurface()` call:
   ```cpp
   auto t0 = std::chrono::high_resolution_clock::now();
   glDispatchCompute(...);
   glFinish();
   auto t1 = std::chrono::high_resolution_clock::now();
   std::cout << "Mode 6 dispatch: " << duration_ms << " ms\n";
   ```
   
   Compare mode 5 (no classification) vs mode 6 (with classification). If overhead > 20%, consider optimization.

### Priority 3 (Nice-to-have)

7. **Extract Magic Numbers to Constants** (refactoring)
8. **Add Optional Verbose Logging** for classification statistics
9. **Create Object Chart Stub** for future box support
10. **Document Epsilon Sensitivity** for different scenes/resolutions

---

## Verdict

### Phase 2B-3 Status: ✅ IMPLEMENTED SUCCESSFULLY (with known limitations)

The implementation achieves its stated goals: hit surface classification for Cornell room planes works with 71% success rate after epsilon tuning. UV mappings appear correct, multiple chart colors are present, and Chart 6 remains properly inactive.

**However**, the 12% unknown hit rate and complete lack of box geometry support present significant risks for Phase 2B-4 (NEE). Direct lighting requires accurate chart/UV to write to correct atlas locations.

### Confidence Level: 70% Ready for Phase 2B-4

- **30% uncertainty** due to:
  - Unanalyzed spatial distribution of unknown hits
  - Unknown misclassification rate for nearest-plane heuristic
  - No UV round-trip verification
  - Box geometry completely unsupported

- **Recommendation**: Complete Priority 1 actions above before starting 2B-4 implementation
- **If Priority 1 passes AND you accept box limitation**: Confidence increases to 85%+
- **If you add basic box charts**: Confidence increases to 95%+

---

## Comparison to Plan

| Aspect | Planned | Implemented | Deviation | Status |
|--------|---------|-------------|-----------|--------|
| TraceResult with pos | ✓ | ✓ | None | ✅ |
| HitSurface struct | ✓ | ✓ | None | ✅ |
| classifyHitSurface() | ✓ | ✓ | None | ✅ |
| Mode 6 chart ID | ✓ | ✓ | None | ✅ |
| Mode 7 chart UV | ✓ | ✓ | None | ✅ |
| C++ clamp 0..7 | ✓ | ✓ | None | ✅ |
| Verification | Basic structure checks | Enhanced with before/after epsilon tuning | Exceeded plan | ✅ Exceeded |
| Self-critique | 4 points listed | 5 points + epsilon improvement applied | Exceeded plan | ✅ Exceeded |
| Spatial analysis | Not planned | Not done | **Gap identified** | ❌ Missing |
| Misclassification rate | Not planned | Not measured | **Gap identified** | ❌ Missing |
| UV round-trip test | Not planned | Not done | **Gap identified** | ❌ Missing |
| Per-chart stats | Suggested | Not done | **Gap identified** | ❌ Missing |

---

## Final Recommendation

### Proceed to Phase 2B-4 **only after** completing spatial distribution analysis and UV round-trip verification.

**Rationale**:
- Current implementation correctly classifies room planes but 12% unknown rate needs characterization
- Understanding whether unknowns are box hits (acceptable) vs UDF artifacts (concerning) is critical
- UV mapping correctness is fundamental to feedback - errors here will corrupt entire atlas
- NEE will amplify any classification errors through persistent feedback loop

**Decision Tree**:
```
Spatial analysis results:
├─ > 80% of unknowns align with box geometry
│  ├─ UV round-trip test passes (0 errors)
│  │  └─ ✅ PROCEED to Phase 2B-4, document box limitation
│  │     Confidence: 85%
│  │
│  └─ UV round-trip test fails (> 5 errors)
│     └─ ❌ STOP, fix UV mapping before continuing
│
├─ Unknowns randomly distributed (not box-correlated)
│  └─ ❌ STOP, investigate UDF quality or classification logic
│     Likely causes: UDF too coarse, planeEps too small, TBN orientation wrong
│
└─ One chart has > 25% unknown rate
   └─ ❌ STOP, investigate that chart's UV mapping or boundary conditions
      Check: remap01 range, chartToWorld inverse, plane equation signs
```

**Box Geometry Decision**:
```
If box hit rate > 10% of total hits:
├─ Option A: Add basic box charts (6 faces × 2 boxes = 12 charts)
│  └─ Estimated effort: 4-8 hours
│  └─ Confidence after: 95%+
│
└─ Option B: Accept limitation, document that NEE won't work for boxes
   └─ Estimated effort: 1 hour documentation
   └─ Confidence after: 75% (box surfaces will have no indirect lighting)
```

**Estimated Effort for Priority 1 Actions**: 3-5 hours
- Spatial distribution analysis: 1-2 hours (automated script or manual inspection)
- Box hit rate estimation: 30 minutes (distance distribution comparison)
- UV round-trip test: 1 hour (test shader or CPU verification)
- Documentation update: 30 minutes

This investment will significantly de-risk Phase 2B-4 and prevent costly debugging when feedback loop amplifies classification errors.

---

## Appendix: Quick Reference Commands

### Spatial Distribution Analysis
```python
# analyze_unknown_distribution.py
from PIL import Image
import numpy as np

img = Image.open('hit_m6_eps006.png')
pixels = np.array(img)

# Find yellow pixels (unknown hits)
yellow_mask = (pixels[:,:,0] > 0.9) & (pixels[:,:,1] > 0.9) & (pixels[:,:,2] < 0.1)
yellow_coords = np.argwhere(yellow_mask)

print(f"Found {len(yellow_coords)} unknown hit pixels")
print(f"Distribution across atlas:")

# Map atlas coordinates back to chart/UV space
for y, x in yellow_coords[:100]:  # Sample first 100
    cascade = y // 256
    chart_x = x % 1024
    
    if chart_x < 256:
        chart_name = "floor"
        uv = (chart_x / 256.0, (y % 256) / 256.0)
    elif chart_x < 512:
        chart_name = "ceiling"
        uv = ((chart_x - 256) / 256.0, (y % 256) / 256.0)
    # ... etc for other charts
    
    print(f"  Cascade {cascade}, {chart_name}, UV={uv}")

# TODO: Overlay on Cornell wireframe to check box alignment
```

### UV Round-Trip Test Shader
```glsl
// Add to surface_radiance_debug.comp as temporary debug mode 8
else if (uDebugMode == 8) {
    // UV round-trip verification test
    vec2 test_uv = probeUVChart;  // Use existing probe UV
    vec3 world_pos = chartToWorld(c, test_uv);
    HitSurface hs = classifyHitSurface(world_pos);
    
    if (hs.chartId == c.id && distance(hs.uv, test_uv) < 0.01) {
        rgb = vec3(0.0, 1.0, 0.0);  // green = pass
        a = 1.0;
    } else {
        rgb = vec3(1.0, 0.0, 0.0);  // red = fail
        a = 1.0;
        // Debug: encode error type in RGB
        // R = chart mismatch, G = UV mismatch, B = both
        if (hs.chartId != c.id) rgb.r = 1.0;
        if (distance(hs.uv, test_uv) >= 0.01) rgb.g = 1.0;
    }
}
```

Usage:
```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 `
    --surface-debug-target=radiance --surface-radiance-debug-mode=8 `
    --exit-frames=2 --screenshot="tools/phase2b3_visual/uv_roundtrip_test.png"

# Count red vs green pixels
# Expected: > 99% green (pass), < 1% red (fail)
```

### Per-Chart Statistics Script
```python
# per_chart_stats.py
from PIL import Image
import numpy as np

img = Image.open('hit_m6_eps006.png')
pixels = np.array(img)

# Define chart regions
charts = {
    'floor':    {'x': (0, 256),   'color': (0.85, 0.85, 0.85)},
    'ceiling':  {'x': (256, 512), 'color': (0.55, 0.70, 1.00)},
    'left':     {'x': (512, 640), 'color': (0.95, 0.15, 0.12)},
    'right':    {'x': (640, 768), 'color': (0.10, 0.85, 0.18)},
    'back':     {'x': (768, 896), 'color': (0.95, 0.95, 0.95)},
}

yellow = (0.9, 0.9, 0.0)
red_miss = (0.35, 0.0, 0.0)

for chart_name, info in charts.items():
    x_start, x_end = info['x']
    region = pixels[193:767, x_start:x_end]  # Overlay y range
    
    # Count classified hits (match chart color within tolerance)
    chart_color = np.array(info['color'])
    classified = np.sum(np.all(np.abs(region[:,:,:3] - chart_color) < 0.1, axis=2))
    
    # Count unknown (yellow)
    unknown = np.sum(np.all(np.abs(region[:,:,:3] - np.array(yellow)) < 0.1, axis=2))
    
    # Count misses (red)
    misses = np.sum(np.all(np.abs(region[:,:,:3] - np.array(red_miss)) < 0.1, axis=2))
    
    total = classified + unknown + misses
    
    print(f"{chart_name:10s}: classified={classified:5d}, unknown={unknown:5d}, "
          f"misses={misses:5d}, total={total:5d}, "
          f"unknown_rate={unknown/max(total,1)*100:.1f}%")
```

### Misclassification Rate Test
```cpp
// Add to demo3d.cpp as temporary diagnostic
void Demo3D::testClassificationAccuracy() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> uv_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> dir_dist(-1.0f, 1.0f);
    
    int misclassifications = 0;
    int total_tests = 0;
    
    for (int chart_id = 1; chart_id <= 5; chart_id++) {
        for (int i = 0; i < 100; i++) {
            // Pick random UV on chart
            vec2 uv(uv_dist(gen), uv_dist(gen));
            vec3 ray_origin = chartToWorld(chart_id, uv) + getChartNormal(chart_id) * 0.01f;
            
            // Random hemisphere direction
            vec2 disk(dir_dist(gen), dir_dist(gen));
            float len = length(disk);
            if (len > 1.0f) continue;  // Reject outside unit disk
            float z = sqrt(1.0f - len*len);
            vec3 local_dir = normalize(vec3(disk.x, disk.y, z));
            vec3 world_dir = transformToWorld(local_dir, chart_id);
            
            // Analytical intersection
            int analytic_plane = intersectRayWithRoomPlanes(ray_origin, world_dir);
            
            // SDF trace and classify
            TraceResult tr = traceSDF(ray_origin, world_dir);
            if (tr.state != 1) continue;  // Skip non-hits
            
            HitSurface hs = classifyHitSurface(tr.pos);
            if (!hs.valid) continue;  // Skip unknown classifications
            
            total_tests++;
            if (analytic_plane != hs.chartId) {
                misclassifications++;
                std::cout << "Misclassification: chart " << chart_id 
                          << " -> analytic=" << analytic_plane 
                          << ", classified=" << hs.chartId << "\n";
            }
        }
    }
    
    float rate = (total_tests > 0) ? (100.0f * misclassifications / total_tests) : 0.0f;
    std::cout << "Classification accuracy test: " << (total_tests - misclassifications) 
              << "/" << total_tests << " correct (" << rate << "% misclassification rate)\n";
}
```

---

**End of Critique**
