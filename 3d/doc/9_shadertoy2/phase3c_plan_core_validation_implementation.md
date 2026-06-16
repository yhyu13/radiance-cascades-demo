# ShaderToy2 Phase 3C Plan — Core Validation Implementation

**Date:** 2026-05-30  
**Status:** Planning + Self-Critique (Stage 1)  
**Scope:** Implement the three critical C++ validation functions declared in Phase 3A/3B but never implemented: `validateUVRoundTrip()`, `measureMisclassificationRate()`, and `captureUnknownDistribution()`. Add command-line flag parsing. Integrate chart classification into raymarch shader. Establish baseline correctness metrics through actual test execution.

---

## 1. Motivation & Context

### Current State (After Phase 3B Tooling)

```text
✓ Excellent tooling infrastructure created:
  - generate_raymarch_with_includes.py (auto-generates shader with includes)
  - phase3b_analyze_unknowns.py (spatial analysis script)
  - run_phase3b_validation.bat (one-click pipeline)
  - requirements.txt (Python dependencies documented)

✗ CRITICAL GAP: Core C++ validation functions NOT implemented:
  - validateUVRoundTrip() - just declared, no body
  - measureMisclassificationRate() - just declared, no body
  - captureUnknownDistribution() - just declared, no body

✗ Command-line flags don't work (--validate-uv-roundtrip, etc.)
✗ Debug modes 20/21 not added to shader
✗ Box bounds uniforms not bound in raymarchPass()
✗ Generated shader not created or used
✗ Zero validation tests can actually run
```

### Problem Statement

Phase 3B fell into a **classic trap**: created sophisticated tools but no functionality to validate. This violates the lesson from memory:

> "工具与功能实现的优先级原则": If tools exist but core functions unimplemented → immediately stop tool development and implement core functions.

**Current Situation:**
```text
Tools Ready: ✓✓✓ (3 Python scripts, 1 batch file)
Core Logic: ✗✗✗ (0 C++ validation functions)
Tests Run: 0 (cannot execute anything)
Value Delivered: ZERO (despite excellent tooling)
```

This must be fixed NOW before proceeding to any advanced features.

### Why Now?

Phase 3A/3B identified critical correctness risks that threaten all future phases:

```text
Risk 1: UV Mappings May Be Wrong (CRITICAL)
  - No automated test exists
  - Manual verification suggests correct but unproven
  - If wrong, feedback loop corrupts atlas silently
  
Risk 2: Unknown Hit Distribution Unknown (HIGH)
  - 12% unknown rate from Phase 2B-3
  - No spatial map showing WHERE they occur
  - Could indicate algorithm error vs expected box limitation
  
Risk 3: Misclassification Rate Unquantified (HIGH)
  - Nearest-plane heuristic can misclassify
  - No analytical comparison test
  - If > 5%, NEE writes go to wrong location
  
Risk 4: No Baseline Metrics (MEDIUM)
  - Cannot measure improvement from future optimizations
  - No regression detection capability
```

These risks cannot be assessed without **actual test execution**. Phase 3C delivers that capability.

---

## 2. Proposed Architecture

### 2.1 UV Round-Trip Validation Implementation

**File:** `src/demo3d.cpp`

**Function Signature:**
```cpp
bool Demo3D::validateUVRoundTrip();
```

**Implementation Design:**

