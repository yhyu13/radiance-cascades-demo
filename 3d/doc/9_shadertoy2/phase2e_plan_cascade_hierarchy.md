# ShaderToy2 Phase 2E Plan — Cascade Hierarchy / Multi-Bounce GI

**Date:** 2026-05-29  
**Status:** Planning + Self-Critique (Stage 1)  
**Scope:** Implement 5-level cascade hierarchy (C0-C4) for true multi-bounce indirect lighting. Enable inter-cascade injection from coarse to fine levels. Add final GI lookup in raymarch pass. Requires Phase 2D feedback system to be complete first.

---

## 1. Motivation & Context

### Current State (After Phase 2D)

```text
✓ Room-plane charts implemented (Charts 1-5)
✓ Box charts implemented (Charts 7-18: short_box + tall_box)
✓ Direct lighting computed at hit points
✓ Single-frame direct radiance atlas write (mode 15)
✓ Temporal accumulation via ping-pong feedback (modes 17/18)
✓ Unknown hit rate < 2%
✓ Smooth convergence over 10-50 frames

✗ Only single-bounce direct lighting (no indirect bounces)
✗ No cascade hierarchy (all probes at same resolution)
✗ No inter-surface light transport
✗ No color bleeding between surfaces
✗ No final GI lookup in raymarch pass
```

### Problem Statement

Phase 2D provides **temporally-smoothed direct lighting**, but lacks:

```text
1. Multi-bounce indirect illumination (light bouncing between surfaces)
2. Hierarchical detail levels (fine probes near geometry, coarse far away)
3. Light transport from one surface to another (color bleeding)
4. Integration with final raymarch shading pass
```

To achieve true GI matching ShaderToy reference, we need:

```text
Cascade Hierarchy:
  C0 (finest):   Per-probe direct + inject from C1
  C1:            Aggregated from C2, inject to C0
  C2:            Aggregated from C3, inject to C1
  C3:            Aggregated from C4, inject to C2
  C4 (coarsest): Global ambient term, inject to C3

Multi-Bounce Flow:
  Frame N:
    1. Compute direct lighting at all probes (C0)
    2. Aggregate C0 → C1 (downsample)
    3. Aggregate C1 → C2 (downsample)
    4. ... continue to C4
    5. Inject C4 → C3 (upsample with filtering)
    6. Inject C3 → C2 (upsample with filtering)
    7. ... continue to C0
    8. Final: C0 contains direct + multi-bounce indirect
    9. Raymarch pass samples C0 for final GI
```

### Why Now?

Phase 2D completed the feedback mechanism, which is a **prerequisite** for cascade hierarchy:

```text
Before Phase 2D: No temporal smoothing → cascades would flicker violently
After Phase 2D:  Stable accumulated radiance → cascades can propagate smoothly
```

Additionally, Phase 2C completed box chart support, ensuring **complete geometry coverage**:

```text
Before Phase 2C: ~12% unknown hits → cascade aggregation would miss data
After Phase 2C:  < 2% unknown hits → cascades have sufficient input data
```

---

## 2. Proposed Architecture

### 2.1 Cascade Hierarchy Design

**Resolution Strategy:**

```text
C0: 32×32×32 probes (finest, per-surface detail)
C1: 16×16×16 probes (2× downsampled)
C2: 8×8×8 probes   (4× downsampled)
C3: 4×4×4 probes   (8× downsampled)
C4: 2×2×2 probes   (16× downsampled, global ambient)

Total probes: 32³ + 16³ + 8³ + 4³ + 2³ = 32,768 + 4,096 + 512 + 64 + 8 = 37,448
```

**Atlas Layout:**

```text
Each cascade level gets its own atlas texture:
  - C0 atlas: 2560 × 1536 (existing ring atlas, 18 charts × 128px width)
  - C1 atlas: 1280 × 768  (half resolution)
  - C2 atlas: 640 × 384   (quarter resolution)
  - C3 atlas: 320 × 192   (eighth resolution)
  - C4 atlas: 160 × 96    (sixteenth resolution)

Alternative: Single mega-atlas with all levels packed
  Width:  2560 (max of all levels)
  Height: 1536 + 768 + 384 + 192 + 96 = 2976
  Total memory: 2560 × 2976 × 4 bytes ≈ 30.5 MB (RGBA16F)
```

**Decision:** Use separate textures per cascade for simplicity and flexibility. Memory cost similar to mega-atlas but easier to manage.

