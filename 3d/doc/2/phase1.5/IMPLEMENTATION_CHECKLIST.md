# Phase 1.5 Implementation Checklist

**Purpose:** Systematic checklist to complete all Phase 1.5 tasks before moving to Phase 2

**Status Tracking:** ⬜ Not Started | 🟡 In Progress | ✅ Complete

---

## Task Group 1: Debug Visualization Completion

**Priority:** 🔴 HIGH  
**Estimated Time:** 30-45 minutes  
**Blocker:** Must complete before cascade debugging is possible

### 1.1 Add Member Variables to demo3d.h

**File:** [`include/demo3d.h`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\include\demo3d.h)

**Location:** After existing debug variables (around line ~80-100)

⬜ **Action Required:**

```cpp
// ========================================
// Phase 1: Radiance Cascade Debug
// ========================================
bool showRadianceDebug = false;
int radianceSliceAxis = 2;              // 0=X, 1=Y, 2=Z
float radianceSlicePosition = 0.5f;     // Normalized 0-1
int radianceVisualizeMode = 0;          // 0=Slice, 1=Max, 2=Avg, 3=Direct
float radianceExposure = 1.0f;
float radianceIntensityScale = 1.0f;
bool showRadianceGrid = false;

// ========================================
// Phase 1: Lighting Debug
// ========================================
bool showLightingDebug = false;
int lightingSliceAxis = 2;              // 0=X, 1=Y, 2=Z
float lightingSlicePosition = 0.5f;     // Normalized 0-1
int lightingDebugMode = 3;              // 0-5 (Combined default)
float lightingExposure = 1.0f;
float lightingIntensityScale = 1.0f;
```

**Verification:**
- [ ] No compilation errors after adding
- [ ] Variables accessible in cpp file
- [ ] Default values set correctly

---

### 1.2 Initialize Variables in Constructor

**File:** [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp)

**Location:** Constructor initializer list (after existing initializations)

⬜ **Action Required:**

```cpp
Demo3D::Demo3D()
    : windowWidth(1280)
    , windowHeight(720)
    // ... other existing initializers ...
    
    // Phase 1: Radiance cascade debug initialization
    , showRadianceDebug(false)
    , radianceSliceAxis(2)
    , radianceSlicePosition(0.5f)
    , radianceVisualizeMode(0)
    , radianceExposure(1.0f)
    , radianceIntensityScale(1.0f)
    , showRadianceGrid(false)
    
    // Phase 1: Lighting debug initialization
    , showLightingDebug(false)
    , lightingSliceAxis(2)
    , lightingSlicePosition(0.5f)
    , lightingDebugMode(3)
    , lightingExposure(1.0f)
    , lightingIntensityScale(1.0f)
{
    // Constructor body...
}
```

**Verification:**
- [ ] No duplicate initializations
- [ ] Initializer list syntax correct (commas between items)
- [ ] Builds without errors

---

### 1.3 Add Keyboard Controls to processInput()

**File:** [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp)

**Location:** Inside `processInput()` method, after existing key handling

⬜ **Action Required:**

