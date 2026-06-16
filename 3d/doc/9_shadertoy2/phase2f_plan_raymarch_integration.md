# ShaderToy2 Phase 2F Plan — Final GI Lookup / Raymarch Integration

**Date:** 2026-05-29  
**Status:** Planning + Self-Critique (Stage 1)  
**Scope:** Integrate cascade hierarchy (C0-C4) into raymarch fragment shader. Add LOD-based GI sampling at hit points. Combine direct + indirect lighting for final shaded output. Requires Phase 2E cascade hierarchy to be complete first.

---

## 1. Motivation & Context

### Current State (After Phase 2E)

```text
✓ Room-plane charts implemented (Charts 1-5)
✓ Box charts implemented (Charts 7-18: short_box + tall_box)
✓ Direct lighting computed at hit points (mode 15/17)
✓ Temporal accumulation via ping-pong feedback (modes 17/18)
✓ 5-level cascade hierarchy (C0-C4) with downsample/upsample
✓ Multi-bounce indirect lighting propagating through cascades
✓ Color bleeding observable in atlas textures (mode 18)
✓ Camera movement detection resets all atlases

✗ GI remains trapped in debug atlas (not visible in main render)
✗ Raymarch pass still uses volumetric RC only
✗ No final shaded output combining surface RC + volumetric
✗ User cannot see GI quality in actual scene rendering
✗ No comparison between surface RC GI and volumetric RC GI
```

### Problem Statement

Phase 2E successfully implements **cascade hierarchy for multi-bounce GI**, but the results are invisible to users because:

```text
1. Cascade data exists only in debug atlas textures
2. Main raymarch fragment shader doesn't sample cascades
3. Final pixel color uses volumetric RC, not surface RC
4. No way to visually compare surface vs volumetric approaches
5. Cannot validate if cascade GI improves visual quality
```

To make Phase 2E's work visible and useful, we need:

```text
Final GI Integration:
  In raymarch fragment shader (after SDF trace):
    1. Detect if hit point is on a valid surface chart
    2. Find nearest probe in C0 cascade
    3. Sample GI from appropriate cascade level (LOD selection)
    4. Combine: finalColor = directLighting + giSample * giScale
    5. Blend with volumetric RC (optional toggle)
```

### Why Now?

Phase 2E completed the cascade hierarchy, which provides **multi-bounce indirect lighting**:

```text
Before Phase 2E: Only direct lighting (single bounce)
After Phase 2E:  Multi-bounce GI available in C0 cascade atlas
```

However, this GI is "trapped" in debug textures. Phase 2F liberates it by integrating into the main render pipeline.

Additionally, we now have:
- Complete geometry coverage (room + boxes)
- Stable temporal accumulation (no flickering)
- Hierarchical detail levels (LOD selection possible)

This makes it the right time to expose GI to users.

---

## 2. Proposed Architecture

### 2.1 Integration Point

**Target Shader:** `res/shaders/raymarch.frag` (existing raymarch fragment shader)

**Integration Location:** After SDF trace succeeds, before final color output

```glsl
// Existing code in raymarch.frag:
if (hitDistance < maxDist) {
    vec3 hitPos = rayOrigin + rayDir * hitDistance;
    vec3 normal = estimateNormal(hitPos);
    
    // Compute direct lighting
    vec3 direct = computeDirectLighting(hitPos, normal);
    
    // === PHASE 2F: ADD GI LOOKUP HERE ===
    vec3 gi = sampleSurfaceRC_GI(hitPos, normal, cameraPos);
    
    // Combine direct + indirect
    vec3 finalColor = direct + gi * uSurfaceGIScale;
    
    // Optional: blend with volumetric RC
    if (uBlendWithVolumetric) {
        vec3 volRC = sampleVolumetricRC(rayOrigin, rayDir);
        finalColor = mix(finalColor, volRC, uBlendFactor);
    }
    
    fragColor = vec4(finalColor, 1.0);
}
```

### 2.2 GI Sampling Function