### 2.2 Inter-Cascade Injection

**Downsampling (Aggregation):**

```glsl
// For each probe in cascade N+1:
// Average 2×2×2 block from cascade N
vec3 aggregateFromFiner(vec3 coarsePos, int coarseLevel) {
    vec3 sum = vec3(0.0);
    int count = 0;
    
    // Sample 8 neighbors from finer level
    for (int dx = 0; dx < 2; dx++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dz = 0; dz < 2; dz++) {
                vec3 finePos = coarsePos * 2.0 + vec3(dx, dy, dz);
                vec3 fineRadiance = sampleCascade(coarseLevel - 1, finePos);
                if (fineRadiance != vec3(0.0)) {  // Skip inactive probes
                    sum += fineRadiance;
                    count++;
                }
            }
        }
    }
    
    return (count > 0) ? sum / float(count) : vec3(0.0);
}
```

**Upsampling (Injection):**

```glsl
// For each probe in cascade N:
// Trilinear interpolate from cascade N+1
vec3 injectFromCoarser(vec3 finePos, int fineLevel) {
    vec3 coarsePos = finePos / 2.0;
    
    // Trilinear interpolation from coarser level
    vec3 c000 = sampleCascade(fineLevel + 1, floor(coarsePos));
    vec3 c001 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(0, 0, 1));
    vec3 c010 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(0, 1, 0));
    vec3 c011 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(0, 1, 1));
    vec3 c100 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(1, 0, 0));
    vec3 c101 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(1, 0, 1));
    vec3 c110 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(1, 1, 0));
    vec3 c111 = sampleCascade(fineLevel + 1, floor(coarsePos) + vec3(1, 1, 1));
    
    vec3 fracPos = fract(coarsePos);
    
    // Trilinear blend
    vec3 c00 = mix(c000, c100, fracPos.x);
    vec3 c01 = mix(c001, c101, fracPos.x);
    vec3 c10 = mix(c010, c110, fracPos.x);
    vec3 c11 = mix(c011, c111, fracPos.x);
    
    vec3 c0 = mix(c00, c10, fracPos.y);
    vec3 c1 = mix(c01, c11, fracPos.y);
    
    return mix(c0, c1, fracPos.z);
}
```

**Final Radiance Formula:**

```glsl
// For C0 probes (finest level):
vec3 directLighting = computeDirect(hitPos, normal, lightPos);
vec3 indirectFromC1 = injectFromCoarser(probePos, 0) * uIndirectScale;
vec3 totalRadiance = directLighting + indirectFromC1;

// Store in C0 atlas
imageStore(oCascade0Atlas, p, vec4(totalRadiance, 1.0));
```

### 2.3 LOD Selection for Probes

**Distance-Based LOD:**

```glsl
// In raymarch pass, select cascade level based on distance to camera
float distToCamera = length(probeWorldPos - uCameraPos);
int lodLevel;

if (distToCamera < 2.0)      lodLevel = 0;  // C0: finest
else if (distToCamera < 4.0) lodLevel = 1;  // C1
else if (distToCamera < 8.0) lodLevel = 2;  // C2
else if (distToCamera < 16.0) lodLevel = 3; // C3
else                          lodLevel = 4; // C4: coarsest

vec3 gi = sampleCascade(lodLevel, probeCoord);
```

**Alternative: Importance-Based LOD**

```glsl
// Use gradient of radiance field to determine detail needed
vec3 gradient = computeRadianceGradient(probePos);
float importance = length(gradient);

if (importance > 0.5)      lodLevel = 0;  // High detail needed
else if (importance > 0.2) lodLevel = 1;
else if (importance > 0.1) lodLevel = 2;
else                       lodLevel = 3;  // Low detail OK
```

**Decision:** Start with distance-based LOD (simpler). Can upgrade to importance-based later if quality issues arise.

### 2.4 Final GI Lookup in Raymarch Pass

**Integration Point:**

```glsl
// In existing raymarch fragment shader (after SDF trace):
if (hitSurface) {
    vec3 hitPos = rayOrigin + rayDir * hitDistance;
    vec3 normal = estimateNormal(hitPos);
    
    // Find nearest C0 probe
    vec3 probeCoord = worldToProbeCoord(hitPos, 0);  // C0 level
    vec3 gi = sampleCascade(0, probeCoord);
    
    // Combine with direct lighting
    vec3 direct = computeDirect(hitPos, normal, lightPos);
    vec3 finalColor = direct + gi * uGIScale;
    
    fragColor = vec4(finalColor, 1.0);
}
```

