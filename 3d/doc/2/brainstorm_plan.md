# 3D Radiance Cascades - Brainstorm Plan & New Directions

**Document Version:** 1.0  
**Date:** 2026-04-18  
**Status:** Planning Phase  
**Author:** AI Assistant  

---

## Executive Summary

This document explores **new directions and optimization strategies** for achieving a working 3D radiance cascade implementation by migrating proven algorithms from the ShaderToy reference and insights from the Radiance Cascades research paper. The goal is to identify practical shortcuts, alternative approaches, and hybrid strategies that can accelerate development while maintaining visual quality.

### Key Insights from Analysis:

1. **ShaderToy uses cubemap storage** instead of 3D textures - this is more memory-efficient for directional data
2. **Analytic SDF vs Voxel SDF** - ShaderToy uses implicit surfaces, we use explicit voxelization (different tradeoffs)
3. **Flatland visibility assumption** simplifies probe-to-probe occlusion testing
4. **Cubemap face layout** encodes both geometry and cascades in single texture

---

## Table of Contents

1. [ShaderToy Migration Strategy](#1-shadertoy-migration-strategy)
2. [Paper-Based Optimizations](#2-paper-based-optimizations)
3. [Alternative Approaches](#3-alternative-approaches)
4. [Hybrid Strategies](#4-hybrid-strategies)
5. [Implementation Roadmap](#5-implementation-roadmap)
6. [Risk Assessment](#6-risk-assessment)

---

## 1. ShaderToy Migration Strategy

### 1.1 Cubemap Storage vs 3D Textures

**Current Approach:** Using 3D textures (`GL_TEXTURE_3D`) for probe grids  
**ShaderToy Approach:** Using cubemaps with clever UV layout

#### Advantages of Cubemap Approach:

```glsl
// From shader_toy/Image.glsl - Cubemap sampling
vec4 TextureCube(vec2 uv) {
    float tcSign = -mod(floor(uv.y*I1024), 2.)*2. + 1.;
    vec3 tcD = vec3(vec2(uv.x, mod(uv.y, 1024.))*I512 - 1., tcSign);
    if (uv.y > 4096.) tcD = tcD.xzy;
    else if (uv.y > 2048.) tcD = tcD.zxy;
    return textureLod(iChannel3, tcD, 0.);
}
```

**Benefits:**
- ✅ Hardware-accelerated trilinear filtering
- ✅ Seamless cube face transitions
- ✅ Built-in mipmapping for LOD
- ✅ Directional data naturally fits cubemap structure
- ✅ Memory efficient (no wasted voxels in empty space)

**Drawbacks:**
- ❌ Complex UV mapping logic
- ❌ Limited to cubic topology
- ❌ Harder to debug than 3D textures

#### Recommendation: **HYBRID APPROACH**

Use **3D textures for probe positions** (easier to work with) but store **directional radiance in cubemaps** per probe. This gives us:
- Simple probe grid management (3D texture)
- Efficient directional storage (cubemap)
- Best of both worlds

**Migration Task:** Create `ProbeCubemap` class that manages per-probe cubemaps.

---

### 1.2 Probe Ray Distribution Algorithm

**ShaderToy Implementation** (Image.glsl lines 120-150):

```glsl
// Probe ray distribution calculation
float probeCascade = floor(mod(UV.y, 1536.)/256.);
float probeSize = pow(2., probeCascade + 1.);
vec2 probePositions = gRes/probeSize;
vec3 probePos = gPos + mod(modUV.x, probePositions.x)*probeSize/256.*gTan +
                       mod(modUV.y, probePositions.y)*probeSize/256.*gBit;

// Theta/phi mapping for ray directions
vec2 probeRel = probeUV - probeSize*0.5;
float probeThetai = max(abs(probeRel.x), abs(probeRel.y));
float probeTheta = probeThetai/probeSize*3.14192653;
float probePhi = /* complex piecewise function */;
vec3 probeDir = vec3(vec2(sin(probePhi), cos(probePhi))*sin(probeTheta), cos(probeTheta));
probeDir = probeDir.x*gTan + probeDir.y*gBit + probeDir.z*gNor;
```

**Key Insight:** Uses **UV-space tiling** to encode ray directions, avoiding expensive spherical coordinate calculations per ray.

#### Migration Strategy:

**Option A: Direct Port** (Quick but inflexible)
- Copy theta/phi mapping logic directly
- Works only for specific probe counts (powers of 2)
- Fastest to implement

**Option B: Fibonacci Sphere** (More flexible)
- Generate uniform ray distributions on sphere
- Works for any ray count
- Slightly slower but more general

**Option C: Hammersley Sequence** (Best quality)
- Low-discrepancy sequence for better convergence
- Optimal for Monte Carlo integration
- Recommended for production

**Recommendation:** Start with **Option A** for quick prototype, then migrate to **Option C** for final implementation.

**Code Example - Fibonacci Sphere (for reference):**

```cpp
std::vector<glm::vec3> generateFibonacciSphere(int numRays) {
    std::vector<glm::vec3> directions;
    directions.reserve(numRays);
    
    float goldenRatio = (1.0f + std::sqrt(5.0f)) / 2.0f;
    float angleIncrement = 2.0f * glm::pi<float>() * goldenRatio;
    
    for (int i = 0; i < numRays; ++i) {
        float t = (float)i / (float)numRays;
        float inclination = std::acos(1.0f - 2.0f * t);
        float azimuth = angleIncrement * i;
        
        float x = std::sin(inclination) * std::cos(azimuth);
        float y = std::sin(inclination) * std::sin(azimuth);
        float z = std::cos(inclination);
        
        directions.emplace_back(x, y, z);
    }
    
    return directions;
}
```

---

### 1.3 Weighted Visibility Sampling

**ShaderToy's Flatland Assumption** (Image.glsl lines 30-55):

```glsl
vec4 WeightedSample(vec2 luvo, vec2 luvd, vec2 luvp, vec2 uvo, vec3 probePos,
                    vec3 gTan, vec3 gBit, vec3 gPos, float lProbeSize) {
    // Calculate relative position between probes
    vec3 lastProbePos = gPos + gTan*(luvp.x*lProbeSize/256.) + gBit*(luvp.y*lProbeSize/256.);
    vec3 relVec = probePos - lastProbePos;
    
    // Approximate visibility using angular test
    float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*3.141592653*0.5;
    float phi = atan(-dot(relVec, gTan), -dot(relVec, gBit));
    
    // Check if ray distance allows visibility
    float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;
    if (lProbeRayDist < -0.5 || length(relVec) < lProbeRayDist*cos(theta) + 0.01) {
        // Visible - accumulate radiance
        return vec4(/* sample and accumulate */, 1.);
    }
    return vec4(0.); // Occluded
}
```

**Key Innovation:** Instead of full ray tracing between probes, uses **angular visibility cone** approximation.

#### Why This Matters:

- **Full ray tracing:** O(N²) probe pairs × raymarching cost = very slow
- **Angular approximation:** O(1) per probe pair = fast
- **Trade-off:** Less accurate but good enough for diffuse GI

#### Migration Plan:

**Step 1:** Implement simplified version without angular test
```cpp
bool isProbeVisible(const Probe& from, const Probe& to, const SDFVolume& sdf) {
    // Simple ray cast between probe centers
    glm::vec3 direction = glm::normalize(to.position - from.position);
    float distance = glm::length(to.position - from.position);
    
    // Raymarch through SDF
    float t = 0.0f;
    while (t < distance) {
        glm::vec3 pos = from.position + direction * t;
        float sdfValue = sdf.sample(pos);
        
        if (sdfValue < 0.0f) return false; // Hit surface
        t += sdfValue; // Advance by SDF distance
    }
    
    return true;
}
```

**Step 2:** Add angular approximation for performance
```cpp
bool isProbeVisibleFast(const Probe& from, const Probe& to, float probeRadius) {
    glm::vec3 relVec = to.position - from.position;
    float distance = glm::length(relVec);
    
    // Angular visibility test
    float cosTheta = probeRadius / (2.0f * distance);
    float angleThreshold = std::acos(cosTheta);
    
    // Simplified: assume visible if within cone
    return distance < probeRadius * 2.0f; // Placeholder
}
```

**Step 3:** Hybrid approach based on distance
```cpp
float sampleProbeContribution(const Probe& from, const Probe& to, 
                              const SDFVolume& sdf, float maxDistance) {
    float distance = glm::length(to.position - from.position);
    
    if (distance > maxDistance) return 0.0f; // Too far
    
    if (distance < from.cellSize * 2.0f) {
        // Close probes: use fast angular test
        return isProbeVisibleFast(from, to, from.cellSize) ? 1.0f : 0.0f;
    } else {
        // Distant probes: full raymarch for accuracy
        return isProbeVisible(from, to, sdf) ? 1.0f : 0.0f;
    }
}
```

---

### 1.4 Cascade Merging Strategy

**ShaderToy Approach** (from Image.glsl analysis):

The ShaderToy implementation stores multiple cascade levels in different regions of the cubemap:
- **Y < 256×6:** Base geometry faces (6 faces × 256 resolution)
- **Y > 256×6:** Cascade levels at different resolutions (128, 64, 32...)

**Merging Logic:**
```glsl
// Pseudocode based on ShaderToy pattern
vec3 sampleRadiance(vec3 position, vec3 direction, float detailLevel) {
    // Determine which cascade level to use
    int cascadeIndex = calculateCascadeIndex(detailLevel);
    
    // Sample from appropriate cascade
    vec2 cascadeUV = mapToCascadeUV(position, direction, cascadeIndex);
    vec4 radiance = TextureCube(cascadeUV);
    
    // Blend with adjacent cascade for smooth transitions
    if (nearCascadeBoundary(detailLevel)) {
        vec4 nextCascade = TextureCube(cascadeUV, nextCascadeIndex);
        float blendFactor = calculateBlendFactor(detailLevel);
        radiance = mix(radiance, nextCascade, blendFactor);
    }
    
    return radiance.xyz;
}
```

#### Our Implementation Strategy:

**Current Code Structure** (from demo3d.h):
```cpp
struct RadianceCascade3D {
    GLuint probeGridTexture;  // 3D texture storing radiance
    int resolution;            // e.g., 128, 64, 32
    float cellSize;            // World space size per voxel
    float intervalStart;       // Min ray distance
    float intervalEnd;         // Max ray distance
};
```

**Proposed Enhancement:**
```cpp
struct RadianceCascade3D {
    GLuint probeGridTexture;   // 3D texture for probe positions
    GLuint directionCubemap;   // NEW: Cubemap for directional radiance
    int resolution;
    float cellSize;
    float intervalStart;
    float intervalEnd;
    
    // NEW: Blending parameters
    float blendStart;          // Where to start blending with next cascade
    float blendEnd;            // Where to finish blending
};
```

**Cascade Blending Implementation:**
```cpp
glm::vec3 Demo3D::sampleCascades(const glm::vec3& position, 
                                  const glm::vec3& direction,
                                  float rayDistance) {
    glm::vec3 accumulatedRadiance(0.0f);
    float totalWeight = 0.0f;
    
    for (int i = 0; i < cascadeCount; ++i) {
        const auto& cascade = cascades[i];
        
        // Check if ray distance falls within this cascade's range
        if (rayDistance >= cascade.intervalStart && 
            rayDistance <= cascade.intervalEnd) {
            
            // Sample from this cascade
            glm::vec3 radiance = sampleCascade(cascade, position, direction);
            
            // Calculate blend weight based on distance within range
            float rangeSize = cascade.intervalEnd - cascade.intervalStart;
            float normalizedDist = (rayDistance - cascade.intervalStart) / rangeSize;
            
            // Smoothstep for seamless blending
            float weight = smoothstep(0.0f, 1.0f, normalizedDist);
            
            accumulatedRadiance += radiance * weight;
            totalWeight += weight;
        }
    }
    
    // Normalize by total weight
    if (totalWeight > 0.0f) {
        accumulatedRadiance /= totalWeight;
    }
    
    return accumulatedRadiance;
}
```

---

## 2. Paper-Based Optimizations

Based on the Radiance Cascades paper (`RadianceCascades.pdf`), here are key optimizations to implement:

### 2.1 Sparse Voxel Octree (SVO)

**Problem:** Dense 3D textures waste memory on empty space  
**Solution:** Use octree to store only occupied voxels

**Memory Savings:**
- Dense 128³ grid: 128³ × 4 bytes = **8 MB** per cascade
- Sparse representation: ~10-20% occupancy = **0.8-1.6 MB**

**Implementation Approach:**

```cpp
struct OctreeNode {
    bool isLeaf;
    uint8_t childMask;  // Bitmask indicating which children exist
    union {
        struct {
            glm::vec4 radiance;  // Leaf node data
        };
        struct {
            int children[8];     // Internal node: indices to children
        };
    };
};

class SparseVoxelOctree {
private:
    std::vector<OctreeNode> nodes;
    int rootNode;
    
public:
    // GPU-friendly representation
    struct GPUNode {
        uint32_t flags;      // isLeaf + childMask packed
        union {
            glm::vec4 data;  // For leaf nodes
            uint32_t childOffsets[8];  // For internal nodes
        };
    };
    
    void uploadToSSBO(GLuint ssbo);
};
```

**Shader Adaptation:**
```glsl
// radiance_3d.comp - Modified for SVO
layout(std430, binding = 0) buffer OctreeBuffer {
    GPUNode nodes[];
};

vec4 traverseOctree(vec3 position, float minSize) {
    int nodeIdx = 0;  // Start at root
    vec3 nodeMin = vec3(0.0);
    vec3 nodeMax = vec3(1.0);
    
    while (true) {
        GPUNode node = nodes[nodeIdx];
        
        if (node.flags & IS_LEAF_FLAG) {
            return node.data;  // Found leaf
        }
        
        // Determine which child contains position
        vec3 mid = (nodeMin + nodeMax) * 0.5;
        int childIdx = 0;
        if (position.x > mid.x) childIdx |= 1;
        if (position.y > mid.y) childIdx |= 2;
        if (position.z > mid.z) childIdx |= 4;
        
        if ((node.flags & (1 << childIdx)) == 0) {
            return vec4(0.0);  // Child doesn't exist (empty space)
        }
        
        // Navigate to child
        nodeIdx = int(node.childOffsets[childIdx]);
        if (childIdx & 1) nodeMin.x = mid.x; else nodeMax.x = mid.x;
        if (childIdx & 2) nodeMin.y = mid.y; else nodeMax.y = mid.y;
        if (childIdx & 4) nodeMin.z = mid.z; else nodeMax.z = mid.z;
    }
}
```

**Priority:** Medium-term optimization (after basic version works)

---

### 2.2 Temporal Reprojection

**Concept:** Reuse radiance from previous frame, reprojected to current camera position

**Benefits:**
- Reduces noise from stochastic sampling
- Allows fewer rays per probe (performance boost)
- Smoother temporal coherence

**Implementation:**

```cpp
class TemporalAccumulator {
private:
    GLuint previousRadianceTexture;  // Last frame's radiance
    GLuint currentRadianceTexture;   // This frame's radiance
    GLuint reprojectedTexture;       // After reprojection
    glm::mat4 previousViewProjMatrix;
    glm::mat4 currentViewProjMatrix;
    
public:
    void reprojectAndAccumulate() {
        // 1. Calculate velocity buffer (pixel motion vectors)
        computeVelocityBuffer();
        
        // 2. Reproject previous frame's radiance
        glUseProgram(reprojectionShader);
        glBindTextureUnit(0, previousRadianceTexture);
        glBindTextureUnit(1, velocityBuffer);
        glBindImageTexture(0, reprojectedTexture, 0, GL_FALSE, 0, 
                          GL_WRITE_ONLY, GL_RGBA16F);
        dispatchCompute();
        
        // 3. Blend with current frame
        glUseProgram(blendShader);
        glBindTextureUnit(0, reprojectedTexture);
        glBindTextureUnit(1, currentRadianceTexture);
        glBindImageTexture(0, finalRadianceTexture, 0, GL_FALSE, 0,
                          GL_WRITE_ONLY, GL_RGBA16F);
        glUniform1f(blendFactorLoc, 0.9f);  // 90% previous, 10% new
        dispatchCompute();
        
        // 4. Swap buffers for next frame
        std::swap(previousRadianceTexture, currentRadianceTexture);
    }
};
```

**Reprojection Shader** (reproject.comp):
```glsl
#version 430
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(binding = 0) uniform sampler3D previousRadiance;
layout(binding = 1) uniform sampler3D velocityBuffer;
layout(rgba16f, binding = 0) uniform image3D outputRadiance;

uniform mat4 inverseCurrentViewProj;
uniform mat4 previousViewProj;

void main() {
    ivec3 texelCoord = ivec3(gl_GlobalInvocationID);
    vec3 uvw = (vec3(texelCoord) + 0.5) / textureSize(previousRadiance, 0);
    
    // Get velocity vector from previous position
    vec3 velocity = texelFetch(velocityBuffer, texelCoord, 0).xyz;
    vec3 previousUVW = uvw - velocity;
    
    // Sample previous frame's radiance
    vec4 previousRadiance = texture(previousRadiance, previousUVW);
    
    // Store reprojected radiance
    imageStore(outputRadiance, texelCoord, previousRadiance);
}
```

**Priority:** High (significant quality improvement with moderate effort)

---

### 2.3 Adaptive Ray Counting

**Concept:** Use more rays for nearby probes, fewer for distant ones

**Implementation:**
```cpp
int calculateRayCount(float distance, float probeCellSize) {
    // Base ray count
    int baseRays = 16;
    
    // Increase rays for close probes (more detail needed)
    float distanceRatio = distance / probeCellSize;
    
    if (distanceRatio < 2.0f) {
        return baseRays * 4;  // 64 rays for very close
    } else if (distanceRatio < 5.0f) {
        return baseRays * 2;  // 32 rays for medium distance
    } else {
        return baseRays;      // 16 rays for far probes
    }
}
```

**Shader Implementation:**
```glsl
// In radiance_3d.comp
int getRayCountForProbe(uint cascadeIndex, vec3 probePosition) {
    float cellSize = cascadeData[cascadeIndex].cellSize;
    vec3 cameraPos = uniforms.cameraPosition;
    float distance = length(probePosition - cameraPos);
    
    if (distance < cellSize * 2.0) return 64;
    if (distance < cellSize * 5.0) return 32;
    return 16;
}
```

**Priority:** Low (nice-to-have optimization)

---

## 3. Alternative Approaches

### 3.1 Analytic SDF for Primitives (Quick Start)

**Problem:** 3D JFA is complex and time-consuming to implement  
**Solution:** Use analytic SDF for simple test shapes initially

**Advantages:**
- ✅ Immediate visual feedback
- ✅ No voxelization needed
- ✅ Perfect SDF (no discretization artifacts)
- ✅ Easy to debug

**Disadvantages:**
- ❌ Only works for primitives (boxes, spheres, etc.)
- ❌ Can't load arbitrary meshes
- ❌ Not scalable to complex scenes

**Implementation:**

```cpp
class AnalyticSDF {
public:
    enum PrimitiveType { BOX, SPHERE, CYLINDER, TORUS };
    
    struct Primitive {
        PrimitiveType type;
        glm::vec3 position;
        glm::vec3 scale;
        glm::quat rotation;
    };
    
    float evaluate(const glm::vec3& point) const {
        float minDistance = FLT_MAX;
        
        for (const auto& prim : primitives) {
            // Transform point to primitive's local space
            glm::vec3 localPoint = rotate(point - prim.position, inverse(prim.rotation));
            
            float dist;
            switch (prim.type) {
                case BOX:
                    dist = sdfBox(localPoint, prim.scale);
                    break;
                case SPHERE:
                    dist = sdfSphere(localPoint, prim.scale.x);
                    break;
                // ... other primitives
            }
            
            minDistance = std::min(minDistance, dist);
        }
        
        return minDistance;
    }
    
private:
    float sdfBox(const glm::vec3& p, const glm::vec3& b) const {
        glm::vec3 d = glm::abs(p) - b * 0.5f;
        return glm::length(glm::max(d, 0.0f)) + 
               std::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
    }
    
    float sdfSphere(const glm::vec3& p, float radius) const {
        return glm::length(p) - radius;
    }
    
    std::vector<Primitive> primitives;
};
```

**GPU Version** (sdf_analytic.comp):
```glsl
#version 430
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba32f, binding = 0) uniform image3D sdfVolume;

uniform vec3 volumeOrigin;
uniform vec3 volumeSize;
uniform vec3 voxelSize;

// Primitive definitions
struct Primitive {
    int type;
    vec3 position;
    vec3 scale;
    vec4 rotation;  // Quaternion
};

layout(std430, binding = 0) buffer PrimitiveBuffer {
    Primitive primitives[];
};

float sdfBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b * 0.5;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

float evaluateSDF(vec3 worldPos) {
    float minDist = 1e10;
    
    for (int i = 0; i < primitives.length(); ++i) {
        Primitive prim = primitives[i];
        
        // Transform to local space
        vec3 localPos = worldPos - prim.position;
        // Apply inverse rotation (simplified - would need full quaternion math)
        
        float dist;
        if (prim.type == 0) {  // BOX
            dist = sdfBox(localPos, prim.scale);
        } else if (prim.type == 1) {  // SPHERE
            dist = length(localPos) - prim.scale.x;
        }
        
        minDist = min(minDist, dist);
    }
    
    return minDist;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(sdfVolume);
    
    if (any(greaterThanEqual(coord, size))) return;
    
    // Calculate world position
    vec3 uvw = (vec3(coord) + 0.5) / vec3(size);
    vec3 worldPos = volumeOrigin + uvw * volumeSize;
    
    // Evaluate SDF
    float sdf = evaluateSDF(worldPos);
    
    // Store in volume
    imageStore(sdfVolume, coord, vec4(sdf, 0.0, 0.0, 0.0));
}
```

**Recommendation:** Use this as **intermediate step** while implementing full JFA. Provides immediate gratification and helps validate rest of pipeline.

---

### 3.2 Screen-Space Probes (Simpler Alternative)

**Concept:** Place probes in screen space instead of world space

**Advantages:**
- ✅ Much simpler implementation
- ✅ Automatically adapts to camera view
- ✅ Lower memory usage
- ✅ Easier debugging

**Disadvantages:**
- ❌ Off-screen lighting not captured
- ❌ View-dependent artifacts
- ❌ Doesn't match paper's approach

**When to Use:** For rapid prototyping or if full 3D proves too complex

---

### 3.3 Precomputed Radiance Transfer (PRT) Hybrid

**Concept:** Precompute lighting for static geometry, combine with dynamic cascades

**Approach:**
1. Bake indirect lighting for static scene elements
2. Use radiance cascades only for dynamic objects/lights
3. Combine results in final shading pass

**Benefits:**
- Dramatically reduces runtime computation
- Higher quality for static parts
- Good for architectural visualization

**Implementation Complexity:** High (requires lightmap baking pipeline)

**Priority:** Future enhancement (not for initial implementation)

---

## 4. Hybrid Strategies

### 4.1 Progressive Refinement Pipeline

**Strategy:** Implement features in order of visual impact vs. implementation effort

```
Week 1-2: Analytic SDF + Single Cascade + Direct Lighting
          ↓ (Basic GI working)
Week 3-4: Voxel-based SDF (JFA) + Multi-Cascade
          ↓ (Scalable to complex scenes)
Week 5-6: Temporal Reprojection + Optimization
          ↓ (Production quality)
Week 7-8: Sparse Octree + Advanced Features
          ↓ (Optimized)
```

**Rationale:** Each stage produces a working, visually impressive result. Avoids "big bang" integration where nothing works until everything is done.

---

### 4.2 Dual-Path Development

**Path A: Quick Win** (Analytic SDF)
- Focus on getting GI working quickly
- Use simple primitives
- Validate algorithm correctness
- Timeline: 2 weeks to visible results

**Path B: Production Ready** (Voxel SDF)
- Full voxelization pipeline
- Complex mesh support
- Scalable architecture
- Timeline: 6-8 weeks to production quality

**Execute Both in Parallel:**
- Path A provides motivation and learning
- Path B ensures long-term viability
- Share code where possible (cascade logic, raymarching)

---

### 4.3 ShaderToy-Inspired Shortcuts

**Shortcut #1: Fixed Cascade Layout**

Instead of fully dynamic cascade system, hardcode layout like ShaderToy:
```cpp
// Hardcoded cascade configuration (like ShaderToy)
void initFixedCascades() {
    cascades.resize(4);
    
    // Cascade 0: 256³ probes, covers near field
    cascades[0] = { 256, 0.01f, 0.0f, 0.5f };
    
    // Cascade 1: 128³ probes, mid range
    cascades[1] = { 128, 0.02f, 0.5f, 2.0f };
    
    // Cascade 2: 64³ probes, far field
    cascades[2] = { 64, 0.04f, 2.0f, 8.0f };
    
    // Cascade 3: 32³ probes, very far
    cascades[3] = { 32, 0.08f, 8.0f, 32.0f };
}
```

**Benefit:** Simpler memory management, predictable performance

---

**Shortcut #2: Simplified Visibility Test**

Replace full SDF raymarching with distance-based heuristic:
```cpp
float approximateVisibility(const Probe& from, const Probe& to) {
    float distance = glm::length(to.position - from.position);
    float maxOcclusionDistance = (from.cellSize + to.cellSize) * 0.5f;
    
    // Simple falloff - not physically accurate but fast
    return glm::smoothstep(maxOcclusionDistance * 2.0f, 0.0f, distance);
}
```

**Benefit:** Eliminates need for accurate SDF during early development

---

**Shortcut #3: Reduced Ray Count with Denoising**

Use very few rays (4-8 per probe) + spatial/temporal denoising:
```cpp
// In radiance_3d.comp
const int RAYS_PER_PROBE = 8;  // Very low for speed

// Then apply denoising filter in post-process
void applyDenoising() {
    // Bilateral filter on cascade textures
    // Edge-aware smoothing to preserve details
}
```

**Benefit:** 4-8x speedup, acceptable quality with good denoiser

---

## 5. Implementation Roadmap

### Phase 0: Validation (Week 1)

**Goal:** Prove concept with minimal implementation

**Tasks:**
1. ✅ Fix compilation errors (DONE)
2. Implement analytic SDF for box primitive
3. Create single cascade level (128³)
4. Dispatch radiance_3d.comp with 8 rays/probe
5. Visualize cascade as colored point cloud

**Success Criteria:** See colored points representing indirect lighting

**Estimated Effort:** 3-5 days

---

### Phase 1: Basic GI (Weeks 2-3)

**Goal:** Working global illumination with direct + indirect lighting

**Tasks:**
1. Implement direct lighting injection
2. Connect cascade sampling in raymarch.frag
3. Add multiple light sources
4. Create Cornell box test scene
5. Tune cascade parameters

**Success Criteria:** Cornell box shows color bleeding and soft shadows

**Estimated Effort:** 7-10 days

---

### Phase 2: Multi-Cascade (Weeks 4-5)

**Goal:** Multiple cascade levels for full scene coverage

**Tasks:**
1. Implement 4-level cascade hierarchy
2. Add cascade blending logic
3. Optimize ray counts per level
4. Implement proper interval calculation
5. Performance profiling

**Success Criteria:** Large scenes render with consistent GI quality

**Estimated Effort:** 7-10 days

---

### Phase 3: Voxel Pipeline (Weeks 6-7)

**Goal:** Replace analytic SDF with voxel-based approach

**Tasks:**
1. Implement triangle mesh voxelization
2. Create 3D JFA for SDF generation
3. Test with complex meshes
4. Compare quality vs. analytic SDF
5. Optimize JFA passes

**Success Criteria:** Load arbitrary OBJ/PLY files with correct GI

**Estimated Effort:** 10-14 days

---

### Phase 4: Polish & Optimize (Weeks 8-10)

**Goal:** Production-ready performance and quality

**Tasks:**
1. Add temporal reprojection
2. Implement sparse voxel octree
3. GPU performance optimization
4. UI/UX improvements
5. Documentation and examples

**Success Criteria:** Real-time performance (>30 FPS) on mid-range GPU

**Estimated Effort:** 14-20 days

---

## 6. Risk Assessment

### High-Risk Items

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| 3D JFA too slow | Medium | High | Use analytic SDF fallback; optimize with shared memory |
| Memory overflow with dense grids | High | High | Implement SVO early; limit max resolution |
| Cascade seams visible | Medium | Medium | Improve blending functions; increase overlap |
| Raymarching performance | High | High | Reduce ray count; add adaptive stepping |
| Temporal artifacts | Low | Medium | Tune reprojection parameters; add reset on camera jump |

### Contingency Plans

**If 3D JFA proves too complex:**
- Switch to rasterization-based SDF (render depth from 6 directions)
- Use precomputed SDF for static scenes
- Accept lower quality with faster method

**If memory is insufficient:**
- Reduce cascade count (3 instead of 5)
- Lower base resolution (64³ instead of 128³)
- Implement streaming (load/unload cascades dynamically)

**If performance is inadequate:**
- Reduce rays per probe (4 instead of 16)
- Use hierarchical ray tracing (coarse then fine)
- Implement async compute (overlap with other GPU work)

---

## 7. Key Learnings from ShaderToy

### What Works Well:

1. **Cubemap storage** - Natural fit for directional data
2. **UV-space ray encoding** - Clever way to avoid trigonometry
3. **Flatland visibility** - Good approximation for diffuse GI
4. **Fixed cascade layout** - Predictable and simple

### What Needs Adaptation:

1. **Hardcoded geometry** → Need general mesh support
2. **Single-frame accumulation** → Need proper temporal reprojection
3. **No voxelization** → Must add for arbitrary scenes
4. **Limited interactivity** → Need real-time parameter adjustment

---

## 8. Conclusion & Recommendations

### Immediate Next Steps (This Week):

1. **Implement analytic SDF** for quick validation
2. **Dispatch existing shaders** (they're already written!)
3. **Visualize intermediate results** (voxel grid, SDF slices)
4. **Get basic GI working** with single cascade

### Critical Success Factors:

✅ **Start simple** - Don't try to implement everything at once  
✅ **Validate each step** - Visualize before moving to next phase  
✅ **Profile early** - Identify bottlenecks before they become entrenched  
✅ **Keep ShaderToy open** - Reference implementation for algorithms  

### Final Recommendation:

**Follow the progressive refinement approach:**
1. Week 1-2: Analytic SDF + basic GI (quick win)
2. Week 3-4: Multi-cascade + temporal (quality improvement)
3. Week 5-6: Voxel pipeline (generalization)
4. Week 7-8: Optimization (production readiness)

This balances speed-to-results with long-term maintainability.

---

**End of Brainstorm Plan**