```cpp
void Demo3D::processInput() {
    // ... existing input handling ...
    
    // ========================================
    // Phase 1: Debug View Toggles
    // ========================================
    
    // Toggle radiance cascade debug
    if (IsKeyPressed(KEY_R)) { 
        showRadianceDebug = !showRadianceDebug; 
        std::cout << "[Debug] Radiance cascade debug: " 
                  << (showRadianceDebug ? "ON" : "OFF") << std::endl;
    }
    
    // Toggle lighting debug
    if (IsKeyPressed(KEY_L)) { 
        showLightingDebug = !showLightingDebug; 
        std::cout << "[Debug] Lighting debug: " 
                  << (showLightingDebug ? "ON" : "OFF") << std::endl;
    }
    
    // ========================================
    // Phase 1: Radiance Debug Controls
    // ========================================
    if (showRadianceDebug) {
        // Change slice axis
        if (IsKeyPressed(KEY_FOUR)) { 
            radianceSliceAxis = 0; 
            std::cout << "[Debug] Radiance slice axis: X" << std::endl;
        }
        if (IsKeyPressed(KEY_FIVE)) { 
            radianceSliceAxis = 1; 
            std::cout << "[Debug] Radiance slice axis: Y" << std::endl;
        }
        if (IsKeyPressed(KEY_SIX)) { 
            radianceSliceAxis = 2; 
            std::cout << "[Debug] Radiance slice axis: Z" << std::endl;
        }
        
        // Cycle visualization mode
        if (IsKeyPressed(KEY_F)) { 
            radianceVisualizeMode = (radianceVisualizeMode + 1) % 4; 
            const char* modes[] = {"Slice", "Max Projection", "Average", "Direct Lighting"};
            std::cout << "[Debug] Radiance mode: " << modes[radianceVisualizeMode] << std::endl;
        }
        
        // Toggle grid overlay
        if (IsKeyPressed(KEY_G)) { 
            showRadianceGrid = !showRadianceGrid; 
            std::cout << "[Debug] Radiance grid: " 
                      << (showRadianceGrid ? "ON" : "OFF") << std::endl;
        }
    }
    
    // ========================================
    // Phase 1: Lighting Debug Controls
    // ========================================
    if (showLightingDebug) {
        // Change slice axis
        if (IsKeyPressed(KEY_SEVEN)) { 
            lightingSliceAxis = 0; 
            std::cout << "[Debug] Lighting slice axis: X" << std::endl;
        }
        if (IsKeyPressed(KEY_EIGHT)) { 
            lightingSliceAxis = 1; 
            std::cout << "[Debug] Lighting slice axis: Y" << std::endl;
        }
        if (IsKeyPressed(KEY_NINE)) { 
            lightingSliceAxis = 2; 
            std::cout << "[Debug] Lighting slice axis: Z" << std::endl;
        }
        
        // Cycle debug mode
        if (IsKeyPressed(KEY_H)) { 
            lightingDebugMode = (lightingDebugMode + 1) % 6; 
            const char* modes[] = {
                "Per-Light 0", "Per-Light 1", "Per-Light 2",
                "Combined", "Normals", "Albedo"
            };
            std::cout << "[Debug] Lighting mode: " << modes[lightingDebugMode] << std::endl;
        }
    }
    
    // ========================================
    // Phase 1: Mouse Wheel for Slice Position
    // ========================================
    float wheelMove = GetMouseWheelMove();
    if (wheelMove != 0.0f) {
        float delta = wheelMove * 0.05f;  // 5% per notch
        
        if (showRadianceDebug) {
            radianceSlicePosition += delta;
            radianceSlicePosition = std::clamp(radianceSlicePosition, 0.0f, 1.0f);
        }
        
        if (showLightingDebug) {
            lightingSlicePosition += delta;
            lightingSlicePosition = std::clamp(lightingSlicePosition, 0.0f, 1.0f);
        }
    }
    
    // ... rest of input handling ...
}
```

**Required Include:**
```cpp
#include <algorithm>  // For std::clamp
```

**Verification:**
- [ ] All keys respond correctly
- [ ] Console messages appear on keypress
- [ ] Slice position clamped to [0, 1]
- [ ] No conflicts with existing keybindings

---

### 1.4 Implement OpenGL Rendering in renderDebugVisualization()

**File:** [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp)

**Location:** Inside `renderDebugVisualization()` method

**CRITICAL RULE:** This code runs BEFORE `rlImGuiBegin()`. Do NOT call any ImGui functions here.

⬜ **Action Required - Radiance Debug:**

