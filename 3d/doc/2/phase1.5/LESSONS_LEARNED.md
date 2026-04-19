# Phase 1.5 - Lessons Learned & Best Practices

**Date:** 2026-04-19  
**Phase:** Phase 1.5 - Knowledge Consolidation  
**Purpose:** Capture critical insights from Phase 1 implementation to prevent future mistakes

---

## Executive Summary

This document consolidates all lessons learned during Phase 1 (Days 6-7) implementation of multi-light support and debug visualization. These insights are categorized by severity and applicability to future phases.

### Key Takeaways

1. **Always verify struct definitions before accessing members** - Saved 5+ minutes per error
2. **Maintain strict separation between OpenGL and ImGui rendering** - Prevents assertion failures
3. **Use incremental validation** - Catch errors early, isolate problems quickly
4. **Document shader-CPU binding alignment** - Critical for texture access correctness

---

## Lesson Category 1: Code Structure & Naming

### Lesson 1.1: Field Naming Consistency ⚠️ HIGH IMPACT

**Issue Encountered:**
Used incorrect field names when accessing `RadianceCascade3D` struct members:
- Called `radianceCascades` instead of `cascades`
- Called `texture` instead of `probeGridTexture`

**Impact:**
- Two compilation errors
- 5 minutes debugging time
- Frustration from "obvious" mistake

**Root Cause Analysis:**
Didn't verify the actual struct definition in header file before writing access code. Made assumptions based on naming conventions rather than checking facts.

**Correct Approach:**
```cpp
// ❌ WRONG - Guessed field names
if (!radianceCascades.empty()) {
    auto tex = radianceCascades[0].texture;
}

// ✅ CORRECT - Verified from demo3d.h line ~60
struct RadianceCascade3D {
    GLuint probeGridTexture = 0;  // ← Actual field name
    bool active = false;
    // ...
};

if (cascades[0].active) {
    auto tex = cascades[0].probeGridTexture;
}
```

**Rule Established:**
> **ALWAYS check header file definitions before accessing struct/class members.** Use IDE autocomplete or grep to confirm exact field names. Never guess.

**Tool Recommendation:**
```bash
# Quick field lookup
grep -n "probeGridTexture" include/demo3d.h

# Or use IDE features
# VSCode: Ctrl+Click on type name → Go to Definition
# Visual Studio: F12 → Go to Definition
```

**Applicability:** Universal - applies to all C++ projects

---

### Lesson 1.2: Array vs. Vector Confusion ⚠️ MEDIUM IMPACT

**Issue Encountered:**
Tried to call STL methods on fixed-size C-style array:
```cpp
RadianceCascade3D cascades[MAX_CASCADES];  // Fixed array, NOT std::vector

// This fails:
if (!cascades.empty()) { ... }  // ❌ Compilation error: 'empty' is not a member
```

**Impact:**
- Compilation error
- Confusion about container type
- Time wasted checking if vector was intended

**Root Cause Analysis:**
Assumed modern C++ containers were used everywhere. Didn't check actual declaration in header.

**Correct Approach:**
```cpp
// Check declaration first:
// In demo3d.h:
RadianceCascade3D cascades[MAX_CASCADES];  // ← Fixed array

// Use appropriate checks:
if (cascades[0].active) { ... }           // Check individual element
for (int i = 0; i < MAX_CASCADES; ++i) {  // Iterate with known size
    if (cascades[i].active) { ... }
}
```

**Rule Established:**
> **Always verify container type declaration.** Fixed arrays don't have STL methods like `.empty()`, `.size()`, `.push_back()`, etc.

**Common Mistakes:**
| Method | Works on Vector | Works on Array | Fix for Array |
|--------|----------------|----------------|---------------|
| `.empty()` | ✅ Yes | ❌ No | Check manually: `count == 0` |
| `.size()` | ✅ Yes | ❌ No | Use `sizeof(arr)/sizeof(arr[0])` or constant |
| `.push_back()` | ✅ Yes | ❌ No | Pre-allocate, track count separately |
| `.begin()` | ✅ Yes | ❌ No | Use `std::begin(arr)` (C++11+) |

**Applicability:** High - common in mixed C/C++ codebases

---

### Lesson 1.3: No Duplicate Code Blocks ⚠️ MEDIUM IMPACT

