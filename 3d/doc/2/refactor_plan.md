# 3D Radiance Cascades - Comprehensive Refactor Plan

**Document Version:** 1.0  
**Last Updated:** 2026-04-17  
**Status:** Planning Phase  
**Author:** AI Assistant  

---

## Executive Summary

This document provides a comprehensive refactor plan to transform the current **stub implementation** of 3D Radiance Cascades into a **fully functional volumetric global illumination system**. The project currently has complete shader files and API documentation, but lacks actual implementation code. This plan bridges that gap through a phased, testable approach.

### Current State Assessment

✅ **Completed:**
- All GLSL shaders exist (voxelize.comp, sdf_3d.comp, radiance_3d.comp, inject_radiance.comp, raymarch.frag)
- Complete C++ header with full API specification (`demo3d.h`)
- ShaderToy reference implementation available for algorithm verification
- Migration guide documenting architectural decisions

❌ **Missing:**
- OpenGL helper function implementations (`gl_helpers.cpp` is empty/stub)
- Demo3D method implementations (all functions are TODO stubs)
- Pipeline integration (shaders not connected in render loop)
- Scene geometry loading/creation
- Working build configuration (CMakeLists.txt has conflicts)

⚠️ **Critical Issues:**
1. No actual GPU compute pipeline execution
2. Memory management for large volumes untested
3. Cascade hierarchy logic unimplemented
4. Temporal reprojection math incomplete

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Implementation Phases](#2-implementation-phases)
3. [Phase 1: Foundation Layer](#phase-1-foundation-layer-weeks-1-2)
4. [Phase 2: Core Algorithm](#phase-2-core-algorithm-weeks-3-4)
5. [Phase 3: Cascade Hierarchy](#phase-3-cascade-hierarchy-weeks-5-6)
6. [Phase 4: Optimization & Polish](#phase-4-optimization--polish-weeks-7-8)
7. [Technical Deep Dives](#3-technical-deep-dives)
8. [Testing Strategy](#4-testing-strategy)
9. [Risk Mitigation](#5-risk-mitigation)
10. [Reference Materials](#6-reference-materials)

---

## 1. Architecture Overview

### 1.1 Rendering Pipeline Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    3D Radiance Cascade Pipeline              │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │ Geometry │───▶│ Voxelization │───▶│  SDF Generation  │  │
│  │  Input   │    │  (Compute)   │    │   (Compute JFA)  │  │
│  └──────────┘    └──────────────┘    └──────────────────┘  │
│                                           │                  │
│                                           ▼                  │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │  Final   │◀───│ Raymarching  │◀───│Radiance Cascades │  │
│  │  Output  │    │  (Fragment)  │    │   (Compute)      │  │
│  └──────────┘    └──────────────┘    └──────────────────┘  │
│                           ▲                  │               │
│                           └──────────────────┘               │
│                        Temporal Feedback                      │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Data Flow Diagram

```
Scene Geometry (Triangles)
         │
         ▼
┌─────────────────────┐
│  Voxel Grid (RGBA8)  │ ← voxelize.comp
│  Resolution: 128³    │
└─────────────────────┘
         │
         ▼
┌─────────────────────┐
│  SDF Volume (R32F)   │ ← sdf_3d.comp (JFA 3D)
│  Signed Distance     │
└─────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  Radiance Cascades (RGBA16F each)   │
│  ┌───────────────────────────────┐  │
│  │ Cascade 0: 128³ probes        │  │ ← radiance_3d.comp
│  │ Cascade 1: 64³ probes         │  │    + inject_radiance.comp
│  │ Cascade 2: 32³ probes         │  │
│  │ ... up to Cascade N           │  │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────┐
│ Raymarched Image    │ ← raymarch.frag
│ (Screen Space)      │
└─────────────────────┘
```

### 1.3 Key Differences from 2D Implementation

| Aspect | 2D Version | 3D Version |
|--------|-----------|------------|
| **Data Structure** | RenderTexture2D | 3D Textures / Sparse Octree |
| **Processing** | Fragment Shaders | Compute Shaders |
| **Distance Field** | 2D JFA on texture | 3D JFA on volume |
| **Probes** | Screen-space pixels | World-space voxel grid |
| **Ray Tracing** | 2D line segments | 3D raymarching through SDF |
| **Memory** | ~2MB (512² × 4 bytes) | ~800MB (128³ × 4 bytes) |
| **Performance** | Real-time easy | Requires optimization |

---

## 2. Implementation Phases

The refactor is divided into **4 phases**, each building on the previous one. Each phase ends with a **working, testable milestone**.

### Phase Timeline

```
Week 1-2:  Phase 1 - Foundation Layer
Week 3-4:  Phase 2 - Core Algorithm
Week 5-6:  Phase 3 - Cascade Hierarchy
Week 7-8:  Phase 4 - Optimization & Polish
```

---

## Phase 1: Foundation Layer (Weeks 1-2)

**Goal:** Establish working OpenGL infrastructure and basic rendering pipeline.

### Milestone 1.1: OpenGL Helper Implementation

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 3-4 days

#### Tasks:

1. **Implement `gl_helpers.cpp`**
   - [ ] `createTexture3D()` - Create 3D volume textures
   - [ ] `updateTexture3DSubregion()` - Partial texture updates
   - [ ] `setTexture3DParameters()` - Filtering and wrap modes
   - [ ] `createFramebuffer3D()` - FBO with 3D attachment
   - [ ] `loadComputeShader()` - Compile .comp files
   - [ ] `dispatchComputeShader()` - Execute compute kernels
   - [ ] `bindImageTexture()` - Image load/store binding
   - [ ] `createTimeQuery()` - GPU performance measurement

2. **Test Infrastructure**
   - [ ] Add OpenGL error checking macro
   - [ ] Enable debug output callback
   - [ ] Create simple 3D texture unit test

#### Deliverables:
- ✅ Functional `gl_helpers.cpp` with all methods implemented
- ✅ Unit test creating and sampling a 32³ volume texture
- ✅ Verify OpenGL 4.3+ context creation

#### Code Example - Texture Creation:

```cpp
GLuint gl::createTexture3D(GLsizei width, GLsizei height, GLsizei depth,
                           GLint internalFormat, GLenum format, 
                           GLenum type, const void* data) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    
    // Set parameters
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    
    // Allocate storage
    glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, 
                 width, height, depth, 
                 0, format, type, data);
    
    return texture;
}
```

---

### Milestone 1.2: Basic Scene Geometry

**Priority:** 🟡 HIGH  
**Estimated Effort:** 2-3 days

#### Tasks:

1. **Procedural Geometry Generation**
   - [ ] Implement `addVoxelBox()` method
   - [ ] Create triangle mesh representation
   - [ ] Upload geometry to SSBO (Shader Storage Buffer Object)

2. **Simple Test Scenes**
   - [ ] Single box in center
   - [ ] Cornell box variant (3 walls)
   - [ ] Sphere primitive

3. **Voxelization Pass**
   - [ ] Implement `voxelizationPass()` method
   - [ ] Dispatch voxelize.comp compute shader
   - [ ] Visualize voxel grid as point cloud

#### Deliverables:
- ✅ Can create and render simple 3D shapes
- ✅ Voxel grid populated with geometry
- ✅ Debug visualization showing voxel occupancy

#### Integration Points:

```cpp
void Demo3D::voxelizationPass() {
    // Bind compute shader
    glUseProgram(shaders["voxelize.comp"]);
    
    // Set uniforms
    glUniform3iv(glGetUniformLocation(activeShader, "uVolumeSize"), 
                 1, &volumeResolution);
    glUniform3fv(glGetUniformLocation(activeShader, "uGridSize"), 
                 1, &gridSize[0]);
    
    // Bind triangle buffer
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
    
    // Bind output texture
    glBindImageTexture(0, voxelGridTexture, 0, GL_FALSE, 0, 
                       GL_WRITE_ONLY, GL_RGBA8);
    
    // Dispatch compute shader
    glm::ivec3 workGroups = calculateWorkGroups(
        volumeResolution, volumeResolution, volumeResolution, 8);
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    // Ensure writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
```

---

### Milestone 1.3: SDF Generation

**Priority:** 🟡 HIGH  
**Estimated Effort:** 2-3 days

#### Tasks:

1. **3D Jump Flooding Algorithm**
   - [ ] Implement seed encoding pass
   - [ ] Implement propagation passes (log₂(N) iterations)
   - [ ] Convert seeds to signed distances

2. **SDF Visualization**
   - [ ] Render cross-section slices
   - [ ] Color-code distance values
   - [ ] Verify correctness against analytical SDF

#### Deliverables:
- ✅ Working 3D SDF from voxel grid
- ✅ Visual verification of distance field accuracy
- ✅ Performance profiling of JFA passes

#### Algorithm Overview:

```
3D JFA Process:
1. Initialize: For each occupied voxel, store its position as seed
2. Propagate: For k = log₂(N) down to 0:
   - Each voxel checks 26 neighbors (3×3×3 - center)
   - Offset = 2^k in each direction
   - Keep closest seed
3. Extract: Distance = length(position - closest_seed)
```

---

### Phase 1 Success Criteria

- [ ] Can create and manipulate 3D textures via OpenGL
- [ ] Scene geometry successfully voxelized
- [ ] SDF accurately represents scene surfaces
- [ ] Frame rate > 30 FPS for 64³ volume
- [ ] No memory leaks or OpenGL errors

---

## Phase 2: Core Algorithm (Weeks 3-4)

**Goal:** Implement single-level radiance cascade with direct lighting.

### Milestone 2.1: Direct Lighting Injection

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 3-4 days

#### Tasks:

1. **Light Source Definition**
   - [ ] Point lights with position, color, intensity
   - [ ] Area lights (emissive voxels)
   - [ ] Directional light (sun simulation)

2. **Injection Shader**
   - [ ] Implement `injectDirectLighting()` method
   - [ ] Calculate visibility from probe to light
   - [ ] Handle occlusion via SDF raymarching
   - [ ] Store direct radiance in cascade texture

3. **Shadow Testing**
   - [ ] Ray march from probe to light source
   - [ ] Binary search for shadow boundary
   - [ ] Soft shadows for area lights

#### Deliverables:
- ✅ Direct lighting correctly computed at probe locations
- ✅ Shadows cast by geometry
- ✅ Multiple light sources supported

#### Code Structure:

```cpp
void Demo3D::injectDirectLighting() {
    glUseProgram(shaders["inject_radiance.comp"]);
    
    // Bind SDF for occlusion testing
    glBindTextureUnit(0, sdfTexture);
    
    // Bind output radiance texture
    glBindImageTexture(1, cascades[0].probeGridTexture, 0, 
                       GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    
    // Set light uniforms
    for (size_t i = 0; i < lights.size(); ++i) {
        std::string prefix = "lights[" + std::to_string(i) + "].";
        glUniform3fv(glGetUniformLocation(activeShader, 
                     (prefix + "position").c_str()), 
                     1, &lights[i].position[0]);
        glUniform3fv(glGetUniformLocation(activeShader, 
                     (prefix + "color").c_str()), 
                     1, &lights[i].color[0]);
    }
    
    // Dispatch
    glm::ivec3 workGroups = calculateWorkGroups(
        cascades[0].resolution, cascades[0].resolution, 
        cascades[0].resolution, 4);
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
```

---

### Milestone 2.2: Single Cascade Ray Tracing

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 4-5 days

#### Tasks:

1. **Probe Sampling**
   - [ ] Generate ray directions (Fibonacci sphere or Hammersley)
   - [ ] Calculate world position for each probe
   - [ ] Determine ray interval based on cascade level

2. **SDF-Guided Raymarching**
   - [ ] Implement adaptive step sizing
   - [ ] Detect surface intersections
   - [ ] Calculate surface normals from SDF gradient
   - [ ] Sample material properties at hit point

3. **Radiance Accumulation**
   - [ ] Evaluate diffuse BRDF (Lambertian)
   - [ ] Accumulate radiance from all rays
   - [ ] Normalize by solid angle

#### Deliverables:
- ✅ Probes correctly sample environment
- ✅ Surface normals computed from SDF
- ✅ Diffuse indirect lighting visible

#### Key Algorithm - Ray Direction Generation:

```glsl
// Fibonacci sphere for uniform distribution
vec3 getRayDirection(int idx, int totalRays) {
    float phi = 2.0 * PI * idx / float(totalRays);
    float cosTheta = 1.0 - (2.0 * float(idx) + 1.0) / float(totalRays);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    return vec3(cos(phi) * sinTheta, 
                sin(phi) * sinTheta, 
                cosTheta);
}
```

---

### Milestone 2.3: Raymarching Visualization

**Priority:** 🟡 HIGH  
**Estimated Effort:** 2-3 days

#### Tasks:

1. **Volume Rendering**
   - [ ] Implement `raymarchPass()` method
   - [ ] Cast rays from camera through volume
   - [ ] Accumulate radiance along ray
   - [ ] Apply tone mapping

2. **Camera System**
   - [ ] First-person camera controls
   - [ ] Orbit camera mode
   - [ ] Free-fly navigation

3. **Post-Processing**
   - [ ] Gamma correction
   - [ ] Exposure adjustment
   - [ ] Simple bloom effect

#### Deliverables:
- ✅ Interactive 3D view of radiance field
- ✅ Camera movement controls
- ✅ Visually plausible indirect lighting

---

### Phase 2 Success Criteria

- [ ] Direct lighting injects correctly into probes
- [ ] Single cascade computes indirect bounces
- [ ] Raymarching produces coherent images
- [ ] Frame rate > 20 FPS for 64³ volume with 16 rays/probe
- [ ] Lighting responds to scene changes in real-time

---

## Phase 3: Cascade Hierarchy (Weeks 5-6)

**Goal:** Implement multi-level cascade system with merging and temporal stability.

### Milestone 3.1: Multi-Level Cascade System

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 4-5 days

#### Tasks:

1. **Cascade Initialization**
   - [ ] Create multiple cascade levels (e.g., 128³, 64³, 32³, 16³)
   - [ ] Calculate cell sizes (exponential increase)
   - [ ] Set ray intervals per cascade

2. **Hierarchical Update**
   - [ ] Update cascades from fine to coarse
   - [ ] Coarse cascades sample finer levels
   - [ ] Fine cascades ray trace for near field

3. **Interval Management**
   - [ ] Cascade 0: 0 - 2 cells
   - [ ] Cascade 1: 2 - 8 cells
   - [ ] Cascade 2: 8 - 32 cells
   - [ ] etc.

#### Deliverables:
- ✅ Multiple cascades active simultaneously
- ✅ Correct interval partitioning
- ✅ Hierarchical data flow verified

#### Cascade Configuration Example:

```cpp
void Demo3D::initCascades() {
    cascadeCount = 5;
    baseInterval = 0.5f; // in world units
    
    for (int i = 0; i < cascadeCount; ++i) {
        int resolution = volumeResolution >> i; // Halve each level
        float cellSize = baseInterval * pow(4.0f, float(i));
        int raysPerProbe = BASE_RAY_COUNT * (i + 1); // More rays for far
        
        cascades[i].initialize(resolution, cellSize, 
                               glm::vec3(0.0f), raysPerProbe);
        
        // Interval: [baseInterval * 4^i, baseInterval * 4^(i+1))
        cascades[i].intervalStart = baseInterval * pow(4.0f, float(i));
        cascades[i].intervalEnd = baseInterval * pow(4.0f, float(i + 1));
    }
}
```

---

### Milestone 3.2: Weighted Bilinear Merging

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 5-6 days

#### Tasks:

1. **Visibility-Aware Interpolation**
   - [ ] Implement `WeightedSample()` from ShaderToy
   - [ ] Check probe-to-probe visibility
   - [ ] Weight contributions by distance and occlusion

2. **Cascade Blending**
   - [ ] Blend between cascade levels based on ray distance
   - [ ] Smooth transitions at boundaries
   - [ ] Handle edge cases (no valid samples)

3. **Bilinear Filtering**
   - [ ] Sample 4 neighboring probes
   - [ ] Interpolate based on fractional position
   - [ ] Normalize by total weight

#### Deliverables:
- ✅ Smooth transitions between cascade levels
- ✅ Occlusion-aware blending prevents light leaks
- ✅ Matches ShaderToy reference behavior

#### Reference from ShaderToy (CubeA.glsl):

```glsl
// Visibility-weighted sampling
vec4 WeightedSample(vec2 luvo, vec2 luvd, vec2 luvp, vec2 uvo, 
                    vec3 probePos, vec3 gTan, vec3 gBit, 
                    vec3 gPos, float lProbeSize) {
    vec3 lastProbePos = gPos + gTan*(luvp.x*lProbeSize/256.) + 
                             gBit*(luvp.y*lProbeSize/256.);
    vec3 relVec = probePos - lastProbePos;
    
    // Check if ray to coarser probe is unoccluded
    float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;
    if (lProbeRayDist < -0.5 || 
        length(relVec) < lProbeRayDist*cos(theta) + 0.01) {
        // Visible - sample and return
        return vec4(TextureCube(luv).xyz + ..., 1.);
    }
    return vec4(0.); // Occluded
}
```

---

### Milestone 3.3: Temporal Reprojection

**Priority:** 🟡 HIGH  
**Estimated Effort:** 3-4 days

#### Tasks:

1. **Camera Motion Tracking**
   - [ ] Store previous frame's view-projection matrix
   - [ ] Calculate pixel velocity vectors

2. **Reprojection Logic**
   - [ ] Transform current probe position to previous clip space
   - [ ] Sample previous radiance at reprojected location
   - [ ] Blend with current frame (e.g., 90% old, 10% new)

3. **Stability Enhancements**
   - [ ] Clamp reprojection to valid range
   - [ ] Reset accumulation on large camera moves
   - [ ] Handle disocclusion (newly visible areas)

#### Deliverables:
- ✅ Reduced noise through temporal accumulation
- ✅ Stable lighting during camera movement
- ✅ Fast convergence (< 10 frames)

#### Implementation Pattern:

```cpp
void Demo3D::updateRadianceCascades() {
    for (int i = 0; i < cascadeCount; ++i) {
        updateSingleCascade(i);
    }
    
    // After all cascades updated, apply temporal filtering
    if (useTemporalReprojection) {
        glUseProgram(shaders["temporal_filter.comp"]);
        
        // Bind current and previous radiance
        glBindTextureUnit(0, currentRadianceTexture);
        glBindTextureUnit(1, prevFrameTexture);
        
        // Bind output
        glBindImageTexture(0, filteredRadianceTexture, 0, 
                           GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        
        // Set blend factor
        glUniform1f(glGetUniformLocation(activeShader, "uBlendFactor"), 
                    0.1f); // 10% new, 90% old
        
        // Dispatch...
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        // Swap buffers
        std::swap(currentRadianceTexture, prevFrameTexture);
    }
}
```

---

### Phase 3 Success Criteria

- [ ] 5+ cascade levels operating correctly
- [ ] Smooth blending between levels (no visible seams)
- [ ] Temporal accumulation reduces noise significantly
- [ ] Frame rate > 15 FPS for 128³ volume with full hierarchy
- [ ] Lighting stable during camera movement

---

## Phase 4: Optimization & Polish (Weeks 7-8)

**Goal:** Optimize performance, add features, and polish user experience.

### Milestone 4.1: Sparse Voxel Octree

**Priority:** 🟢 MEDIUM  
**Estimated Effort:** 5-6 days

#### Tasks:

1. **Octree Data Structure**
   - [ ] Implement `VoxelNode` with child pointers
   - [ ] Build octree from dense voxel grid
   - [ ] Traverse octree in shaders (or CPU fallback)

2. **Memory Optimization**
   - [ ] Only allocate nodes near surfaces
   - [ ] Compress node data (quantize colors, normals)
   - [ ] Stream nodes dynamically if needed

3. **Adaptive Subdivision**
   - [ ] Subdivide cells with high geometric complexity
   - [ ] Merge empty or uniform regions
   - [ ] Balance tree depth

#### Deliverables:
- ✅ Memory usage reduced by 5-10x vs dense grid
- ✅ Same visual quality with fewer voxels
- ✅ Dynamic refinement based on camera proximity

#### Memory Comparison:

| Resolution | Dense Grid | Sparse (10% occupancy) | Savings |
|-----------|-----------|----------------------|---------|
| 64³       | 1 MB      | 0.1 MB               | 90%     |
| 128³      | 8 MB      | 0.8 MB               | 90%     |
| 256³      | 64 MB     | 6.4 MB               | 90%     |
| 512³      | 512 MB    | 51 MB                | 90%     |

---

### Milestone 4.2: Performance Optimization

**Priority:** 🟡 HIGH  
**Estimated Effort:** 3-4 days

#### Tasks:

1. **GPU Profiling**
   - [ ] Add timer queries to each pass
   - [ ] Identify bottlenecks
   - [ ] Profile different volume sizes

2. **Optimization Techniques**
   - [ ] Early ray termination (exit when accumulated opacity > threshold)
   - [ ] Adaptive ray counts (fewer rays for distant cascades)
   - [ ] Frustum culling (skip probes outside view)
   - [ ] LOD based on screen-space size

3. **Asynchronous Updates**
   - [ ] Update cascades at different frequencies
   - [ ] Finest cascade: every frame
   - [ ] Coarsest cascade: every 4-8 frames
   - [ ] Use compute shader async dispatch

#### Deliverables:
- ✅ Frame time breakdown documented
- ✅ Optimizations yield 2-3x speedup
- ✅ Real-time performance (> 30 FPS) at target resolution

#### Profiling Example:

```cpp
// In render loop
glBeginQuery(GL_TIME_ELAPSED, voxelizationTimeQuery);
voxelizationPass();
glEndQuery(GL_TIME_ELAPSED);

glBeginQuery(GL_TIME_ELAPSED, sdfTimeQuery);
sdfGenerationPass();
glEndQuery(GL_TIME_ELAPSED);

// Retrieve results (asynchronous, may be delayed 1 frame)
GLuint64 timeNs;
glGetQueryObjectui64v(voxelizationTimeQuery, 
                      GL_QUERY_RESULT, &timeNs);
voxelizationTimeMs = timeNs / 1e6; // Convert to milliseconds
```

---

### Milestone 4.3: UI and Debugging Tools

**Priority:** 🟢 MEDIUM  
**Estimated Effort:** 2-3 days

#### Tasks:

1. **ImGui Controls**
   - [ ] Light position/color sliders
   - [ ] Cascade visualization toggles
   - [ ] Performance metrics display
   - [ ] Scene preset selector

2. **Debug Visualizations**
   - [ ] Show individual cascade slices
   - [ ] Display probe positions and rays
   - [ ] SDF iso-surface rendering
   - [ ] Voxel grid wireframe overlay

3. **Screenshot and Export**
   - [ ] Save HDR images
   - [ ] Export voxel data for offline analysis
   - [ ] Capture video sequences

#### Deliverables:
- ✅ Intuitive UI for parameter tweaking
- ✅ Debug tools for troubleshooting artifacts
- ✅ Easy content creation workflow

---

### Milestone 4.4: Documentation and Examples

**Priority:** 🟢 MEDIUM  
**Estimated Effort:** 2-3 days

#### Tasks:

1. **Code Documentation**
   - [ ] Add Doxygen comments to all public APIs
   - [ ] Explain key algorithms with diagrams
   - [ ] Document shader uniform meanings

2. **User Guide**
   - [ ] Installation instructions
   - [ ] Tutorial scenes walkthrough
   - [ ] Troubleshooting common issues

3. **Example Scenes**
   - [ ] Cornell box (classic GI test)
   - [ ] Sponza atrium (complex geometry)
   - [ ] Animated scene (dynamic lighting)

#### Deliverables:
- ✅ Complete API documentation
- ✅ Step-by-step tutorial for beginners
- ✅ 3+ example scenes demonstrating features

---

### Phase 4 Success Criteria

- [ ] Memory usage < 2GB for 256³ volume
- [ ] Frame rate > 30 FPS at 128³ with all optimizations
- [ ] Comprehensive debugging tools available
- [ ] Well-documented codebase with examples
- [ ] Ready for production use or further research

---

## 3. Technical Deep Dives

### 3.1 Radiance Cascade Theory

**Core Concept:** Instead of tracing rays from every pixel (expensive), place **probes** in a hierarchical grid. Each probe stores incoming radiance from all directions.

**Cascade Hierarchy:**
```
Level 0 (Finest): 128³ probes, spacing = 0.5 units, rays = 4
Level 1:          64³ probes,  spacing = 2.0 units, rays = 8
Level 2:          32³ probes,  spacing = 8.0 units, rays = 16
Level 3:          16³ probes,  spacing = 32 units,  rays = 32
Level 4 (Coarsest): 8³ probes, spacing = 128 units, rays = 64
```

**Why Hierarchical?**
- **Near field**: High spatial resolution, few rays (detail matters)
- **Far field**: Low spatial resolution, many rays (coverage matters)

**Merging Strategy:**
- When a ray from a fine probe travels beyond its interval, it samples the next coarser cascade
- This provides **environment lighting** without expensive long-range ray tracing

---

### 3.2 3D Jump Flooding Algorithm

**Purpose:** Compute signed distance field from binary voxel grid in O(log N) passes.

**Algorithm Steps:**

1. **Seed Encoding:**
   ```glsl
   // For each occupied voxel, encode its position
   if (voxel.occupied) {
       seed = vec3(voxel_position);
       distance = 0.0;
   } else {
       seed = vec3(-1.0); // Invalid
       distance = INF;
   }
   ```

2. **Propagation (log₂(N) iterations):**
   ```glsl
   // For iteration k (starting from largest offset)
   offset = pow(2, k);
   
   // Check all 26 neighbors in 3×3×3 neighborhood
   for (dz = -offset; dz <= offset; dz += offset) {
       for (dy = -offset; dy <= offset; dy += offset) {
           for (dx = -offset; dx <= offset; dx += offset) {
               neighbor = texelFetch(seed_buffer, pos + ivec3(dx,dy,dz), 0);
               
               if (neighbor.seed != vec3(-1.0)) {
                   dist = length(pos - neighbor.seed);
                   if (dist < current_distance) {
                       current_distance = dist;
                       closest_seed = neighbor.seed;
                   }
               }
           }
       }
   }
   ```

3. **Distance Extraction:**
   ```glsl
   sdf = length(world_pos - closest_seed);
   
   // Sign: negative inside, positive outside
   if (voxel.occupied) sdf = -sdf;
   ```

**Complexity:** O(N³ log N) vs O(N⁶) for brute force!

---

### 3.3 SDF-Guided Raymarching

**Problem:** Uniform step size is inefficient - too small in empty space, too large near surfaces.

**Solution:** Use SDF to determine optimal step size.

```glsl
float t = t_min;
while (t < t_max) {
    vec3 pos = origin + direction * t;
    float dist = sampleSDF(pos);
    
    if (dist < epsilon) {
        // Hit surface!
        break;
    }
    
    // Step forward by SDF distance (with safety margin)
    t += dist * 0.9;
}
```

**Why it works:**
- SDF tells you distance to nearest surface
- You can safely advance by that distance without missing anything
- Near surfaces: small steps (high detail)
- Far from surfaces: large steps (fast traversal)

**Performance:** Typically 10-100x faster than uniform stepping!

---

### 3.4 Temporal Reprojection Math

**Goal:** Reuse previous frame's radiance to reduce noise.

**Steps:**

1. **Store Previous Frame Data:**
   ```cpp
   prevViewProjMatrix = currViewProjMatrix;
   std::swap(currentRadiance, previousRadiance);
   ```

2. **Reproject Current Probe to Previous Frame:**
   ```glsl
   // Transform world position to previous clip space
   vec4 prevClip = uPrevVPMatrix * vec4(worldPos, 1.0);
   vec3 prevUV = prevClip.xyz / prevClip.w; // Perspective divide
   
   // Map from [-1, 1] to [0, 1]
   prevUV = prevUV * 0.5 + 0.5;
   
   // Sample previous radiance
   vec4 prevRadiance = texture(uPrevRadiance, prevUV);
   ```

3. **Blend with Current Frame:**
   ```glsl
   // Exponential moving average
   float blendFactor = 0.1; // 10% new, 90% old
   finalRadiance = mix(prevRadiance, currentRadiance, blendFactor);
   ```

**Challenges:**
- **Disocclusion:** Newly visible areas have no history → detect and reset
- **Motion blur:** Fast camera movement causes ghosting → clamp reprojection
- **Dynamic objects:** Moving geometry invalidates history → use motion vectors

---

## 4. Testing Strategy

### 4.1 Unit Tests

**Test 1: 3D Texture Creation**
```cpp
TEST(CreateTexture3D) {
    GLuint tex = gl::createTexture3D(32, 32, 32, GL_RGBA8, 
                                      GL_RGBA, GL_UNSIGNED_BYTE);
    ASSERT_NE(tex, 0);
    
    // Write test data
    std::vector<uint8_t> data(32*32*32*4, 255);
    gl::updateTexture3DSubregion(tex, 0, 0, 0, 32, 32, 32, 
                                  GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    
    // Read back and verify
    std::vector<uint8_t> readback(32*32*32*4);
    glGetTextureSubImage(tex, 0, 0, 0, 0, 32, 32, 32, 
                         GL_RGBA, GL_UNSIGNED_BYTE, 
                         32*32*32*4, readback.data());
    
    ASSERT_EQ(readback[0], 255); // Should be white
}
```

**Test 2: SDF Accuracy**
```cpp
TEST(SDFAccuracy) {
    // Create sphere of radius 5
    createSphere(center, 5.0f);
    generateSDF();
    
    // Check distance at known points
    float dist_at_surface = sampleSDF(center + vec3(5, 0, 0));
    ASSERT_NEAR(dist_at_surface, 0.0f, 0.01f);
    
    float dist_outside = sampleSDF(center + vec3(10, 0, 0));
    ASSERT_NEAR(dist_outside, 5.0f, 0.1f); // Should be 5 units away
}
```

---

### 4.2 Integration Tests

**Test 1: Full Pipeline**
```cpp
TEST(FullPipeline) {
    // Setup scene
    demo.setScene(CORNELL_BOX);
    
    // Run one frame
    demo.voxelizationPass();
    demo.sdfGenerationPass();
    demo.injectDirectLighting();
    demo.updateRadianceCascades();
    demo.raymarchPass();
    
    // Verify no OpenGL errors
    ASSERT_NO_GL_ERRORS();
    
    // Check output image is not black
    Image output = readBackFramebuffer();
    ASSERT_GT(averageBrightness(output), 0.01f);
}
```

---

### 4.3 Visual Regression Tests

**Golden Images:**
- Render reference scenes and save as PNG
- Compare future renders pixel-by-pixel
- Allow small tolerance for floating-point variations

**Test Scenes:**
1. Single emissive sphere in empty room
2. Cornell box with colored walls
3. Multiple bouncing light scenario

---

## 5. Risk Mitigation

### 5.1 Technical Risks

| Risk | Probability | Impact | Mitigation Strategy |
|------|------------|--------|-------------------|
| **Memory overflow** | High | Critical | Start with 64³ volumes, implement sparse octree early |
| **Performance too slow** | High | High | Profile each pass, use adaptive techniques, reduce ray counts |
| **Artifacts in blending** | Medium | Medium | Extensive testing of weighted sampling, fallback to simple bilinear |
| **Temporal instability** | Medium | High | Conservative blend factors, motion vector validation |
| **Shader compilation failures** | Low | High | Validate GLSL syntax, provide fallback paths |

---

### 5.2 Schedule Risks

| Risk | Mitigation |
|------|-----------|
| **Phase takes longer than estimated** | Build working MVP first, add features incrementally |
| **Blocking bugs in OpenGL helpers** | Test helpers in isolation before integrating |
| **Algorithm doesn't converge** | Reference ShaderToy implementation for verification |
| **Hardware limitations** | Support multiple quality levels (low/medium/high) |

---

### 5.3 Fallback Strategies

**If Sparse Voxel Octree is too complex:**
- Use dense grids with lower resolution (64³ instead of 128³)
- Implement simple frustum culling instead

**If temporal reprojection causes artifacts:**
- Disable temporal filtering (slower but stable)
- Use spatial denoising instead (e.g., bilateral filter)

**If cascade merging fails:**
- Use single high-resolution cascade only
- Accept performance penalty for correctness

---

## 6. Reference Materials

### 6.1 Code References

**ShaderToy Implementation:**
- Location: `3d/shader_toy/`
- Key files:
  - `Common.glsl` - SDF primitives, ray tracing utilities
  - `CubeA.glsl` - Cascade merging logic, weighted sampling
  - `Image.glsl` - Main rendering entry point

**2D Implementation (for patterns):**
- Location: `src/demo.cpp`, `src/demo.h`
- Useful for: ImGui integration, resource management patterns

**OpenGL Helpers:**
- Location: `3d/include/gl_helpers.h`
- Status: Declarations complete, implementations needed

---

### 6.2 Academic Papers

**Primary References:**
1. **"Radiance Cascades"** by Alexander Sannikov
   - Location: `C:\Git-repo-my\GameDevVault\Rendering\Paper\GI\Radiance_Cascade`
   - Core algorithm description

2. **"Jump Flooding Algorithm"** 
   - Original JFA paper for distance field computation
   - Extended to 3D in this project

**Supplementary Reading:**
- "Sparse Voxel Octrees" - Laine & Karras
- "Real-Time Global Illumination" - various SIGGRAPH courses

---

### 6.3 Online Resources

**Documentation:**
- [OpenGL Wiki - Compute Shaders](https://www.khronos.org/opengl/wiki/Compute_Shader)
- [Learn OpenGL - Advanced Lighting](https://learnopengl.com/Advanced-Lighting/Light-Casting-Shadows)
- [Raylib Documentation](https://www.raylib.com/)

**Tutorials:**
- Vulkan/DX12 RC implementations on GitHub
- ShaderToy examples tagged "global illumination"

---

### 6.4 Tools

**Development:**
- **RenderDoc**: GPU frame capture and debugging
  - Config: `RenderDoc/rc.xml`
- **NSight Graphics**: NVIDIA GPU profiling
- **Intel GPA**: Intel GPU analysis

**Build System:**
- CMake 3.25+
- Compiler: GCC 11+, Clang 13+, or MSVC 2022+

---

## 7. Getting Started Checklist

### Immediate Actions (Day 1)

- [ ] Read this entire refactor plan
- [ ] Review ShaderToy reference code (`3d/shader_toy/`)
- [ ] Set up development environment (OpenGL 4.3+, CMake, compiler)
- [ ] Verify existing shaders compile without errors
- [ ] Create feature branch: `git checkout -b feature/3d-rc-implementation`

### Week 1 Tasks

- [ ] Implement `gl_helpers.cpp` (Milestone 1.1)
- [ ] Write unit tests for 3D texture operations
- [ ] Get basic OpenGL context working with compute shaders
- [ ] Commit working helper functions

### Week 2 Tasks

- [ ] Implement procedural geometry generation (Milestone 1.2)
- [ ] Get voxelization pass working
- [ ] Implement 3D JFA for SDF (Milestone 1.3)
- [ ] Verify SDF accuracy with test cases

### Milestone Reviews

At the end of each phase:
- [ ] Run all unit tests
- [ ] Performance profiling
- [ ] Code review (self or peer)
- [ ] Update documentation
- [ ] Tag release: `v0.1-phase1-complete`, etc.

---

## 8. Success Metrics

### Functional Requirements

- [x] Voxelizes arbitrary 3D geometry
- [x] Computes accurate signed distance fields
- [x] Generates radiance cascades with 5+ levels
- [x] Injects direct lighting with shadows
- [x] Computes indirect bounces via probe hierarchy
- [x] Renders final image via raymarching
- [x] Supports interactive camera navigation
- [x] Provides debug visualization tools

### Performance Targets

| Metric | Target | Stretch Goal |
|--------|--------|--------------|
| **Frame Rate (64³)** | > 60 FPS | > 120 FPS |
| **Frame Rate (128³)** | > 30 FPS | > 60 FPS |
| **Frame Rate (256³)** | > 15 FPS | > 30 FPS |
| **Memory Usage (128³)** | < 2 GB | < 1 GB |
| **Convergence Time** | < 10 frames | < 5 frames |
| **Load Time** | < 5 seconds | < 2 seconds |

### Quality Metrics

- No visible seams between cascade levels
- Stable temporal accumulation (no flickering)
- Accurate shadow boundaries
- Physically plausible light bounces
- Smooth camera movement without artifacts

---

## 9. Future Enhancements

### Post-v1.0 Features

1. **Advanced Materials**
   - Specular reflections
   - Roughness/glossiness maps
   - Normal mapping

2. **Participating Media**
   - Fog and volume scattering
   - God rays through gaps
   - Absorption and emission

3. **Dynamic Scenes**
   - Animated geometry
   - Moving light sources
   - Real-time voxel updates

4. **Machine Learning Denoising**
   - Train neural network on path-traced ground truth
   - Real-time inference for noise removal
   - Reduce required ray counts

5. **VR Support**
   - Stereoscopic rendering
   - Foveated rendering
   - Motion-to-photon latency optimization

---

## 10. Conclusion

This refactor plan provides a **comprehensive roadmap** from the current stub implementation to a fully functional 3D Radiance Cascades system. By following the phased approach:

1. **Phase 1** establishes the OpenGL foundation
2. **Phase 2** implements the core algorithm
3. **Phase 3** adds hierarchical cascades and temporal stability
4. **Phase 4** optimizes and polishes the experience

Each phase delivers a **working, testable milestone**, ensuring steady progress and early detection of issues.

### Key Success Factors

✅ **Start small**: Begin with 64³ volumes, scale up gradually  
✅ **Test frequently**: Unit tests catch bugs early  
✅ **Profile continuously**: Identify bottlenecks before they become critical  
✅ **Reference ShaderToy**: Verify algorithm correctness against known-good implementation  
✅ **Iterate rapidly**: Get something working, then improve it  

### Next Steps

1. **Review this plan** with stakeholders
2. **Prioritize phases** based on available resources
3. **Begin Phase 1** implementation immediately
4. **Schedule weekly check-ins** to track progress
5. **Update this document** as new insights emerge

---

## Appendix A: Glossary

**SDF (Signed Distance Field):** A scalar field where each point stores the distance to the nearest surface. Negative inside, positive outside.

**JFA (Jump Flooding Algorithm):** An efficient parallel algorithm for computing distance fields in O(log N) passes.

**Radiance Cascade:** A hierarchical grid of probes that store incoming light from all directions, enabling efficient global illumination.

**Probe:** A sample point in the cascade grid that stores radiance information (incoming light from multiple directions).

**Voxelization:** The process of converting continuous geometry into a discrete 3D grid of voxels.

**Raymarching:** A rendering technique that steps along rays through a volume, accumulating color and opacity.

**Temporal Reprojection:** Reusing data from previous frames to reduce noise and improve performance.

**SSBO (Shader Storage Buffer Object):** OpenGL buffer object for storing large amounts of data accessible by shaders.

---

## Appendix B: Common Pitfalls

### Pitfall 1: Coordinate Space Confusion

**Problem:** Mixing world space, voxel space, and normalized device coordinates.

**Solution:** Always comment which space a variable is in. Use naming conventions:
- `worldPos` - World space coordinates
- `voxelPos` - Integer voxel indices
- `uv` - Normalized [0,1] texture coordinates

### Pitfall 2: Memory Exhaustion

**Problem:** Allocating 512³ volume = 512MB per texture × multiple textures = out of memory.

**Solution:** 
- Start with 64³ or 128³
- Use half-float (16-bit) instead of full float (32-bit) where possible
- Implement sparse representation early

### Pitfall 3: Incorrect Cascade Intervals

**Problem:** Overlapping or gapped intervals between cascades cause artifacts.

**Solution:** 
- Cascade i interval: [base × 4^i, base × 4^(i+1))
- Ensure continuity: end of cascade i = start of cascade i+1
- Test with simple scenes first

### Pitfall 4: Temporal Ghosting

**Problem:** Fast camera movement leaves trails from previous frames.

**Solution:**
- Detect large camera motions and reset accumulation
- Clamp reprojection to valid screen bounds
- Use motion vectors for per-pixel reprojection

### Pitfall 5: Light Leaks

**Problem:** Light passes through thin walls due to insufficient ray sampling.

**Solution:**
- Increase ray count for fine cascades
- Use visibility-aware weighted sampling
- Add conservative bias to SDF (inflate obstacles slightly)

---

## Appendix C: Debugging Checklist

When encountering issues, systematically check:

### Visual Artifacts

- [ ] **Black output:** Check if shaders compile, verify texture bindings
- [ ] **Noise:** Increase ray count, enable temporal reprojection
- [ ] **Seams between cascades:** Verify interval calculations, check blending weights
- [ ] **Light leaks:** Increase SDF resolution, add occlusion checks
- [ ] **Banding:** Use higher precision formats (RGBA16F vs RGBA8)

### Performance Issues

- [ ] **Slow voxelization:** Reduce triangle count, optimize SSBO access
- [ ] **Slow SDF:** Verify JFA iteration count = log₂(N)
- [ ] **Slow raymarching:** Reduce ray count, enable early termination
- [ ] **GPU memory full:** Reduce volume resolution, use sparse structures

### Correctness Issues

- [ ] **Wrong normals:** Check SDF gradient calculation (central differences)
- [ ] **Incorrect shadows:** Verify ray origin offset from surface
- [ ] **Missing bounces:** Check cascade interval coverage
- [ ] **Temporal flicker:** Reduce blend factor, validate reprojection UVs

---

**Document End**

*For questions or updates to this plan, please create an issue in the project repository or contact the development team.*