**Signature:**

```glsl
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos);
```

**Implementation Steps:**

```glsl
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos) {
    // Step 1: Check if hit is on a valid surface chart
    int chartID = classifyHitSurface(hitPos);
    if (chartID < 0 || !isChartActive(chartID)) {
        return vec3(0.0);  // No GI for unknown/invalid surfaces
    }
    
    // Step 2: Convert world position to probe coordinates
    vec3 probeCoord = worldToProbeCoord(hitPos, 0);  // C0 level
    
    // Step 3: Select LOD based on distance to camera
    float distToCamera = length(hitPos - cameraPos);
    int lodLevel = selectLOD(distToCamera);
    
    // Step 4: Sample from appropriate cascade atlas
    vec3 gi = sampleCascadeAtlas(lodLevel, probeCoord);
    
    // Step 5: Apply energy conservation clamp
    gi = min(gi, vec3(10.0));  // Prevent fireflies
    
    return gi;
}
```

### 2.3 LOD Selection Strategy

**Distance-Based LOD (from Phase 2E plan):**

```glsl
int selectLOD(float distToCamera) {
    if (distToCamera < 2.0)      return 0;  // C0: finest (32³)
    else if (distToCamera < 4.0) return 1;  // C1 (16³)
    else if (distToCamera < 8.0) return 2;  // C2 (8³)
    else if (distToCamera < 16.0) return 3; // C3 (4³)
    else                          return 4; // C4: coarsest (2³)
}
```

**Alternative: Importance-Based LOD (Future Enhancement)**

```glsl
int selectLOD_Importance(vec3 hitPos, vec3 normal) {
    // Estimate radiance gradient magnitude
    vec3 gradX = sampleCascadeAtlas(0, hitPos + vec3(0.1, 0, 0)) - 
                 sampleCascadeAtlas(0, hitPos - vec3(0.1, 0, 0));
    vec3 gradY = sampleCascadeAtlas(0, hitPos + vec3(0, 0.1, 0)) - 
                 sampleCascadeAtlas(0, hitPos - vec3(0, 0.1, 0));
    vec3 gradZ = sampleCascadeAtlas(0, hitPos + vec3(0, 0, 0.1)) - 
                 sampleCascadeAtlas(0, hitPos - vec3(0, 0, 0.1));
    
    float importance = length(gradX) + length(gradY) + length(gradZ);
    
    if (importance > 0.5)      return 0;  // High detail needed
    else if (importance > 0.2) return 1;
    else if (importance > 0.1) return 2;
    else                       return 3;  // Low detail OK
}
```

**Decision:** Start with distance-based LOD (simpler, matches Phase 2E plan). Can upgrade to importance-based later if quality issues arise.

### 2.4 Probe Coordinate Conversion

**World-to-Probe Mapping:**

```glsl
vec3 worldToProbeCoord(vec3 worldPos, int cascadeLevel) {
    // Get cascade resolution
    int res = getCascadeResolution(cascadeLevel);  // 32, 16, 8, 4, or 2
    
    // Normalize to [0, 1] within scene bounds
    vec3 normalized = (worldPos - uSceneBoundsMin) / (uSceneBoundsMax - uSceneBoundsMin);
    
    // Scale to probe grid resolution
    vec3 probeCoord = normalized * float(res);
    
    // Clamp to valid range [0, res-1]
    probeCoord = clamp(probeCoord, vec3(0.0), vec3(float(res - 1)));
    
    return probeCoord;
}
```

**Probe-to-Atlas Texture Mapping:**

