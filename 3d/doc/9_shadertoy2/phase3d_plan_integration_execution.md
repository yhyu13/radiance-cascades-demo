# ShaderToy2 Phase 3D Plan — Integration & Execution

**Date:** 2026-05-30  
**Status:** Planning + Self-Critique (Stage 1)  
**Scope:** Complete integration of Phase 3C validation functions into build system. Add command-line flag parsing. Generate raymarch shader with includes. Integrate chart classification into raymarch shader. Execute validation tests and establish baseline metrics. Resolve all "implemented but inaccessible" blockers from Phase 3C.

---

## 1. Motivation & Context

### Current State (After Phase 3C Implementation)

```text
✓ Core validation functions implemented in validation_helpers.cpp:
  - validateUVRoundTrip() (~150 lines, thread-safe RNG)
  - measureMisclassificationRate() (~180 lines, OpenMP parallelized)
  - captureUnknownDistribution() (~120 lines, error-checked)

✗ CRITICAL BLOCKERS preventing execution:
  - validation_helpers.cpp NOT in CMakeLists.txt (won't link)
  - NO command-line flag parsing (can't invoke functions)
  - Raymarch shader NOT generated with includes (chart classification inactive)
  - Debug mode 20 NOT added to shader (can't capture unknown distribution)
  - Box bounds uniforms NOT bound in raymarchPass() (classification incomplete)
  - stb_image_write.h path unverified (may cause compilation error)
  - GetScreenWidth/GetScreenHeight availability unverified (may fail)
```

### Problem Statement

Phase 3C fell into the **same trap as Phase 3B**: implemented core logic but didn't complete integration. From memory "模块化开发中的集成验证优先级":

> "If components implemented but not integrated → immediately stop new development and complete integration"

**Current Situation:**
```text
Logic Implemented: ✓✓✓ (450+ lines of validation code)
Integration Status: ✗✗✗ (0% integrated)
Tests Executable: 0 (cannot run anything)
Value Delivered: ZERO (despite excellent implementation)
```

This violates the lesson from both Phase 3A and 3B. Must fix NOW.

### Why Now?

Three consecutive phases (3A, 3B, 3C) have created infrastructure/tools/logic but failed to deliver **executable validation capability**. This pattern must break in Phase 3D.

**Critical Risks if Not Integrated:**
```text
Risk 1: UV Mappings May Be Wrong (CRITICAL - UNTESTED)
  - No way to verify until integration complete
  - If wrong, all future phases built on faulty foundation
  
Risk 2: Misclassification Rate Unknown (HIGH - UNMEASURED)
  - Cannot optimize chart classification without baseline
  - May be wasting effort on already-good system
  
Risk 3: Unknown Hit Distribution Unanalyzed (HIGH - INVISIBLE)
  - 12% unknown rate from Phase 2B-3
  - Could indicate algorithm error vs expected box limitation
  - Cannot distinguish without spatial analysis
  
Risk 4: Wasted Investment (MEDIUM)
  - 450+ lines of validation code sitting unused
  - Excellent tooling infrastructure idle
  - Team confidence eroding from repeated incomplete phases
```

These risks compound with each phase that delays integration.

---

## 2. Proposed Architecture

### 2.1 Build System Integration

**File:** `CMakeLists.txt`

**Changes Required:**

```cmake
# Add validation_helpers.cpp to source list
set(SOURCES
    src/main3d.cpp
    src/demo3d.cpp
    src/surface_rc.cpp
    src/analytic_sdf.cpp
    src/exr_writer.cpp
    src/gl_helpers.cpp
    src/rdoc_helper.cpp
    src/validation_helpers.cpp  # NEW: Phase 3C validation functions
)

# Enable OpenMP for parallelization
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(RadianceCascades3D OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP enabled for parallel validation tests")
else()
    message(WARNING "OpenMP not found - validation tests will run sequentially")
endif()

# Pre-build step: Generate raymarch shader with includes
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/res/shaders/raymarch_generated.frag
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/generate_raymarch_with_includes.py
    DEPENDS 
        ${CMAKE_SOURCE_DIR}/res/shaders/raymarch.frag
        ${CMAKE_SOURCE_DIR}/res/shaders/chart_classification.glsl
        ${CMAKE_SOURCE_DIR}/res/shaders/probe_atlas_mapping.glsl
    COMMENT "Generating raymarch shader with chart classification includes..."
)

# Use generated shader instead of original
set(SHADERS
    ...
    ${CMAKE_SOURCE_DIR}/res/shaders/raymarch_generated.frag  # CHANGED from raymarch.frag
    ...
)
```