**Issue Encountered:**
Multiple copies of debug rendering code in same function caused compilation errors:
```cpp
void Demo3D::renderDebugVisualization() {
    // First copy...
    if (showRadianceDebug) { ... }
    
    // Accidentally pasted again...
    if (showRadianceDebug) { ... }  // ❌ Duplicate logic
    
    // And again...
    if (showRadianceDebug) { ... }  // ❌ Triple duplicate
}
```

**Impact:**
- Compilation errors (redefinition)
- Confusing code structure
- Maintenance nightmare

**Root Cause Analysis:**
Copy-paste during implementation without removing old code. Lost track of which version was current.

**Correct Approach:**
Before adding new code block, search for existing implementations:
```bash
# Search for function to ensure single definition
grep -n "renderDebugVisualization" src/demo3d.cpp

# Should return only 1 match (the function definition)
# If multiple matches, you have duplicates
```

**Rule Established:**
> **Before adding new code block, search for existing implementations. Ensure only ONE instance of each function/rendering block exists.**

**Prevention Strategy:**
1. Write code once
2. Test it works
3. Commit to version control
4. If refactoring, delete old code BEFORE writing new code
5. Use diff tools to verify changes

**Applicability:** Universal - applies to all programming

---

## Lesson Category 2: OpenGL & Graphics Programming

### Lesson 2.1: Shader-Binding Alignment ⚠️ HIGH IMPACT

**Issue Encountered:**
Added new texture binding in shader but forgot to update CPU-side binding code:

**Shader Side:**
```glsl
layout(r32f, binding = 1) uniform image3D uSDFVolume;
```

**CPU Side (Missing):**
```cpp
// Forgot this line:
// glBindImageTexture(1, sdfTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
```

**Impact:**
- Shader reads from uninitialized memory
- Garbage data or crashes
- Hard to debug (no explicit error message)

**Root Cause Analysis:**
Added shader binding point but didn't immediately add corresponding CPU code. Lost context between shader editing and CPU editing.

**Correct Approach:**
When adding new texture bindings, update BOTH sides immediately:

```glsl
// Step 1: Add to shader
layout(r32f, binding = 1) uniform image3D uSDFVolume;
```

```cpp
// Step 2: IMMEDIATELY add to CPU code
glBindImageTexture(
    1,              // ← MUST match shader's binding = 1
    sdfTexture,     // Texture object
    0,              // Mip level
    GL_FALSE,       // Layered (false for 3D textures)
    0,              // Layer
    GL_READ_ONLY,   // Access mode
    GL_R32F         // Format
);
```

**Rule Established:**
> **When adding new texture bindings in shader (e.g., `layout(binding = N)`), immediately update corresponding CPU-side `glBindImageTexture(N, ...)` calls. The binding point MUST match.**

**Verification Checklist:**
- [ ] Shader binding point noted (e.g., `binding = 0`)
- [ ] CPU binding uses same number (first param of `glBindImageTexture`)
- [ ] Texture format matches (`GL_R32F`, `GL_RGBA16F`, etc.)
- [ ] Access mode correct (`GL_READ_ONLY`, `GL_WRITE_ONLY`, `GL_READ_WRITE`)

**Debugging Tip:**
If getting garbage data, check binding points first:
```bash
# Find all bindings in shader
grep -n "binding =" res/shaders/*.comp

# Find all CPU-side bindings
grep -n "glBindImageTexture" src/demo3d.cpp
```

**Applicability:** High - critical for all OpenGL compute shader work

---

### Lesson 2.2: ImGui Rendering Order ⚠️ CRITICAL IMPACT

**Issue Encountered:**
Mixed OpenGL rendering calls with ImGui UI drawing caused assertion failure:

**Wrong Pattern:**
```cpp
rlImGuiBegin();

// OpenGL rendering inside ImGui frame
glDrawArrays(GL_TRIANGLES, 0, 6);  // ❌ ASSERTION FAILURE!
// Error: g.WithinFrameScope assertion failed

ImGui::Text("Some UI");

rlImGuiEnd();
```

**Impact:**
- Application crash
- Assertion failure in ImGui internals
- Confusing error message

**Root Cause Analysis:**
Didn't understand that ImGui maintains internal state about whether it's in a "frame". OpenGL rendering must happen outside this scope.

