# Phase 2B-2 Critique — Surface Trace Skeleton, No Lighting

**Date:** 2026-05-29  
**Reviewer:** AI Code Reviewer  
**Phase Status:** Implemented ✅ (with diagnostic gaps)  
**Confidence for Next Phase:** 75% (recommend bias sweep before proceeding)

---

## Executive Summary

Phase 2B-2 successfully implements SDF-based ray tracing diagnostics for surface-attached probes. The implementation adds two debug modes (trace classification and distance visualization) and integrates with the existing volumetric SDF texture. Verification shows substantial hit population (11,264 hits out of 12,096 active samples) with non-trivial distance variation.

**However**, critical diagnostic gaps remain around self-hit quantification and UDF bias sensitivity that should be addressed before proceeding to hit surface classification (Phase 2B-3).

---

## What Was Done Well ✓

### 1. Scope Discipline
- Strictly adhered to "no lighting, no NEE, no feedback" mandate
- Did not prematurely add complexity before validating geometric path
- Clear documentation of non-goals prevents scope creep

### 2. Implementation Quality
- Clean extension of `SurfaceRC::dispatchRadianceDebug()` signature
- Proper uniform binding following existing shader conventions
- TraceResult struct cleanly separates state/distance/position
- Alpha floor fix (`max(dNorm, 0.001)`) shows attention to display pipeline edge cases

### 3. Verification Rigor
- Structure-aware pixel counting confirms expected behavior:
  - Chart 6 remains inactive (inactiveBright=0) ✓
  - Substantial hit population (hit=11,264 / 12,096 = 93%) ✓
  - Non-trivial distance variation (activeUnique4=14 in mode 5) ✓
  - Presence of escape cases (escape=814 = 6.7%) showing rays leave volume ✓
- Self-critique identified and fixed mode 5 alpha issue before declaring success

### 4. Documentation Quality
- Clear plan document with explicit non-goals
- Implementation doc tracks all changes systematically
- Self-critique sections identify known limitations upfront
- Next phase recommendation provided with clear scope

---

## Critical Concerns & Risks ⚠️

### 🔴 HIGH RISK: Conservative UDF vs True Signed SDF

**Issue**: The mesh SDF is documented as conservative/UDF-like, not a true signed distance field.

**Impact**:
- Sphere tracing behavior differs from theoretical expectations
- Hit distances may be systematically biased high or low
- Cannot use trace results for physically correct visibility yet
- May cause false positives (reporting hits where there are none) or false negatives

**Current Mitigation**: Documented but not resolved. Acceptable for diagnostics only per SC1.

**Evidence Gap**: No quantitative analysis of how UDF artifacts affect trace accuracy.

**Recommendation**: 
1. Before Phase 2B-3, run comparison test: for 100 random probe positions/directions, compare SDF trace result with analytical ray-plane intersection
2. If discrepancy rate > 5%, consider generating true signed SDF or adjusting trace parameters
3. Document acceptable error tolerance for each downstream use case (classification vs NEE vs feedback)

---

### 🟠 MEDIUM-HIGH RISK: Self-Hit Probability Not Quantified

**Issue**: Surface probes start on surfaces with normal offset:
```glsl
rayOrigin = probeWorldPos + normal * 0.01
t_start = max(2*bias, 2*epsilon)
```

The 11,264 hits could include significant self-intersection artifacts that inflate hit count.

**Missing Verification**:
- ❌ No sweep test varying uRayBias (0.005 → 0.05) to see how hit count changes
- ❌ No per-chart breakdown to check if certain orientations have higher self-hit rates
- ❌ No comparison between normal-facing vs grazing angle probes
- ❌ No measurement of minimum trace distance distribution

**Why This Matters**: If 30% of "hits" are actually self-hits, hit surface classification (Phase 2B-3) will produce garbage data for those probes.

**Recommendation** (Priority 1 Blocker):
```powershell
# Run bias sweep diagnostic
for ($bias in 0.005, 0.01, 0.015, 0.02, 0.03, 0.05) {
    .\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 `
        --surface-debug-target=radiance --surface-radiance-debug-mode=4 `
        --surface-ray-bias=$bias --exit-frames=2
    # Extract hit/miss/escape counts via pixel analysis
}
```

Expected outcome: If hit count drops < 20% when bias increases from 0.01 to 0.03, self-hit contamination is significant and must be addressed.