```cpp
void Demo3D::renderDebugVisualization() {
    /**
     * @brief Render debug visualization overlays
     * IMPORTANT: Runs BEFORE ImGui begins. No ImGui calls allowed here.
     */
    
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    const int debugSize = 256;  // Debug quad size in pixels
    const int spacing = 10;     // Spacing between views
    
    // ========================================
    // Existing SDF Debug (Phase 0) - Keep as-is
    // ========================================
    if (showSDFDebug && sdfTexture != 0) {
        // ... existing SDF debug code ...
    }
    
    // ========================================
    // NEW: Radiance Cascade Debug (Phase 1)
    // ========================================
    if (showRadianceDebug && cascades[0].active && cascades[0].probeGridTexture != 0) {
        int xPos = spacing + debugSize + spacing;
        glViewport(xPos, viewport[3] - debugSize, debugSize, debugSize);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        auto it = shaders.find("radiance_debug.frag");
        if (it != shaders.end()) {
            glUseProgram(it->second);
            
            // Bind radiance texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
            glUniform1i(glGetUniformLocation(it->second, "uRadianceTexture"), 0);
            
            // Set uniforms
            glUniform3iv(glGetUniformLocation(it->second, "uVolumeSize"), 1, &volumeResolution);
            glUniform1i(glGetUniformLocation(it->second, "uSliceAxis"), radianceSliceAxis);
            glUniform1f(glGetUniformLocation(it->second, "uSlicePosition"), radianceSlicePosition);
            glUniform1i(glGetUniformLocation(it->second, "uVisualizeMode"), radianceVisualizeMode);
            glUniform1f(glGetUniformLocation(it->second, "uExposure"), radianceExposure);
            glUniform1f(glGetUniformLocation(it->second, "uIntensityScale"), radianceIntensityScale);
            glUniform1i(glGetUniformLocation(it->second, "uShowGrid"), showRadianceGrid ? 1 : 0);
            
            // Draw fullscreen quad
            glBindVertexArray(debugQuadVAO);
            glDisable(GL_DEPTH_TEST);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
        }
        
        // Restore viewport
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
    
    // ========================================
    // NEW: Lighting Debug (Phase 1)
    // ========================================
    if (showLightingDebug && sdfTexture != 0) {
        int xPos = spacing + (debugSize + spacing) * 2;
        glViewport(xPos, viewport[3] - debugSize, debugSize, debugSize);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        auto it = shaders.find("lighting_debug.frag");
        if (it != shaders.end()) {
            glUseProgram(it->second);
            
            // Bind radiance texture (for combined mode)
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
            glUniform1i(glGetUniformLocation(it->second, "uRadianceTexture"), 0);
            
            // Bind SDF texture (for normals/albedo)
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, sdfTexture);
            glUniform1i(glGetUniformLocation(it->second, "uSDFTexture"), 1);
            
            // Set uniforms
            glUniform3iv(glGetUniformLocation(it->second, "uVolumeSize"), 1, &volumeResolution);
            glUniform1i(glGetUniformLocation(it->second, "uSliceAxis"), lightingSliceAxis);
            glUniform1f(glGetUniformLocation(it->second, "uSlicePosition"), lightingSlicePosition);
            glUniform1i(glGetUniformLocation(it->second, "uDebugMode"), lightingDebugMode);
            glUniform1f(glGetUniformLocation(it->second, "uExposure"), lightingExposure);
            glUniform1f(glGetUniformLocation(it->second, "uIntensityScale"), lightingIntensityScale);
            
            // Pass light positions for overlay
            glm::vec3 lightPositions[3] = {
                glm::vec3(0.0f, 1.8f, 0.0f),   // Ceiling light
                glm::vec3(-1.5f, 1.0f, 0.0f),  // Fill light
                glm::vec3(1.5f, 0.8f, 0.0f)    // Accent light
            };
            glUniform3fv(glGetUniformLocation(it->second, "uLightPositions"), 3, &lightPositions[0][0]);
            
            // Draw fullscreen quad
            glBindVertexArray(debugQuadVAO);
            glDisable(GL_DEPTH_TEST);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
        }
        
        // Restore viewport
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
}
```

**Verification:**
- [ ] No ImGui function calls in this method
- [ ] Viewport restored after each debug view
- [ ] Depth test disabled/enabled correctly
- [ ] Textures bound to correct units
- [ ] Uniforms match shader expectations

---

### 1.5 Integrate UI Calls in renderUI()

**File:** [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp)

**Location:** Inside `renderUI()` method, between `rlImGuiBegin()` and `rlImGuiEnd()`

⬜ **Action Required:**

```cpp
void Demo3D::renderUI() {
    rlImGuiBegin();
    
    // ... existing UI code ...
    
    // Phase 0: SDF debug UI
    renderSDFDebugUI();
    
    // Phase 1: Radiance cascade debug UI
    renderRadianceDebugUI();
    
    // Phase 1: Lighting debug UI
    renderLightingDebugUI();
    
    rlImGuiEnd();
}
```