**Correct Pattern:**
```cpp
// Phase 1: Main scene rendering
renderScene();

// Phase 2: Debug overlays (OpenGL native)
renderDebugVisualization();  // ← ALL OpenGL rendering HERE

// Phase 3: UI panels (ImGui)
rlImGuiBegin();              // ← Start ImGui frame
renderUI();                   // ← ONLY ImGui calls here
rlImGuiEnd();                 // ← End ImGui frame

// Phase 4: Post-frame cleanup (if needed)
```

**Rule Established:**
> **CRITICAL: Separate rendering phases strictly:**
> 1. **OpenGL native rendering** → BEFORE `rlImGuiBegin()`
> 2. **ImGui UI drawing** → BETWEEN `rlImGuiBegin()` and `rlImGuiEnd()`
> 3. **Post-frame cleanup** → AFTER `rlImGuiEnd()`
> 
> **NEVER mix OpenGL draw calls with ImGui calls.**

**Memory Aid:**
```
┌─────────────────────────────────────┐
│  Frame Start                        │
├─────────────────────────────────────┤
│  1. Render Scene (OpenGL)           │
│  2. Render Debug Overlays (OpenGL)  │  ← Native OpenGL
├─────────────────────────────────────┤
│  rlImGuiBegin()                     │
│  3. Draw UI Panels (ImGui)          │  ← ImGui Only
│  rlImGuiEnd()                       │
├─────────────────────────────────────┤
│  4. Cleanup (if needed)             │
└─────────────────────────────────────┘
```

**Common Violations:**
```cpp
// ❌ WRONG: OpenGL inside ImGui
rlImGuiBegin();
glUseProgram(shader);
glDrawArrays(...);
rlImGuiEnd();

// ✅ CORRECT: OpenGL before ImGui
glUseProgram(shader);
glDrawArrays(...);
rlImGuiBegin();
// ImGui calls only
rlImGuiEnd();
```

**Applicability:** Critical - applies to all projects using ImGui + OpenGL

---

### Lesson 2.3: Viewport Management ⚠️ MEDIUM IMPACT

**Issue Encountered:**
After rendering debug quad to custom viewport, forgot to restore original viewport:

**Wrong Pattern:**
```cpp
// Set custom viewport for debug view
glViewport(100, 100, 256, 256);
glDrawArrays(...);

// Forgot to restore! Next render uses wrong viewport
renderScene();  // ❌ Renders to wrong area
```

**Impact:**
- Subsequent renders drawn to wrong screen area
- Corrupted display
- Hard to trace back to viewport issue

**Correct Pattern:**
```cpp
// Save original viewport
GLint viewport[4];
glGetIntegerv(GL_VIEWPORT, viewport);

// Set custom viewport for debug view
glViewport(xPos, yPos, debugSize, debugSize);
glDrawArrays(...);

// Restore original viewport
glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
```

**Rule Established:**
> **Always save and restore viewport when temporarily changing it. Use RAII pattern if possible.**

**Advanced Pattern (RAII):**
```cpp
class ViewportGuard {
    GLint viewport[4];
public:
    ViewportGuard(int x, int y, int w, int h) {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glViewport(x, y, w, h);
    }
    ~ViewportGuard() {
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

// Usage:
{
    ViewportGuard guard(100, 100, 256, 256);
    glDrawArrays(...);  // Automatically restored when guard goes out of scope
}
```

**Applicability:** Medium - important for multi-viewport rendering

---

### Lesson 2.4: Depth Test State Management ⚠️ LOW IMPACT

**Issue Encountered:**
Debug quads rendered behind scene geometry because depth test was enabled:

**Wrong Pattern:**
```cpp
glEnable(GL_DEPTH_TEST);  // Still enabled from scene render
glDrawArrays(...);         // Debug quad fails depth test, doesn't appear
```

**Correct Pattern:**
```cpp
glDisable(GL_DEPTH_TEST);  // Disable for overlay
glDrawArrays(...);
glEnable(GL_DEPTH_TEST);   // Re-enable for next render
```

**Rule Established:**
> **For 2D overlays (debug views, UI backgrounds), disable depth test. Always restore state after.**

**State Management Best Practice:**
```cpp
// Save state
GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);

// Change state
glDisable(GL_DEPTH_TEST);
glDrawArrays(...);

// Restore state
if (depthTestEnabled) glEnable(GL_DEPTH_TEST);
```

**Applicability:** Low-Medium - relevant for overlay rendering

---

## Lesson Category 3: Development Workflow

### Lesson 3.1: Incremental Validation ✅ BEST PRACTICE