---

### 🟡 MEDIUM RISK: Direction Mapping Still Debug-Only

**Issue**: Hemisphere direction uses simplified mapping:
```glsl
vec2 disk = clamp(probeRel / max(probeSize * 0.5, 1.0), vec2(-1.0), vec2(1.0));
float localZ = sqrt(max(0.0, 1.0 - dot(disk, disk)));
vec3 localDir = normalize(vec3(disk.x, disk.y, localZ));
```

This is NOT the final ShaderToy angular distribution (likely missing cosine-weighting or proper stratification).

**Impact**: 
- Trace results validate data path but not final radiance quality
- Angular distribution affects which surfaces are hit, biasing classification statistics
- Will need remapping before Phase 2B-4 (NEE) anyway

**Status**: Acknowledged in SC3 but no timeline for migration.

**Recommendation**:
1. Document exact target ShaderToy mapping (provide code snippet or reference)
2. List specific differences from current implementation
3. Consider implementing proper cosine-weighted hemisphere sampling before Phase 2B-4
4. For now, note that trace validation is topology/path correctness only, not angular quality

---

### 🟡 MEDIUM RISK: Escape Rate Analysis Incomplete

**Observation**: 814 escapes out of 12,096 active samples = 6.7% escape rate.

**Questions Unanswered**:
- Are escapes concentrated in specific charts/directions?
- Do escapes correlate with grazing angles near room edges/corners?
- Is 6.7% expected for Cornell geometry or indicative of tracing issues (e.g., early termination)?
- What is the average trace distance for escaped rays vs hit rays?

**Missing Data**: Per-chart escape statistics would reveal if certain probe orientations are problematic.

**Recommendation**: Add per-chart breakdown to structure checks:
```text
Chart 1 (floor):   hits=X, escapes=Y, misses=Z
Chart 2 (ceiling): hits=X, escapes=Y, misses=Z
...
```

If one chart has > 15% escape rate while others are < 5%, investigate TBN orientation or boundary conditions for that chart.

---

### 🟢 LOW-MEDIUM RISK: Miss Count Suspiciously Low

**Observation**: Only 18 max-distance misses out of 12,096 samples = 0.15%.

**Interpretation Options**:
- **A) Expected**: In closed Cornell room, most hemisphere rays from surfaces should hit another surface within bounds
- **B) Suspicious**: Tracing may be terminating early due to UDF artifacts or overly aggressive max distance

**Missing Context**: No baseline comparison with analytical ray-box intersection to verify trace completeness.

**Quick Check**: For Cornell box with dimensions ~1x1x1, maximum ray length within bounds is √3 ≈ 1.73. With `uTraceMaxDist = length(gridSize)` and typical gridSize matching room bounds, this should be sufficient.

**Recommendation**: 
1. Verify `uTraceMaxDist` value being used (should be ~1.73 for unit Cornell)
2. For 10 random probes, manually compute analytical intersection with room planes and compare to SDF trace result
3. If SDF reports miss where analytical says hit, investigate UDF resolution or step size

---

### 🟢 LOW RISK: Coupling Increase Between SurfaceRC and Demo3D

**Issue**: `SurfaceRC` now depends on external SDF texture and volume bounds passed from `Demo3D`.

**Current State**: Acceptable coupling - only texture handle and grid parameters, no scene-specific logic in C++.

**Future Risk**: As more features are added (hit classification, albedo lookup, NEE), this coupling will increase and SurfaceRC will become harder to test in isolation.

**Recommendation**: Consider defining a minimal interface:
```cpp
class ISurfaceScene {
public:
    virtual float sampleSDF(const glm::vec3& worldPos) const = 0;
    virtual glm::vec3 getVolumeOrigin() const = 0;
    virtual glm::vec3 getVolumeSize() const = 0;
    // Future: virtual HitSurface classifyHit(const glm::vec3& hitPos) const = 0;
};
```

This would allow SurfaceRC to remain scene-agnostic and testable with mock scenes.

---

## Documentation Quality Assessment

### Strengths ✓
- ✅ Clear non-goals explicitly stated in both plan and implementation docs
- ✅ Self-critique sections identify known limitations proactively
- ✅ Verification includes quantitative metrics (pixel counts, unique values)
- ✅ Next phase recommendation provided with clear scope boundaries
- ✅ Files changed list enables easy code review