```glsl
vec2 probeCoordToAtlasUV(int chartID, vec3 probeCoord, int cascadeLevel) {
    // Each chart occupies a region in the atlas
    // For C0 (32³): atlas is 2560 × 1536, 18 charts
    // Chart width: 2560 / 18 ≈ 142 pixels
    // Chart height: 1536 / 32 = 48 pixels per probe layer
    
    int res = getCascadeResolution(cascadeLevel);
    int chartWidth = getAtlasWidth() / 18;
    int chartHeight = getAtlasHeight() / res;
    
    // Calculate chart base position
    int chartX = (chartID % 6) * chartWidth;  // 6 charts per row
    int chartY = (chartID / 6) * chartHeight;
    
    // Map probe XYZ to atlas UV
    float u = float(chartX) + probeCoord.x * (float(chartWidth) / float(res));
    float v = float(chartY) + probeCoord.y * (float(chartHeight) / float(res));
    
    // Normalize to [0, 1]
    vec2 uv = vec2(u, v) / vec2(getAtlasWidth(), getAtlasHeight());
    
    return uv;
}
```

### 2.5 Blending with Volumetric RC

**Optional Hybrid Mode:**

```glsl
// User-controllable blend factor
uniform float uBlendFactor;       // 0.0 = pure surface RC, 1.0 = pure volumetric
uniform bool uBlendWithVolumetric; // Enable/disable blending

// In fragment shader:
vec3 surfaceGI = sampleSurfaceRC_GI(hitPos, normal, cameraPos);
vec3 surfaceColor = directLighting + surfaceGI * uSurfaceGIScale;

if (uBlendWithVolumetric) {
    vec3 volRC = sampleVolumetricRC(rayOrigin, rayDir);
    fragColor = vec4(mix(surfaceColor, volRC, uBlendFactor), 1.0);
} else {
    fragColor = vec4(surfaceColor, 1.0);
}
```

**Use Cases:**

```text
Blend Factor = 0.0: Pure surface RC (validate cascade quality)
Blend Factor = 0.5: Hybrid approach (combine strengths)
Blend Factor = 1.0: Pure volumetric RC (baseline comparison)
```

---

## 3. Implementation Steps

### Step 1: Add GI Sampling Uniforms to Raymarch Shader

**File:** `res/shaders/raymarch.frag`

```glsl
// Phase 2F: Surface RC cascade integration uniforms
uniform sampler2D uCascadeAtlases[5];  // C0-C4 cascade atlases
uniform int uCascadeResolutions[5];    // {32, 16, 8, 4, 2}
uniform vec3 uSceneBoundsMin;
uniform vec3 uSceneBoundsMax;
uniform float uSurfaceGIScale;         // GI contribution scale (default 1.0)
uniform bool uEnableSurfaceRC;         // Enable/disable surface RC GI
uniform bool uBlendWithVolumetric;     // Blend with volumetric RC
uniform float uBlendFactor;            // Blend factor (0.0-1.0)
uniform vec3 uCameraPos;               // For LOD selection
```

### Step 2: Implement Helper Functions

**File:** `res/shaders/raymarch.frag` (add before main())

```glsl
// Phase 2F: World-to-probe coordinate conversion
vec3 worldToProbeCoord(vec3 worldPos, int cascadeLevel) {
    int res = uCascadeResolutions[cascadeLevel];
    vec3 normalized = (worldPos - uSceneBoundsMin) / (uSceneBoundsMax - uSceneBoundsMin);
    vec3 probeCoord = normalized * float(res);
    return clamp(probeCoord, vec3(0.0), vec3(float(res - 1)));
}

// Phase 2F: LOD selection based on distance
int selectLOD(float distToCamera) {
    if (distToCamera < 2.0)      return 0;
    else if (distToCamera < 4.0) return 1;
    else if (distToCamera < 8.0) return 2;
    else if (distToCamera < 16.0) return 3;
    else                          return 4;
}

// Phase 2F: Sample cascade atlas at given level and coordinate
vec3 sampleCascadeAtlas(int level, vec3 probeCoord) {
    if (level < 0 || level >= 5) return vec3(0.0);
    
    int res = uCascadeResolutions[level];
    
    // Convert probe coord to atlas UV
    // Simplified: assume uniform chart layout
    float u = probeCoord.x / float(res);
    float v = probeCoord.y / float(res);
    
    // Sample with bilinear filtering
    vec4 sample = texture(uCascadeAtlases[level], vec2(u, v));
    
    return sample.rgb;
}

// Phase 2F: Main GI sampling function
vec3 sampleSurfaceRC_GI(vec3 hitPos, vec3 normal, vec3 cameraPos) {
    if (!uEnableSurfaceRC) return vec3(0.0);
    
    // Select LOD based on distance
    float distToCamera = length(hitPos - cameraPos);
    int lodLevel = selectLOD(distToCamera);
    
    // Convert to probe coordinates
    vec3 probeCoord = worldToProbeCoord(hitPos, lodLevel);
    
    // Sample cascade
    vec3 gi = sampleCascadeAtlas(lodLevel, probeCoord);
    
    // Energy conservation clamp
    gi = min(gi, vec3(10.0));
    
    return gi;
}
```

