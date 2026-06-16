# ShaderToy2 Phase 2D Plan — Persistent Feedback / Recursive Bounce Closure

**Date:** 2026-05-29  
**Status:** Planning + Self-Critique  
**Scope:** Implement persistent ping-pong feedback loop for surface radiance cascades. Enable multi-bounce GI through temporal accumulation. Requires box charts (Phase 2C) to be complete first.

---

## 1. Motivation & Context

### Current State (After Phase 2C)

```text
✓ Room-plane charts implemented (Charts 1-5)
✓ Box charts implemented (Charts 7-18: short_box + tall_box)
✓ Direct lighting computed at hit points (modes 10, 13, 14, 15)
✓ Single-frame direct radiance atlas write (mode 15)
✓ Chart 6 remains inactive (front wall reserved)
✓ Unknown hit rate reduced from ~12% to < 2%

✗ No temporal accumulation / feedback
✗ No multi-bounce indirect lighting
✗ No cascade merge from lower levels
✗ No final GI lookup in raymarch pass
```

### Problem Statement

Current surface RC path computes only **direct lighting** per frame. This produces:

```text
- Grainy/noisy appearance (no temporal smoothing)
- No color bleeding between surfaces
- No soft shadows from indirect bounces
- No global illumination effects
```

To achieve true GI, we need:

```text
1. Temporal accumulation (feedback from previous frame)
2. Multi-bounce computation (indirect light from other surfaces)
3. Cascade hierarchy (coarse-to-fine detail propagation)
```

### Why Now?

Phase 2C completed box chart support, which was a **hard prerequisite** for feedback:

```text
Before Phase 2C: ~12% unknown hits → feedback would corrupt atlas with missing data
After Phase 2C:  < 2% unknown hits → feedback can safely accumulate on complete geometry
```

---

## 2. Proposed Architecture

### 2.1 Ping-Pong Atlas System

**Concept:** Maintain two atlas textures that alternate roles each frame:

```text
Frame N:
  - Read from: atlas_A (previous frame's accumulated radiance)
  - Write to:  atlas_B (current frame's result)
  
Frame N+1:
  - Read from: atlas_B (previous frame's accumulated radiance)
  - Write to:  atlas_A (current frame's result)
```

**Texture Specifications:**

```cpp
// Two RGBA16F textures, same dimensions as current atlas
GLuint atlasPing;   // 2560 × 1536 × 4 bytes ≈ 15.7 MB
GLuint atlasPong;   // 2560 × 1536 × 4 bytes ≈ 15.7 MB
// Total additional memory: ~31.4 MB (acceptable)
```

**Uniforms:**

```glsl
uniform sampler2D uAtlasRead;   // Previous frame's accumulated radiance
uniform int uAtlasWriteTarget;  // 0 = ping, 1 = pong (for debug visualization)
```

### 2.2 Feedback Formula

**Temporal Accumulation:**

```glsl
// For each probe texel in atlas:
vec3 prevRadiance = texture(uAtlasRead, uv).rgb;
vec3 currDirect   = computeDirectLighting(hitPos, normal);
vec3 currIndirect = sampleLowerCascade(hitPos, normal);  // Phase 2E later

// Exponential moving average (EMA):
float alpha = 0.1;  // Blend factor (tunable)
vec3 newRadiance = mix(prevRadiance, currDirect + currIndirect, alpha);

// Write to output atlas:
imageStore(oRadianceAtlas, p, vec4(newRadiance, 1.0));
```

**Key Parameters:**

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `alpha` | 0.05 - 0.15 | Lower = smoother but slower convergence |
| Initial value | Black (0,0,0) | First frame starts from zero |
| Reset trigger | Camera move / scene change | Prevent ghosting artifacts |

### 2.3 Cascade Hierarchy (Deferred to Phase 2E)

**Not implemented in Phase 2D**, but architecture must support future expansion:

```text
Cascade 0 (finest):  Per-probe direct + indirect from Cascade 1
Cascade 1:           Aggregated from Cascade 2
Cascade 2:           Aggregated from Cascade 3
Cascade 3:           Aggregated from Cascade 4
Cascade 4 (coarsest): Global ambient term
```

**Phase 2D Scope:** Only Cascade 0 feedback. Higher cascades added in Phase 2E.

### 2.4 Integration Points

**Modified Shader Modes:**

```text
Mode 15: Direct-only atlas write (existing, keep for comparison)
Mode 17: Feedback-enabled atlas write (NEW - accumulates over time)
Mode 18: Feedback readback visualization (NEW - shows accumulated GI)
```