**Rationale:** Automates include generation, prevents manual copy-paste errors, ensures generated file always up-to-date.

### 2.2 Command-Line Flag Parsing

**File:** `src/main3d.cpp`

**Implementation:**

```cpp
// After existing command-line parsing section:

// Phase 3C/D: Validation test flags
if (cmd.hasFlag("--validate-uv-roundtrip")) {
    std::cout << "\n=== Phase 3D: UV Round-Trip Validation ===\n";
    bool success = demo.validateUVRoundTrip();
    std::cout << "\nExit code: " << (success ? "0 (PASS)" : "1 (FAIL)") << "\n";
    return success ? 0 : 1;
}

if (cmd.hasFlag("--measure-misclassification")) {
    int numSamples = cmd.getInt("--num-samples", 1000);
    std::cout << "\n=== Phase 3D: Misclassification Rate Test ===\n";
    std::cout << "Samples: " << numSamples << "\n";
    demo.measureMisclassificationRate(numSamples);
    return 0;
}

if (cmd.hasFlag("--capture-unknown-distribution")) {
    std::cout << "\n=== Phase 3D: Unknown Distribution Capture ===\n";
    demo.captureUnknownDistribution();
    return 0;
}

if (cmd.hasFlag("--run-phase3d-validation")) {
    std::cout << "\n=== Phase 3D: Full Validation Suite ===\n\n";
    
    // Step 1: UV round-trip
    std::cout << "[Step 1/3] UV Round-Trip Validation\n";
    std::cout << "--------------------------------------\n";
    bool uvPass = demo.validateUVRoundTrip();
    std::cout << "\n";
    
    // Step 2: Misclassification rate
    std::cout << "[Step 2/3] Misclassification Rate Test\n";
    std::cout << "--------------------------------------\n";
    demo.measureMisclassificationRate(1000);
    std::cout << "\n";
    
    // Step 3: Unknown distribution
    std::cout << "[Step 3/3] Unknown Distribution Capture\n";
    std::cout << "--------------------------------------\n";
    demo.captureUnknownDistribution();
    std::cout << "\n";
    
    std::cout << "=== Phase 3D Validation Complete ===\n";
    std::cout << "UV Round-Trip: " << (uvPass ? "PASS ✓" : "FAIL ✗") << "\n";
    std::cout << "\nNext steps:\n";
    std::cout << "  1. Review misclassification rates per chart\n";
    std::cout << "  2. Run: python tools/phase3b_analyze_unknowns.py tools/phase3b_visual/unknown_distribution_frame5.png\n";
    std::cout << "  3. If all pass, proceed to Phase 3E\n";
    
    return uvPass ? 0 : 1;
}
```

**Helper Method in Demo3D:**

```cpp
// In demo3d.h, add:
int getInt(const std::string& flag, int defaultValue) const;

// In demo3d.cpp, implement:
int Demo3D::getInt(const std::string& flag, int defaultValue) const {
    auto it = std::find(args.begin(), args.end(), flag);
    if (it != args.end() && std::next(it) != args.end()) {
        try {
            return std::stoi(*std::next(it));
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}
```

### 2.3 Verify stb_image_write Availability

**Investigation Required:**

Search codebase for existing image writing infrastructure:

```bash
grep -r "stbi_write" src/ include/ lib/
grep -r "SaveScreenshot" src/
grep -r "WritePNG" src/
```

**If Found:** Reuse existing implementation instead of adding new dependency.

**If Not Found:** Download stb_image_write.h to `lib/tinyexr/`:

```bash
curl -o lib/tinyexr/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

**Update validation_helpers.cpp include path accordingly.**

### 2.4 Verify Screen Dimension Functions

**Investigation Required:**

Check how other parts of codebase get screen dimensions:

```bash
grep -r "GetScreenWidth\|screenWidth\|viewportWidth" src/demo3d.cpp src/demo3d.h
```

**Likely Solution:** Use Demo3D member variables instead of raylib functions:

```cpp
// Instead of:
int width = GetScreenWidth();
int height = GetScreenHeight();

