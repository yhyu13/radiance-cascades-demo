# Migration Guide: 2D to 3D Radiance Cascades

## Overview

This guide explains how to migrate the current 2D Radiance Cascades implementation to 3D. The migration involves fundamental changes to data structures, rendering pipeline, and shader architecture.

**Key Differences:**
- **2D**: Screen-space pixel-based lighting with cascades in UV space
- **3D**: Volume-based voxel lighting with cascades in world space

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Core Data Structure Changes](#core-data-structure-changes)
3. [Rendering Pipeline Modifications](#rendering-pipeline-modifications)
4. [Shader Migration](#shader-migration)
5. [Memory & Performance Considerations](#memory--performance-considerations)
6. [Implementation Roadmap](#implementation-roadmap)
7. [Code Examples](#code-examples)

---

## Architecture Overview

### Current 2D Pipeline
```
Scene (2D sprites) → Occlusion/Emission Maps → JFA (2D distance field) → 
Radiance Cascades (screen-space) → Final Composite
```

### Target 3D Pipeline
```
Scene (3D geometry) → Voxelization → 3D SDF Generation → 
3D Radiance Cascades (volume) → Raymarching → Final Composite
```

### Critical Design Decisions

**Option A: Screen-Space 3D (Easier)**
- Keep 2D texture buffers but render 3D scene
- Project 3D radiance onto screen space
- Similar to current architecture
- **Pros**: Minimal changes to cascade logic
- **Cons**: Limited to visible surfaces, no volumetric effects

**Option B: True Volumetric 3D (Recommended)**
- Use 3D volume textures (voxel grids)
- Store radiance in 3D space
- Full volumetric lighting
- **Pros**: Physically accurate, supports participating media
- **Cons**: Higher memory, requires sparse data structures

---

## Core Data Structure Changes

### 1. Texture Buffers → Volume Textures

**Current 2D (demo.h):**
```cpp
RenderTexture2D jfaBufferA;
RenderTexture2D distFieldBuf;
RenderTexture2D radianceBufferA;
```

**3D Migration:**
```cpp
// Option 1: OpenGL 3D textures (manual management)
GLuint jfaVolume;
GLuint sdfVolume;
GLuint radianceVolume;

// Option 2: Sparse Voxel Octree (memory efficient)
struct VoxelNode {
    float density;
    vec4 radiance;
    int children[8]; // child node indices
};

class SparseVoxelOctree {
    std::vector<VoxelNode> nodes;
    glm::vec3 rootPos;
    float rootSize;
};
```

### 2. Scene Representation

**Current 2D:**
- 2D sprites drawn to RenderTexture2D
- Distance field via JFA on 2D texture

**3D Requirements:**
```cpp
// Voxelization buffer
RenderTexture3D voxelGrid; // GL_TEXTURE_3D

// Signed Distance Field (3D)
Texture3D sdfVolume;

// For raylib (limited 3D support):
typedef struct {
    unsigned int id;
    int width, height, depth;
    PixelFormat format;
} RenderTexture3D;
```

### 3. Camera System

**Add to demo.h:**
```cpp
#include "camera.h"

class Demo {
    Camera3D camera;
    // ... existing members
    
    void updateCamera();
    void render3DScene();
};
```

---

## Rendering Pipeline Modifications

### Phase 1: Voxelization

Replace 2D sprite drawing with 3D voxelization:

```cpp
void Demo::voxelizeScene() {
    // Bind 3D framebuffer
    glBindTexture(GL_TEXTURE_3D, voxelGrid);
    glFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                           GL_TEXTURE_3D, voxelGrid, 0, 0);
    
    // Render geometry from multiple views (6 faces for cubic voxels)
    for (int face = 0; face < 6; face++) {
        setupVoxelizationCamera(face);
        renderGeometryAsVoxels();
    }
}
```

### Phase 2: 3D Distance Field Generation

**Replace JFA with 3D SDF:**

```glsl
// 3D SDF computation shader (compute shader recommended)
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

uniform sampler3D uVoxelGrid;
uniform writeonly image3D oSDF;

void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID);
    
    // Jump flood in 3D
    vec3 closestSurface = findClosestSurface3D(pos);
    float distance = length(vec3(pos) - closestSurface);
    
    imageStore(oSDF, pos, vec4(distance));
}
```

### Phase 3: 3D Radiance Cascades

**Key Concept:** Instead of screen-space cascades, use **spatial cascades** in world space.

```glsl
// 3D Cascade structure
struct Cascade3D {
    int resolution;      // e.g., 64³, 128³, 256³
    float cellSize;      // world units per voxel
    vec3 origin;         // world space origin
    int rayCount;        // rays per voxel
};

// Hierarchical cascade system
Cascade3D cascades[MAX_CASCADES] = {
    {64,   0.1,  vec3(0), 4},   // Fine cascade (near camera)
    {128,  0.5,  vec3(0), 4},   // Medium cascade
    {256,  2.0,  vec3(0), 4},   // Coarse cascade (far field)
};
```

### Phase 4: Volume Raymarching

```glsl
// Final composite - raymarch through volume
vec4 raymarchVolume(vec3 rayOrigin, vec3 rayDir, float tNear, float tFar) {
    vec4 accumulatedColor = vec4(0.0);
    float t = tNear;
    
    while (t < tFar) {
        vec3 pos = rayOrigin + rayDir * t;
        
        // Sample 3D radiance volume
        vec4 radiance = texture(uRadianceVolume, pos);
        
        // Accumulate (front-to-back compositing)
        float alpha = radiance.a;
        accumulatedColor.rgb += alpha * radiance.rgb * (1.0 - accumulatedColor.a);
        accumulatedColor.a += alpha * (1.0 - accumulatedColor.a);
        
        if (accumulatedColor.a >= 0.95) break;
        
        t += stepSize;
    }
    
    return accumulatedColor;
}
```

---

## Shader Migration

### Fragment Shader → Compute Shader

Most 2D fragment shaders need to become 3D compute shaders:

**2D Prep Scene (prepscene.frag):**
```glsl
// 2D: Process single pixel
void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    vec4 occlusion = texture(uOcclusionMap, uv);
    vec4 emission = texture(uEmissionMap, uv);
    fragColor = combine(occlusion, emission);
}
```

**3D Prep Scene (prepscene.comp):**
```glsl
// 3D: Process voxel in volume
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID);
    vec3 coord = vec3(pos) / vec3(volumeSize);
    
    vec4 voxelData = texture(uVoxelGrid, coord);
    imageStore(oProcessedVolume, pos, processVoxel(voxelData));
}
```

### Required Shader Conversions

| 2D Shader | 3D Equivalent | Type Change | Location |
|-----------|---------------|-------------|----------|
| `draw.frag` | `voxelize.comp` | Fragment → Compute | `3d/res/shaders/` |
| `jfa.frag` | `sdf_3d.comp` | Fragment → Compute | `3d/res/shaders/` |
| `rc.frag` | `radiance_3d.comp` | Fragment → Compute | `3d/res/shaders/` |
| `gi.frag` | `gi_volume.comp` | Fragment → Compute | (planned) |
| `final.frag` | `raymarch.frag` or keep as Fragment | Fragment | `3d/res/shaders/` |

**Note**: All 3D shader files are now located in the `3d/res/shaders/` directory instead of `res/shaders/`. This separation keeps 2D and 3D resources organized.

---

## Memory & Performance Considerations

### Memory Requirements

**2D Current (1920×1080):**
- Single texture: ~8 MB (RGBA32F)
- Total buffers: ~80 MB

**3D Target (512³ volume):**
- Single volume: ~2 GB (RGBA32F)
- **Problem**: 256× more memory!

### Optimization Strategies

**1. Sparse Voxel Representation**
```cpp
// Only allocate voxels near surfaces
class SparseVoxelGrid {
    std::unordered_map<uint64_t, VoxelData> activeVoxels;
    
    uint64_t hashPosition(ivec3 pos) {
        // Morton encoding / Z-order curve
        return mortonEncode(pos.x, pos.y, pos.z);
    }
};
```

**2. Adaptive Resolution**
```cpp
// Use lower resolution far from camera
float getVoxelResolution(vec3 worldPos) {
    float dist = distance(cameraPos, worldPos);
    return baseRes * (1.0 + dist * lodFactor);
}
```

**3. Temporal Reprojection**
```cpp
// Reuse previous frame's radiance
vec4 temporalAccumulation(vec3 pos) {
    vec4 prev = texture(uPrevFrameRadiance, reprojectionUV);
    vec4 curr = texture(uCurrentRadiance, uv);
    return mix(prev, curr, 0.1); // 10% new data
}
```

### Performance Targets

| Resolution | Voxel Count | Target FPS | Memory |
|------------|-------------|------------|---------|
| 128³ | 2.1M | 60+ | 128 MB |
| 256³ | 16.8M | 30-60 | 1 GB |
| 512³ | 134M | 15-30 | 8 GB |

**Recommendation**: Start with 128³ for prototyping.

---

## Implementation Roadmap

### Phase 1: Foundation (Week 1-2)

**Step 1.1: Setup 3D Camera**
```cpp
// In demo.cpp constructor
camera.position = {0.0f, 0.0f, -5.0f};
camera.target = {0.0f, 0.0f, 0.0f};
camera.up = {0.0f, 1.0f, 0.0f};
camera.fovy = 45.0f;
camera.projection = CAMERA_PERSPECTIVE;
```

**Step 1.2: Create 3D Texture Helpers**
```cpp
RenderTexture3D LoadRenderTexture3D(int width, int height, int depth) {
    RenderTexture3D target = {0};
    
    glGenTextures(1, &target.id);
    glBindTexture(GL_TEXTURE_3D, target.id);
    
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, 
                 width, height, depth, 0, 
                 GL_RGBA, GL_FLOAT, NULL);
    
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    target.width = width;
    target.height = height;
    target.depth = depth;
    
    return target;
}
```

### Phase 2: Voxelization (Week 3-4)

**Step 2.1: Convert 2D Sprites to 3D Geometry**
- Replace `DrawRectangle()` with `DrawCube()` or custom meshes
- Load 3D models instead of 2D textures

**Step 2.2: Implement Voxelization Shader**
```glsl
// voxelization.vert
#version 330 core
in vec3 vertex;
out flat ivec3 vVoxelPos;

void main() {
    // Snap vertex to voxel grid
    vec3 worldPos = (model * vec4(vertex, 1.0)).xyz;
    ivec3 voxel = ivec3((worldPos - gridOrigin) / voxelSize);
    vVoxelPos = voxel;
    
    gl_Position = projection * view * vec4(worldPos, 1.0);
}
```

### Phase 3: 3D Distance Field (Week 5-6)

**Step 3.1: Implement 3D JFA**
```glsl
// jfa_3d.comp
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

uniform int uStepSize;
uniform sampler3D uInput;
uniform writeonly image3D uOutput;

void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID);
    
    // Find minimum in 3x3x3 neighborhood
    float minDist = texture(uInput, pos).r;
    ivec3 closestPos = pos;
    
    for (int dx = -uStepSize; dx <= uStepSize; dx += uStepSize)
    for (int dy = -uStepSize; dy <= uStepSize; dy += uStepSize)
    for (int dz = -uStepSize; dz <= uStepSize; dz += uStepSize) {
        ivec3 samplePos = pos + ivec3(dx, dy, dz);
        float dist = texture(uInput, samplePos).r;
        if (dist < minDist) {
            minDist = dist;
            closestPos = samplePos;
        }
    }
    
    imageStore(uOutput, pos, vec4(minDist));
}
```

### Phase 4: 3D Radiance Cascades (Week 7-10)

**Step 4.1: Define Cascade Structure**
```cpp
struct RadianceCascade3D {
    GLuint probeGrid; // 3D texture storing probe data
    int resolution;   // e.g., 32, 64, 128
    float cellSize;   // world units
    int raysPerProbe;
};

std::vector<RadianceCascade3D> cascades;

void initCascades() {
    for (int i = 0; i < cascadeCount; i++) {
        RadianceCascade3D cascade;
        cascade.resolution = baseRes * pow(2, i);
        cascade.cellSize = baseCellSize * pow(2, i);
        cascade.raysPerProbe = baseRays;
        cascades.push_back(cascade);
    }
}
```

**Step 4.2: Implement Cascade Injection**
```glsl
// inject_radiance.comp
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main() {
    ivec3 probePos = ivec3(gl_GlobalInvocationID);
    vec3 worldPos = getProbeWorldPos(probePos);
    
    // Cast rays from probe
    for (int i = 0; i < uRayCount; i++) {
        vec3 rayDir = getRayDirection(i);
        vec4 hitInfo = traceRay(worldPos, rayDir);
        
        // Accumulate radiance
        atomicAdd(radianceSum, hitInfo.color * hitInfo.visibility);
    }
}
```

### Phase 5: Integration (Week 11-12)

**Step 5.1: Update Main Loop**
```cpp
while (!WindowShouldClose()) {
    updateCamera();
    
    // 1. Voxelize dynamic geometry
    voxelizeScene();
    
    // 2. Update SDF
    updateSDF3D();
    
    // 3. Update radiance cascades
    for (auto& cascade : cascades)
        updateRadianceCascade(cascade);
    
    // 4. Raymarch final image
    BeginDrawing();
        BeginMode3D(camera);
            renderRaymarchedScene();
        EndMode3D();
        
        rlImGuiBegin();
            renderUI();
        rlImGuiEnd();
    EndDrawing();
}
```

---

## Code Examples

### Complete 3D Buffer Setup

```cpp
// demo.h
struct VolumeBuffer {
    GLuint texture;
    int width, height, depth;
};

class Demo {
    VolumeBuffer voxelGrid;
    VolumeBuffer sdfVolume;
    VolumeBuffer radianceVolumes[MAX_CASCADES];
    VolumeBuffer directLighting;
    
    void createVolumeBuffer(VolumeBuffer& buf, int w, int h, int d, GLenum internalFormat) {
        buf.width = w;
        buf.height = h;
        buf.depth = d;
        
        glGenTextures(1, &buf.texture);
        glBindTexture(GL_TEXTURE_3D, buf.texture);
        
        glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, w, h, d, 0, 
                     GL_RGBA, GL_FLOAT, NULL);
        
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
};

// demo.cpp constructor
Demo::Demo() {
    // Create 3D buffers (start small: 128³)
    int volSize = 128;
    createVolumeBuffer(voxelGrid, volSize, volSize, volSize, GL_RGBA8);
    createVolumeBuffer(sdfVolume, volSize, volSize, volSize, GL_R32F);
    createVolumeBuffer(directLighting, volSize, volSize, volSize, GL_RGBA16F);
    
    for (int i = 0; i < cascadeAmount; i++) {
        createVolumeBuffer(radianceVolumes[i], volSize, volSize, volSize, GL_RGBA16F);
    }
}
```

### 3D Framebuffer Binding

```cpp
void bindVolumeFramebuffer(VolumeBuffer& volume) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_3D, volume.texture, 0, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("Framebuffer incomplete!\n");
    }
    
    // Set viewport to volume dimensions
    glViewport(0, 0, volume.width, volume.height);
}
```

### Compute Shader Dispatch

```cpp
void dispatchComputeShader(Shader& computeShader, int dimX, int dimY, int dimZ) {
    computeShader.use();
    
    // Bind 3D textures
    GLint loc = GetShaderLocation(computeShader, "uVolume");
    glUniform1i(loc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, voxelGrid.texture);
    
    // Dispatch
    glDispatchCompute(dimX/8, dimY/8, dimZ/8);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
```

---

## Testing Strategy

### Incremental Validation

1. **Test 3D Camera**: Verify navigation in 3D space
2. **Test Voxelization**: Render simple cube, check voxel grid
3. **Test SDF**: Visualize distance field slices
4. **Test Single Cascade**: One low-res cascade (32³)
5. **Test Multiple Cascades**: Add cascades incrementally
6. **Full Integration**: All systems together

### Debug Visualization

```cpp
void renderDebugSlices() {
    // Render XY, XZ, YZ slices through volume
    for (int z = 0; z < volumeDepth; z += 16) {
        drawVolumeSliceXY(z);
    }
}
```

---

## Common Pitfalls

### 1. Memory Overrun
**Problem**: 512³ volume = 134M voxels × 16 bytes = 2.1 GB

**Solution**: 
- Start with 64³ or 128³
- Use sparse representation
- Implement frustum culling

### 2. Performance Bottleneck
**Problem**: 3D raymarching is expensive

**Solution**:
- Use early ray termination
- Implement adaptive step size
- Leverage temporal reprojection

### 3. Cache Coherency
**Problem**: 3D texture access patterns hurt cache

**Solution**:
- Use Morton ordering (Z-order curve)
- Optimize data layout for spatial locality

---

## Resources & References

### Key Papers
- **"Real-Time Global Illumination using Radiance Cascades"** (Alexander Sannikov)
- **"Sparse Voxel Octree Global Illumination"** (McGuire et al.)
- **"Voxel Cone Tracing"** (Crassin et al.)

### Tools
- **RenderDoc**: Debug 3D graphics pipelines
- **NVIDIA Nsight**: GPU performance profiling
- **Vulkan/SPIRV-Cross**: Modern compute shader development

### Libraries
- **OpenGL**: Direct 3D texture/compute shader access
- **Vulkan**: Low-level GPU control
- **OpenVDB**: Sparse volume data structure

---

## Conclusion

Migrating from 2D to 3D Radiance Cascades is a significant undertaking requiring:

1. **Architecture Redesign**: 2D textures → 3D volumes
2. **New Algorithms**: JFA → 3D SDF, screen-space cascades → spatial cascades
3. **Performance Optimization**: Sparse data structures, LOD, temporal techniques
4. **Modern GPU Features**: Compute shaders, atomic operations, image load/store

**Estimated Timeline**: 3-6 months for production-ready implementation

**Start Simple**: Begin with 64³ volume, single cascade, basic voxelization. Iterate and scale up.

Good luck! 🚀