**Modified C++ Dispatch:**

```cpp
// In dispatchRadianceDebug():
if (radianceDebugMode == 17 || radianceDebugMode == 18) {
    // Bind ping-pong textures
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, currentReadAtlas);
    glUniform1i(glGetUniformLocation(program, "uAtlasRead"), 1);
    
    // Write to opposite atlas
    glBindImageTexture(0, currentWriteAtlas, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
}
```

**Frame Management:**

```cpp
class SurfaceRC {
private:
    GLuint atlasPing, atlasPong;
    bool writeToPing;  // Toggle each frame
    
public:
    void flipAtlases();  // Called after each render frame
    GLuint getCurrentReadAtlas() const;
    GLuint getCurrentWriteAtlas() const;
};
```

---

## 3. Implementation Steps

### Step 1: Add Ping-Pong Textures (C++)

**File:** `src/surface_rc.h`

```cpp
// Add members:
GLuint atlasPing;
GLuint atlasPong;
bool writeToPing;  // true = write to ping, read from pong
```

**File:** `src/surface_rc.cpp`

```cpp
// In constructor:
atlasPing(0), atlasPong(0), writeToPing(true)

// In initialize():
glGenTextures(1, &atlasPing);
glBindTexture(GL_TEXTURE_2D, atlasPing);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ringAtlasWidth, ringAtlasHeight, 0,
             GL_RGBA, GL_HALF_FLOAT, nullptr);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
// Same for atlasPong

// In destroy():
if (atlasPing) glDeleteTextures(1, &atlasPing);
if (atlasPong) glDeleteTextures(1, &atlasPong);

// New method:
void SurfaceRC::flipAtlases() {
    writeToPing = !writeToPing;
}

GLuint SurfaceRC::getCurrentReadAtlas() const {
    return writeToPing ? atlasPong : atlasPing;
}

GLuint SurfaceRC::getCurrentWriteAtlas() const {
    return writeToPing ? atlasPing : atlasPong;
}
```

### Step 2: Update Dispatch Functions

**File:** `src/surface_rc.cpp`

```cpp
// In dispatchRadianceDebug():
if (radianceDebugMode == 17 || radianceDebugMode == 18) {
    // Bind read atlas
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, getCurrentReadAtlas());
    glUniform1i(glGetUniformLocation(computeProgram, "uAtlasRead"), 1);
    
    // Bind write atlas
    glBindImageTexture(0, getCurrentWriteAtlas(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
} else if (radianceDebugMode == 15) {
    // Mode 15: single-frame direct write (existing behavior)
    glBindImageTexture(0, directAtlasTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
}
```

**File:** `demo3d.cpp`

```cpp
// After render loop iteration:
if (useSurfaceRC && surfaceRC) {
    surfaceRC->flipAtlases();  // Swap ping-pong buffers
}
```

### Step 3: Extend Shader with Feedback Logic

**File:** `res/shaders/surface_radiance_debug.comp`

```glsl
// Add uniforms:
uniform sampler2D uAtlasRead;       // Previous frame's accumulated radiance
uniform float uFeedbackAlpha;       // EMA blend factor (default 0.1)
uniform int uResetFeedback;         // 1 = clear atlases to black

// Add mode 17: Feedback-enabled write
else if (uDebugMode == 17) {
    if (tr.state == 1 && hs.valid) {
        vec2 uv = vec2(p) / vec2(uAtlasSize);
        vec3 prevRadiance = texture(uAtlasRead, uv).rgb;
        
        // Compute current direct lighting
        vec3 hitNormal = estimateNormal(tr.pos);
        vec3 L = uLightPos - tr.pos;
        float dist2 = max(dot(L, L), 1e-4);
        vec3 lightDir = normalize(L);
        float ndotl = max(dot(hitNormal, lightDir), 0.0);
        vec3 direct = uLightColor * ndotl / dist2;
        
        // Apply shadow visibility (optional, controlled by mode)
        if (uDebugMode >= 13) {
            float visibility = shadowVisibility(tr.pos, lightDir, sqrt(dist2));
            direct *= visibility;
        }
        
        // Temporal accumulation (EMA)
        vec3 newRadiance = mix(prevRadiance, direct, uFeedbackAlpha);
        
        imageStore(oRadianceDebug, p, vec4(newRadiance, 1.0));
        return;
    } else {
        // Clear non-hit texels
        imageStore(oRadianceDebug, p, vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }
}

// Add mode 18: Feedback readback
else if (uDebugMode == 18) {
    vec2 uv = vec2(p) / vec2(uAtlasSize);
    vec4 accumulated = texture(uAtlasRead, uv);
    rgb = accumulated.rgb;
    a = accumulated.a;
}
```