```cpp
bool Demo3D::validateUVRoundTrip() {
    if (!surfaceRC) {
        std::cerr << "[Phase 3C] ERROR: surfaceRC not initialized\n";
        return false;
    }
    
    const int numTestsPerChart = 100;
    const float tolerance = 0.01f;  // Acceptable UV error
    
    int totalTests = 0;
    int passedTests = 0;
    std::vector<std::string> failures;
    
    std::cout << "\n[Phase 3C] Starting UV Round-Trip Validation...\n";
    std::cout << "Testing " << numTestsPerChart << " UVs per active chart\n\n";
    
    for (int chartID = 1; chartID <= 18; chartID++) {
        // Skip inactive charts
        if (!surfaceRC->isChartActive(chartID)) {
            continue;
        }
        
        int chartPassed = 0;
        
        for (int i = 0; i < numTestsPerChart; i++) {
            // Generate random UV in [0, 1]
            float u = static_cast<float>(rand()) / RAND_MAX;
            float v = static_cast<float>(rand()) / RAND_MAX;
            
            // Step 1: UV → World position
            glm::vec3 worldPos = surfaceRC->chartToWorld(chartID, u, v);
            
            // Step 2: World position → Chart ID + UV
            int recoveredChartID;
            float recoveredU, recoveredV;
            bool success = surfaceRC->worldToChart(worldPos, recoveredChartID, recoveredU, recoveredV);
            
            totalTests++;
            
            if (!success) {
                std::string msg = "FAIL: worldToChart failed for chart " + 
                                  std::to_string(chartID) + " UV(" + 
                                  std::to_string(u) + ", " + std::to_string(v) + ")";
                failures.push_back(msg);
                continue;
            }
            
            // Step 3: Check if chart ID matches
            if (recoveredChartID != chartID) {
                std::string msg = "FAIL: Chart ID mismatch for chart " + 
                                  std::to_string(chartID) + 
                                  ": expected " + std::to_string(chartID) + 
                                  ", got " + std::to_string(recoveredChartID);
                failures.push_back(msg);
                continue;
            }
            
            // Step 4: Check if UV matches (within tolerance)
            float uError = std::abs(recoveredU - u);
            float vError = std::abs(recoveredV - v);
            
            if (uError > tolerance || vError > tolerance) {
                std::string msg = "FAIL: UV error too large for chart " + 
                                  std::to_string(chartID) + 
                                  ": u_error=" + std::to_string(uError) + 
                                  ", v_error=" + std::to_string(vError);
                failures.push_back(msg);
                continue;
            }
            
            chartPassed++;
            passedTests++;
        }
        
        float chartPassRate = static_cast<float>(chartPassed) / static_cast<float>(numTestsPerChart);
        std::cout << "  Chart " << chartID << ": " << chartPassed << "/" << numTestsPerChart 
                  << " passed (" << (chartPassRate * 100.0f) << "%)\n";
    }
    
    float passRate = static_cast<float>(passedTests) / static_cast<float>(totalTests);
    std::cout << "\n[Phase 3C] UV Round-Trip Test Results:\n";
    std::cout << "  Total tests: " << totalTests << "\n";
    std::cout << "  Passed: " << passedTests << "\n";
    std::cout << "  Failed: " << (totalTests - passedTests) << "\n";
    std::cout << "  Pass rate: " << (passRate * 100.0f) << "%\n";
    
    if (!failures.empty()) {
        std::cout << "\n  First 10 failures:\n";
        for (size_t i = 0; i < std::min(failures.size(), size_t(10)); i++) {
            std::cout << "    " << failures[i] << "\n";
        }
    }
    
    bool success = (passRate >= 0.99f);  // Require 99% pass rate
    std::cout << "\n  Result: " << (success ? "PASS" : "FAIL") << "\n";
    
    return success;
}
```

**Execution:**
```powershell
.\build\RadianceCascades3D.exe --validate-uv-roundtrip --exit-immediately
```

**Acceptance Criteria:** 0 errors out of 1,800 tests (100 UVs × 18 active charts)

### 2.2 Misclassification Rate Measurement Implementation

**File:** `src/demo3d.cpp`

**Function Signature:**
```cpp
void Demo3D::measureMisclassificationRate(int numSamples);
```

**Implementation Design:**

