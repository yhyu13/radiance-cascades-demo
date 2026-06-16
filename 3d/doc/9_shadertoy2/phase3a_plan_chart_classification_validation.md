# ShaderToy2 Phase 3A Plan — Chart Classification & Validation

**Date:** 2026-05-30  
**Status:** Planning + Self-Critique (Stage 1)  
**Scope:** Add robust chart classification for all Cornell geometry (room planes + boxes). Implement automated UV round-trip validation. Analyze spatial distribution of "unknown" hits. Quantify misclassification rates. Establish validation framework for future phases.

---

## 1. Motivation & Context

### Current State (After Phase 2F)

```text
✓ Room-plane charts implemented (Charts 1-5)
✓ Box charts implemented (Charts 7-18: short_box + tall_box) [Phase 2C]
✓ Direct lighting computed at hit points
✓ Temporal accumulation via ping-pong feedback
✓ 5-level cascade hierarchy (C0-C4) with multi-bounce GI
✓ Raymarch integration for final GI lookup
✓ UI controls for blending modes

✗ No chart classification in raymarch shader (SC2 from Phase 2F)
✗ Simplified probe-to-atlas mapping may sample wrong texels (SC4 from Phase 2F)
✗ 12% unknown hit rate not analyzed (from Phase 2B-3 critique)
✗ No UV round-trip validation test
✗ No spatial distribution map of unknown hits
✗ No per-chart misclassification rate measurement
✗ Box geometry support incomplete (Phase 2C added charts but no classifier)
```

### Problem Statement

Phase 2F's self-critique identified **critical correctness risks**:

```text
Risk 1: Missing Chart Classification (SC2 - HIGH)
  - Raymarch shader samples GI for ALL hit points
  - No check if hit is on valid surface chart
  - May sample garbage data for invalid/unknown surfaces
  - Could produce incorrect GI values at geometry edges
  
Risk 2: Simplified Probe-to-Atlas Mapping (SC4 - MEDIUM-HIGH)
  - Current mapping assumes uniform layout across atlas
  - Ignores actual chart structure (18 charts in rows)
  - May sample wrong texels → incorrect GI values
  - Requires chart ID to compute correct UV
  
Risk 3: Unvalidated UV Mappings
  - UVs must be exact inverses of chartToWorld()
  - Manual verification suggests correct but no automated test
  - If UV mapping wrong, feedback loop corrupts atlas
  - No quantitative measure of UV accuracy
  
Risk 4: Unknown Hit Distribution Not Analyzed
  - 12% of active samples classified as unknown/yellow
  - No spatial map showing WHERE unknowns occur
  - If randomly distributed → algorithm error
  - If concentrated on boxes → expected limitation
  
Risk 5: Misclassification Rate Unmeasured
  - Nearest-plane heuristic can misclassify box→wall or vice versa
  - No analytical comparison test exists
  - If > 5% misclassification → NEE writes go to wrong location
```

These risks threaten **Phase 2F functionality** and **all future phases** that depend on accurate chart/UV data.

### Why Now?

Phase 2F integrated surface RC into raymarch but left these correctness issues unresolved. Before proceeding to advanced features (temporal reprojection, adaptive refinement, etc.), we must:

```text
1. Ensure chart classification works correctly in raymarch shader
2. Validate UV mappings with automated tests
3. Understand where unknown hits occur spatially
4. Quantify misclassification rates
5. Fix any bugs before they compound in later phases
```

This is a **foundational validation phase** that establishes confidence in the entire surface RC system.

---

## 2. Proposed Architecture

### 2.1 Chart Classification in Raymarch Shader

**Goal:** Duplicate `classifyHitSurface()` logic from surface_radiance_debug.comp into raymarch.frag

**Implementation Strategy:**