### Step 4: Add Feedback Controls

**File:** `src/demo3d.h`

```cpp
// Add UI controls:
float surfaceFeedbackAlpha = 0.1f;  // EMA blend factor
bool surfaceResetFeedback = false;  // Reset button flag
```

**File:** `src/demo3d.cpp`

```cpp
// In ImGui UI section:
if (ImGui::CollapsingHeader("Surface RC Feedback")) {
    ImGui::SliderFloat("Feedback Alpha", &surfaceFeedbackAlpha, 0.01f, 0.5f, "%.2f");
    if (ImGui::Button("Reset Accumulation")) {
        surfaceResetFeedback = true;
    }
    ImGui::Text("Mode 17: Feedback write | Mode 18: Feedback readback");
}

// Pass to shader:
glUniform1f(glGetUniformLocation(program, "uFeedbackAlpha"), surfaceFeedbackAlpha);
glUniform1i(glGetUniformLocation(program, "uResetFeedback"), surfaceResetFeedback ? 1 : 0);
surfaceResetFeedback = false;  // Reset flag after use
```

### Step 5: Handle Camera Movement

**Problem:** When camera moves, accumulated radiance becomes invalid (ghosting).

**Solution:** Detect camera movement and reset feedback.

**File:** `src/demo3d.cpp`

```cpp
// Track previous camera position:
static glm::vec3 prevCameraPos = glm::vec3(0.0f);
static float prevCameraYaw = 0.0f;
static float prevCameraPitch = 0.0f;

// In render loop:
glm::vec3 currCameraPos = camera.position;
float currCameraYaw = camera.yaw;
float currCameraPitch = camera.pitch;

bool cameraMoved = length(currCameraPos - prevCameraPos) > 0.01f ||
                   abs(currCameraYaw - prevCameraYaw) > 0.1f ||
                   abs(currCameraPitch - prevCameraPitch) > 0.1f;

if (cameraMoved && useSurfaceRC && surfaceRC) {
    surfaceRC->clearAtlases();  // New method to zero out both atlases
}

prevCameraPos = currCameraPos;
prevCameraYaw = currCameraYaw;
prevCameraPitch = currCameraPitch;
```

**File:** `src/surface_rc.h/cpp`

```cpp
// Add method:
void SurfaceRC::clearAtlases() {
    // Fill both atlases with zeros
    std::vector<float> zeros(ringAtlasWidth * ringAtlasHeight * 4, 0.0f);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasPing);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ringAtlasWidth, ringAtlasHeight,
                    GL_RGBA, GL_HALF_FLOAT, zeros.data());
    
    glBindTexture(GL_TEXTURE_2D, atlasPong);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ringAtlasWidth, ringAtlasHeight,
                    GL_RGBA, GL_HALF_FLOAT, zeros.data());
}
```

---

## 4. Testing Strategy

### 4.1 Visual Verification

**Capture Sequence:**

```powershell
# Frame 1: Initial state (should be black)
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --screenshot=tools/phase2d_visual/frame000.png --exit-frames=1

# Frames 1-10: Convergence progression
for ($i = 1; $i -le 10; $i++) {
  .\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
    --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
    --screenshot="tools/phase2d_visual/frame$( '{0:D3}' -f $i ).png" \
    --exit-frames=$i
}

# Frame 50: Near-convergence
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --screenshot=tools/phase2d_visual/frame050.png --exit-frames=50
```

**Expected Results:**

```text
Frame 0:   All black (initial state)
Frame 1:   Direct lighting visible, noisy
Frame 5:   Smoother, some indirect contribution starting
Frame 10:  Noticeably smoother, color bleeding beginning
Frame 50:  Converged, smooth GI with soft shadows
```

### 4.2 Statistical Checks

**Pixel Count Analysis:**

```text
Mode 18, Frame 1:
  nonzero pixels: ~8000 (matches direct-only count from mode 15)
  bright (>0.5):  ~6000
  
Mode 18, Frame 10:
  nonzero pixels: ~8000 (same, no new geometry)
  bright (>0.5):  ~6500 (increased due to accumulation)
  
Mode 18, Frame 50:
  nonzero pixels: ~8000
  bright (>0.5):  ~7000 (converged)
```