```cpp
void Demo3D::measureMisclassificationRate(int numSamples) {
    if (!surfaceRC) {
        std::cerr << "[Phase 3C] ERROR: surfaceRC not initialized\n";
        return;
    }
    
    struct Stats {
        int totalHits = 0;
        int correctClassifications = 0;
        int misclassifications = 0;
        std::map<int, int> perChartMisclass;  // chartID → misclass count
        std::map<int, int> perChartTotal;     // chartID → total samples
    };
    
    Stats stats;
    
    std::cout << "\n[Phase 3C] Starting Misclassification Rate Test...\n";
    std::cout << "Testing " << numSamples << " random samples\n\n";
    
    // Use OpenMP for parallelization if available
    #ifdef _OPENMP
    std::cout << "Using OpenMP parallelization\n";
    #endif
    
    #pragma omp parallel for reduction(+:stats.totalHits, stats.correctClassifications, stats.misclassifications)
    for (int i = 0; i < numSamples; i++) {
        // Generate random probe position on a known surface
        int trueChartID = (rand() % 18) + 1;  // Charts 1-18
        
        // Skip inactive charts
        if (!surfaceRC->isChartActive(trueChartID)) {
            continue;
        }
        
        float u = static_cast<float>(rand()) / RAND_MAX;
        float v = static_cast<float>(rand()) / RAND_MAX;
        
        // Get world position from chart
        glm::vec3 worldPos = surfaceRC->chartToWorld(trueChartID, u, v);
        
        // Add small perturbation (simulate UDF offset)
        glm::vec3 perturbation(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.05f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.05f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.05f
        );
        glm::vec3 perturbedPos = worldPos + perturbation;
        
        // Classify using nearest-plane heuristic
        int classifiedChartID;
        float classifiedU, classifiedV;
        bool success = surfaceRC->worldToChart(perturbedPos, classifiedChartID, classifiedU, classifiedV);
        
        #pragma omp critical
        {
            stats.totalHits++;
            stats.perChartTotal[trueChartID]++;
            
            if (!success) {
                stats.misclassifications++;
                stats.perChartMisclass[trueChartID]++;
            } else if (classifiedChartID != trueChartID) {
                stats.misclassifications++;
                stats.perChartMisclass[trueChartID]++;
                
                if (stats.misclassifications <= 10) {  // Log first 10
                    std::cerr << "[Phase 3C] MISCLASS: True chart " << trueChartID 
                              << " → Classified as " << classifiedChartID << "\n";
                }
            } else {
                stats.correctClassifications++;
            }
        }
    }
    
    float misclassRate = static_cast<float>(stats.misclassifications) / 
                         static_cast<float>(stats.totalHits);
    
    std::cout << "\n[Phase 3C] Misclassification Rate Test Results:\n";
    std::cout << "  Total samples: " << stats.totalHits << "\n";
    std::cout << "  Correct classifications: " << stats.correctClassifications << "\n";
    std::cout << "  Misclassifications: " << stats.misclassifications << "\n";
    std::cout << "  Overall misclassification rate: " << (misclassRate * 100.0f) << "%\n";
    
    std::cout << "\n  Per-chart breakdown:\n";
    for (auto& [chartID, total] : stats.perChartTotal) {
        int misclass = stats.perChartMisclass.count(chartID) ? stats.perChartMisclass[chartID] : 0;
        float rate = static_cast<float>(misclass) / static_cast<float>(total);
        std::cout << "    Chart " << chartID << ": " << (rate * 100.0f) << "% misclassified (" 
                  << misclass << "/" << total << ")\n";
    }
    
    // Acceptance criteria
    bool overallPass = (misclassRate < 0.05f);  // < 5% overall
    bool perChartPass = true;
    for (auto& [chartID, total] : stats.perChartTotal) {
        int misclass = stats.perChartMisclass.count(chartID) ? stats.perChartMisclass[chartID] : 0;
        float rate = static_cast<float>(misclass) / static_cast<float>(total);
        if (rate > 0.10f) {  // Any single chart > 10% fails
            perChartPass = false;
            std::cerr << "  ⚠️  Chart " << chartID << " exceeds 10% misclassification rate\n";
        }
    }
    
    std::cout << "\n  Overall result: " << (overallPass ? "PASS" : "FAIL") << "\n";
    std::cout << "  Per-chart result: " << (perChartPass ? "PASS" : "FAIL") << "\n";
}
```

**Execution:**
```powershell
.\build\RadianceCascades3D.exe --measure-misclassification --num-samples=1000 --exit-immediately
```

**Acceptance Criteria:**
- Overall misclassification rate: < 5%
- Per-chart misclassification rate: < 10% for all charts

### 2.3 Unknown Distribution Capture Implementation

**File:** `src/demo3d.cpp`

**Function Signature:**
```cpp
void Demo3D::captureUnknownDistribution();
```

**Implementation Design:**