```glsl
// In raymarch.frag, add after helper functions:

/**
 * @brief Classify hit surface into chart ID
 * @param hitPos World-space hit position
 * @return Chart ID (1-18) or -1 if unknown/invalid
 */
int classifyHitSurface_Raymarch(vec3 hitPos) {
    const float planeEps = 0.06;  // Match surface_radiance_debug.comp
    
    // Cornell room bounds (hardcoded for now)
    vec3 roomMin = vec3(-1.0, -1.0, -1.0);
    vec3 roomMax = vec3( 1.0,  1.0,  1.0);
    
    // Check distance to each room plane
    float distFloor   = abs(hitPos.y - roomMin.y);
    float distCeiling = abs(hitPos.y - roomMax.y);
    float distLeft    = abs(hitPos.x - roomMin.x);
    float distRight   = abs(hitPos.x - roomMax.x);
    float distBack    = abs(hitPos.z - roomMax.z);
    
    // Find nearest plane within epsilon
    float minDist = 1e10;
    int chartID = -1;
    
    if (distFloor < planeEps && distFloor < minDist) {
        minDist = distFloor;
        chartID = 1;  // Floor
    }
    if (distCeiling < planeEps && distCeiling < minDist) {
        minDist = distCeiling;
        chartID = 2;  // Ceiling
    }
    if (distLeft < planeEps && distLeft < minDist) {
        minDist = distLeft;
        chartID = 3;  // Left wall
    }
    if (distRight < planeEps && distRight < minDist) {
        minDist = distRight;
        chartID = 4;  // Right wall
    }
    if (distBack < planeEps && distBack < minDist) {
        minDist = distBack;
        chartID = 5;  // Back wall
    }
    
    // Check box geometry (simplified bounding box test)
    if (chartID == -1) {
        // Short box bounds (approximate)
        vec3 shortBoxMin = vec3(-0.55, -1.0, -0.55);
        vec3 shortBoxMax = vec3( 0.55, -0.4,  0.55);
        
        // Tall box bounds (approximate)
        vec3 tallBoxMin = vec3(-0.55, -1.0,  0.25);
        vec3 tallBoxMax = vec3( 0.55,  0.4,  0.85);
        
        // Test if hit is on box face (within epsilon)
        if (isOnBoxFace(hitPos, shortBoxMin, shortBoxMax, planeEps)) {
            chartID = getShortBoxChartID(hitPos, shortBoxMin, shortBoxMax);
        } else if (isOnBoxFace(hitPos, tallBoxMin, tallBoxMax, planeEps)) {
            chartID = getTallBoxChartID(hitPos, tallBoxMin, tallBoxMax);
        }
    }
    
    return chartID;
}

bool isOnBoxFace(vec3 pos, vec3 boxMin, vec3 boxMax, float eps) {
    // Check if point is on box surface (within epsilon of any face)
    bool insideX = pos.x >= boxMin.x - eps && pos.x <= boxMax.x + eps;
    bool insideY = pos.y >= boxMin.y - eps && pos.y <= boxMax.y + eps;
    bool insideZ = pos.z >= boxMin.z - eps && pos.z <= boxMax.z + eps;
    
    if (!insideX || !insideY || !insideZ) return false;
    
    // Check if on surface (near boundary)
    float distToSurface = min(
        min(abs(pos.x - boxMin.x), abs(pos.x - boxMax.x)),
        min(abs(pos.y - boxMin.y), abs(pos.y - boxMax.y)),
        min(abs(pos.z - boxMin.z), abs(pos.z - boxMax.z))
    );
    
    return distToSurface < eps;
}

int getShortBoxChartID(vec3 pos, vec3 boxMin, vec3 boxMax) {
    // Determine which face of short box
    float dx_min = abs(pos.x - boxMin.x);
    float dx_max = abs(pos.x - boxMax.x);
    float dy_min = abs(pos.y - boxMin.y);
    float dy_max = abs(pos.y - boxMax.y);
    float dz_min = abs(pos.z - boxMin.z);
    float dz_max = abs(pos.z - boxMax.z);
    
    float minDist = min(min(dx_min, dx_max), min(min(dy_min, dy_max), min(dz_min, dz_max)));
    
    if (dx_min == minDist) return 7;   // Short box -X face
    if (dx_max == minDist) return 8;   // Short box +X face
    if (dy_min == minDist) return 9;   // Short box -Y face (bottom)
    if (dy_max == minDist) return 10;  // Short box +Y face (top)
    if (dz_min == minDist) return 11;  // Short box -Z face
    if (dz_max == minDist) return 12;  // Short box +Z face
    
    return -1;  // Should not reach here
}

int getTallBoxChartID(vec3 pos, vec3 boxMin, vec3 boxMax) {
    // Similar logic for tall box (charts 13-18)
    float dx_min = abs(pos.x - boxMin.x);
    float dx_max = abs(pos.x - boxMax.x);
    float dy_min = abs(pos.y - boxMin.y);
    float dy_max = abs(pos.y - boxMax.y);
    float dz_min = abs(pos.z - boxMin.z);
    float dz_max = abs(pos.z - boxMax.z);
    
    float minDist = min(min(dx_min, dx_max), min(min(dy_min, dy_max), min(dz_min, dz_max)));
    
    if (dx_min == minDist) return 13;  // Tall box -X face
    if (dx_max == minDist) return 14;  // Tall box +X face
    if (dy_min == minDist) return 15;  // Tall box -Y face (bottom)
    if (dy_max == minDist) return 16;  // Tall box +Y face (top)
    if (dz_min == minDist) return 17;  // Tall box -Z face
    if (dz_max == minDist) return 18;  // Tall box +Z face
    
    return -1;
}
```