**Convergence Metric:**

```python
# Compute RMS difference between consecutive frames:
rms_diff = sqrt(mean((frame_N - frame_N-1)^2))

Expected:
  Frame 1→2:  rms_diff ≈ 0.3  (large change)
  Frame 5→6:  rms_diff ≈ 0.1  (moderate change)
  Frame 10→11: rms_diff ≈ 0.03 (small change)
  Frame 50→51: rms_diff ≈ 0.005 (near-zero, converged)
```

### 4.3 Camera Movement Test

```powershell
# Render 20 frames static
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --screenshot=tools/phase2d_visual/static_frame20.png --exit-frames=20

# Move camera slightly, render 1 more frame (should reset)
# Manually move camera in viewport, then capture
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 \
  --surface-debug-target=radiance --surface-radiance-debug-mode=18 \
  --screenshot=tools/phase2d_visual/post_move_frame1.png --exit-frames=1

# Expected: post_move_frame1.png should be nearly black (reset occurred)
```

---

## 5. Performance Considerations

### Memory Impact

```text
Additional textures: 2 × 2560 × 1536 × 4 bytes = 31.4 MB
Total surface RC memory: ~47 MB (was 15.7 MB)
Acceptable for modern GPUs (2GB+ VRAM requirement already exists)
```

### Computational Cost

```text
Per-pixel operations added:
  - 1 texture fetch (uAtlasRead): ~1-2 cycles
  - 1 mix() operation: ~1 cycle
  - Total overhead: < 5% of shader time
  
Overall frame time impact: < 2% (shader-bound, not CPU-bound)
```

### Bandwidth

```text
Per frame:
  - Read: 2560 × 1536 × 4 bytes = 15.7 MB
  - Write: 2560 × 1536 × 4 bytes = 15.7 MB
  - Total: 31.4 MB/frame
  
At 60 FPS: 1.88 GB/s (negligible on modern GPUs with 100+ GB/s bandwidth)
```

---

## 6. Known Limitations & Future Work

### Not Implemented in Phase 2D

```text
✗ Multi-bounce indirect lighting (requires cascade hierarchy)
✗ Surface cascade merge (coarse-to-fine propagation)
✗ Final GI lookup in raymarch pass
✗ Adaptive alpha based on motion/variance
✗ Variance-guided denoising
✗ EXR/PT quality metrics for validation
```

### Deferred to Phase 2E

```text
- Cascade hierarchy (Levels 0-4)
- Inter-cascade injection
- LOD selection for probes
- Hierarchical importance sampling
```

### Potential Issues

```text
1. Ghosting on fast camera movement (mitigated by reset on move)
2. Slow convergence in dark regions (may need higher alpha)
3. Fireflies from high-variance samples (future: variance clamping)
4. No handling of dynamic lights (future: detect light changes)
```

---

## 7. Success Criteria

Phase 2D succeeds if:

```text
✓ Mode 17 writes accumulated radiance to ping-pong atlas
✓ Mode 18 reads back accumulated radiance correctly
✓ Visual convergence observed over 10-50 frames
✓ RMS difference between frames decreases monotonically
✓ Camera movement triggers atlas reset (no ghosting)
✓ Performance impact < 5% frame time
✓ Memory increase < 32 MB
✓ No regression in existing modes (0-16 still work)
✓ Box charts receive feedback (not just room planes)
```

**Quantitative Targets:**

```text
- Frame 1 → Frame 10: RMS difference drops by > 80%
- Frame 50: RMS difference < 0.01 (near-convergence)
- Frame time increase: < 2 ms at 128³ resolution
- Memory increase: < 32 MB total
```

---

## 8. Rollback Plan

If Phase 2D introduces critical bugs:

```text
1. Revert to mode 15 (single-frame direct) as fallback
2. Disable feedback by setting alpha = 1.0 (behaves like mode 15)
3. Keep ping-pong textures allocated but unused
4. Document failure mode for future debugging
```

**Fallback Command:**

```powershell
# Use mode 15 instead of 17/18 if feedback fails
--surface-radiance-debug-mode=15
```

---

## 9. Documentation Updates Required

After implementation:

```text
1. Update README.md with new debug modes (17, 18)
2. Add Phase 2D implementation document
3. Update architecture diagram to show feedback loop
4. Document feedback alpha tuning guidelines
5. Add troubleshooting section for ghosting artifacts
```

---

## 10. Self-Critique of Plan

