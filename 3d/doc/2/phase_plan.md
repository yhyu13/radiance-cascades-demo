# 3D Radiance Cascades - Detailed Phase Implementation Plans

**Document Version:** 1.0  
**Date:** 2026-04-18  
**Status:** Planning Phase - Dual Agent Review Complete  
**Authors:** AI Assistant (Architect Agent + Implementer Agent)  

---

## Executive Summary

This document provides **detailed, actionable implementation plans** for each phase of the 3D Radiance Cascades project, reviewed through a dual-agent lens:

- **🎓 Architect Agent**: Focuses on design quality, scalability, maintainability, and long-term vision
- **⚙️ Implementer Agent**: Focuses on practical feasibility, immediate blockers, debugging strategies, and quick wins

Each phase includes consensus decisions, trade-off analysis, and concrete code-level tasks.

---

## Table of Contents

1. [Phase 0: Validation & Quick Start](#phase-0-validation--quick-start-week-1)
2. [Phase 1: Basic Global Illumination](#phase-1-basic-global-illumination-weeks-2-3)
3. [Phase 2: Multi-Cascade Hierarchy](#phase-2-multi-cascade-hierarchy-weeks-4-5)
4. [Phase 3: Voxel Pipeline & JFA](#phase-3-voxel-pipeline--jfa-weeks-6-7)
5. [Phase 4: Optimization & Polish](#phase-4-optimization--polish-weeks-8-10)
6. [Cross-Phase Concerns](#cross-phase-concerns)
7. [Dual-Agent Decision Log](#dual-agent-decision-log)

---

## Phase 0: Validation & Quick Start (Week 1)

**Goal:** Prove the entire pipeline works end-to-end with minimal complexity using analytic SDF.

### Success Criteria
- ✅ Window opens without errors
- ✅ Compute shaders dispatch successfully
- ✅ Visual feedback shows radiance data (colored point cloud or volume slices)
- ✅ Frame rate > 10 FPS for 64³ grid
- ✅ Can toggle between debug views (voxels, SDF, cascades)

---

### Day 1-2: Analytic SDF Implementation

#### 🎓 Architect Agent Perspective:
> "We need a clean abstraction that allows swapping between analytic and voxel-based SDF later. Design an interface pattern."

#### ⚙️ Implementer Agent Perspective:
> "Let's just get something working fast. Hardcode box/sphere SDF directly in the shader for now, refactor later."

#### ✅ Consensus Decision:
Create `AnalyticSDF` class with simple interface, but implement directly in compute shader for speed. Refactor to abstract interface in Phase 3.

#### Tasks:

**Task 0.1.1: Create Analytic SDF CPU Class**
- File: `src/analytic_sdf.h` (new)
- Lines: ~80 lines
```cpp
#pragma once
#include <glm/glm.hpp>
#include <vector>

class AnalyticSDF {
public:
    enum PrimitiveType { BOX = 0, SPHERE = 1 };
    
    struct Primitive {
        PrimitiveType type;
        glm::vec3 position;
        glm::vec3 scale;
    };
    
    void addBox(const glm::vec3& pos, const glm::vec3& size);
    void addSphere(const glm::vec3& pos, float radius);
    void clear();
    
    // For debugging/visualization
    const std::vector<Primitive>& getPrimitives() const { return primitives; }
    
private:
    std::vector<Primitive> primitives;
};
```

**Task 0.1.2: Implement Analytic SDF Compute Shader**
- File: `res/shaders/sdf_analytic.comp` (new, based on existing sdf_3d.comp structure)
- Lines: ~120 lines
```glsl
#version 430
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba32f, binding = 0) uniform image3D sdfVolume;

uniform vec3 volumeOrigin;
uniform vec3 volumeSize;

// Primitive buffer
struct Primitive {
    int type;
    vec3 position;
    vec3 scale;
};

layout(std430, binding = 0) buffer PrimitiveBuffer {
    Primitive primitives[];
};

uniform int primitiveCount;

float sdfBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b * 0.5;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

float sdfSphere(vec3 p, float r) {
    return length(p) - r;
}

float evaluateSDF(vec3 worldPos) {
    float minDist = 1e10;
    for (int i = 0; i < primitiveCount; ++i) {
        Primitive prim = primitives[i];
        vec3 localPos = worldPos - prim.position;
        
        float dist;
        if (prim.type == 0) {
            dist = sdfBox(localPos, prim.scale);
        } else {
            dist = sdfSphere(localPos, prim.scale.x);
        }
        minDist = min(minDist, dist);
    }
    return minDist;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(sdfVolume);
    if (any(greaterThanEqual(coord, size))) return;
    
    vec3 uvw = (vec3(coord) + 0.5) / vec3(size);
    vec3 worldPos = volumeOrigin + uvw * volumeSize;
    
    float sdf = evaluateSDF(worldPos);
    imageStore(sdfVolume, coord, vec4(sdf, 0.0, 0.0, 0.0));
}
```

**Task 0.1.3: Integrate into Demo3D**
- File: `src/demo3d.cpp` - Modify `sdfGenerationPass()`
- Replace TODO stub with actual dispatch call
```cpp
void Demo3D::sdfGenerationPass() {
    if (!analyticSDFEnabled) {
        std::cout << "[Demo3D] SDF generation skipped (analytic SDF not enabled)" << std::endl;
        return;
    }
    
    std::cout << "[Demo3D] Generating analytic SDF..." << std::endl;
    
    // Upload primitives to SSBO
    uploadPrimitivesToGPU();
    
    // Dispatch compute shader
    glUseProgram(shaders["sdf_analytic.comp"]);
    
    glUniform3fv(glGetUniformLocation(activeShader, "volumeOrigin"), 
                 1, &volumeOrigin[0]);
    glUniform3fv(glGetUniformLocation(activeShader, "volumeSize"), 
                 1, &volumeSize[0]);
    glUniform1i(glGetUniformLocation(activeShader, "primitiveCount"), 
                analyticSDF.getPrimitives().size());
    
    glBindImageTexture(0, sdfTexture, 0, GL_FALSE, 0, 
                       GL_WRITE_ONLY, GL_R32F);
    
    glm::ivec3 workGroups = calculateWorkGroups(
        volumeResolution, volumeResolution, volumeResolution, 8);
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    std::cout << "[Demo3D] SDF generation complete." << std::endl;
}
```

**Testing Checklist:**
- [ ] Compile shader without errors
- [ ] Verify SDF texture contains valid distance values
- [ ] Visualize SDF cross-section (slice through center)
- [ ] Confirm negative values inside geometry, positive outside

---

### Day 3: Single Cascade Initialization

#### 🎓 Architect Agent:
> "Design cascade system to be extensible. Use configuration-driven initialization rather than hardcoded values."

#### ⚙️ Implementer Agent:
> "Just hardcode one cascade level at 64³ resolution. We can make it flexible later."

#### ✅ Consensus:
Hardcode single cascade for Phase 0, but structure code so adding more levels is trivial (array-based).

#### Tasks:

**Task 0.2.1: Initialize Single Cascade**
- File: `src/demo3d.cpp` - Modify `initCascades()`
```cpp
void Demo3D::initCascades() {
    std::cout << "[Demo3D] Initializing cascades..." << std::endl;
    
    // Phase 0: Single cascade for validation
    cascadeCount = 1;
    cascades.resize(cascadeCount);
    
    // Cascade 0: Base level
    cascades[0].resolution = 64;  // Lower res for quick testing
    cascades[0].cellSize = volumeSize.x / 64.0f;
    cascades[0].origin = volumeOrigin;
    cascades[0].raysPerProbe = 8;  // Minimal rays for speed
    cascades[0].intervalStart = 0.0f;
    cascades[0].intervalEnd = volumeSize.x * 0.5f;  // Half scene extent
    cascades[0].active = true;
    
    // Create probe grid texture
    cascades[0].probeGridTexture = gl::createTexture3D(
        cascades[0].resolution,
        cascades[0].resolution,
        cascades[0].resolution,
        GL_RGBA16F,  // Half-float for radiance
        GL_RGBA,
        GL_HALF_FLOAT,
        nullptr
    );
    
    std::cout << "  Cascade 0: " << cascades[0].resolution << "^3, cell=" 
              << cascades[0].cellSize << std::endl;
}
```

**Task 0.2.2: Add Cascade Debug Visualization**
- File: `src/demo3d.cpp` - New method `renderCascadeDebug()`
- Render probes as colored points based on radiance magnitude

---

### Day 4: Direct Lighting Injection

#### 🎓 Architect Agent:
> "Support multiple light types from day one (point, directional, area). Design flexible light interface."

#### ⚙️ Implementer Agent:
> "Start with single point light. Get it working, then add complexity."

#### ✅ Consensus:
Implement point light only for Phase 0, but design uniform buffer structure to support expansion.

#### Tasks:

**Task 0.3.1: Define Light Structure**
- File: `src/demo3d.h` - Add to Demo3D class
```cpp
struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;  // For area lights (future)
};

std::vector<Light> lights;
```

**Task 0.3.2: Implement Direct Lighting Shader**
- File: `res/shaders/inject_radiance.comp` (already exists, needs activation)
- Simplify to point light only for now
```glsl
#version 430
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(binding = 0) uniform sampler3D sdfVolume;
layout(rgba16f, binding = 0) uniform image3D radianceGrid;

uniform vec3 lightPosition;
uniform vec3 lightColor;
uniform float lightIntensity;
uniform vec3 volumeOrigin;
uniform vec3 volumeSize;
uniform float cellSize;

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(radianceGrid);
    if (any(greaterThanEqual(coord, size))) return;
    
    // Calculate probe world position
    vec3 uvw = (vec3(coord) + 0.5) / vec3(size);
    vec3 probePos = volumeOrigin + uvw * volumeSize;
    
    // Sample SDF to check if probe is in free space
    float sdf = texelFetch(sdfVolume, coord, 0).r;
    if (sdf < 0.0) {
        // Inside geometry - no lighting
        imageStore(radianceGrid, coord, vec4(0.0));
        return;
    }
    
    // Calculate direct lighting
    vec3 lightDir = normalize(lightPosition - probePos);
    float distance = length(lightPosition - probePos);
    
    // Simple shadow test via SDF raymarching
    bool inShadow = false;
    float t = 0.0;
    while (t < distance) {
        vec3 samplePos = probePos + lightDir * t;
        vec3 sampleUVW = (samplePos - volumeOrigin) / volumeSize;
        ivec3 sampleCoord = ivec3(sampleUVW * vec3(textureSize(sdfVolume, 0)));
        
        float sampleSDF = texelFetch(sdfVolume, sampleCoord, 0).r;
        if (sampleSDF < 0.0) {
            inShadow = true;
            break;
        }
        t += max(sampleSDF, 0.01);
    }
    
    if (!inShadow) {
        // Lambertian diffuse
        vec3 normal = vec3(0.0, 1.0, 0.0);  // Placeholder - use SDF gradient later
        float NdotL = max(dot(normal, lightDir), 0.0);
        
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColor * lightIntensity * NdotL * attenuation;
        
        imageStore(radianceGrid, coord, vec4(radiance, 1.0));
    } else {
        imageStore(radianceGrid, coord, vec4(0.0));
    }
}
```

**Task 0.3.3: Connect to Render Loop**
- File: `src/demo3d.cpp` - Modify `injectDirectLighting()`
- Add default point light at scene center

---

### Day 5: Basic Raymarching & Visualization

#### 🎓 Architect Agent:
> "Design raymarcher to support both SDF visualization and final GI rendering. Use modular shader functions."

#### ⚙️ Implementer Agent:
> "Just render SDF as grayscale volume for now. We'll do proper GI later."

#### ✅ Consensus:
Implement basic volume raymarching with SDF, display as grayscale. Add radiance sampling as bonus if time permits.

#### Tasks:

**Task 0.4.1: Update Raymarch Fragment Shader**
- File: `res/shaders/raymarch.frag` (already exists, enhance it)
```glsl
#version 330 core
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler3D sdfVolume;
uniform sampler3D radianceGrid;
uniform vec3 volumeOrigin;
uniform vec3 volumeSize;
uniform mat4 viewMatrix;
uniform mat4 projMatrix;
uniform vec3 cameraPosition;

const int MAX_STEPS = 128;
const float MIN_DIST = 0.01;
const float MAX_DIST = 100.0;

void main() {
    // Setup ray from camera through pixel
    vec2 uv = fragTexCoord * 2.0 - 1.0;
    vec4 clipPos = vec4(uv, -1.0, 1.0);
    vec4 viewPos = inverse(projMatrix) * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = inverse(viewMatrix) * viewPos;
    
    vec3 rayDir = normalize(worldPos.xyz - cameraPosition);
    vec3 rayOrig = cameraPosition;
    
    // Volume raymarching
    float t = 0.0;
    vec3 accumulatedColor = vec3(0.0);
    float alpha = 0.0;
    
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 pos = rayOrig + rayDir * t;
        
        // Check if outside volume
        vec3 uvw = (pos - volumeOrigin) / volumeSize;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) {
            break;
        }
        
        // Sample SDF
        ivec3 coord = ivec3(uvw * vec3(textureSize(sdfVolume, 0)));
        float sdf = texelFetch(sdfVolume, coord, 0).r;
        
        if (sdf < 0.0) {
            // Hit surface - sample radiance
            vec3 radiance = texelFetch(radianceGrid, coord, 0).rgb;
            accumulatedColor += radiance * (1.0 - alpha);
            alpha += 0.1;  // Simple accumulation
            
            if (alpha > 0.95) break;
        }
        
        t += max(abs(sdf), 0.05);
        if (t > MAX_DIST) break;
    }
    
    // Background gradient
    vec3 background = mix(vec3(0.1, 0.1, 0.2), vec3(0.0), fragTexCoord.y);
    finalColor = vec4(mix(background, accumulatedColor, alpha), 1.0);
}
```

**Task 0.4.2: Test & Debug**
- Verify raymarching produces visible output
- Adjust step size and max steps for performance
- Add keyboard shortcut to toggle SDF-only vs radiance view

---

### Phase 0 Deliverables Checklist

- [ ] Analytic SDF compute shader working
- [ ] Single cascade initialized (64³)
- [ ] Point light direct lighting injected
- [ ] Basic volume raymarching rendering
- [ ] Debug visualization toggles (SDF slice, cascade probes, final render)
- [ ] Performance > 10 FPS
- [ ] No OpenGL errors in console

**Estimated Effort:** 5 days  
**Risk Level:** Low (all components are isolated and testable)

---

## Phase 1: Basic Global Illumination (Weeks 2-3)

**Goal:** Achieve convincing indirect lighting with color bleeding and soft shadows in Cornell Box scene.

### Success Criteria
- ✅ Cornell Box renders with visible color bleeding (red wall affects floor, etc.)
- ✅ Soft shadows from area lights
- ✅ Multiple light sources supported
- ✅ Frame rate > 15 FPS for 64³ grid
- ✅ UI controls for light parameters

---

### Week 2: Enhanced Lighting & Materials

#### Day 6-7: Multi-Light Support

**Tasks:**
1. Extend light uniform buffer to support array
2. Modify `inject_radiance.comp` to loop over lights
3. Add UI sliders for light position/color/intensity
4. Create 3-light test setup (key, fill, rim)

**Key Code Changes:**
```glsl
// inject_radiance.comp - Multi-light loop
#define MAX_LIGHTS 8
uniform int lightCount;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];

for (int i = 0; i < lightCount; ++i) {
    // Calculate contribution from light i
    // Accumulate into radiance variable
}
```

---

#### Day 8-9: Material System

**🎓 Architect Agent:**
> "Design PBR-inspired material system even if we only use diffuse for now. Future-proof the architecture."

**⚙️ Implementer Agent:**
> "Just add albedo color per voxel. Keep it simple."

**✅ Consensus:** Store albedo in voxel grid's RGB channels, reserve alpha for roughness (future).

**Tasks:**
1. Change voxel grid format from RGBA8 to RGBA16F
2. Store material color during voxelization
3. Use albedo in lighting calculations: `radiance *= albedo`
4. Add test boxes with different colors (red, green, blue)

---

#### Day 10: SDF Gradient Normals

**Tasks:**
1. Implement central difference gradient in shader
2. Replace placeholder normal `(0,1,0)` with computed normal
3. Verify normals point outward from surfaces
4. Improve lighting accuracy with correct NdotL

```glsl
vec3 computeNormal(ivec3 coord) {
    float dx = texelFetch(sdfVolume, coord + ivec3(1,0,0), 0).r -
               texelFetch(sdfVolume, coord - ivec3(1,0,0), 0).r;
    float dy = texelFetch(sdfVolume, coord + ivec3(0,1,0), 0).r -
               texelFetch(sdfVolume, coord - ivec3(0,1,0), 0).r;
    float dz = texelFetch(sdfVolume, coord + ivec3(0,0,1), 0).r -
               texelFetch(sdfVolume, coord - ivec3(0,0,1), 0).r;
    return normalize(vec3(dx, dy, dz));
}
```

---

### Week 3: Indirect Lighting (Radiance Cascades)

#### Day 11-12: Single Cascade Ray Tracing

**Critical Task:** This is where actual GI happens!

**Tasks:**
1. Implement `radiance_3d.comp` dispatch
2. Generate ray directions (start with 8 fixed directions)
3. Raymarch through SDF to find intersections
4. Sample material at hit point
5. Accumulate radiance back into cascade texture

**Key Algorithm:**
```glsl
// radiance_3d.comp - Simplified version
void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    
    // For each ray direction
    for (int r = 0; r < RAYS_PER_PROBE; ++r) {
        vec3 rayDir = rayDirections[r];
        
        // Raymarch through SDF
        float t = 0.0;
        vec3 hitPos;
        bool hit = false;
        
        for (int step = 0; step < MAX_STEPS; ++step) {
            vec3 pos = probePos + rayDir * t;
            float sdf = sampleSDF(pos);
            
            if (sdf < EPSILON) {
                hitPos = pos;
                hit = true;
                break;
            }
            t += sdf;
        }
        
        if (hit) {
            // Sample direct lighting at hit point
            vec3 indirectRadiance = sampleDirectLighting(hitPos);
            accumulateRadiance(coord, indirectRadiance);
        }
    }
}
```

---

#### Day 13-14: Cascade Sampling in Final Render

**Tasks:**
1. Modify `raymarch.frag` to sample from cascade texture
2. When ray hits surface, query cascade for indirect lighting
3. Combine direct + indirect: `finalColor = direct + indirect * albedo`
4. Tune blending weights for visual quality

---

#### Day 15: Cornell Box Scene

**Tasks:**
1. Create Cornell Box geometry (5 walls with specific colors)
2. Place light source on ceiling
3. Position camera for classic Cornell Box view
4. Capture screenshot showing color bleeding

**Expected Result:** Red wall should cast red tint on white floor, green wall casts green tint.

---

### Phase 1 Deliverables Checklist

- [ ] Multi-light system working (≥3 lights)
- [ ] Material colors affect GI (color bleeding visible)
- [ ] Correct surface normals from SDF gradients
- [ ] Single cascade ray tracing producing indirect lighting
- [ ] Cornell Box renders convincingly
- [ ] UI controls for all major parameters
- [ ] Performance > 15 FPS

**Estimated Effort:** 10 days  
**Risk Level:** Medium (ray tracing logic can be tricky to debug)

---

## Phase 2: Multi-Cascade Hierarchy (Weeks 4-5)

**Goal:** Support large scenes with consistent GI quality through hierarchical cascade levels.

### Success Criteria
- ✅ 4 cascade levels active simultaneously
- ✅ Smooth transitions between cascade boundaries
- ✅ Large scene (256³ equivalent) renders without artifacts
- ✅ Frame rate > 10 FPS
- ✅ Distance-based LOD selection working

---

### Week 4: Cascade Hierarchy Implementation

#### Day 16-17: Multi-Level Initialization

**Tasks:**
1. Extend `initCascades()` to create 4 levels
2. Configure resolutions: 128, 64, 32, 16
3. Calculate appropriate intervals for each level
4. Allocate textures for all levels

```cpp
void Demo3D::initMultiCascades() {
    cascadeCount = 4;
    cascades.resize(cascadeCount);
    
    struct CascadeConfig {
        int resolution;
        float intervalMultiplier;
    };
    
    CascadeConfig configs[] = {
        {128, 1.0f},   // Near field
        {64, 2.0f},    // Mid-near
        {32, 4.0f},    // Mid-far
        {16, 8.0f}     // Far field
    };
    
    for (int i = 0; i < cascadeCount; ++i) {
        cascades[i].resolution = configs[i].resolution;
        cascades[i].cellSize = volumeSize.x / configs[i].resolution;
        cascades[i].intervalStart = (i == 0) ? 0.0f : configs[i-1].intervalMultiplier * baseInterval;
        cascades[i].intervalEnd = configs[i].intervalMultiplier * baseInterval;
        // ... create texture
    }
}
```

---

#### Day 18-19: Inter-Cascade Blending

**🎓 Architect Agent:**
> "Use smoothstep blending with overlap regions to avoid visible seams. Consider trilinear interpolation across cascade boundaries."

**⚙️ Implementer Agent:**
> "Just pick the closest cascade level. Blending can cause ghosting artifacts."

**✅ Consensus:** Implement smoothstep blending but add debug option to disable it for comparison.

**Tasks:**
1. Implement `sampleCascades()` function with blending
2. Calculate blend weights based on ray distance
3. Test with synthetic data to verify no seams
4. Add UI toggle to show/hide blend regions

```cpp
glm::vec3 Demo3D::sampleCascades(const glm::vec3& pos, 
                                  const glm::vec3& dir,
                                  float rayDistance) {
    glm::vec3 result(0.0f);
    float totalWeight = 0.0f;
    
    for (int i = 0; i < cascadeCount; ++i) {
        const auto& c = cascades[i];
        
        // Calculate how much this cascade contributes
        float rangeSize = c.intervalEnd - c.intervalStart;
        float normalizedDist = (rayDistance - c.intervalStart) / rangeSize;
        
        // Smoothstep for seamless blending
        float weight = smoothstep(0.0f, 1.0f, normalizedDist);
        weight *= smoothstep(1.0f, 0.0f, normalizedDist);  // Fade out at end
        
        if (weight > 0.01f) {
            glm::vec3 radiance = sampleSingleCascade(i, pos, dir);
            result += radiance * weight;
            totalWeight += weight;
        }
    }
    
    return (totalWeight > 0.0f) ? result / totalWeight : glm::vec3(0.0f);
}
```

---

#### Day 20: Adaptive Ray Counts

**Tasks:**
1. Modify ray count per cascade level
2. Near cascades: 32 rays/probe
3. Far cascades: 8 rays/probe
4. Profile performance improvement

---

### Week 5: Large Scene Testing

#### Day 21-22: Sponza Atrium Simplified

**Tasks:**
1. Load simplified Sponza geometry (reduced polycount)
2. Increase volume resolution to 128³
3. Verify all 4 cascades contribute correctly
4. Identify and fix any artifacts

---

#### Day 23-24: Performance Optimization

**Tasks:**
1. Profile each cascade level's execution time
2. Optimize work group sizes
3. Reduce unnecessary memory barriers
4. Target >10 FPS at 128³

---

#### Day 25: Quality Tuning

**Tasks:**
1. Adjust cascade intervals for optimal coverage
2. Fine-tune blend weights
3. Compare against reference (path tracer if available)
4. Document parameter choices

---

### Phase 2 Deliverables Checklist

- [ ] 4 cascade levels active
- [ ] Smooth blending between levels
- [ ] Large scene (128³) renders correctly
- [ ] No visible cascade seams
- [ ] Performance > 10 FPS
- [ ] Parameter tuning documented

**Estimated Effort:** 10 days  
**Risk Level:** Medium-High (blending artifacts can be subtle and hard to fix)

---

## Phase 3: Voxel Pipeline & JFA (Weeks 6-7)

**Goal:** Replace analytic SDF with full voxelization pipeline supporting arbitrary triangle meshes.

### Success Criteria
- ✅ Triangle mesh voxelization working
- ✅ 3D JFA generates accurate SDF
- ✅ Can load OBJ/PLY files
- ✅ SDF quality matches or exceeds analytic version
- ✅ Frame rate remains acceptable (>8 FPS)

---

### Week 6: Voxelization Pipeline

#### Day 26-27: Triangle Mesh Loader

**Tasks:**
1. Integrate tinyobjloader or similar library
2. Parse OBJ file format (vertices, indices, normals)
3. Upload mesh data to GPU SSBO
4. Test with simple models (cube, sphere mesh)

---

#### Day 28-29: Voxelization Compute Shader

**Tasks:**
1. Implement `voxelize.comp` with triangle rasterization
2. Use conservative rasterization to avoid holes
3. Store occupancy in voxel grid
4. Visualize voxel cloud for verification

**Key Challenge:** Efficiently determining which voxels a triangle intersects.

**Approach:** Bounding box test + per-voxel triangle intersection test.

---

#### Day 30: Material Extraction

**Tasks:**
1. Sample vertex colors during voxelization
2. Average colors for voxels hit by multiple triangles
3. Store in voxel grid's RGB channels

---

### Week 7: 3D Jump Flooding Algorithm

#### Day 31-32: JFA Seed Initialization

**🎓 Architect Agent:**
> "Implement full 3D JFA with proper seed encoding. This is critical for accurate SDF."

**⚙️ Implementer Agent:**
> "Can we approximate SDF by blurring the voxel grid? Much simpler."

**✅ Consensus:** Implement proper JFA - it's well-understood algorithm and ShaderToy has 2D reference.

**Tasks:**
1. Encode occupied voxel positions as seeds
2. Use high-precision format (RGBA32F) for seed texture
3. Pack 3D position into RGBA: `(x, y, z, 0)`

---

#### Day 33-34: JFA Propagation Passes

**Tasks:**
1. Implement log₂(N) propagation passes
2. Each pass checks 26 neighbors (3×3×3 - center)
3. Offset doubles each pass: 1, 2, 4, 8, 16...
4. Keep closest seed at each voxel

```glsl
// sdf_3d.comp - One JFA pass
uniform int passOffset;  // 1, 2, 4, 8, 16...

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    vec4 currentSeed = texelFetch(seedTexture, coord, 0);
    float currentDist = length(currentSeed.xyz - vec3(coord));
    
    // Check 26 neighbors
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0) continue;
                
                ivec3 neighbor = coord + ivec3(x, y, z) * passOffset;
                vec4 neighborSeed = texelFetch(seedTexture, neighbor, 0);
                float neighborDist = length(neighborSeed.xyz - vec3(coord));
                
                if (neighborDist < currentDist) {
                    currentDist = neighborDist;
                    currentSeed = neighborSeed;
                }
            }
        }
    }
    
    imageStore(outputTexture, coord, currentSeed);
}
```

---

#### Day 35: Distance Extraction & Validation

**Tasks:**
1. Convert final seeds to distances: `dist = length(pos - seed)`
2. Compare against analytic SDF for same geometry
3. Measure error metrics (RMSE, max error)
4. Tune JFA parameters if needed

---

#### Day 36-37: Integration & Testing

**Tasks:**
1. Switch from analytic SDF to JFA-generated SDF
2. Test with complex meshes (Sponza, Buddha statue)
3. Verify GI still works correctly
4. Profile performance impact

---

### Phase 3 Deliverables Checklist

- [ ] OBJ/PLY mesh loader working
- [ ] Triangle voxelization complete
- [ ] 3D JFA produces accurate SDF
- [ ] SDF error < 5% compared to analytic
- [ ] Complex meshes render with GI
- [ ] Performance > 8 FPS

**Estimated Effort:** 12 days  
**Risk Level:** High (JFA is complex and bugs can be subtle)

---

## Phase 4: Optimization & Polish (Weeks 8-10)

**Goal:** Achieve production-ready performance and quality with advanced features.

### Success Criteria
- ✅ Temporal reprojection reduces noise
- ✅ Sparse voxel octree reduces memory by 50%+
- ✅ Frame rate > 30 FPS at 128³
- ✅ Professional-quality UI/UX
- ✅ Comprehensive documentation

---

### Week 8: Temporal Reprojection

#### Day 38-39: Velocity Buffer

**Tasks:**
1. Render previous frame's depth and position
2. Calculate per-pixel motion vectors
3. Store in velocity buffer texture

---

#### Day 40-41: Reprojection Shader

**Tasks:**
1. Implement `reproject.comp`
2. Warp previous frame's radiance using velocity buffer
3. Handle disocclusions (reset accumulation)
4. Exponential blend: `result = 0.9 * reprojected + 0.1 * current`

---

#### Day 42: Noise Reduction Validation

**Tasks:**
1. Compare noisy (8 rays) vs denoised results
2. Measure variance reduction
3. Tune blend factor for optimal quality/performance

---

### Week 9: Sparse Voxel Octree

#### Day 43-45: Octree Construction

**Tasks:**
1. Implement CPU-side octree builder
2. Convert dense voxel grid to sparse representation
3. Upload to GPU as SSBO
4. Verify memory savings

---

#### Day 46-47: Octree Traversal Shader

**Tasks:**
1. Rewrite SDF sampling to traverse octree
2. Optimize for GPU coherence (warp-level optimizations)
3. Profile performance vs dense grid

---

### Week 10: Polish & Documentation

#### Day 48-49: UI/UX Improvements

**Tasks:**
1. Add presets (Low/Medium/High/Ultra quality)
2. Real-time FPS counter with breakdown
3. Interactive tutorial mode
4. Screenshot/video capture

---

#### Day 50: Performance Profiling

**Tasks:**
1. GPU timer queries for each pass
2. Identify bottlenecks
3. Optimize critical paths
4. Document optimization techniques

---

#### Day 51-52: Documentation

**Tasks:**
1. Write user guide
2. Create API documentation
3. Record demo video
4. Prepare GitHub README

---

### Phase 4 Deliverables Checklist

- [ ] Temporal reprojection working
- [ ] Sparse voxel octree implemented
- [ ] Memory usage reduced by 50%+
- [ ] Performance > 30 FPS
- [ ] Professional UI/UX
- [ ] Complete documentation
- [ ] Demo video recorded

**Estimated Effort:** 15 days  
**Risk Level:** Medium (optimizations are iterative and unpredictable)

---

## Cross-Phase Concerns

### Memory Management

**All Phases:**
- Implement RAII wrappers for OpenGL resources
- Add proper destructors to all classes
- Test for memory leaks with Valgrind/RenderDoc
- Monitor VRAM usage in real-time

### Error Handling

**All Phases:**
- Check OpenGL errors after every call (`glGetError()`)
- Validate shader compilation/linking
- Graceful degradation when features unavailable
- Informative error messages for users

### Testing Strategy

**Each Phase:**
- Unit tests for individual components
- Visual regression tests (screenshots)
- Performance benchmarks
- Cross-platform testing (Windows, Linux)

---

## Dual-Agent Decision Log

### Decision 1: Analytic SDF vs Voxel JFA (Phase 0)

**Architect Agent:** "Build proper voxel pipeline from start. Analytic SDF is a distraction."  
**Implementer Agent:** "Analytic SDF lets us validate everything else first. Faster feedback loop."  
**Decision:** ✅ Analytic SDF for Phase 0, migrate to JFA in Phase 3.  
**Rationale:** Reduces initial complexity, allows parallel development of other systems.

---

### Decision 2: Single vs Multi-Cascade (Phase 0-1)

**Architect Agent:** "Design for multi-cascade from day one. Refactoring later is painful."  
**Implementer Agent:** "Get one cascade working perfectly first. Adding more is easy once the algorithm is correct."  
**Decision:** ✅ Single cascade for Phase 0, expand to multi in Phase 2.  
**Rationale:** Isolates algorithm bugs from hierarchy bugs.

---

### Decision 3: Ray Count Strategy

**Architect Agent:** "Use adaptive ray counts immediately. It's better architecture."  
**Implementer Agent:** "Fixed ray count is simpler to debug. Optimize later."  
**Decision:** ✅ Fixed 8 rays for Phase 0, adaptive in Phase 2.  
**Rationale:** Premature optimization complicates debugging.

---

### Decision 4: Storage Format (3D Texture vs Cubemap)

**Architect Agent:** "Cubemaps are more efficient for directional data. Use them from start."  
**Implementer Agent:** "3D textures are easier to understand and debug. Stick with them."  
**Decision:** ✅ 3D textures throughout, consider cubemap migration in Phase 4 if needed.  
**Rationale:** Development speed > marginal performance gain initially.

---

### Decision 5: JFA Implementation Timing

**Architect Agent:** "Implement JFA alongside voxelization in Phase 3. They're coupled."  
**Implementer Agent:** "Can we skip JFA entirely and use rasterized depth maps?"  
**Decision:** ✅ Full 3D JFA required for Phase 3. No shortcuts.  
**Rationale:** Paper specifically requires JFA for quality. Alternatives produce inferior results.

---

## Appendix: File Creation Checklist

### Phase 0 New Files:
- [ ] `src/analytic_sdf.h`
- [ ] `res/shaders/sdf_analytic.comp`

### Phase 1 New Files:
- [ ] `res/shaders/material_utils.glsl` (shared functions)

### Phase 3 New Files:
- [ ] `src/mesh_loader.h/cpp`
- [ ] `src/octree.h/cpp`
- [ ] `res/shaders/voxelize.comp` (enhanced)

### Phase 4 New Files:
- [ ] `src/temporal_reprojection.h/cpp`
- [ ] `res/shaders/reproject.comp`
- [ ] `res/shaders/velocity_buffer.vert/frag`

---

## Conclusion

This phased approach balances **speed-to-results** with **long-term quality**. Each phase delivers a working, testable milestone while building toward the complete system. The dual-agent review process ensures both architectural soundness and practical feasibility.

**Total Estimated Timeline:** 10 weeks (50 working days)  
**Confidence Level:** High for Phases 0-2, Medium for Phases 3-4 (JFA and SVO complexity)

**Next Immediate Action:** Begin Phase 0, Day 1 - Create `analytic_sdf.h` and implement box/sphere SDF functions.

---

**End of Phase Implementation Plans**