### Gaps ❌
- ❌ **No performance measurements**: GPU dispatch time for radiance debug compute shader not reported
- ❌ **No memory impact assessment**: Additional uniforms/textures bandwidth cost not quantified
- ❌ **No comparison with volumetric cascade**: How do surface trace results compare to volumetric RC visibility?
- ❌ **Missing "lessons learned" section**: What would you do differently next time?
- ❌ **No error handling documentation**: What happens if SDF texture is null/unbound?

### Recommendations for Future Phases
1. Add performance section to implementation doc (dispatch time, memory overhead)
2. Include comparison baselines (e.g., "volumetric cascade reports X% visibility, surface trace reports Y%")
3. Add "Lessons Learned" section capturing insights for future work
4. Document error handling strategy (graceful degradation vs assert vs skip)

---

## Code Quality Observations

### Positive Patterns ✓
1. **Consistent naming**: Uniform names follow existing conventions (`uSDF`, `uGridOrigin`, `uTraceSteps`)
2. **Clean abstraction**: TraceResult struct encapsulates trace state cleanly
3. **Defensive coding**: Alpha floor fix prevents invisible near-hits in display pipeline
4. **Separation of concerns**: C++ handles resource binding, shader handles logic

### Areas for Improvement 🔧

#### 1. Magic Numbers
Current code has hardcoded values scattered throughout:
```glsl
uniform float uRayBias;           // Set to 0.01 in C++
uniform int uTraceSteps;          // Set to 96 in C++
uniform float uHitEpsilon;        // Set to 0.002 in C++
// Inside shader:
if (d >= INF * 0.5)              // Why 0.5?
t += max(d, uHitEpsilon);        // Epsilon used twice with different semantics
```

**Recommendation**: Define named constants at top of shader:
```glsl
const float TRACE_ESCAPE_THRESHOLD = 0.5;  // Fraction of INF indicating outside volume
const float MIN_STEP_SIZE = 0.002;         // Minimum advance to avoid infinite loops
const int MAX_TRACE_ITERATIONS = 96;       // Safety limit to prevent hangs
```

#### 2. Error Handling
No validation that SDF texture is valid before dispatch:
```cpp
void SurfaceRC::dispatchRadianceDebug(...) {
    if (!enabled || radianceDebugTexture == 0 || computeProgram == 0) return;
    // Missing: if (sdfTexture == 0) { std::cerr << "WARN: SDF texture null\n"; return; }
```

**Recommendation**: Add guard clause and warning log.

#### 3. Debug Statistics
Could add optional verbose output for trace diagnostics:
```glsl
#ifdef DEBUG_TRACE_STATS
shared uint g_hitCount, g_missCount, g_escapeCount, g_totalSteps;
// Atomic increments in main()
// Print via transform feedback or SSBO readback
#endif
```

This would enable real-time trace health monitoring without screenshot analysis.

#### 4. Uniform Validation
No check that uploaded uniform locations are valid:
```cpp
glUniform1i(glGetUniformLocation(computeProgram, "uSDF"), 0);
// Should be:
GLint loc = glGetUniformLocation(computeProgram, "uSDF");
if (loc == -1) std::cerr << "WARN: uSDF uniform not found\n";
else glUniform1i(loc, 0);
```

---

## Recommended Actions Before Phase 2B-3

### Priority 1 (Blockers - Must Complete)

1. **Run Bias Sweep Diagnostic**
   ```powershell
   # Test bias values: 0.005, 0.01, 0.015, 0.02, 0.03, 0.05
   # Plot hit/miss/escape counts vs bias
   # If hit count drops > 20% in range 0.01-0.03, self-hit is contaminating results
   ```
   **Acceptance Criteria**: Hit count stable (±10%) across bias range 0.01-0.02

2. **Add Per-Chart Statistics**
   Modify structure check to report per-chart breakdown:
   ```text
   Chart 1 (floor):   hits=2250, escapes=150, misses=3
   Chart 2 (ceiling): hits=2100, escapes=180, misses=2
   ...
   ```
   **Acceptance Criteria**: No single chart has > 2x escape rate of average