**Observation:**
Built and tested after each logical unit:
1. Implemented multi-light support → Built → Tested
2. Added SDF normals → Built → Tested
3. Added albedo sampling → Built → Tested

**Benefit:**
- Errors caught immediately
- Easy to isolate which change broke things
- Confidence in each component
- Faster debugging (smaller change sets)

**Rule Established:**
> **Continue this pattern - build/test after each day's work. Never accumulate multiple days of changes without validation.**

**Recommended Workflow:**
```
Day 6 Morning: Implement feature A
Day 6 Afternoon: Build → Test → Fix bugs → Commit
Day 7 Morning: Implement feature B
Day 7 Afternoon: Build → Test → Fix bugs → Commit
```

**Anti-Pattern to Avoid:**
```
❌ BAD:
Day 6-10: Implement features A, B, C, D, E
Day 11: Build → Everything broken, can't isolate issue

✅ GOOD:
Day 6: Implement A → Build → Test → Commit
Day 7: Implement B → Build → Test → Commit
Day 8: Implement C → Build → Test → Commit
...
```

**Applicability:** Universal - fundamental software engineering practice

---

### Lesson 3.2: Documentation While Fresh ✅ BEST PRACTICE

**Observation:**
Created progress reports and debug status documents immediately after implementation while details were fresh.

**Benefit:**
- Accurate recollection of decisions
- Complete technical details captured
- Future self (and teammates) can understand reasoning
- Easier to resume work after break

**Rule Established:**
> **Document implementation decisions, trade-offs, and lessons learned immediately after completing a task. Don't wait.**

**Documentation Checklist:**
- [ ] What was implemented
- [ ] Why this approach was chosen
- [ ] What alternatives were considered
- [ ] What issues were encountered
- [ ] How issues were resolved
- [ ] What remains to be done

**Applicability:** High - improves long-term maintainability

---

### Lesson 3.3: Use Console Logging for Debug State ⚠️ MEDIUM IMPACT

**Observation:**
Added console messages when toggling debug views:
```cpp
if (IsKeyPressed(KEY_R)) { 
    showRadianceDebug = !showRadianceDebug; 
    std::cout << "[Debug] Radiance cascade debug: " 
              << (showRadianceDebug ? "ON" : "OFF") << std::endl;
}
```

**Benefit:**
- Clear feedback on state changes
- Easy to verify keybindings work
- Helps users understand what's happening
- Useful for remote debugging (logs)

**Rule Established:**
> **Add informative console messages for state changes, especially for toggle actions. Use consistent prefix like `[Debug]` for easy filtering.**

**Good Examples:**
```cpp
std::cout << "[Debug] Radiance slice axis: X" << std::endl;
std::cout << "[Debug] Radiance mode: Max Projection" << std::endl;
std::cout << "[Demo3D] Direct lighting injected with 3 lights" << std::endl;
```

**Bad Examples:**
```cpp
std::cout << "Changed axis" << std::endl;  // Too vague
std::cout << x << std::endl;               // No context
// No logging at all                          // Silent failure
```

**Applicability:** Medium - improves developer experience

---

## Lesson Category 4: Shader Programming

### Lesson 4.1: Central Difference for SDF Normals ✅ BEST PRACTICE

**Implementation:**
```glsl
vec3 computeNormal(ivec3 coord) {
    // Central difference gradient (more accurate than forward/backward)
    float dx = imageLoad(uSDFVolume, coord + ivec3(1,0,0)).r -
               imageLoad(uSDFVolume, coord - ivec3(1,0,0)).r;
    float dy = imageLoad(uSDFVolume, coord + ivec3(0,1,0)).r -
               imageLoad(uSDFVolume, coord - ivec3(0,1,0)).r;
    float dz = imageLoad(uSDFVolume, coord + ivec3(0,0,1)).r -
               imageLoad(uSDFVolume, coord - ivec3(0,0,1)).r;
    
    vec3 grad = vec3(dx, dy, dz);
    float len = length(grad);
    
    if (len < 1e-6)
        return vec3(0.0, 1.0, 0.0); // Default up for flat regions
    
    return normalize(grad);
}
```

**Why Central Difference?**
- More accurate than forward/backward difference
- Symmetric error (cancels out)
- Standard technique in graphics