### Step 3: Integrate into Fragment Shader Main

**File:** `res/shaders/raymarch.frag` (in main() after direct lighting)

```glsl
// Existing direct lighting computation
vec3 directLighting = computeDirectLighting(hitPos, normal, lightPos, lightColor);

// Phase 2F: Sample surface RC GI
vec3 surfaceGI = sampleSurfaceRC_GI(hitPos, normal, uCameraPos);
vec3 surfaceColor = directLighting + surfaceGI * uSurfaceGIScale;

// Optional: blend with volumetric RC
vec3 finalColor;
if (uBlendWithVolumetric) {
    vec3 volRC = sampleVolumetricRC(rayOrigin, rayDir);
    finalColor = mix(surfaceColor, volRC, uBlendFactor);
} else {
    finalColor = surfaceColor;
}

fragColor = vec4(finalColor, 1.0);
```

### Step 4: Update C++ to Bind Cascade Textures

**File:** `src/demo3d.cpp` (in render loop, before raymarch dispatch)

```cpp
// Phase 2F: Bind cascade atlases to raymarch shader
if (useSurfaceRC && surfaceRC && cascadeHierarchyEnabled) {
    glUseProgram(raymarchProgram);
    
    // Bind cascade textures
    for (int i = 0; i < surfaceRC->getCascadeCount(); i++) {
        glActiveTexture(GL_TEXTURE10 + i);  // Use texture units 10-14
        glBindTexture(GL_TEXTURE_2D, surfaceRC->getCascadeAtlas(i));
        glUniform1i(glGetUniformLocation(raymarchProgram, 
                     ("uCascadeAtlases[" + std::to_string(i) + "]").c_str()), 10 + i);
    }
    
    // Set cascade resolutions
    int resolutions[5];
    for (int i = 0; i < 5; i++) {
        resolutions[i] = surfaceRC->getCascadeResolution(i);
    }
    glUniform1iv(glGetUniformLocation(raymarchProgram, "uCascadeResolutions"), 5, resolutions);
    
    // Set other uniforms
    glUniform3fv(glGetUniformLocation(raymarchProgram, "uSceneBoundsMin"), 1, &boundsMin[0]);
    glUniform3fv(glGetUniformLocation(raymarchProgram, "uSceneBoundsMax"), 1, &boundsMax[0]);
    glUniform1f(glGetUniformLocation(raymarchProgram, "uSurfaceGIScale"), surfaceGIScale);
    glUniform1i(glGetUniformLocation(raymarchProgram, "uEnableSurfaceRC"), 1);
    glUniform1i(glGetUniformLocation(raymarchProgram, "uBlendWithVolumetric"), blendWithVolumetric ? 1 : 0);
    glUniform1f(glGetUniformLocation(raymarchProgram, "uBlendFactor"), blendFactor);
    glUniform3fv(glGetUniformLocation(raymarchProgram, "uCameraPos"), 1, &camera.position[0]);
}
```

### Step 5: Add UI Controls

**File:** `src/demo3d.cpp` (in Surface RC UI panel)