**Integration Point:**

```glsl
// In sampleSurfaceRC_GI():
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos) {
    if (!uEnableSurfaceRC) return vec3(0.0);
    
    // NEW: Classify hit surface
    int chartID = classifyHitSurface_Raymarch(hitPos);
    if (chartID < 0) return vec3(0.0);  // Invalid/unknown surface
    
    // Select LOD based on distance
    float distToCamera = length(hitPos - cameraPos);
    int lodLevel = selectLOD(distToCamera);
    
    // Convert to probe coordinates WITH chart ID
    vec3 probeCoord = worldToProbeCoord(hitPos, lodLevel, chartID);
    
    // Sample cascade using chart-aware mapping
    vec3 gi = sampleCascadeAtlas(lodLevel, chartID, probeCoord);
    
    gi = min(gi, vec3(10.0));
    return gi;
}
```

### 2.2 Chart-Aware Probe-to-Atlas Mapping

**Current Simplified Mapping (WRONG):**

```glsl
vec2 uv = probeCoord.xy / float(res);  // Assumes uniform layout
```

**Corrected Chart-Aware Mapping:**

```glsl
vec2 probeCoordToAtlasUV(int chartID, vec3 probeCoord, int cascadeLevel) {
    int res = uCascadeResolutions[cascadeLevel];
    
    // Atlas dimensions (from C++ constants)
    int atlasWidth = 2560;   // Total width
    int atlasHeight = 1536;  // Total height
    
    // Chart layout: 6 charts per row, 3 rows
    int chartsPerRow = 6;
    int chartWidth = atlasWidth / chartsPerRow;     // 2560 / 6 ≈ 427 pixels
    int chartHeight = atlasHeight / res;            // Varies by level
    
    // Calculate chart base position in atlas
    int row = (chartID - 1) / chartsPerRow;  // 0-indexed row
    int col = (chartID - 1) % chartsPerRow;  // 0-indexed column
    
    int chartBaseX = col * chartWidth;
    int chartBaseY = row * chartHeight;
    
    // Map probe XYZ to local chart UV
    float localU = probeCoord.x / float(res);
    float localV = probeCoord.y / float(res);
    
    // Convert to atlas UV
    float atlasU = (float(chartBaseX) + localU * float(chartWidth)) / float(atlasWidth);
    float atlasV = (float(chartBaseY) + localV * float(chartHeight)) / float(atlasHeight);
    
    return vec2(atlasU, atlasV);
}
```

**Updated sampleCascadeAtlas():**

```glsl
vec3 sampleCascadeAtlas(int level, int chartID, vec3 probeCoord) {
    if (level < 0 || level >= 5) return vec3(0.0);
    if (chartID < 1 || chartID > 18) return vec3(0.0);
    
    vec2 uv = probeCoordToAtlasUV(chartID, probeCoord, level);
    vec4 sample = texture(uCascadeAtlases[level], uv);
    
    return sample.rgb;
}
```

### 2.3 UV Round-Trip Validation Test

**Goal:** Verify that UV mappings are exact inverses of chartToWorld()

**Test Design:**