---

## 3. Implementation Steps

### Step 1: Add Cascade Textures (C++)

**File:** `src/surface_rc.h`

```cpp
// Phase 2E: Cascade hierarchy textures
static const int CASCADE_COUNT = 5;  // C0-C4
GLuint cascadeAtlases[CASCADE_COUNT];  // One atlas per level
int cascadeResolutions[CASCADE_COUNT]; // {32, 16, 8, 4, 2}
```

**File:** `src/surface_rc.cpp`

```cpp
// In constructor:
cascadeResolutions = {32, 16, 8, 4, 2};
for (int i = 0; i < CASCADE_COUNT; i++) {
    cascadeAtlases[i] = 0;
}

// In initialize():
for (int i = 0; i < CASCADE_COUNT; i++) {
    int res = cascadeResolutions[i];
    int width = res * 128;  // 128px per chart, 18 charts
    int height = res * 256; // Assuming 256px height per chart
    
    glGenTextures(1, &cascadeAtlases[i]);
    glBindTexture(GL_TEXTURE_2D, cascadeAtlases[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// In destroy():
for (int i = 0; i < CASCADE_COUNT; i++) {
    if (cascadeAtlases[i]) {
        glDeleteTextures(1, &cascadeAtlases[i]);
        cascadeAtlases[i] = 0;
    }
}
```

### Step 2: Implement Downsampling Shader

**File:** `res/shaders/cascade_downsample.comp` (NEW)

```glsl
#version 430 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, binding = 0) uniform image2D oCoarseAtlas;
uniform sampler2D uFineAtlas;
uniform ivec2 uFineAtlasSize;
uniform ivec2 uCoarseAtlasSize;
uniform int uFineLevel;  // Which cascade level we're downsampling FROM

void main() {
    ivec2 coarsePx = ivec2(gl_GlobalInvocationID.xy);
    if (coarsePx.x >= uCoarseAtlasSize.x || coarsePx.y >= uCoarseAtlasSize.y) return;
    
    // Each coarse texel aggregates 2x2 block from fine atlas
    ivec2 fineBase = coarsePx * 2;
    
    vec3 sum = vec3(0.0);
    int count = 0;
    
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            ivec2 finePx = fineBase + ivec2(dx, dy);
            if (finePx.x < uFineAtlasSize.x && finePx.y < uFineAtlasSize.y) {
                vec4 sample = texelFetch(uFineAtlas, finePx, 0);
                if (sample.a > 0.0) {  // Only count active probes
                    sum += sample.rgb;
                    count++;
                }
            }
        }
    }
    
    vec3 avgRadiance = (count > 0) ? sum / float(count) : vec3(0.0);
    imageStore(oCoarseAtlas, coarsePx, vec4(avgRadiance, float(count > 0)));
}
```

### Step 3: Implement Upsampling/Injection Shader

**File:** `res/shaders/cascade_upsample.comp` (NEW)

```glsl
#version 430 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, binding = 0) uniform image2D oFineAtlas;
uniform sampler2D uCoarseAtlas;
uniform ivec2 uCoarseAtlasSize;
uniform ivec2 uFineAtlasSize;
uniform int uCoarseLevel;  // Which cascade level we're upsampling FROM
uniform float uInjectionWeight;  // How much to blend (default 0.5)

void main() {
    ivec2 finePx = ivec2(gl_GlobalInvocationID.xy);
    if (finePx.x >= uFineAtlasSize.x || finePx.y >= uFineAtlasSize.y) return;
    
    // Read current fine-level radiance
    vec4 currentSample = imageLoad(oFineAtlas, finePx);
    vec3 fineRadiance = currentSample.rgb;
    
    // Trilinear interpolate from coarse level
    vec2 coarseUV = vec2(finePx) / vec2(uFineAtlasSize) * 2.0;  // Map to coarse space
    vec3 coarseRadiance = texture(uCoarseAtlas, coarseUV).rgb;
    
    // Blend: keep direct lighting, add indirect from coarse
    vec3 injectedRadiance = fineRadiance + coarseRadiance * uInjectionWeight;
    
    imageStore(oFineAtlas, finePx, vec4(injectedRadiance, 1.0));
}
```