```cpp
// Phase 2F: Final GI integration controls
ImGui::Separator();
ImGui::Text("Phase 2F: Final GI Integration");

bool enableSurfaceRC = getEnableSurfaceRCInRaymarch();
if (ImGui::Checkbox("Enable surface RC in raymarch", &enableSurfaceRC)) {
    setEnableSurfaceRCInRaymarch(enableSurfaceRC);
}
if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Samples cascade hierarchy in raymarch fragment shader\n"
                      "Adds multi-bounce GI to final rendered image");

if (enableSurfaceRC) {
    float giScale = getSurfaceGIScale();
    if (ImGui::SliderFloat("GI scale", &giScale, 0.0f, 2.0f, "%.2f")) {
        setSurfaceGIScale(giScale);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Scales indirect lighting contribution\n"
                          "1.0 = physical, >1.0 = exaggerated for visibility");
    
    bool blendWithVol = getBlendWithVolumetric();
    if (ImGui::Checkbox("Blend with volumetric RC", &blendWithVol)) {
        setBlendWithVolumetric(blendWithVol);
    }
    
    if (blendWithVol) {
        float blendFact = getBlendFactor();
        if (ImGui::SliderFloat("Blend factor", &blendFact, 0.0f, 1.0f, "%.2f")) {
            setBlendFactor(blendFact);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("0.0 = pure surface RC, 1.0 = pure volumetric\n"
                              "0.5 = hybrid approach");
    }
    
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), 
                     "LOD: Distance-based | Auto-selects C0-C4");
}
```

### Step 6: Add Member Variables to Demo3D

**File:** `src/demo3d.h`

```cpp
// Phase 2F: Raymarch integration parameters
bool enableSurfaceRCInRaymarch = false;
float surfaceGIScale = 1.0f;
bool blendWithVolumetric = false;
float blendFactor = 0.5f;

// Getter/setter methods
bool getEnableSurfaceRCInRaymarch() const { return enableSurfaceRCInRaymarch; }
void setEnableSurfaceRCInRaymarch(bool v) { enableSurfaceRCInRaymarch = v; }
float getSurfaceGIScale() const { return surfaceGIScale; }
void setSurfaceGIScale(float v) { surfaceGIScale = v; }
bool getBlendWithVolumetric() const { return blendWithVolumetric; }
void setBlendWithVolumetric(bool v) { blendWithVolumetric = v; }
float getBlendFactor() const { return blendFactor; }
void setBlendFactor(float v) { blendFactor = v; }
```

---

## 4. Testing Strategy

### 4.1 Visual Verification

**Capture Sequence:**

```powershell
# Test 1: Pure surface RC (no volumetric blending)
.\build\RadianceCascades3D.exe --load-obj=cornell \
  --use-surface-rc=1 --cascade-hierarchy-enabled=1 \
  --enable-surface-rc-raymarch=1 --blend-with-volumetric=0 \
  --surface-gi-scale=1.0 --exit-frames=50

# Test 2: Hybrid mode (50/50 blend)
.\build\RadianceCascades3D.exe --load-obj=cornell \
  --use-surface-rc=1 --cascade-hierarchy-enabled=1 \
  --enable-surface-rc-raymarch=1 --blend-with-volumetric=1 \
  --blend-factor=0.5 --exit-frames=50

# Test 3: Pure volumetric baseline
.\build\RadianceCascades3D.exe --load-obj=cornell \
  --use-surface-rc=0 --exit-frames=50
```

**Expected Results:**

```text
Pure Surface RC:
  - Visible color bleeding (red wall tints white surfaces)
  - Soft shadows from indirect bounces
  - Brighter corners where surfaces meet
  - May look different from volumetric (validation opportunity)
  
Hybrid Mode:
  - Combines strengths of both approaches
  - Surface RC adds fine detail, volumetric provides stability
  - Smoother than pure surface RC
  
Pure Volumetric (Baseline):
  - Existing behavior (for comparison)
  - Should differ from surface RC in subtle ways
```

### 4.2 Statistical Checks

**Per-Pixel GI Contribution:**