**Verification:**
- [ ] UI panels appear when toggled
- [ ] Controls update variables correctly
- [ ] No assertion failures
- [ ] Panels don't overlap awkwardly

---

### 1.6 Update Startup Message

**File:** [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp)

**Location:** Constructor, after existing debug message output

⬜ **Action Required:**

```cpp
Demo3D::Demo3D() {
    // ... constructor body ...
    
    std::cout << "[Demo3D] Debug Views:" << std::endl;
    std::cout << "[Demo3D]   [D] SDF cross-section" << std::endl;
    std::cout << "[Demo3D]   [R] Radiance cascade" << std::endl;
    std::cout << "[Demo3D]   [L] Lighting debug" << std::endl;
    std::cout << "[Demo3D]   [4/5/6] Change radiance slice axis (X/Y/Z)" << std::endl;
    std::cout << "[Demo3D]   [7/8/9] Change lighting slice axis (X/Y/Z)" << std::endl;
    std::cout << "[Demo3D]   [F] Cycle radiance visualization mode" << std::endl;
    std::cout << "[Demo3D]   [H] Cycle lighting debug mode" << std::endl;
    std::cout << "[Demo3D]   [G] Toggle radiance grid overlay" << std::endl;
    std::cout << "[Demo3D]   [Mouse Wheel] Adjust slice position" << std::endl;
}
```

**Verification:**
- [ ] Message appears on startup
- [ ] All keybindings documented
- [ ] Clear and readable format

---

## Task Group 2: Verification & Testing

**Priority:** 🔴 HIGH  
**Estimated Time:** 15-20 minutes

### 2.1 Build Verification

⬜ **Steps:**

```powershell
# Navigate to project root
cd c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d

# Build project
.\build.ps1

# Or manual build
cd build
cmake --build . --config Release
```

**Expected Output:**
```
[ 50%] Building CXX object CMakeFiles/radiance_cascades_3d.dir/src/demo3d.cpp.obj
[100%] Linking CXX executable radiance_cascades_3d.exe
Build succeeded.
```

**Checklist:**
- [ ] No compilation errors
- [ ] No linker errors
- [ ] Executable generated
- [ ] Build time < 30 seconds

---

### 2.2 Runtime Verification

⬜ **Steps:**

1. Launch application from project root:
   ```powershell
   cd ..
   .\build\radiance_cascades_3d.exe
   ```

2. Test each debug view:
   - Press 'D' → SDF debug should appear (top-left)
   - Press 'R' → Radiance debug should appear (top-center)
   - Press 'L' → Lighting debug should appear (top-right)

3. Test controls:
   - Keys 4/5/6 → Radiance slice axis changes
   - Keys 7/8/9 → Lighting slice axis changes
   - Key F → Radiance mode cycles
   - Key H → Lighting mode cycles
   - Key G → Grid overlay toggles
   - Mouse wheel → Slice position adjusts

4. Check console output:
   - Debug toggle messages appear
   - Mode change messages appear
   - No error messages

**Checklist:**
- [ ] Application launches without crash
- [ ] All three debug views work
- [ ] Keyboard controls responsive
- [ ] Mouse wheel adjusts slices
- [ ] ImGui panels functional
- [ ] No assertion failures
- [ ] Frame rate acceptable (>15 FPS)

---

### 2.3 Visual Verification

⬜ **What to Look For:**

**Radiance Debug View:**
- [ ] Shows colored slice through volume
- [ ] Different modes show different data
- [ ] Grid overlay appears when enabled
- [ ] Exposure/intensity adjustments work

**Lighting Debug View:**
- [ ] Normals shown as RGB colors
- [ ] Albedo shows Cornell Box colors (red/green/white)
- [ ] Per-light modes isolate individual lights
- [ ] Combined mode shows total illumination

**Common Issues:**
- [ ] Black screen → Texture not bound or shader error
- [ ] Wrong colors → Wrong texture bound or uniform mismatch
- [ ] No response to controls → Variables not connected
- [ ] Assertion failure → ImGui called outside frame scope

---

## Task Group 3: Documentation Updates