### Step 4: Update Dispatch Functions

**File:** `src/surface_rc.cpp`

```cpp
void SurfaceRC::dispatchCascadeHierarchy(GLuint downsampleProgram, 
                                         GLuint upsampleProgram) {
    if (!enabled) return;
    
    // Phase 1: Downsample from C0 → C1 → C2 → C3 → C4
    for (int level = 0; level < CASCADE_COUNT - 1; level++) {
        glUseProgram(downsampleProgram);
        
        // Bind fine atlas as input
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[level]);
        glUniform1i(glGetUniformLocation(downsampleProgram, "uFineAtlas"), 0);
        
        // Bind coarse atlas as output
        glBindImageTexture(0, cascadeAtlases[level + 1], 0, GL_FALSE, 0, 
                          GL_WRITE_ONLY, GL_RGBA16F);
        
        // Set uniforms
        int fineRes = cascadeResolutions[level];
        int coarseRes = cascadeResolutions[level + 1];
        glUniform2i(glGetUniformLocation(downsampleProgram, "uFineAtlasSize"),
                   fineRes * 128, fineRes * 256);
        glUniform2i(glGetUniformLocation(downsampleProgram, "uCoarseAtlasSize"),
                   coarseRes * 128, coarseRes * 256);
        glUniform1i(glGetUniformLocation(downsampleProgram, "uFineLevel"), level);
        
        // Dispatch
        int coarseWidth = coarseRes * 128;
        int coarseHeight = coarseRes * 256;
        GLuint groupsX = (coarseWidth + 7) / 8;
        GLuint groupsY = (coarseHeight + 7) / 8;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    
    // Phase 2: Upsample/inject from C4 → C3 → C2 → C1 → C0
    for (int level = CASCADE_COUNT - 2; level >= 0; level--) {
        glUseProgram(upsampleProgram);
        
        // Bind coarse atlas as input
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[level + 1]);
        glUniform1i(glGetUniformLocation(upsampleProgram, "uCoarseAtlas"), 0);
        
        // Bind fine atlas as input/output (read-modify-write)
        glBindImageTexture(0, cascadeAtlases[level], 0, GL_FALSE, 0, 
                          GL_READ_WRITE, GL_RGBA16F);
        
        // Set uniforms
        int fineRes = cascadeResolutions[level];
        int coarseRes = cascadeResolutions[level + 1];
        glUniform2i(glGetUniformLocation(upsampleProgram, "uFineAtlasSize"),
                   fineRes * 128, fineRes * 256);
        glUniform2i(glGetUniformLocation(upsampleProgram, "uCoarseAtlasSize"),
                   coarseRes * 128, coarseRes * 256);
        glUniform1i(glGetUniformLocation(upsampleProgram, "uCoarseLevel"), level + 1);
        glUniform1f(glGetUniformLocation(upsampleProgram, "uInjectionWeight"), 0.5f);
        
        // Dispatch
        int fineWidth = fineRes * 128;
        int fineHeight = fineRes * 256;
        GLuint groupsX = (fineWidth + 7) / 8;
        GLuint groupsY = (fineHeight + 7) / 8;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}
```

### Step 5: Integrate with Render Loop

**File:** `src/demo3d.cpp`

```cpp
// After surfaceRC->dispatchRadianceDebug() in render loop:
if (useSurfaceRC && surfaceRC && cascadeHierarchyEnabled) {
    auto downProg = shaders.find("cascade_downsample.comp");
    auto upProg = shaders.find("cascade_upsample.comp");
    
    if (downProg != shaders.end() && upProg != shaders.end()) {
        surfaceRC->dispatchCascadeHierarchy(downProg->second, upProg->second);
    }
}
```

### Step 6: Add UI Controls

**File:** `src/demo3d.cpp` (in Surface RC UI panel)

```cpp
// Phase 2E: Cascade hierarchy controls
ImGui::Separator();
ImGui::Text("Phase 2E: Cascade Hierarchy");

bool cascadeEnabled = getCascadeHierarchyEnabled();
if (ImGui::Checkbox("Enable cascade hierarchy", &cascadeEnabled)) {
    setCascadeHierarchyEnabled(cascadeEnabled);
}

if (cascadeEnabled) {
    ImGui::Text("Cascade levels: C0 (32³), C1 (16³), C2 (8³), C3 (4³), C4 (2³)");
    
    float injectionWeight = getCascadeInjectionWeight();
    if (ImGui::SliderFloat("Injection weight", &injectionWeight, 0.0f, 1.0f, "%.2f")) {
        setCascadeInjectionWeight(injectionWeight);
    }
    
    ImGui::Text("LOD mode: Distance-based (future: importance-based)");
}
```