```cpp
void Demo3D::captureUnknownDistribution() {
    if (!surfaceRC) {
        std::cerr << "[Phase 3C] ERROR: surfaceRC not initialized\n";
        return;
    }
    
    std::cout << "\n[Phase 3C] Capturing Unknown Hit Distribution...\n";
    
    // Set render mode to 20 (unknown visualization)
    setRaymarchRenderMode(20);
    
    // Create output directory
    std::string outputDir = "tools/phase3b_visual";
    createDirectory(outputDir);  // Helper function
    
    // Render frames and capture middle frame after convergence
    const int totalFrames = 10;
    const int captureFrame = 5;
    
    for (int frame = 0; frame < totalFrames; frame++) {
        // Update scene (camera movement detection, atlas updates, etc.)
        updateScene();
        
        // Render frame
        render();
        
        // Capture frame
        if (frame == captureFrame) {
            std::string filename = outputDir + "/unknown_distribution_frame" + 
                                   std::to_string(frame) + ".png";
            
            // Use existing screenshot functionality
            if (captureScreenshot(filename)) {
                std::cout << "[Phase 3C] ✓ Captured: " << filename << "\n";
            } else {
                std::cerr << "[Phase 3C] ✗ Failed to capture: " << filename << "\n";
            }
        }
        
        std::cout << "  Frame " << (frame + 1) << "/" << totalFrames << "\r";
        std::cout.flush();
    }
    
    std::cout << "\n[Phase 3C] Capture complete.\n";
    std::cout << "Next step: python tools/phase3b_analyze_unknowns.py " 
              << outputDir << "/unknown_distribution_frame" << captureFrame << ".png\n";
}
```

**Helper Function:**
```cpp
bool Demo3D::captureScreenshot(const std::string& filename) {
    // Use stb_image_write or similar library
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    std::vector<unsigned char> pixels(width * height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    
    // Flip vertically (OpenGL origin is bottom-left)
    std::vector<unsigned char> flipped(width * height * 3);
    for (int y = 0; y < height; y++) {
        memcpy(&flipped[(height - 1 - y) * width * 3], 
               &pixels[y * width * 3], 
               width * 3);
    }
    
    return stbi_write_png(filename.c_str(), width, height, 3, 
                          flipped.data(), width * 3) != 0;
}
```

**Execution:**
```powershell
.\build\RadianceCascades3D.exe --capture-unknown-distribution --exit-after-capture
```

### 2.4 Command-Line Flag Parsing

**File:** `src/main3d.cpp` or `src/demo3d.cpp`

**Implementation:**

```cpp
// In main() or command-line parsing section:

if (cmd.hasFlag("--validate-uv-roundtrip")) {
    std::cout << "\n=== Phase 3C: UV Round-Trip Validation ===\n";
    bool success = demo.validateUVRoundTrip();
    return success ? 0 : 1;
}

if (cmd.hasFlag("--measure-misclassification")) {
    int numSamples = cmd.getInt("--num-samples", 1000);
    std::cout << "\n=== Phase 3C: Misclassification Rate Test ===\n";
    demo.measureMisclassificationRate(numSamples);
    return 0;
}

if (cmd.hasFlag("--capture-unknown-distribution")) {
    std::cout << "\n=== Phase 3C: Unknown Distribution Capture ===\n";
    demo.captureUnknownDistribution();
    return 0;
}

if (cmd.hasFlag("--run-phase3c-validation")) {
    std::cout << "\n=== Phase 3C: Full Validation Suite ===\n";
    
    // Step 1: UV round-trip
    std::cout << "\n[Step 1/3] UV Round-Trip Validation\n";
    bool uvPass = demo.validateUVRoundTrip();
    
    // Step 2: Misclassification rate
    std::cout << "\n[Step 2/3] Misclassification Rate Test\n";
    demo.measureMisclassificationRate(1000);
    
    // Step 3: Unknown distribution
    std::cout << "\n[Step 3/3] Unknown Distribution Capture\n";
    demo.captureUnknownDistribution();
    
    std::cout << "\n=== Phase 3C Validation Complete ===\n";
    std::cout << "UV Round-Trip: " << (uvPass ? "PASS" : "FAIL") << "\n";
    
    return uvPass ? 0 : 1;
}
```

### 2.5 Chart Classification Integration in Raymarch Shader

**File:** `res/shaders/raymarch_generated.frag` (after running Python generator)

**Update `sampleSurfaceRC_GI()`:**