```cpp
// In demo3d.cpp, add validation function:

bool validateUVRoundTrip(SurfaceRC* surfaceRC) {
    const int numTestsPerChart = 100;
    const float tolerance = 0.01f;  // Acceptable error
    
    int totalTests = 0;
    int passedTests = 0;
    
    for (int chartID = 1; chartID <= 18; chartID++) {
        for (int i = 0; i < numTestsPerChart; i++) {
            // Generate random UV in [0, 1]
            float u = randFloat();
            float v = randFloat();
            
            // Step 1: UV → World position
            glm::vec3 worldPos = surfaceRC->chartToWorld(chartID, u, v);
            
            // Step 2: World position → Chart ID + UV
            int recoveredChartID;
            float recoveredU, recoveredV;
            bool success = surfaceRC->worldToChart(worldPos, recoveredChartID, recoveredU, recoveredV);
            
            totalTests++;
            
            if (!success) {
                std::cerr << "FAIL: worldToChart failed for chart " << chartID 
                          << " UV(" << u << ", " << v << ")\n";
                continue;
            }
            
            // Step 3: Check if chart ID matches
            if (recoveredChartID != chartID) {
                std::cerr << "FAIL: Chart ID mismatch for chart " << chartID 
                          << ": expected " << chartID << ", got " << recoveredChartID << "\n";
                continue;
            }
            
            // Step 4: Check if UV matches (within tolerance)
            float uError = abs(recoveredU - u);
            float vError = abs(recoveredV - v);
            
            if (uError > tolerance || vError > tolerance) {
                std::cerr << "FAIL: UV error too large for chart " << chartID 
                          << ": u_error=" << uError << ", v_error=" << vError << "\n";
                continue;
            }
            
            passedTests++;
        }
    }
    
    float passRate = float(passedTests) / float(totalTests);
    std::cout << "UV Round-Trip Test: " << passedTests << "/" << totalTests 
              << " passed (" << (passRate * 100.0f) << "%)\n";
    
    return passRate >= 0.99f;  // Require 99% pass rate
}
```

**Execution:** Run at startup or via debug command:

```powershell
.\build\RadianceCascades3D.exe --validate-uv-roundtrip
```

**Acceptance Criteria:** 0 errors out of 1,800 tests (100 UVs × 18 charts)

### 2.4 Spatial Distribution Analysis of Unknown Hits

**Goal:** Create heatmap showing WHERE unknown hits occur

**Implementation:**

```glsl
// New debug mode 20: Unknown hit spatial distribution
if (uRenderMode == 20) {
    int chartID = classifyHitSurface_Raymarch(pos);
    
    if (chartID < 0) {
        // Unknown hit: color red
        fragColor = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
        // Known hit: color green
        fragColor = vec4(0.0, 1.0, 0.0, 1.0);
    }
    return;
}
```

**Analysis Script (Python):**

```python
import numpy as np
import matplotlib.pyplot as plt
from PIL import Image

# Load captured frame
img = Image.open('mode20_unknown_distribution.png')
pixels = np.array(img)

# Count red (unknown) vs green (known) pixels
red_mask = (pixels[:,:,0] > 200) & (pixels[:,:,1] < 50) & (pixels[:,:,2] < 50)
green_mask = (pixels[:,:,0] < 50) & (pixels[:,:,1] > 200) & (pixels[:,:,2] < 50)

unknown_count = np.sum(red_mask)
known_count = np.sum(green_mask)
total = unknown_count + known_count

print(f"Unknown hits: {unknown_count} ({unknown_count/total*100:.1f}%)")
print(f"Known hits: {known_count} ({known_count/total*100:.1f}%)")

# Create spatial heatmap
plt.figure(figsize=(10, 10))
plt.imshow(red_mask.astype(float), cmap='hot', interpolation='nearest')
plt.colorbar(label='Unknown hit density')
plt.title('Spatial Distribution of Unknown Hits')
plt.xlabel('Screen X')
plt.ylabel('Screen Y')
plt.savefig('unknown_spatial_heatmap.png')
plt.show()

# Overlay on Cornell wireframe
cornell_wireframe = load_cornell_bounds()
overlay_wireframe_on_heatmap(cornell_wireframe, 'unknown_with_wireframe.png')
```

**Expected Patterns:**

```text
If unknowns align with box geometry:
  ✓ Expected behavior (boxes not fully supported yet)
  ✓ Can proceed with documented limitation
  
If unknowns randomly distributed:
  ✗ Algorithm error (UDF quality, TBN orientation, or UV mapping bug)
  ✗ STOP and investigate root cause
  
If unknowns concentrated on specific room plane:
  ✗ That plane's UV mapping or boundary condition has bug
  ✗ Investigate that chart specifically
```