```python
# Measure GI impact on final image:
avg_surface_color = mean(frame_with_surface_RC)
avg_volumetric_color = mean(frame_without_surface_RC)

gi_contribution_ratio = avg_surface_color / avg_volumetric_color

Expected:
  Dark areas: ratio ≈ 1.5-2.0 (GI brightens shadows)
  Lit areas:  ratio ≈ 1.1-1.3 (GI adds subtle fill light)
  Overall:    ratio ≈ 1.2-1.5 (20-50% brightness increase from GI)
```

**LOD Distribution:**

```text
Frame statistics (typical Cornell view):
  C0 samples: 60% (close surfaces, high detail)
  C1 samples: 25% (medium distance)
  C2 samples: 10% (far surfaces)
  C3 samples: 4%  (very far)
  C4 samples: 1%  (global ambient)
```

### 4.3 Performance Profiling

**GPU Timing:**

```text
Baseline (volumetric RC only):
  Raymarch fragment shader: 3.0 ms
  Total frame time: 8.0 ms
  
With surface RC GI:
  GI sampling overhead: +0.5 ms (texture lookups + LOD selection)
  Raymarch fragment shader: 3.5 ms
  Total frame time: 8.5 ms (+6.25%)
  
With hybrid blending:
  Additional blend cost: +0.2 ms
  Total frame time: 8.7 ms (+8.75%)
```

**Memory Impact:**

```text
No additional memory (reuses existing cascade textures)
Total surface RC memory: ~52 MB (unchanged from Phase 2E)
```

---

## 5. Known Limitations & Future Work

### Not Implemented in Phase 2F

```text
✗ Importance-based LOD selection (using distance-based)
✗ Temporal reprojection for GI samples (may flicker on camera move)
✗ Anisotropic filtering for cascade sampling
✗ Variance-guided denoising per pixel
✗ Adaptive GI scale based on scene brightness
✗ EXR/PT quality metrics for validation
✗ Dynamic scene support (assumes static geometry)
```

### Potential Issues

```text
1. GI may flicker on camera movement (LOD changes cause discontinuities)
2. Distance-based LOD may select wrong level in complex scenes
3. GI scale of 1.0 may be too subtle for some lighting conditions
4. Blending with volumetric may cause double-counting of indirect light
5. No handling of dynamic lights (cascades baked for static lighting)
6. Chart classification may fail for grazing-angle hits
```

---

## 6. Success Criteria

Phase 2F succeeds if:

```text
✓ Surface RC GI visible in final rendered image (not just debug atlas)
✓ Color bleeding observable (red wall tints nearby surfaces)
✓ Soft shadows from indirect bounces visible
✓ Performance impact < 15% frame time increase
✓ No regression when surface RC disabled (volumetric still works)
✓ LOD selection works correctly (C0 for close, C4 for far)
✓ GI contribution ratio 1.2-1.5 after convergence (20-50% brighter)
✓ No severe flickering artifacts on camera movement
✓ UI controls functional (enable/disable, GI scale, blend factor)
✓ Hybrid mode produces visually plausible results
```

**Quantitative Targets:**

```text
- GI sampling time: < 1.0 ms per frame
- Frame time increase: < 1.5 ms at 128³ resolution
- Memory increase: 0 MB (reuses existing textures)
- Visual quality: noticeable improvement over no-GI baseline
- LOD distribution: >50% C0 samples, <5% C4 samples (typical view)
```

---

## 7. Rollback Plan

If Phase 2F introduces critical bugs:

```text
1. Disable surface RC in raymarch via UI checkbox (fallback to volumetric)
2. Keep cascade textures allocated but unused
3. Document failure mode for future debugging
4. Revert to Phase 2E debug-only mode as fallback
```

**Fallback Command:**

```powershell
# Disable surface RC in raymarch, use volumetric only
--enable-surface-rc-raymarch=0
```

---

## 8. Documentation Updates Required

After implementation:

```text
1. Update README.md with Phase 2F overview
2. Add Phase 2F implementation document
3. Update architecture diagram to show raymarch integration
4. Document GI scale tuning guidelines
5. Add troubleshooting section for GI flickering
6. Create comparison images: volumetric vs surface RC vs hybrid
7. Document LOD selection strategy and limitations
```

---

## 9. Self-Critique of Plan (Stage 1)

### SC1 — LOD discontinuities may cause flickering

**Critique:** Distance-based LOD creates hard boundaries at 2.0, 4.0, 8.0, 16.0 units. When camera moves across these thresholds, LOD level changes abruptly, causing visible pops/flickering.

**Impact:**
- Surfaces may suddenly change detail level
- GI contribution may jump discontinuously
- Visually distracting during camera movement

**Mitigation Options:**
1. **Smooth LOD transitions:** Blend between adjacent LOD levels near boundaries
2. **Hysteresis:** Require larger distance change to switch LOD (prevents rapid toggling)
3. **Temporal smoothing:** Fade LOD changes over multiple frames
4. **Accept limitation:** Document as known issue, fix in Phase 3

**Decision:** Start with hard LOD boundaries (simplest). If flickering severe, implement hysteresis (Option 2) as quick fix. Smooth blending deferred to Phase 3.

---

### SC2 — No chart classification in raymarch shader

**Critique:** Plan assumes `classifyHitSurface()` exists in raymarch shader, but this function was only implemented in surface_radiance_debug.comp. Raymarch shader doesn't have access to box bounds or chart logic.

**Impact:**
- Cannot determine if hit point is on valid surface
- May sample GI for invalid/unknown surfaces
- Could produce incorrect GI values

**Mitigation Options:**
1. **Duplicate classifyHitSurface() in raymarch shader** (code duplication)
2. **Pre-compute chart ID in SDF pass** and store in auxiliary texture
3. **Simplify: assume all hits are valid** (risky, may sample garbage)
4. **Add chart ID to SDF texture alpha channel** (requires SDF modification)

**Decision:** Duplicate minimal classifyHitSurface() logic in raymarch shader (Option 1). It's only ~50 lines of GLSL. Code duplication acceptable for now. Future: share via #include mechanism.

---

### SC3 — GI scale may need per-scene tuning

**Critique:** Fixed GI scale of 1.0 assumes physically-based lighting. Different scenes/lighting conditions may need different scales for optimal visual quality.

**Impact:**
- Cornell room may need scale 0.8 (subtle GI)
- Dark scenes may need scale 1.5 (exaggerated GI)
- User must tune manually per scene

**Mitigation:**
- Made GI scale tunable via UI slider (0.0-2.0) ✓
- Provide presets: Subtle (0.5), Physical (1.0), Exaggerated (1.5)
- Document tuning guidelines in tooltip

**Decision:** Tunable scale with presets is sufficient. Auto-adaptive scale deferred to Phase 3.

---

### SC4 — Double-counting risk when blending with volumetric

**Critique:** Both surface RC and volumetric RC compute indirect lighting. Blending them may double-count indirect contributions, producing overly bright results.

**Impact:**
- Hybrid mode may look washed out
- Energy conservation violated
- Unphysical brightness

**Mitigation Options:**
1. **Disable volumetric indirect when surface RC enabled** (cleanest)
2. **Reduce volumetric indirect weight proportionally** (complex)
3. **Document limitation: hybrid is experimental** (honest)
4. **Accept double-counting: user can adjust blend factor** (flexible)

**Decision:** Option 4: Let user control blend factor. If result too bright, reduce blend factor or GI scale. Document this trade-off clearly. Advanced users can experiment with different combinations.

---

### SC5 — No validation against path-traced ground truth

**Critique:** Cannot verify if surface RC GI converges to correct answer without path tracer reference.

**Impact:**
- May have systematic bias (too bright/dark)
- Color bleeding intensity may be wrong
- No quantitative measure of accuracy