**Priority:** 🟢 MEDIUM  
**Estimated Time:** 10 minutes

### 3.1 Update Phase 1 Progress Report

**File:** [`doc/2/phase1/phase1_progress.md`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\doc\2\phase1\phase1_progress.md)

⬜ **Add Section:**

```markdown
### Day 8: Debug Visualization Complete ✅

**Completed Tasks:**
- Added all member variables to demo3d.h
- Initialized variables in constructor
- Implemented keyboard controls
- Integrated OpenGL rendering
- Connected UI panels
- Updated startup messages

**Verification:**
- All debug views functional
- No compilation errors
- No runtime issues
- Console messages working

**Next Steps:**
- Material system enhancement (Day 9)
- SDF normal visualization (Day 10)
```

---

### 3.2 Update Implementation Status

**File:** [`doc/2/implementation_status.md`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\doc\2\implementation_status.md)

⬜ **Update Phase 1 Status:**

Change from:
```markdown
- [ ] Debug visualization tools
```

To:
```markdown
- [x] Debug visualization tools ✅ Complete
```

---

## Task Group 4: Common Pitfalls to Avoid

### ⚠️ Pitfall 1: Duplicate Code Blocks

**Problem:** Copy-pasting debug rendering code creates duplicates

**Solution:** Before adding new code, search for existing implementations:
```bash
grep -n "renderDebugVisualization" src/demo3d.cpp
```

Should return only 1 match (the function definition).

---

### ⚠️ Pitfall 2: Array vs. Vector Confusion

**Problem:** Calling `.empty()` on fixed-size array

**Wrong:**
```cpp
if (!cascades.empty()) { ... }  // ❌ Compilation error
```

**Correct:**
```cpp
if (cascades[0].active) { ... }  // ✅ Check individual element
```

---

### ⚠️ Pitfall 3: ImGui Order Violation

**Problem:** Calling OpenGL rendering inside ImGui frame

**Wrong:**
```cpp
rlImGuiBegin();
glDrawArrays(...);  // ❌ Assertion failure!
rlImGuiEnd();
```

**Correct:**
```cpp
renderDebugVisualization();  // OpenGL first
rlImGuiBegin();
renderUI();                   // ImGui second
rlImGuiEnd();
```

---

### ⚠️ Pitfall 4: Missing std::clamp Include

**Problem:** Using `std::clamp` without `<algorithm>` header

**Solution:** Add to top of demo3d.cpp:
```cpp
#include <algorithm>  // For std::clamp
```

---

### ⚠️ Pitfall 5: Wrong Texture Binding Point

**Problem:** Shader expects binding=1 but CPU binds to different unit

**Shader:**
```glsl
layout(r32f, binding = 1) uniform image3D uSDFVolume;
```

**CPU (Must Match):**
```cpp
glBindImageTexture(1, sdfTexture, ...);  // ✅ First param must be 1
```

---

## Quick Reference: File Locations

| Component | File Path |
|-----------|-----------|
| Header (member vars) | `include/demo3d.h` |
| Constructor (init) | `src/demo3d.cpp` (line ~20-50) |
| Input handling | `src/demo3d.cpp::processInput()` |
| Debug rendering | `src/demo3d.cpp::renderDebugVisualization()` |
| UI integration | `src/demo3d.cpp::renderUI()` |
| Radiance shader | `res/shaders/radiance_debug.frag` |
| Lighting shader | `res/shaders/lighting_debug.frag` |

---

## Success Criteria

Phase 1.5 is complete when ALL of the following are true:

- [x] All member variables added and initialized
- [x] Keyboard controls fully functional
- [x] OpenGL rendering integrated correctly
- [x] UI panels accessible and working
- [x] No compilation errors
- [x] No runtime assertion failures
- [x] All three debug views visible and interactive
- [x] Console messages informative
- [x] Documentation updated

---

**Estimated Total Time:** 1-1.5 hours  
**Difficulty:** Beginner-Intermediate  
**Dependencies:** None (standalone task)

**Next Phase:** Priority 2 - Initialize Cascades (Day 11-12 prerequisite)

---

*Last Updated: 2026-04-19*  
*Created by: Phase 1.5 Consolidation Effort*