3. **Verify Non-Self-Hit Distance Distribution**
   From mode 5 capture, verify that at least 10% of hits have trace distance > 0.1 * gridSize
   ```text
   # If gridSize ≈ 1.0, then 10% of hits should have distance > 0.1
   # This proves rays are traveling meaningful distances, not self-hitting immediately
   ```
   **Acceptance Criteria**: ≥10% of hits have normalized distance > 0.1

### Priority 2 (Important - Should Complete)

4. **Document Hemisphere Mapping Differences**
   Create comparison table:
   ```text
   Current Implementation          | Target ShaderToy Mapping
   --------------------------------|--------------------------
   Uniform disk sampling           | Cosine-weighted?
   Simple hemisphere cap           | Full hemisphere?
   No stratification               | Stratified/Halton?
   ```

5. **Analytical Ray-Plane Comparison**
   For 100 random probes, compare SDF trace with analytical plane intersection:
   ```cpp
   // Pseudo-code
   for (i = 0; i < 100; i++) {
       pick random probe position/direction
       sdf_result = traceSDF(rayOrigin, worldDir)
       analytic_result = intersectRayBox(rayOrigin, worldDir, bounds)
       if (sdf_result.hit != analytic_result.hit) discrepancy_count++
   }
   ```
   **Acceptance Criteria**: Discrepancy rate < 5%

6. **Measure GPU Dispatch Time**
   Add timing around `glDispatchCompute`:
   ```cpp
   auto t0 = std::chrono::high_resolution_clock::now();
   glDispatchCompute(...);
   glFinish();  // Force completion
   auto t1 = std::chrono::high_resolution_clock::now();
   std::cout << "Radiance debug dispatch: " 
             << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() 
             << " ms\n";
   ```

### Priority 3 (Nice-to-have)

7. **Extract Magic Numbers to Constants** (refactoring)
8. **Add Optional Verbose Logging** for trace statistics
9. **Create ISurfaceScene Interface Stub** for future decoupling
10. **Compare with Volumetric Cascade** visibility results

---

## Verdict

### Phase 2B-2 Status: ✅ IMPLEMENTED SUCCESSFULLY (with caveats)

The implementation achieves its stated goals and passes basic structural verification. The trace skeleton correctly identifies hits, misses, and escapes with plausible distributions for Cornell geometry.

**However**, several diagnostic gaps should be filled before proceeding to hit surface classification (Phase 2B-3):

1. **Self-hit contamination** is the highest risk - unquantified and could invalidate hit classification
2. **UDF bias sensitivity** needs characterization before trusting trace distances
3. **Per-chart variation** might reveal orientation-specific issues masked by aggregate stats

### Confidence Level: 75% Ready for Phase 2B-3

- **25% uncertainty** due to unquantified self-hit rate and UDF artifacts
- **Recommendation**: Complete Priority 1 actions above before starting 2B-3 implementation
- **If Priority 1 passes**: Confidence increases to 90%+

---

## Comparison to Plan

| Aspect | Planned | Implemented | Deviation | Status |
|--------|---------|-------------|-----------|--------|
| Trace function | ✓ | ✓ | None | ✅ |
| Mode 4 classification | ✓ | ✓ | None | ✅ |
| Mode 5 distance | ✓ | ✓ | Added alpha floor fix | ✅ Exceeded |
| SDF binding | ✓ | ✓ | None | ✅ |
| CLI/UI updates | ✓ | ✓ | None | ✅ |
| Verification | Basic structure checks | Enhanced with pixel counting | Exceeded plan | ✅ Exceeded |
| Self-critique | Required | Provided + applied fix | Exceeded plan | ✅ Exceeded |
| Bias sweep test | Not planned | Not done | **Gap identified** | ❌ Missing |
| Per-chart stats | Not planned | Not done | **Gap identified** | ❌ Missing |
| Performance metrics | Not mentioned | Not measured | **Gap identified** | ❌ Missing |

---

## Final Recommendation

### Proceed to Phase 2B-3 **only after** completing bias sweep diagnostic.

**Rationale**: 
- Current implementation is sound for diagnostic purposes
- Understanding self-hit contamination is **critical** before adding hit surface classification
- Hit classification will be even more sensitive to trace accuracy than simple hit/miss detection
- A small self-hit rate (~5%) is tolerable for mode 4/5 visualization but catastrophic for mode 6/7 chart ID assignment