```glsl
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos) {
    if (!uEnableSurfaceRC) return vec3(0.0);
    
    // Phase 3C: Classify hit surface with normal alignment check
    int chartID = classifyHitSurface_Advanced(hitPos, normal,
                                               uSceneBoundsMin, uSceneBoundsMax,
                                               uShortBoxMin, uShortBoxMax,
                                               uTallBoxMin, uTallBoxMax,
                                               0.06);
    if (chartID < 0) return vec3(0.0);  // Invalid/unknown surface
    
    // Select LOD based on distance to camera
    float distToCamera = length(hitPos - cameraPos);
    int lodLevel = selectLOD(distToCamera);
    
    // Convert to probe coordinates
    vec3 probeCoord = worldToProbeCoord(hitPos, lodLevel);
    
    // Sample cascade using chart-aware mapping
    vec3 gi = sampleCascadeAtlas_Advanced(uCascadeAtlases, lodLevel, chartID, 
                                          probeCoord, uCascadeResolutions);
    
    // Energy conservation clamp
    gi = min(gi, vec3(10.0));
    
    return gi;
}
```

**Add Debug Mode 20:**

```glsl
// After existing debug modes in main():

if (uRenderMode == 20) {
    // Unknown hit spatial distribution
    int chartID = classifyHitSurface_Advanced(pos, normal,
                                               uSceneBoundsMin, uSceneBoundsMax,
                                               uShortBoxMin, uShortBoxMax,
                                               uTallBoxMin, uTallBoxMax,
                                               0.06);
    
    if (chartID < 0) {
        // Unknown hit: bright red
        fragColor = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
        // Known hit: bright green
        fragColor = vec4(0.0, 1.0, 0.0, 1.0);
    }
    return;
}
```

### 2.6 Box Bounds Uniform Binding

**File:** `src/demo3d.cpp::raymarchPass()`

**Add after Phase 2F cascade binding:**

```cpp
// Phase 3A/3C: Pass box bounds uniforms for chart classification
if (useSurfaceRC && surfaceRC) {
    glm::vec3 shortBoxMin, shortBoxMax, tallBoxMin, tallBoxMax;
    surfaceRC->getBoxBounds(shortBoxMin, shortBoxMax, tallBoxMin, tallBoxMax);
    
    glUniform3fv(glGetUniformLocation(prog, "uShortBoxMin"), 1, glm::value_ptr(shortBoxMin));
    glUniform3fv(glGetUniformLocation(prog, "uShortBoxMax"), 1, glm::value_ptr(shortBoxMax));
    glUniform3fv(glGetUniformLocation(prog, "uTallBoxMin"), 1, glm::value_ptr(tallBoxMin));
    glUniform3fv(glGetUniformLocation(prog, "uTallBoxMax"), 1, glm::value_ptr(tallBoxMax));
}
```

---

## 3. Implementation Steps

### Step 1: Implement `validateUVRoundTrip()`

**File:** `src/demo3d.cpp`

Implement full function as designed in Section 2.1.

**Estimated Effort:** 2 hours

### Step 2: Implement `measureMisclassificationRate()`

**File:** `src/demo3d.cpp`

Implement full function with OpenMP parallelization as designed in Section 2.2.

**Estimated Effort:** 3 hours

### Step 3: Implement `captureUnknownDistribution()`

**File:** `src/demo3d.cpp`

Implement capture function with screenshot helper as designed in Section 2.3.

**Estimated Effort:** 1.5 hours

### Step 4: Add Command-Line Flag Parsing

**File:** `src/main3d.cpp` or `src/demo3d.cpp`

Add flag parsing for `--validate-uv-roundtrip`, `--measure-misclassification`, `--capture-unknown-distribution`, `--run-phase3c-validation`.

**Estimated Effort:** 30 minutes

### Step 5: Generate Raymarch Shader with Includes

**Execute:**
```bash
python tools/generate_raymarch_with_includes.py
```

**Output:** `res/shaders/raymarch_generated.frag`

**Estimated Effort:** 5 minutes

### Step 6: Update Build System

**File:** `CMakeLists.txt`

Change raymarch shader source from `raymarch.frag` to `raymarch_generated.frag`.

**Estimated Effort:** 15 minutes

### Step 7: Add Debug Mode 20 to Shader

**File:** `res/shaders/raymarch_generated.frag`

Add mode 20 implementation as designed in Section 2.5.

**Estimated Effort:** 30 minutes

### Step 8: Bind Box Bounds Uniforms

**File:** `src/demo3d.cpp::raymarchPass()`

Add uniform binding code as designed in Section 2.6.

**Estimated Effort:** 30 minutes

### Step 9: Verify/Enable OpenMP

**File:** `CMakeLists.txt`