---

## 4. Testing Strategy

### 4.1 Visual Verification

**Capture Sequence:**

```powershell
# Test cascade hierarchy visualization
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --cascade-hierarchy-enabled=1 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --exit-frames=50

# Compare with no hierarchy (Phase 2D baseline)
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --cascade-hierarchy-enabled=0 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --exit-frames=50
```

**Expected Results:**

```text
Without hierarchy (Phase 2D):
  - Direct lighting only (smoothed temporally)
  - No color bleeding
  - Sharp shadows
  
With hierarchy (Phase 2E):
  - Visible color bleeding (red wall tints nearby surfaces)
  - Softer shadows from indirect bounces
  - Brighter corners where multiple surfaces meet
  - More realistic ambient occlusion
```

### 4.2 Statistical Checks

**Per-Cascade Statistics:**

```text
C0 (32³):
  Active probes: ~10,000 (on surfaces)
  Mean radiance: 0.3 (direct dominant)
  Variance: 0.15 (high detail variation)
  
C1 (16³):
  Active probes: ~2,000 (aggregated)
  Mean radiance: 0.25 (some indirect contribution)
  Variance: 0.08 (smoother)
  
C2 (8³):
  Active probes: ~400
  Mean radiance: 0.2 (more indirect)
  Variance: 0.04
  
C3 (4³):
  Active probes: ~50
  Mean radiance: 0.15 (mostly indirect)
  Variance: 0.02
  
C4 (2³):
  Active probes: 8
  Mean radiance: 0.1 (global ambient)
  Variance: 0.005 (very smooth)
```

**Convergence Metric:**

```python
# Measure indirect contribution ratio:
indirect_ratio = mean(C0_radiance_after_injection) / mean(C0_direct_only)

Expected:
  Frame 1:  indirect_ratio ≈ 1.0 (no injection yet)
  Frame 10: indirect_ratio ≈ 1.3 (30% indirect contribution)
  Frame 50: indirect_ratio ≈ 1.5 (50% indirect contribution, converged)
```

### 4.3 Performance Profiling

**GPU Timing:**

```text
Baseline (Phase 2D, no hierarchy):
  dispatchRadianceDebug: 2.0 ms
  Total frame time: 8.0 ms
  
With hierarchy (Phase 2E):
  dispatchRadianceDebug: 2.0 ms (unchanged)
  cascadeDownsample (4 passes): 1.5 ms
  cascadeUpsample (4 passes): 1.5 ms
  Total overhead: +3.0 ms
  Total frame time: 11.0 ms (+37.5%)
```

**Memory Usage:**

```text
Additional textures:
  C0: 2560 × 1536 × 4 = 15.7 MB (reuses existing)
  C1: 1280 × 768 × 4 = 3.9 MB
  C2: 640 × 384 × 4 = 1.0 MB
  C3: 320 × 192 × 4 = 0.25 MB
  C4: 160 × 96 × 4 = 0.06 MB
  Total additional: ~5.2 MB
  Total surface RC memory: ~52 MB (was 47 MB)
```

---

## 5. Known Limitations & Future Work

### Not Implemented in Phase 2E

```text
✗ Importance-based LOD selection (using distance-based)
✗ Adaptive injection weights per cascade level
✗ Temporal reprojection for cascades (may flicker on camera move)
✗ EXR/PT quality metrics for validation
✗ Dynamic scene support (cascades assume static geometry)
✗ Anisotropic filtering for upsampling
✗ Variance-guided denoising per cascade level
```

### Potential Issues

```text
1. Cascades may flicker on camera movement (no temporal reprojection yet)
2. Injection weight of 0.5 is heuristic, not physically-based
3. Downsampling averages all probes equally (no importance weighting)
4. C4 global ambient may be too coarse for complex scenes
5. No handling of dynamic lights (cascades baked for static lighting)
```

---

## 6. Success Criteria

Phase 2E succeeds if:

```text
✓ All 5 cascade levels (C0-C4) allocate and render correctly
✓ Downsampling produces progressively smoother results (C0 → C4)
✓ Upsampling/injection adds visible indirect lighting to C0
✓ Color bleeding observable (e.g., red wall tints nearby white surfaces)
✓ Performance impact < 50% frame time increase
✓ Memory increase < 10 MB
✓ No regression in existing modes (0-18 still work)
✓ Convergence stable over 50 frames (no oscillation)
✓ Indirect contribution ratio reaches 1.3-1.5 after convergence
```

**Quantitative Targets:**

```text
- C4 mean radiance > 0.05 (proves indirect light propagates to coarsest level)
- C0 indirect_ratio > 1.2 after 50 frames (proves injection works)
- Frame time increase: < 4 ms at 128³ resolution
- Memory increase: < 10 MB total
- Visual quality: noticeable improvement over Phase 2D baseline
```

---

## 7. Rollback Plan

If Phase 2E introduces critical bugs:

```text
1. Disable cascade hierarchy via UI checkbox (fallback to Phase 2D)
2. Keep cascade textures allocated but unused
3. Document failure mode for future debugging
4. Revert to mode 17/18 without hierarchy as fallback
```

**Fallback Command:**

```powershell
# Disable cascade hierarchy, use Phase 2D behavior
--cascade-hierarchy-enabled=0
```

---

## 8. Documentation Updates Required

After implementation:

```text
1. Update README.md with cascade hierarchy overview
2. Add Phase 2E implementation document
3. Update architecture diagram to show 5-level hierarchy
4. Document injection weight tuning guidelines
5. Add troubleshooting section for cascade flickering
6. Create comparison images: Phase 2D vs Phase 2E side-by-side
```

---

## 9. Self-Critique of Plan (Stage 1)

### SC1 — Memory budget may be underestimated

**Critique:** Calculated 5.2 MB additional, but didn't account for:
- Ping-pong buffers for EACH cascade level (currently assumed single buffer)
- Mipmaps or intermediate textures for filtering
- Alignment padding (GL textures may round up to power-of-2)

**Actual Cost:**
- If each cascade needs ping-pong: 5.2 MB × 2 = 10.4 MB
- Plus existing 31.4 MB from Phase 2D = 41.8 MB total
- Still acceptable (< 50 MB), but tighter than planned

**Mitigation:**
- Share ping-pong buffers across cascades (process sequentially)
- Or accept 10.4 MB increase (modern GPUs have 4-8 GB)
- Document updated memory requirement: 2.5 GB → 3.0 GB VRAM minimum

**Decision:** Accept 10.4 MB increase. Update plan to clarify ping-pong requirement.

---

### SC2 — Downsampling loses spatial detail

**Critique:** Simple 2×2×2 averaging discards high-frequency information. Fine details in C0 won't propagate correctly to coarser levels.

**Impact:**
- Small bright spots in C0 get diluted in C1
- Coarse levels may underestimate peak radiance
- Injection back to C0 may lack detail

**Mitigation Options:**
1. **Max-pooling instead of averaging:** Preserves peaks but loses energy conservation
2. **Weighted average:** Weight by probe activity/importance
3. **Multi-pass downsampling:** First pass averages, second pass preserves max
4. **Accept limitation:** Coarse levels meant for low-frequency ambient anyway

**Decision:** Start with simple averaging (Option 4). If quality issues arise, implement weighted average (Option 2) in Phase 2F.

---

### SC3 — Upsampling may introduce artifacts

**Critique:** Trilinear interpolation assumes smooth radiance field, but actual GI has discontinuities (shadow boundaries, color bleeding edges).

**Impact:**
- Blurred shadow edges after injection
- Color bleeding spreads too far (bleeding beyond geometric occlusion)
- Halo artifacts around high-contrast regions

**Mitigation Options:**
1. **Bilateral filtering:** Preserve edges during upsampling
2. **Normal-aware interpolation:** Don't blend across normal discontinuities
3. **Depth-aware filtering:** Don't blend across depth discontinuities
4. **Lower injection weight:** Reduce artifact visibility (already tunable)

**Decision:** Start with trilinear (simplest). If artifacts visible, add bilateral filtering in Phase 2F. Document known artifact risk.

---

### SC4 — No temporal reprojection for cascades

**Critique:** When camera moves, cascade contents become invalid but aren't reset automatically (unlike Phase 2D ping-pong).

**Impact:**
- Ghosting artifacts from previous camera position
- Flickering as old/new cascade data mixes
- Slow convergence after camera movement