**Alternatives Considered:**
- Forward difference: `f(x+1) - f(x)` - Less accurate
- Backward difference: `f(x) - f(x-1)` - Less accurate
- Analytic normals: Perfect accuracy but not universal

**Rule Established:**
> **Use central difference gradients for SDF normal computation unless analytic normals are available and practical.**

**Performance Note:**
Central difference requires 6 texture reads (±x, ±y, ±z). For performance-critical code, consider:
- Caching nearby samples
- Using lower resolution for distant objects
- Precomputing normals into separate texture

**Applicability:** High - standard technique for SDF-based rendering

---

### Lesson 4.2: Lambertian Diffuse with NdotL ✅ STANDARD TECHNIQUE

**Implementation:**
```glsl
vec3 calculatePointLight(PointLight light, vec3 pos, vec3 normal) {
    vec3 lightDir = light.position - pos;
    float distSq = dot(lightDir, lightDir);
    
    // Normalize light direction
    vec3 L = normalize(lightDir);
    
    // Lambertian diffuse (NdotL)
    float NdotL = max(dot(normal, L), 0.0);  // Clamp to avoid negative
    
    // Attenuation: smooth falloff
    float attenuation = 1.0 - sqrt(distSq) / light.radius;
    attenuation *= attenuation; // Quadratic falloff
    
    return light.color * light.intensity * attenuation * NdotL;
}
```

**Key Points:**
- `max(dot(N, L), 0.0)` prevents negative lighting (backfaces)
- Quadratic attenuation looks more natural than linear
- Separate color and intensity for flexibility

**Common Mistakes:**
```glsl
// ❌ WRONG: Forgetting to clamp NdotL
float NdotL = dot(normal, L);  // Can be negative!

// ✅ CORRECT: Clamp to zero
float NdotL = max(dot(normal, L), 0.0);

// ❌ WRONG: Linear attenuation (looks wrong)
float attenuation = 1.0 - distance / radius;

// ✅ CORRECT: Quadratic attenuation
float attenuation = pow(1.0 - distance / radius, 2.0);
```

**Rule Established:**
> **Always clamp NdotL to [0, 1] range. Use quadratic attenuation for realistic light falloff.**

**Applicability:** Universal - fundamental lighting equation

---

### Lesson 4.3: Hardcoded Values with Clear Migration Path ⚠️ DESIGN DECISION

**Context:**
Cornell Box colors hardcoded in shader for Phase 1, but designed interface for easy replacement:

**Current (Phase 1):**
```glsl
vec3 sampleAlbedo(vec3 worldPos) {
    // Cornell Box wall colors (Phase 1 hardcoded)
    // TODO: Replace with material system in Phase 3
    
    // Left wall: Red
    if (worldPos.x < -1.9 && worldPos.x > -2.1)
        return vec3(0.65, 0.05, 0.05);
    
    // Right wall: Green
    if (worldPos.x > 1.9 && worldPos.x < 2.1)
        return vec3(0.12, 0.45, 0.15);
    
    // Back wall, floor, ceiling: White
    return vec3(0.75, 0.75, 0.75);
}
```

**Future (Phase 3):**
```glsl
vec3 sampleAlbedo(vec3 worldPos) {
    ivec3 voxelCoord = worldToVoxel(worldPos);
    return texelFetch(uMaterialTexture, voxelCoord, 0).rgb;
}
```

**Why This Approach?**
- ✅ Fast iteration on lighting pipeline
- ✅ Easy debugging (no material system complexity)
- ✅ Clear migration path documented
- ✅ Function signature unchanged

**Trade-offs:**
- Pros: Quick to implement, validates core lighting
- Cons: Not extensible, requires rewrite later

**Rule Established:**
> **When prototyping, hardcode values but design interfaces for easy replacement. Document TODO clearly with migration plan.**

**Best Practices for Temporary Code:**
1. Add clear comment: `// Phase 1 hardcoded - TODO: Replace in Phase 3`
2. Keep function signature stable
3. Document replacement approach
4. Don't over-engineer temporary solutions

**Applicability:** Medium - useful for iterative development

---

## Lesson Category 5: Tool Usage

### Lesson 5.1: Grep for Code Exploration ✅ POWERFUL TECHNIQUE

**Usage Patterns:**

**Find field definitions:**
```bash
grep -n "probeGridTexture" include/demo3d.h
# Output: 62:    GLuint probeGridTexture = 0;
```