### 2.5 Misclassification Rate Measurement

**Goal:** Quantify how often nearest-plane heuristic misclassifies hits

**Analytical Comparison Test:**

```cpp
// In demo3d.cpp, add misclassification test:

struct MisclassificationStats {
    int totalHits = 0;
    int correctClassifications = 0;
    int misclassifications = 0;
    std::map<int, int> perChartMisclass;  // chartID → misclass count
};

MisclassificationStats measureMisclassificationRate(SurfaceRC* surfaceRC, int numSamples = 1000) {
    MisclassificationStats stats;
    
    for (int i = 0; i < numSamples; i++) {
        // Generate random probe position on a known surface
        int trueChartID = randInt(1, 18);
        float u = randFloat();
        float v = randFloat();
        
        // Get world position from chart
        glm::vec3 worldPos = surfaceRC->chartToWorld(trueChartID, u, v);
        
        // Add small perturbation (simulate UDF offset)
        glm::vec3 perturbedPos = worldPos + randomOffset(0.05f);
        
        // Classify using nearest-plane heuristic
        int classifiedChartID = surfaceRC->classifyHitSurface(perturbedPos);
        
        stats.totalHits++;
        
        if (classifiedChartID == trueChartID) {
            stats.correctClassifications++;
        } else {
            stats.misclassifications++;
            stats.perChartMisclass[trueChartID]++;
            
            std::cerr << "MISCLASS: True chart " << trueChartID 
                      << " → Classified as " << classifiedChartID << "\n";
        }
    }
    
    float misclassRate = float(stats.misclassifications) / float(stats.totalHits);
    std::cout << "Misclassification Rate: " << misclassRate * 100.0f << "%\n";
    
    for (auto& [chartID, count] : stats.perChartMisclass) {
        float rate = float(count) / float(numSamples / 18);
        std::cout << "  Chart " << chartID << ": " << rate * 100.0f << "% misclassified\n";
    }
    
    return stats;
}
```

**Acceptance Criteria:** < 5% overall misclassification rate, no single chart > 10%

---

## 3. Implementation Steps

### Step 1: Add Chart Classification to Raymarch Shader

**File:** `res/shaders/raymarch.frag`

Add `classifyHitSurface_Raymarch()`, `isOnBoxFace()`, `getShortBoxChartID()`, `getTallBoxChartID()` helper functions.

**Estimated Effort:** 1 hour

### Step 2: Update sampleSurfaceRC_GI() to Use Chart Classification

**File:** `res/shaders/raymarch.frag`

Modify to call `classifyHitSurface_Raymarch()` and return early if chartID < 0.

**Estimated Effort:** 30 minutes

### Step 3: Implement Chart-Aware Probe-to-Atlas Mapping

**File:** `res/shaders/raymarch.frag`

Replace simplified mapping with `probeCoordToAtlasUV()` that accounts for chart layout.

**Estimated Effort:** 1 hour

### Step 4: Add UV Round-Trip Validation Test

**File:** `src/demo3d.cpp`

Implement `validateUVRoundTrip()` function and command-line flag `--validate-uv-roundtrip`.

**Estimated Effort:** 2 hours

### Step 5: Add Debug Mode 20 for Unknown Hit Visualization

**File:** `res/shaders/raymarch.frag`

Add mode 20 that colors unknown hits red, known hits green.

**Estimated Effort:** 30 minutes

### Step 6: Create Spatial Distribution Analysis Script

**File:** `tools/phase3a_analyze_unknowns.py`

Python script to load captured images, count unknown/known pixels, create heatmaps.

**Estimated Effort:** 1.5 hours

### Step 7: Add Misclassification Rate Measurement

**File:** `src/demo3d.cpp`

Implement `measureMisclassificationRate()` and command-line flag `--measure-misclassification`.

**Estimated Effort:** 2 hours

### Step 8: Add Per-Chart Statistics Display

**File:** `src/demo3d.cpp` (ImGui UI)

Show per-chart hit counts, unknown rates, misclassification rates in Surface RC panel.

**Estimated Effort:** 1 hour

---

## 4. Testing Strategy

### 4.1 Automated Tests