**Mitigation:**
- Extend camera movement detection to reset ALL cascade atlases
- Or implement temporal reprojection (complex, requires motion vectors)
- Or accept slower convergence after camera moves

**Decision:** Reset all cascades on camera movement (simple). Temporal reprojection deferred to Phase 3.

---

### SC5 — Injection weight is heuristic, not physical

**Critique:** Fixed weight of 0.5 has no physical basis. True radiosity would compute exact form factors.

**Impact:**
- May over- or under-estimate indirect contribution
- Different scenes may need different weights
- Not energy-conserving

**Mitigation:**
- Make weight tunable via UI (already planned)
- Provide presets: Conservative (0.3), Balanced (0.5), Aggressive (0.7)
- Future: compute approximate form factors based on geometry (Phase 3)

**Decision:** Tunable weight with presets is sufficient for Phase 2E. Physical accuracy deferred.

---

### SC6 — Cascade hierarchy assumes static scene

**Critique:** If objects move or lights change, entire hierarchy becomes invalid.

**Impact:**
- No support for dynamic Cornell variants
- Must rebuild all cascades from scratch on scene change
- Expensive for real-time applications

**Mitigation:**
- Document limitation clearly
- Invalidate all cascades on scene dirty flag
- Future: partial rebuild for dynamic objects only (Phase 3)

**Decision:** Accept static-scene limitation. Dynamic support is advanced feature for Phase 3.

---

## 10. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Updated memory budget: 5.2 MB → 10.4 MB (accounting for ping-pong)
2. Total VRAM requirement: 2.5 GB → 3.0 GB minimum
3. Added note about downsampling losing detail (documented limitation)
4. Added note about upsampling artifacts (documented risk)
5. Extended camera reset to clear ALL cascade atlases (not just C0)
6. Added injection weight presets: Conservative (0.3), Balanced (0.5), Aggressive (0.7)
7. Clarified static-scene assumption in limitations section
```

### Unchanged Core Design

```text
✓ 5-level cascade hierarchy (C0-C4) retained
✓ Resolution strategy (32³ → 2³) unchanged
✓ Downsampling via 2×2×2 averaging unchanged
✓ Upsampling via trilinear interpolation unchanged
✓ Distance-based LOD selection unchanged
✓ Separate textures per cascade level unchanged
```

### Risk Mitigation Added

```text
✓ Documented downsampling detail loss
✓ Documented upsampling artifact risk
✓ Camera reset extended to all cascades
✓ Injection weight made tunable with presets
✓ Static-scene limitation clearly stated
✓ Rollback plan includes disable checkbox
```

---

## 11. Next Steps

After plan approval:

```text
1. Implement Step 1: Add cascade textures (C++)
2. Implement Step 2: Create downsample shader
3. Implement Step 3: Create upsample shader
4. Implement Step 4: Update dispatch functions
5. Implement Step 5: Integrate with render loop
6. Implement Step 6: Add UI controls
7. Build and test
8. Capture comparison sequence (Phase 2D vs 2E)
9. Self-critique implementation (Stage 2)
10. Document results
```

**Estimated Implementation Time:** 3-4 hours (coding + testing + documentation)

**Dependencies:** Phase 2D complete (feedback system working)

**Risk Level:** Medium-High (complex multi-pass pipeline, potential for subtle bugs)

---

## 12. Comparison to ShaderToy Reference

**Target Behavior (from ShaderToy):**

```text
- 5 cascade levels with progressive downsampling
- Hierarchical injection from coarse to fine
- Multi-bounce indirect lighting visible
- Color bleeding between surfaces
- Soft shadows from indirect bounces
- Stable convergence over ~30 frames
```

**Our Implementation Goals:**

```text
✓ Match cascade count (5 levels)
✓ Match resolution strategy (2× downsampling per level)
✓ Match injection direction (coarse → fine)
✓ Achieve visible color bleeding
✓ Achieve soft indirect shadows
✓ Achieve stable convergence
≈ Approximate radiance values (within 20% of reference)
```

**Known Differences:**

```text
- ShaderToy uses custom angular distribution (we use simplified hemisphere)
- ShaderToy may use importance-based LOD (we use distance-based)
- ShaderToy injection weights may differ (we start with 0.5 heuristic)
- ShaderToy may have additional optimizations (we prioritize clarity)
```

**Success Definition:** Visually similar GI quality, not pixel-perfect match.