**Decision Tree**:
```
Bias sweep results:
├─ Hit count stable (±10%) across 0.01-0.03 range
│  └─ ✅ PROCEED to Phase 2B-3 with high confidence (90%+)
│
├─ Hit count drops 10-30% in range
│  ├─ ⚠️ PROCEED with caution, document self-hit rate
│  └─ Consider increasing default bias to 0.02
│
└─ Hit count drops > 30% in range
   └─ ❌ STOP, address UDF/tracing issues before continuing
      - Investigate SDF generation quality
      - Consider true signed SDF
      - Re-evaluate trace parameters
```

**Estimated Effort for Priority 1 Actions**: 2-4 hours
- Bias sweep automation: 1 hour
- Per-chart statistics: 1 hour  
- Distance distribution analysis: 30 minutes
- Documentation update: 30 minutes

This investment will significantly de-risk Phase 2B-3 and subsequent lighting/feedback phases.

---

## Appendix: Quick Reference Commands

### Bias Sweep Test
```powershell
$biases = @(0.005, 0.01, 0.015, 0.02, 0.03, 0.05)
foreach ($bias in $biases) {
    Write-Host "Testing bias=$bias"
    .\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 `
        --surface-debug-target=radiance --surface-radiance-debug-mode=4 `
        --surface-ray-bias=$bias --exit-frames=2 `
        --screenshot="tools/phase2b2_visual/bias_${bias}.png"
    # TODO: Add pixel analysis script to extract hit/miss/escape counts
}
```

### Per-Chart Analysis (requires custom script)
```python
# Pseudo-code for PIL/pillow analysis
from PIL import Image
import numpy as np

img = Image.open('trace_m4.png')
pixels = np.array(img)

# Define chart regions based on atlas layout
chart_regions = {
    'floor':    (0, 0, 256, 256),
    'ceiling':  (256, 0, 256, 256),
    'left':     (512, 0, 128, 256),
    'right':    (640, 0, 128, 256),
    'back':     (768, 0, 128, 256),
    'front':    (896, 0, 128, 256),  # inactive
}

for chart_name, (x, y, w, h) in chart_regions.items():
    region = pixels[y:y+h, x:x+w]
    green_pixels = np.sum((region[:,:,0] < 0.1) & (region[:,:,1] > 0.9) & (region[:,:,2] < 0.1))
    red_pixels = np.sum((region[:,:,0] > 0.9) & (region[:,:,1] < 0.1) & (region[:,:,2] < 0.1))
    blue_pixels = np.sum((region[:,:,0] < 0.1) & (region[:,:,1] < 0.1) & (region[:,:,2] > 0.9))
    print(f"{chart_name}: hits={green_pixels}, misses={red_pixels}, escapes={blue_pixels}")
```

### Analytical Comparison Test
```cpp
// Add to demo3d.cpp temporarily for diagnostic
void Demo3D::testTraceAccuracy() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> dir_dist(-1.0f, 1.0f);
    
    int discrepancies = 0;
    for (int i = 0; i < 100; i++) {
        // Pick random surface point and direction
        int chart_id = rand() % 5 + 1;
        vec2 uv = vec2(pos_dist(gen), pos_dist(gen));
        vec3 ray_origin = chartToWorld(chart_id, uv) + normal * 0.01;
        vec3 ray_dir = normalize(vec3(dir_dist(gen), abs(dir_dist(gen)), dir_dist(gen)));
        
        // SDF trace
        TraceResult sdf_trace = traceSDF(ray_origin, ray_dir);
        
        // Analytical intersection with Cornell box planes
        float t_analytic = intersectRayPlanes(ray_origin, ray_dir, boundsMin, boundsMax);
        bool analytic_hit = (t_analytic > 0 && t_analytic < length(gridSize));
        
        if (sdf_trace.hit != analytic_hit) {
            discrepancies++;
            std::cout << "Discrepancy #" << discrepancies 
                      << ": SDF=" << (sdf_trace.hit ? "HIT" : "MISS")
                      << ", Analytic=" << (analytic_hit ? "HIT" : "MISS")
                      << "\n";
        }
    }
    
    std::cout << "Trace accuracy test: " << (100 - discrepancies) << "/100 matches ("
              << (100.0f * discrepancies / 100.0f) << "% discrepancy rate)\n";
}
```

---

**End of Critique**