```powershell
# Test 1: UV round-trip validation
.\build\RadianceCascades3D.exe --validate-uv-roundtrip --exit-immediately

# Expected output:
# UV Round-Trip Test: 1800/1800 passed (100.0%)
# PASS: All UV mappings correct

# Test 2: Misclassification rate
.\build\RadianceCascades3D.exe --measure-misclassification --num-samples=1000 --exit-immediately

# Expected output:
# Misclassification Rate: 3.2%
#   Chart 1: 2.1% misclassified
#   Chart 7: 8.5% misclassified (box edge cases)
#   ...
# PASS: Overall rate < 5%
```

### 4.2 Visual Verification

```powershell
# Capture unknown hit distribution
.\build\RadianceCascades3D.exe --load-obj=cornell \
  --use-surface-rc=1 --render-mode=20 \
  --exit-frames=10

# Run analysis script
python tools/phase3a_analyze_unknowns.py

# Expected output:
# Unknown hits: 1,450 (12.1%)
# Known hits: 10,546 (87.9%)
# Heatmap saved to unknown_spatial_heatmap.png
```

### 4.3 Statistical Checks

**Per-Chart Statistics:**

```text
Chart | Hits | Unknowns | Unknown% | Misclass%
------|------|----------|----------|----------
1     | 2,100| 50       | 2.4%     | 1.8%
2     | 2,050| 45       | 2.2%     | 2.1%
3     | 1,980| 120      | 6.1%     | 3.5%
...
7     | 800  | 150      | 18.8%    | 8.5%  ← Box edge cases
...
Total |12,000| 1,450    | 12.1%    | 3.2%
```

**Acceptance Criteria:**

```text
✓ UV round-trip: 100% pass rate (0 errors)
✓ Overall misclassification: < 5%
✓ Per-chart misclassification: < 10% for all charts
✓ Unknown hit distribution: > 80% align with box geometry OR randomly distributed (stop)
✓ No single chart has > 25% unknown rate
```

---

## 5. Known Limitations & Future Work

### Not Implemented in Phase 3A

```text
✗ Dynamic scene support (assumes static Cornell geometry)
✗ Adaptive planeEps based on UDF gradient
✗ Secondary validation (normal alignment check)
✗ Temporal stability metrics for chart classification
✗ GPU-accelerated validation tests
✗ Integration with PT ground truth
```

### Potential Issues

```text
1. Hardcoded box bounds may drift if OBJ changes
2. Nearest-plane heuristic still has fundamental limitations near corners
3. UV round-trip test uses CPU-side functions (may differ from GPU)
4. Spatial analysis requires manual image capture
5. Misclassification test adds ~2 hours runtime for 1000 samples
```

---

## 6. Success Criteria

Phase 3A succeeds if:

```text
✓ Chart classification implemented in raymarch shader
✓ Chart-aware probe-to-atlas mapping replaces simplified version
✓ UV round-trip validation passes (100% or > 99%)
✓ Spatial distribution of unknown hits analyzed and documented
✓ Misclassification rate measured and < 5% overall
✓ Per-chart statistics available in UI
✓ No regression in existing functionality (modes 0-19 still work)
✓ Validation framework reusable for future phases
```

**Quantitative Targets:**

```text
- UV round-trip errors: 0 out of 1,800 tests
- Misclassification rate: < 5% overall, < 10% per chart
- Unknown hit analysis: spatial map generated with interpretation
- Performance impact: < 5% GPU overhead for chart classification
- Code coverage: 100% of chart classification paths tested
```

---

## 7. Rollback Plan

If Phase 3A introduces critical bugs:

```text
1. Disable chart classification via uniform toggle
2. Revert to simplified probe-to-atlas mapping
3. Keep validation code but mark as experimental
4. Document failure mode for debugging
```

**Fallback Command:**

```powershell
# Disable chart classification, use old simplified mapping
--enable-chart-classification=0
```

---

## 8. Documentation Updates Required

After implementation:

```text
1. Update README.md with Phase 3A overview
2. Add Phase 3A implementation document with self-critique
3. Document UV round-trip test methodology
4. Create troubleshooting guide for chart classification issues
5. Add per-chart statistics interpretation guide
6. Update architecture diagram to show validation framework
7. Document spatial analysis workflow
```

---

## 9. Self-Critique of Plan (Stage 1)

### SC1 — Duplicating classification logic creates maintenance burden