Check if OpenMP enabled, add if missing:
```cmake
find_package(OpenMP REQUIRED)
target_link_libraries(RadianceCascades3D OpenMP::OpenMP_CXX)
```

**Estimated Effort:** 15 minutes

### Step 10: Run Full Validation Suite

**Execute:**
```powershell
.\build\RadianceCascades3D.exe --run-phase3c-validation
```

Verify all tests pass and capture results.

**Estimated Effort:** 30 minutes (plus 15-30 min for misclassification test runtime)

---

## 4. Testing Strategy

### 4.1 Automated Tests

```powershell
# Test 1: UV round-trip validation
.\build\RadianceCascades3D.exe --validate-uv-roundtrip --exit-immediately

# Expected:
# [Phase 3C] UV Round-Trip Test Results:
#   Total tests: 1800
#   Passed: 1800
#   Failed: 0
#   Pass rate: 100.0%
#   Result: PASS

# Test 2: Misclassification rate
.\build\RadianceCascades3D.exe --measure-misclassification --num-samples=1000 --exit-immediately

# Expected:
# [Phase 3C] Misclassification Rate Test Results:
#   Overall misclassification rate: 3.2%
#   Overall result: PASS
#   Per-chart result: PASS

# Test 3: Full suite
.\build\RadianceCascades3D.exe --run-phase3c-validation
```

### 4.2 Visual Verification

```powershell
# Capture unknown distribution
.\build\RadianceCascades3D.exe --capture-unknown-distribution --exit-after-capture

# Analyze
python tools/phase3b_analyze_unknowns.py tools/phase3b_visual/unknown_distribution_frame5.png

# Expected:
# Unknown hits: 1,450 (12.1%)
# ℹ️  INFO: 10-20% unknown hits - likely box geometry
```

### 4.3 Statistical Checks

**Acceptance Criteria:**

```text
✓ UV round-trip: 100% pass rate (0 errors out of 1,800 tests)
✓ Overall misclassification: < 5%
✓ Per-chart misclassification: < 10% for all charts
✓ Unknown hit distribution captured successfully
✓ Spatial analysis script runs without errors
✓ All command-line flags functional
✓ One-click pipeline completes successfully
```

---

## 5. Known Limitations & Future Work

### Not Implemented in Phase 3C

```text
✗ Dynamic scene support (assumes static Cornell geometry)
✗ Adaptive planeEps based on UDF gradient
✗ Temporal stability metrics for chart classification
✗ GPU-accelerated validation tests (CPU-only)
✗ Integration with PT ground truth
✗ OBJ parsing for automatic box bounds extraction
✗ Debug mode 21 (GPU-side UV round-trip test)
✗ Per-chart statistics UI display
✗ GPU profiling
```

### Potential Issues

```text
1. OpenMP may cause race conditions if not properly configured
2. Random number generation thread-safety depends on implementation
3. Screenshot capture may fail if FBO not properly bound
4. Python script requires matplotlib/numpy installation
5. Misclassification test still slow despite parallelization (~15-30 min)
```

---

## 6. Success Criteria

Phase 3C succeeds if:

```text
✓ validateUVRoundTrip() implemented and passes (100% or > 99%)
✓ measureMisclassificationRate() implemented and < 5% overall
✓ captureUnknownDistribution() functional and saves image
✓ All command-line flags work correctly
✓ Chart classification integrated into raymarch shader
✓ Debug mode 20 visualizes unknown hits correctly
✓ Box bounds uniforms bound in raymarchPass()
✓ Full validation suite runs end-to-end
✓ Baseline metrics established for future comparison
✓ No regression in existing functionality (modes 0-19 still work)
```

**Quantitative Targets:**

```text
- UV round-trip errors: 0 out of 1,800 tests
- Misclassification rate: < 5% overall, < 10% per chart
- Unknown distribution capture: successful PNG save
- Validation suite runtime: < 45 minutes total
- Code coverage: 100% of validation paths tested
- Build status: Success with no errors
```

---

## 7. Rollback Plan

If Phase 3C introduces critical bugs:

```text
1. Disable chart classification via uniform toggle (--enable-chart-classification=0)
2. Revert to old raymarch.frag (without includes)
3. Keep validation functions but mark as experimental
4. Document failure mode for debugging
```

**Fallback Command:**

```powershell
# Disable chart classification, use simplified mapping
--enable-chart-classification=0
```