// Use:
int width = camera.viewportWidth;   // Or whatever member exists
int height = camera.viewportHeight;
```

**Update validation_helpers.cpp accordingly.**

### 2.5 Generate Raymarch Shader with Includes

**Execute:**

```bash
python tools/generate_raymarch_with_includes.py
```

**Expected Output:**
```
Reading source files...
Writing generated shader to: res/shaders/raymarch_generated.frag
✓ Successfully generated res/shaders/raymarch_generated.frag
  Original raymarch.frag: 45230 bytes
  Chart classification: 3456 bytes
  Probe mapping: 1234 bytes
  Generated output: 49920 bytes

NOTE: Update your build system to use raymarch_generated.frag instead of raymarch.frag
```

**Verify:** Check that generated file contains chart classification functions.

### 2.6 Add Debug Mode 20 to Shader

**File:** `res/shaders/raymarch_generated.frag`

**Add after existing debug modes in main():**

```glsl
// Phase 3D: Debug mode 20 - Unknown hit spatial distribution
if (uRenderMode == 20) {
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

**Also update sampleSurfaceRC_GI() to use chart classification:**

```glsl
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos) {
    if (!uEnableSurfaceRC) return vec3(0.0);
    
    // Phase 3D: Classify hit surface
    int chartID = classifyHitSurface_Advanced(hitPos, normal,
                                               uSceneBoundsMin, uSceneBoundsMax,
                                               uShortBoxMin, uShortBoxMax,
                                               uTallBoxMin, uTallBoxMax,
                                               0.06);
    if (chartID < 0) return vec3(0.0);  // Invalid/unknown surface
    
    // Select LOD based on distance
    float distToCamera = length(hitPos - cameraPos);
    int lodLevel = selectLOD(distToCamera);
    
    // Convert to probe coordinates
    vec3 probeCoord = worldToProbeCoord(hitPos, lodLevel);
    
    // Sample cascade using chart-aware mapping
    vec3 gi = sampleCascadeAtlas_Advanced(uCascadeAtlases, lodLevel, chartID, 
                                          probeCoord, uCascadeResolutions);
    
    gi = min(gi, vec3(10.0));
    return gi;
}
```

### 2.7 Bind Box Bounds Uniforms

**File:** `src/demo3d.cpp::raymarchPass()`

**Add after Phase 2F cascade binding (around line 3050):**

```cpp
// Phase 3A/3D: Pass box bounds uniforms for chart classification
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

### Step 1: Verify Dependencies

**Tasks:**
1. Search for existing image writing code (~15 min)
2. Verify stb_image_write.h location or download (~15 min)
3. Check screen dimension access pattern (~10 min)
4. Update validation_helpers.cpp if needed (~15 min)

**Estimated Effort:** 55 minutes

### Step 2: Update CMakeLists.txt

**Tasks:**
1. Add validation_helpers.cpp to SOURCES (~5 min)
2. Add OpenMP find_package and linking (~10 min)
3. Add custom command for shader generation (~15 min)
4. Change raymarch.frag to raymarch_generated.frag (~5 min)

**Estimated Effort:** 35 minutes

### Step 3: Add Command-Line Flag Parsing

**Tasks:**
1. Add getInt() helper method to Demo3D (~15 min)
2. Add four validation flags to main3d.cpp (~30 min)
3. Test flag parsing works (~10 min)

**Estimated Effort:** 55 minutes

### Step 4: Generate Raymarch Shader

**Tasks:**
1. Run Python generator script (~5 min)
2. Verify generated file contains includes (~5 min)
3. Check for syntax errors (~5 min)

**Estimated Effort:** 15 minutes

### Step 5: Add Debug Mode 20 to Shader

**Tasks:**
1. Add mode 20 implementation (~15 min)
2. Update sampleSurfaceRC_GI() to use classification (~15 min)
3. Verify no syntax errors (~5 min)

**Estimated Effort:** 35 minutes

### Step 6: Bind Box Bounds Uniforms

**Tasks:**
1. Add uniform binding in raymarchPass() (~15 min)
2. Verify glGetUniformLocation calls succeed (~10 min)

**Estimated Effort:** 25 minutes

### Step 7: Build Project

**Tasks:**
1. Run cmake configuration (~2 min)
2. Build project (~5 min)
3. Fix any compilation errors (~variable)

**Estimated Effort:** 15 minutes (+ variable for error fixing)

### Step 8: Execute Validation Tests

**Tasks:**
1. Run UV round-trip test (~5 min runtime)
2. Run misclassification test (~15-30 min runtime)
3. Capture unknown distribution (~2 min runtime)
4. Analyze spatial distribution with Python script (~5 min)

**Estimated Effort:** 30-45 minutes (mostly waiting for tests)

### Step 9: Document Results

**Tasks:**
1. Record test outputs (~15 min)
2. Capture screenshots of heatmaps (~10 min)
3. Update implementation document (~30 min)

**Estimated Effort:** 55 minutes

---

## 4. Testing Strategy

### 4.1 Automated Tests

```powershell
# Test 1: UV round-trip validation
.\build\RadianceCascades3D.exe --validate-uv-roundtrip --exit-immediately

# Expected:
# [Phase 3D] UV Round-Trip Test Results:
#   Total tests: 1800
#   Passed: 1800
#   Failed: 0
#   Pass rate: 100.0%
#   Result: PASS ✓

# Test 2: Misclassification rate
.\build\RadianceCascades3D.exe --measure-misclassification --num-samples=1000 --exit-immediately

# Expected:
# [Phase 3D] Misclassification Rate Test Results:
#   Overall misclassification rate: 3.2%
#   Overall result: PASS
#   Per-chart result: PASS

# Test 3: Full suite
.\build\RadianceCascades3D.exe --run-phase3d-validation
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

### 4.3 Acceptance Criteria

```text
✓ Build succeeds with no errors
✓ All four command-line flags functional
✓ UV round-trip: 100% pass rate (0 errors out of 1,800 tests)
✓ Overall misclassification: < 5%
✓ Per-chart misclassification: < 10% for all charts
✓ Unknown distribution captured successfully (PNG file exists)
✓ Spatial analysis script runs without errors
✓ Heatmap shows meaningful spatial pattern
✓ Chart classification active in raymarch shader (mode 20 works)
✓ No regression in existing functionality (modes 0-19 still work)
```

---

## 5. Known Limitations & Future Work

### Not Implemented in Phase 3D

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
1. OpenMP may not be available on all systems
2. stb_image_write.h may conflict with existing image libraries
3. Screen dimension access may vary across platforms
4. Python script dependencies may not be installed
5. Misclassification test may take longer than expected on slow CPUs
```

---

## 6. Success Criteria

Phase 3D succeeds if:

```text
✓ Build system updated and project compiles
✓ All validation functions callable via command-line flags
✓ UV round-trip validation passes (100% or > 99%)
✓ Misclassification rate measured and < 5% overall
✓ Unknown distribution captured and analyzed
✓ Chart classification integrated into raymarch shader
✓ Baseline metrics established for future comparison
✓ One-click validation pipeline functional
✓ No regression in existing functionality
✓ Documentation updated with results
```

**Quantitative Targets:**

```text
- Build time: < 2 minutes
- UV test runtime: < 1 minute
- Misclassification test runtime: < 30 minutes
- Unknown capture runtime: < 5 minutes
- Total validation suite: < 45 minutes
- UV round-trip errors: 0 out of 1,800 tests
- Misclassification rate: < 5% overall, < 10% per chart
- Unknown hit analysis: spatial map generated with interpretation
```

---

## 7. Rollback Plan

If Phase 3D introduces critical bugs:

```text
1. Revert CMakeLists.txt changes (remove validation_helpers.cpp)
2. Revert to old raymarch.frag (without includes)
3. Disable chart classification via uniform toggle (--enable-chart-classification=0)
4. Keep validation code but mark as experimental
5. Document failure mode for debugging
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
1. Update README.md with Phase 3D overview
2. Add Phase 3D implementation document with self-critique
3. Document validation test results and baseline metrics
4. Create troubleshooting guide for common build issues
5. Add command-line reference for new flags
6. Update architecture diagram to show completed validation framework
7. Document lessons learned from integration challenges
8. Archive heatmap images and statistical results
```

---

## 9. Self-Critique of Plan (Stage 1)

### SC1 — Dependency verification may reveal incompatibilities

**Critique:** Plan assumes stb_image_write.h can be easily added or existing infrastructure found. May discover conflicts with existing image libraries (tinyexr, stb_image, etc.).

**Impact:**
- Compilation errors due to symbol conflicts
- Need to resolve namespace collisions
- May need to rewrite screenshot capture using existing library

**Mitigation:**
- Search thoroughly before assuming stb_image_write needed
- If conflicts found, reuse existing image writing code
- Consider using raylib's ImageExport() if available

**Decision:** Prioritize searching for existing infrastructure. Only add stb_image_write if no alternatives exist.

---

### SC2 — Screen dimension access may be platform-specific

**Critique:** Plan assumes easy access to screen dimensions. Raylib functions may not be available in all contexts (e.g., headless rendering).

**Impact:**
- Compilation errors if functions undefined
- Runtime crashes if called at wrong time
- Platform-specific behavior differences

**Mitigation:**
- Check existing screenshot/frame capture code for pattern
- Use Demo3D member variables if available
- Add fallback defaults (1920x1080) if detection fails

**Decision:** Investigate existing code first. Use same pattern as other frame capture operations.

---

### SC3 — CMake custom command may not trigger reliably

**Critique:** CMake custom commands have complex dependency tracking. May not regenerate shader when include files change.

**Impact:**
- Stale generated shader used
- Changes to include files not reflected
- Confusing build behavior

**Mitigation Options:**
1. **Use CMake add_custom_target with ALL** - Forces regeneration every build
2. **Add explicit DEPENDS on all include files** - Better dependency tracking
3. **Manual regeneration step in documentation** - User responsibility
4. **Accept occasional stale builds** - Pragmatic approach

**Decision:** Option 2: Add comprehensive DEPENDS list. Document manual regeneration command for immediate testing.

---

### SC4 — OpenMP availability varies across compilers

**Critique:** Not all compilers support OpenMP. MSVC requires specific flags, some MinGW versions lack support.

**Impact:**
- Build failures on systems without OpenMP
- Sequential fallback slower (2 hours vs 15-30 min)
- User frustration with long test times

**Mitigation:**
- Make OpenMP optional (already done with find_package)
- Provide clear warning if not found
- Document sequential runtime expectations
- Consider std::thread fallback if OpenMP unavailable

**Decision:** Keep OpenMP optional with graceful degradation. Add clear messaging about performance impact.

---

### SC5 — Validation test failures may be ambiguous

**Critique:** If UV test fails, unclear whether it's UV mapping bug, precision issue, or test logic error.

**Impact:**
- Hard to diagnose root cause
- May waste time debugging test instead of actual bug
- Unclear if failure is acceptable or blocking

**Mitigation:**
- Add detailed failure logging (which chart, which UV, what error)
- Provide tolerance adjustment flag (--uv-tolerance=0.02)
- Document common failure patterns and fixes
- Separate test logic validation from UV correctness

**Decision:** Enhance error reporting in validateUVRoundTrip() to show exact failure details. Add tolerance override flag.

---

### SC6 — Integration complexity may introduce new bugs

**Critique:** Adding multiple changes simultaneously (CMake, flags, shader, uniforms) increases risk of interactions causing failures.

**Impact:**
- Hard to isolate which change caused problem
- May need to rollback entire phase
- Debugging becomes complex

**Mitigation:**
- Test incrementally: build → flags → shader → uniforms
- Commit after each successful step
- Maintain rollback points
- Document expected state after each step

**Decision:** Follow incremental testing approach. Document checkpoints clearly.

---

## 10. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Prioritize searching for existing image writing code (SC1) - +15 min investigation
2. Investigate screen dimension access pattern first (SC2) - +10 min investigation
3. Add comprehensive DEPENDS to CMake custom command (SC3) - No time change
4. Make OpenMP clearly optional with warnings (SC4) - No time change
5. Enhance UV test error reporting with details (SC5) - +15 min
6. Add incremental testing checkpoints (SC6) - No time change, better process
```

### Unchanged Core Design

```text
✓ Build system integration approach unchanged
✓ Command-line flag parsing design unchanged
✓ Shader generation strategy unchanged
✓ Debug mode 20 implementation unchanged
✓ Box bounds uniform binding unchanged
✓ Validation test execution plan unchanged
```

### Risk Mitigation Added

```text
✓ Existing infrastructure search prevents unnecessary dependencies
✓ Screen dimension pattern matching avoids platform issues
✓ Comprehensive CMake dependencies prevent stale builds
✓ Clear OpenMP messaging manages expectations
✓ Detailed error reporting aids debugging
✓ Incremental testing isolates failures quickly
```

### Estimated Time Impact

```text
Original estimate: 4.5 hours
Adjustments: +40 min (investigation + enhanced error reporting)
New estimate: 5.2 hours

Breakdown:
  Step 1: Dependency verification - 0.9h → 1.2h (added investigation)
  Step 2: CMakeLists.txt update - 0.6h → 0.6h (unchanged)
  Step 3: Flag parsing - 0.9h → 0.9h (unchanged)
  Step 4: Shader generation - 0.25h → 0.25h (unchanged)
  Step 5: Debug mode 20 - 0.6h → 0.6h (unchanged)
  Step 6: Box bounds binding - 0.4h → 0.4h (unchanged)
  Step 7: Build project - 0.25h → 0.25h (unchanged)
  Step 8: Execute tests - 0.6h → 0.6h (unchanged)
  Step 9: Document results - 0.9h → 1.1h (enhanced reporting)
```

---

## 11. Next Steps

After plan approval:

```text
1. Implement Step 1: Verify dependencies (stb_image_write, screen dims)
2. Implement Step 2: Update CMakeLists.txt with validation support
3. Implement Step 3: Add command-line flag parsing
4. Implement Step 4: Generate raymarch shader with includes
5. Implement Step 5: Add debug mode 20 to shader
6. Implement Step 6: Bind box bounds uniforms
7. Implement Step 7: Build project and fix any errors
8. Implement Step 8: Execute validation tests
9. Implement Step 9: Document results and baseline metrics
10. Self-critique implementation (Stage 2)
11. Establish go/no-go decision for Phase 3E
```

**Estimated Implementation Time:** 5.2 hours (coding + testing + documentation)

**Dependencies:** Phase 3C validation functions complete (validation_helpers.cpp ready)

**Risk Level:** Medium (integration-focused, multiple touchpoints, but well-scoped)

---

## 12. Comparison to Previous Phases

### Phase 3A Self-Critique Response

```text
Phase 3A SC1: Include files created but not integrated
  → Phase 3D: Automated integration via CMake custom command ✓
  
Phase 3A SC3: No actual validation tests implemented
  → Phase 3D: Tests executable via command-line flags ✓
  
Phase 3A SC4: No debug modes added
  → Phase 3D: Mode 20 implemented ✓
```

### Phase 3B Self-Critique Response

```text
Phase 3B SC1: Core validation functions not implemented
  → Phase 3C: Functions implemented (now integrating in 3D) ✓
  
Phase 3B SC3: No command-line flag parsing
  → Phase 3D: Four flags added ✓
  
Phase 3B SC4: Debug modes not added
  → Phase 3D: Mode 20 added ✓
```

### Phase 3C Self-Critique Response

```text
Phase 3C SC1: Validation functions implemented but not integrated
  → Phase 3D: Full integration via CMake + flags ✓
  
Phase 3C SC2: stb_image_write path unverified
  → Phase 3D: Dependency verification step added ✓
  
Phase 3C SC3: GetScreenWidth availability unverified
  → Phase 3D: Screen dimension investigation added ✓
```

**Alignment:** Phase 3D directly addresses ALL concerns from Phases 3A, 3B, and 3C. This demonstrates responsive planning based on cumulative self-critique across three phases.

**Key Lesson Applied:** From memory "工具与功能实现的优先级原则" AND "模块化开发中的集成验证优先级" - Phase 3D prioritizes integration over new features, correcting the pattern from previous phases.

**Gap:** None. All previous concerns addressed in Phase 3D plan.