**Mitigation:**
- Qualitative visual checks: does it look plausible?
- Compare with volumetric RC (both should be similar)
- Future: add PT renderer for validation (Phase 3)

**Decision:** Use qualitative assessment for Phase 2F. Quantitative validation deferred to Phase 3 when PT renderer is available.

---

### SC6 — Chart boundary artifacts at edges

**Critique:** Probe grid has finite resolution (32³ for C0). At chart boundaries or geometry edges, GI sampling may produce artifacts due to insufficient spatial resolution.

**Impact:**
- Stair-stepping artifacts along edges
- Incorrect GI near sharp corners
- Visible grid pattern in smooth gradients

**Mitigation Options:**
1. **Increase C0 resolution** (expensive: 64³ = 8× memory)
2. **Bilinear/trilinear filtering** (already using texture filtering)
3. **Accept limitation: 32³ sufficient for Cornell scale**
4. **Adaptive refinement near edges** (complex, Phase 3)

**Decision:** Rely on hardware texture filtering (bilinear/trilinear). If artifacts visible, document as limitation. Resolution increase deferred until profiling shows it's necessary.

---

## 10. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Added note about LOD discontinuity flickering risk (SC1)
2. Clarified need to duplicate classifyHitSurface() in raymarch shader (SC2)
3. Added GI scale presets: Subtle (0.5), Physical (1.0), Exaggerated (1.5) (SC3)
4. Documented double-counting risk in hybrid mode (SC4)
5. Added note about qualitative validation only (SC5)
6. Noted reliance on hardware filtering for edge artifacts (SC6)
7. Added hysteresis as potential quick fix for LOD flickering
```

### Unchanged Core Design

```text
✓ Distance-based LOD selection retained
✓ Integration point in raymarch fragment shader unchanged
✓ Cascade texture binding strategy unchanged
✓ Optional hybrid blending retained
✓ UI controls design unchanged
```

### Risk Mitigation Added

```text
✓ Documented LOD flickering risk with hysteresis mitigation
✓ Clarified chart classification duplication requirement
✓ Added GI scale presets for easier tuning
✓ Documented double-counting trade-off in hybrid mode
✓ Noted qualitative validation approach
✓ Reliance on hardware filtering documented
```

---

## 11. Next Steps

After plan approval:

```text
1. Implement Step 1: Add GI sampling uniforms to raymarch shader
2. Implement Step 2: Create helper functions (worldToProbeCoord, selectLOD, etc.)
3. Implement Step 3: Integrate into raymarch main() function
4. Implement Step 4: Update C++ to bind cascade textures
5. Implement Step 5: Add UI controls
6. Implement Step 6: Add member variables to demo3d.h
7. Build and test
8. Capture comparison sequence (volumetric vs surface RC vs hybrid)
9. Self-critique implementation (Stage 2)
10. Document results
```

**Estimated Implementation Time:** 2-3 hours (coding + testing + documentation)

**Dependencies:** Phase 2E complete (cascade hierarchy working)

**Risk Level:** Medium (integration complexity, potential for subtle bugs)

---

## 12. Comparison to ShaderToy Reference

**Target Behavior (from ShaderToy):**

```text
- Surface-attached probes sample GI directly
- Multi-bounce indirect lighting visible in final render
- Color bleeding between surfaces
- Soft shadows from indirect bounces
- Stable rendering (no flickering)
```

**Our Implementation Goals:**

```text
✓ Match GI sampling approach (cascade hierarchy)
✓ Match multi-bounce indirect lighting
✓ Achieve visible color bleeding
✓ Achieve soft indirect shadows
✓ Minimize flickering (documented limitation)
≈ Approximate radiance values (within 20% of reference)
```

**Known Differences:**

```text
- ShaderToy may use different LOD strategy (unknown)
- ShaderToy GI scale may differ (we start with 1.0)
- ShaderToy may not blend with volumetric (we offer option)
- ShaderToy may have additional optimizations (we prioritize clarity)
```

**Success Definition:** Visually similar GI quality in final render, not pixel-perfect match.