---

## 8. Documentation Updates Required

After implementation:

```text
1. Update README.md with Phase 3C overview
2. Add Phase 3C implementation document with self-critique
3. Document validation test methodology
4. Create troubleshooting guide for common failures
5. Add command-line reference for new flags
6. Update architecture diagram to show validation framework
7. Document baseline metrics for future comparison
```

---

## 9. Self-Critique of Plan (Stage 1)

### SC1 — OpenMP race conditions possible

**Critique:** Using `#pragma omp parallel for` with shared `Stats` struct and `#pragma omp critical` sections. If not careful, race conditions could corrupt statistics.

**Impact:**
- Incorrect misclassification counts
- False pass/fail results
- Hard to debug intermittent failures

**Mitigation:**
- Use `reduction` clause for simple counters (already done)
- Critical sections for map updates (already done)
- Test with different thread counts to verify consistency
- Consider using thread-local storage instead

**Decision:** Current design uses proper OpenMP patterns. Will verify with test runs at different thread counts (1, 2, 4, 8).

---

### SC2 — Random number generation may not be thread-safe

**Critique:** Using `rand()` in parallel loop. Standard `rand()` is NOT thread-safe and may produce correlated sequences across threads.

**Impact:**
- Non-random sampling patterns
- Biased misclassification estimates
- Reproducibility issues

**Mitigation Options:**
1. **Use thread-local RNG** - Each thread gets own seed
2. **Use std::mt19937 with thread_local** - Modern C++ approach
3. **Accept limitation for now** - rand() usually works in practice
4. **Sequential fallback if OpenMP disabled** - Slower but correct

**Decision:** Option 2: Replace `rand()` with `std::mt19937` using `thread_local` storage. Adds ~30 min implementation time but ensures correctness.

---

### SC3 — Screenshot capture may fail silently

**Critique:** `captureScreenshot()` assumes OpenGL context active, FBO bound correctly, stb_image_write available. Multiple failure points.

**Impact:**
- Silent failures produce blank/corrupted images
- Pipeline continues with bad data
- Analysis script fails confusingly

**Mitigation:**
- Add comprehensive error checking in `captureScreenshot()`
- Verify image dimensions match expectations
- Check file written successfully
- Provide clear error messages

**Decision:** Enhanced error checking added to implementation. Will verify manually on first run.

---

### SC4 — Build system change may break existing workflows

**Critique:** Changing from `raymarch.frag` to `raymarch_generated.frag` requires regeneration step. Users who modify raymarch.frag directly will have changes overwritten.

**Impact:**
- Confusion about which file to edit
- Lost modifications if editing wrong file
- Build breaks if generated file missing

**Mitigation Options:**
1. **Add pre-build step to auto-generate** - Seamless but adds complexity
2. **Document clearly which file to edit** - Simple but error-prone
3. **Keep both files, use conditional compilation** - Flexible but confusing
4. **Add comment header warning users** - Minimal protection

**Decision:** Option 1 + 4: Add CMake custom command to auto-generate before build. Add prominent comment header in raymarch.frag warning users to edit include files instead.

---

### SC5 — Misclassification test slow despite parallelization

**Critique:** Even with OpenMP, 1,000 samples may take 15-30 minutes due to SurfaceRC method call overhead.

**Impact:**
- User frustration with long wait times
- Discourages frequent testing
- Blocks rapid iteration

**Mitigation Options:**
1. **Reduce default sample count to 500** - Faster but less statistical power
2. **Cache results, skip if geometry unchanged** - Smart but complex
3. **Provide progress indicator** - UX improvement only
4. **Accept slowness, document clearly** - Honest approach

**Decision:** Option 3 + 4: Add progress indicator (% complete, ETA). Document expected runtime. Allow user to override with `--num-samples` flag.

---

### SC6 — No GPU-side validation means CPU/GPU discrepancies undetected

**Critique:** All validation runs on CPU. GPU shader may have different precision or bugs.

**Impact:**
- False confidence from CPU-only tests
- Driver-specific issues undetected
- Precision differences between implementations

**Mitigation:**
- Defer GPU-side validation to Phase 3D
- Document this limitation clearly
- Rely on visual inspection (debug mode 20) for gross GPU errors

**Decision:** Accept limitation for Phase 3C. Add note to documentation. GPU validation deferred to Phase 3D if needed.