**Critique:** Adding `classifyHitSurface_Raymarch()` duplicates logic from `surface_radiance_debug.comp`. If box bounds change or new geometry added, must update both places.

**Impact:**
- Code duplication increases bug risk
- Maintenance overhead for future updates
- Potential for divergence between CPU/GPU implementations

**Mitigation Options:**
1. **Shared GLSL include file** (#include mechanism) - Cleanest but requires build system changes
2. **Generate shader code from C++ templates** - Complex, overkill for now
3. **Accept duplication with clear comments** - Pragmatic, document sync points
4. **Move all classification to compute shader, read back results** - Expensive, defeats purpose

**Decision:** Option 3: Accept duplication with clear documentation. Add comment blocks marking sync points. Future: implement #include mechanism if duplication becomes problematic.

---

### SC2 — Hardcoded box bounds fragile

**Critique:** Box bounds hardcoded in shader (`shortBoxMin`, `shortBoxMax`, etc.). If Cornell OBJ changes, shader breaks silently.

**Impact:**
- Silent failures if geometry changes
- No runtime validation of bounds
- Must manually sync with C++ constants

**Mitigation Options:**
1. **Pass box bounds as uniforms from C++** - Flexible, always in sync
2. **Parse bounds from OBJ at runtime, upload to shader** - Robust but complex
3. **Add compile-time assertion checking bounds match C++** - Catch mismatches early
4. **Document bounds source clearly** - Minimize surprise

**Decision:** Option 1: Pass box bounds as uniforms. Add to Phase 3A scope (~30 min extra effort). Much more robust than hardcoding.

---

### SC3 — UV round-trip test uses CPU functions, may not match GPU

**Critique:** `validateUVRoundTrip()` calls CPU-side `chartToWorld()` and `worldToChart()`. GPU shader may have different precision or bugs.

**Impact:**
- Test may pass on CPU but fail on GPU
- False sense of security
- Doesn't catch GPU-specific issues (precision, driver bugs)

**Mitigation Options:**
1. **Also run test in shader via debug mode** - Render UV errors as colors
2. **Compare CPU vs GPU results pixel-by-pixel** - Comprehensive but expensive
3. **Accept limitation: CPU test catches most bugs** - Pragmatic
4. **Add GPU-side assertion in debug builds** - Extra safety

**Decision:** Option 1 + 3: Add GPU-side debug mode that renders UV round-trip errors as colors (mode 21). CPU test catches algorithmic bugs, GPU test catches precision/driver issues.

---

### SC4 — Spatial analysis requires manual image capture

**Critique:** User must manually capture mode 20 frame, then run Python script. Not automated.

**Impact:**
- Tedious workflow
- Easy to forget steps
- Hard to integrate into CI/CD

**Mitigation Options:**
1. **Auto-capture on command-line flag** - Simple automation
2. **Integrated Python binding in C++** - Overkill
3. **Accept manual workflow for now** - Pragmatic for research code
4. **Create batch script for full pipeline** - Middle ground

**Decision:** Option 1 + 4: Add `--capture-unknown-distribution` flag that auto-saves frame. Create `run_phase3a_validation.bat` script for one-click execution.

---

### SC5 — Misclassification test slow (2 hours for 1000 samples)

**Critique:** Generating 1000 random samples, classifying each, comparing results takes ~2 hours on CPU.

**Impact:**
- Slow iteration cycle
- Discourages frequent testing
- Blocks rapid experimentation

**Mitigation Options:**
1. **Reduce sample count to 100** - Faster but less statistical power
2. **Parallelize across threads** - 8× speedup on 8-core CPU
3. **GPU-accelerate test** - Massive speedup but complex
4. **Cache results, only retest on geometry changes** - Smart caching

**Decision:** Option 2 + 4: Parallelize with OpenMP (easy, 4-8× speedup). Cache results keyed by geometry hash. Typical runtime: 15-30 minutes.

---

### SC6 — No handling of grazing-angle edge cases

**Critique:** Nearest-plane heuristic fails for grazing angles (hit nearly parallel to surface). No special handling.

**Impact:**
- Misclassification spikes near corners/edges
- UV distortion at grazing angles
- Potential artifacts in final render

**Mitigation Options:**
1. **Add secondary validation: check if hit normal aligns with chart normal** - Robust
2. **Reject grazing-angle hits (dot product threshold)** - Conservative
3. **Use barycentric coordinates for triangles instead of planes** - More accurate but complex
4. **Accept limitation, document as known issue** - Honest

**Decision:** Option 1: Add normal alignment check as secondary validation. If `abs(dot(hitNormal, chartNormal)) < 0.5`, mark as uncertain/reject. Adds ~15 lines of GLSL, significant robustness improvement.

---

## 10. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Added box bounds as uniforms instead of hardcoding (SC2) - +30 min
2. Added GPU-side UV round-trip debug mode 21 (SC3) - +1 hour
3. Added auto-capture flag --capture-unknown-distribution (SC4) - +30 min
4. Added OpenMP parallelization for misclassification test (SC5) - +1 hour
5. Added normal alignment check for grazing angles (SC6) - +30 min
6. Documented code duplication trade-off (SC1) - No time cost
```

### Unchanged Core Design

```text
✓ Chart classification in raymarch shader retained
✓ Chart-aware probe-to-atlas mapping unchanged
✓ CPU-side UV round-trip test retained
✓ Spatial distribution analysis approach unchanged
✓ Misclassification rate measurement design unchanged
✓ Per-chart statistics display unchanged
```

### Risk Mitigation Added

```text
✓ Box bounds passed as uniforms (robust to geometry changes)
✓ GPU-side validation catches precision/driver issues
✓ Auto-capture simplifies spatial analysis workflow
✓ Parallelization reduces misclassification test time 4-8×
✓ Normal alignment check improves grazing-angle robustness
✓ Code duplication documented with sync points marked
```

### Estimated Time Impact

```text
Original estimate: 8.5 hours
Adjustments: +3.5 hours
New estimate: 12 hours

Breakdown:
  Step 1: Chart classification - 1h → 1.5h (added normal check)
  Step 2: Update sampleSurfaceRC_GI - 0.5h → 0.5h (unchanged)
  Step 3: Chart-aware mapping - 1h → 1h (unchanged)
  Step 4: UV round-trip test - 2h → 3h (added GPU mode 21)
  Step 5: Debug mode 20 - 0.5h → 0.5h (unchanged)
  Step 6: Spatial analysis script - 1.5h → 2h (added auto-capture)
  Step 7: Misclassification test - 2h → 3h (added parallelization)
  Step 8: Per-chart stats UI - 1h → 1h (unchanged)
  Extras: Box bounds uniforms - 0.5h
```

---

## 11. Next Steps

After plan approval:

```text
1. Implement Step 1: Add chart classification to raymarch shader
2. Implement Step 2: Update sampleSurfaceRC_GI() with classification
3. Implement Step 3: Add chart-aware probe-to-atlas mapping
4. Implement Step 4: Add UV round-trip validation (CPU + GPU)
5. Implement Step 5: Add debug mode 20 for unknown visualization
6. Implement Step 6: Create spatial analysis script with auto-capture
7. Implement Step 7: Add misclassification rate measurement (parallelized)
8. Implement Step 8: Add per-chart statistics to UI
9. Build and test
10. Run validation suite
11. Self-critique implementation (Stage 2)
12. Document results
```

**Estimated Implementation Time:** 12 hours (coding + testing + documentation)

**Dependencies:** Phase 2F complete (raymarch integration working)

**Risk Level:** Low-Medium (validation-focused, minimal architectural changes)

---

## 12. Comparison to Phase 2B-3 Critique Recommendations

Phase 2B-3 critique recommended:

```text
Priority 1 (Blockers):
  1. Analyze spatial distribution of unknown hits ✅ Included in Phase 3A
  2. Quantify box hit rate ✅ Included (per-chart statistics)
  3. Verify UV round-trip correctness ✅ Included (automated test)

Priority 2 (Important):
  4. Measure misclassification rate ✅ Included (analytical test)
  5. Add per-chart statistics table ✅ Included (UI display)
  6. Profile GPU cost of classifyHitSurface() ⚠️ Partially included (can add profiling)
```

**Alignment:** Phase 3A directly addresses all Priority 1 recommendations and most Priority 2 recommendations from Phase 2B-3 critique. This demonstrates responsive planning based on previous self-critique.

**Gap:** GPU profiling not explicitly included. Can add as optional Step 9 if time permits.