### SC1 — Ping-pong doubles memory usage

**Critique:** Adding 31.4 MB may push low-end GPUs over memory budget.

**Mitigation:** 
- Document minimum VRAM requirement increase (2GB → 2.5GB)
- Provide option to disable feedback on low-memory systems
- Consider single-atlas approach with read-modify-write (slower but less memory)

**Decision:** Accept memory increase. Modern GPUs have 4-8GB+, and 31.4 MB is < 1% of typical capacity.

---

### SC2 — EMA convergence may be too slow

**Critique:** With alpha=0.1, needs ~40 frames to reach 98% convergence. At 30 FPS, that's 1.3 seconds of visible noise.

**Mitigation:**
- Make alpha tunable via UI (already planned)
- Provide presets: Fast (alpha=0.2), Balanced (alpha=0.1), Smooth (alpha=0.05)
- Consider adaptive alpha based on pixel variance (deferred to Phase 2E)

**Decision:** Start with alpha=0.1, allow user tuning. Document convergence characteristics.

---

### SC3 — Camera movement detection may be too sensitive/insensitive

**Critique:** Threshold of 0.01 units may trigger resets on tiny jitters, or miss intentional small movements.

**Mitigation:**
- Make threshold tunable: `surfaceCameraMoveThreshold`
- Default to 0.05 units (more forgiving)
- Add yaw/pitch thresholds separately (0.5 degrees)

**Decision:** Use conservative defaults (0.05 position, 0.5° rotation). Allow tuning if users report issues.

---

### SC4 — No handling of dynamic scene changes

**Critique:** If objects move or lights change, accumulated radiance becomes invalid but won't reset automatically.

**Mitigation:**
- Manual "Reset Accumulation" button (already planned)
- Future: detect scene hash changes (deferred)
- Document limitation: feedback assumes static scene

**Decision:** Accept limitation for Phase 2D. Dynamic scenes are advanced use case for Phase 3+.

---

### SC5 — Shadow visibility recomputed every frame

**Critique:** Shadow tracing is expensive and redundant if scene/lights don't change. Could cache shadow results.

**Mitigation:**
- Profile shadow cost first
- If > 20% of frame time, consider shadow caching (deferred)
- For now, recompute shadows each frame (simpler, correct for moving lights)

**Decision:** Recompute shadows each frame. Optimization deferred until performance profiling identifies it as bottleneck.

---

### SC6 — No validation against path-traced ground truth

**Critique:** Without PT reference, can't verify correctness of accumulated GI. May converge to wrong answer.

**Mitigation:**
- Compare mode 18 (accumulated) vs mode 15 (direct-only) visually
- Expect mode 18 to show softer shadows, color bleeding
- Future: add PT renderer for validation (Phase 3)

**Decision:** Use qualitative visual checks for Phase 2D. Quantitative validation deferred to Phase 3 PT integration.

---

## 11. Improved Plan Summary

Based on self-critique, the following adjustments are made:

### Changes from Original Plan

```text
1. Camera movement threshold increased: 0.01 → 0.05 units
2. Rotation threshold added: 0.5 degrees for yaw/pitch
3. Alpha presets documented: Fast (0.2), Balanced (0.1), Smooth (0.05)
4. Memory requirement updated: 2GB → 2.5GB minimum
5. Shadow caching deferred (recompute each frame for now)
6. Manual reset button emphasized for dynamic scenes
```

### Unchanged Core Design

```text
✓ Ping-pong atlas system retained
✓ EMA formula unchanged
✓ Mode 17/18 design unchanged
✓ Cascade hierarchy deferred to Phase 2E
✓ Box chart support maintained (Phase 2C prerequisite met)
```

### Risk Mitigation Added

```text
✓ Conservative camera movement thresholds
✓ Tunable alpha with presets
✓ Manual reset for edge cases
✓ Fallback to mode 15 if feedback fails
✓ Documentation of limitations
```

---

## 12. Next Steps

After plan approval:

```text
1. Implement Step 1: Add ping-pong textures (C++)
2. Implement Step 2: Update dispatch functions
3. Implement Step 3: Extend shader with feedback logic
4. Implement Step 4: Add feedback controls
5. Implement Step 5: Handle camera movement
6. Build and test
7. Capture convergence sequence
8. Self-critique implementation
9. Document results
```

**Estimated Implementation Time:** 2-3 hours (coding + testing + documentation)

**Dependencies:** None (Phase 2C already complete)

**Risk Level:** Low (incremental addition, rollback plan exists)