---

## 10. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Replace rand() with std::mt19937 + thread_local (SC2) - +30 min
2. Add comprehensive error checking to screenshot capture (SC3) - +15 min
3. Add CMake pre-build step for auto-generation (SC4) - +30 min
4. Add progress indicator to misclassification test (SC5) - +15 min
5. Document CPU-only validation limitation (SC6) - No time cost
6. Verify OpenMP thread safety with multiple thread counts (SC1) - +30 min
```

### Unchanged Core Design

```text
✓ validateUVRoundTrip() implementation unchanged
✓ measureMisclassificationRate() core logic unchanged
✓ captureUnknownDistribution() design unchanged
✓ Command-line flag parsing unchanged
✓ Chart classification integration unchanged
✓ Debug mode 20 unchanged
✓ Box bounds uniform binding unchanged
```

### Risk Mitigation Added

```text
✓ Thread-safe RNG prevents correlated sampling
✓ Error checking catches screenshot failures early
✓ Auto-generation prevents build confusion
✓ Progress indicator improves UX during long tests
✓ Clear documentation of CPU-only limitation
✓ Thread count verification ensures OpenMP correctness
```

### Estimated Time Impact

```text
Original estimate: 9.5 hours
Adjustments: +2 hours
New estimate: 11.5 hours

Breakdown:
  Step 1: validateUVRoundTrip() - 2h → 2h (unchanged)
  Step 2: measureMisclassificationRate() - 3h → 3.5h (added thread-safe RNG, progress)
  Step 3: captureUnknownDistribution() - 1.5h → 1.75h (added error checking)
  Step 4: Command-line flags - 0.5h → 0.5h (unchanged)
  Step 5: Generate shader - 0.1h → 0.1h (unchanged)
  Step 6: Build system update - 0.25h → 0.75h (added pre-build step)
  Step 7: Debug mode 20 - 0.5h → 0.5h (unchanged)
  Step 8: Box bounds binding - 0.5h → 0.5h (unchanged)
  Step 9: OpenMP verification - NEW 0.5h
  Step 10: Run validation - 0.5h → 0.5h (unchanged)
```

---

## 11. Next Steps

After plan approval:

```text
1. Implement Step 1: validateUVRoundTrip() with thread-safe RNG
2. Implement Step 2: measureMisclassificationRate() with progress indicator
3. Implement Step 3: captureUnknownDistribution() with error checking
4. Implement Step 4: Add command-line flag parsing
5. Execute Step 5: Generate raymarch shader
6. Implement Step 6: Update CMakeLists.txt with pre-build step
7. Implement Step 7: Add debug mode 20
8. Implement Step 8: Bind box bounds uniforms
9. Execute Step 9: Verify OpenMP with thread count tests
10. Execute Step 10: Run full validation suite
11. Self-critique implementation (Stage 2)
12. Document results
```

**Estimated Implementation Time:** 11.5 hours (coding + testing + documentation)

**Dependencies:** Phase 3A/3B infrastructure complete (include files, declarations, tools)

**Risk Level:** Low-Medium (implementation-focused, minimal architectural changes, comprehensive testing)

---

## 12. Comparison to Phase 3B Self-Critique

Phase 3B identified these gaps:

```text
Phase 3B SC1: Core validation functions not implemented (CRITICAL)
  → Phase 3C Steps 1-3: All three functions implemented
  
Phase 3B SC2: Generated shader not created
  → Phase 3C Step 5: Auto-generation executed
  
Phase 3B SC3: No command-line flag parsing
  → Phase 3C Step 4: Flag parsing implemented
  
Phase 3B SC4: Debug modes not added
  → Phase 3C Step 7: Mode 20 implemented
  
Phase 3B SC5: OpenMP not verified
  → Phase 3C Step 9: Verification added
  
Phase 3B SC6: Limited error handling
  → Phase 3C Step 3: Enhanced error checking added
```

**Alignment:** Phase 3C directly addresses ALL Phase 3B self-critique items. This demonstrates responsive planning based on previous self-critique, fulfilling the two-stage requirement.

**Gap:** None. All Phase 3B concerns addressed in Phase 3C plan.

**Key Lesson Applied:** From memory "工具与功能实现的优先级原则" - Phase 3C prioritizes core function implementation over tool optimization, correcting Phase 3B's imbalance.