**Find function implementations:**
```bash
grep -n "renderDebugVisualization" src/demo3d.cpp
# Output: 450:void Demo3D::renderDebugVisualization() {
```

**Check for duplicates:**
```bash
grep -c "renderDebugVisualization" src/demo3d.cpp
# Output: 1 (should be 1, not 2+)
```

**Find all texture bindings:**
```bash
grep -n "glBindImageTexture" src/demo3d.cpp
# Shows all binding points in one place
```

**Rule Established:**
> **Use grep extensively for code exploration. It's faster than manual searching and catches all occurrences.**

**Useful Grep Options:**
| Option | Purpose | Example |
|--------|---------|---------|
| `-n` | Show line numbers | `grep -n "TODO" file.cpp` |
| `-c` | Count matches | `grep -c "func" file.cpp` |
| `-r` | Recursive search | `grep -r "TODO" src/` |
| `-i` | Case insensitive | `grep -i "error" *.log` |
| `-A 3` | Show 3 lines after | `grep -A 3 "TODO" file.cpp` |
| `-B 3` | Show 3 lines before | `grep -B 3 "TODO" file.cpp` |

**Applicability:** Universal - essential for code navigation

---

### Lesson 5.2: PowerShell Command Syntax on Windows ⚠️ PLATFORM-SPECIFIC

**Issue Encountered:**
Used Bash syntax `&&` in PowerShell, which doesn't work:
```powershell
cd build && cmake ..  # ❌ Doesn't work in PowerShell
```

**Correct PowerShell Syntax:**
```powershell
cd build; cmake ..    # ✅ Use semicolon
# OR
cd build
cmake ..              # ✅ Separate commands
```

**Rule Established:**
> **On Windows PowerShell, use semicolon `;` to chain commands, not `&&`. Remember platform differences.**

**Command Chaining Comparison:**
| Operation | Bash/Linux | PowerShell |
|-----------|------------|------------|
| Sequential | `cmd1 && cmd2` | `cmd1; cmd2` |
| Conditional | `cmd1 && cmd2` | `cmd1; if ($?) { cmd2 }` |
| Background | `cmd &` | `Start-Job { cmd }` |

**Applicability:** Platform-specific - important for Windows developers

---

## Summary: Top 10 Rules

Based on all lessons learned, here are the top 10 rules to follow:

1. **Always verify struct definitions before accessing members** - Use IDE or grep
2. **Separate OpenGL and ImGui rendering strictly** - Never mix them
3. **Build and test incrementally** - After each logical unit
4. **Match shader binding points with CPU code** - Binding N must match on both sides
5. **Save and restore OpenGL state** - Viewport, depth test, etc.
6. **Search for duplicates before adding code** - Use grep
7. **Check container types** - Array vs. Vector matters
8. **Document decisions while fresh** - Don't wait
9. **Use console logging for state changes** - Provides feedback
10. **Design temporary code with migration paths** - Make replacement easy

---

## Appendix: Quick Reference Cards

### OpenGL State Management Card
```cpp
// Before custom rendering
GLint viewport[4];
glGetIntegerv(GL_VIEWPORT, viewport);
GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);

// Your rendering
glViewport(x, y, w, h);
glDisable(GL_DEPTH_TEST);
glDrawArrays(...);

// Restore state
glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
if (depthTest) glEnable(GL_DEPTH_TEST);
```

### ImGui Rendering Order Card
```cpp
// 1. OpenGL rendering
renderScene();
renderDebugOverlays();

// 2. ImGui frame
rlImGuiBegin();
renderUI();
rlImGuiEnd();

// 3. Cleanup (if needed)
```

### Shader-CPU Binding Card
```glsl
// Shader
layout(r32f, binding = 0) uniform image3D uTextureA;
layout(rgba16f, binding = 1) uniform image3D uTextureB;
```

```cpp
// CPU (MUST match binding points)
glBindImageTexture(0, textureA, ...);  // binding = 0
glBindImageTexture(1, textureB, ...);  // binding = 1
```

### Grep Quick Reference Card
```bash
# Find definition
grep -n "fieldName" *.h

# Check for duplicates
grep -c "functionName" *.cpp

# Find all usages
grep -rn "TODO" src/

# Show context
grep -A 5 -B 5 "pattern" file.cpp
```

---

**End of Lessons Learned Document**

*Last Updated: 2026-04-19*  
*Maintained by: Development Team*  
*Review Frequency: End of each phase*
