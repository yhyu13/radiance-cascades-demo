#ifndef REFERENCE_LEGACY_SCENE_H
#define REFERENCE_LEGACY_SCENE_H

#include "reference_cornell_scene.h"

#include <glm/glm.hpp>

#include <cstdint>

// Legacy Cornell box as a second reference-kernel scene ("new RC on the old
// Cornell box"). Geometry and materials mirror analytic_sdf.cpp createCornellBox
// exactly (walls as inner-face quads; two interior boxes as diffuse uncharted
// boxes). The light is a small emissive ceiling quad (classic Cornell area
// light) — the legacy renderer's point light cannot be represented in the
// parity kernel, so both the RC kernel and the PT quality reference use the
// same emissive quad; the legacy renderer's panel is same-geometry,
// different light model (documented in comparisons).
//
// Layout contract (self-contained, does NOT alter the locked parity layout):
//   texel scale:    1/128 world units (coarser than parity's locked 1/256;
//                   per-scene choice to fit atlas memory)
//   charts:         6 (floor, ceiling, red, green, back, light), single page
//   logical domain: 1344 x 1536 (6 cascade bands of 256 rows)
//   physical:       1344 x 256 per cascade (single page, no interior page)
//   cascades:       6, probeSize = 2^(c+1), reach = probeSize*8/128 world,
//                   C5 = 10000

namespace reflegacy {

constexpr uint32_t kCascadeCount = 6;
constexpr int kLogicalWidth = 1472;
constexpr int kLogicalHeight = 1536;
constexpr int kPhysicalWidth = 1472;
constexpr int kPhysicalHeight = 256;
constexpr float kTexelScale = 1.0f / 128.0f;
constexpr float kC5Reach = 10000.0f;
constexpr float kThetaPi = 3.14192653f;
constexpr float kPi = 3.141592653f;

struct LegacyChart {
    uint32_t chartId;
    uint32_t materialId;
    glm::vec3 origin;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec3 normal;
    glm::vec2 resolution;
    glm::vec2 logicalBase;
};

const LegacyChart& chart(uint32_t chartId);
uint32_t selectChart(const glm::vec2& logicalUv);

uint32_t cascadeProbeSize(uint32_t cascade);
float cascadeReach(uint32_t cascade);

struct LegacyProbeDecode {
    bool active = false;
    uint32_t chartId = 0;
    uint32_t cascade = 0;
    uint32_t probeSize = 0;
    glm::vec3 probePosition{0.0f};
    glm::vec3 probeDirection{0.0f};
    float thetaIndex = 0.0f;
    float solidAngleWeight = 0.0f;
    float lambertWeight = 0.0f;
    float maxTraceDistance = 0.0f;
    glm::vec2 physicalUv{-1.0f};
};

LegacyProbeDecode decodeGlobalUv(const glm::vec2& logicalUv);
glm::vec2 globalToPhysical(uint32_t cascade, const glm::vec2& globalUv);
glm::vec2 physicalToGlobal(uint32_t cascade, const glm::vec2& physicalUv);

}  // namespace reflegacy

// Scene trace for the legacy Cornell box. Mirrors Common.glsl intersection
// conventions (one-sided quads, ABoxNormal boxes). Boxes are diffuse and
// uncharted (they receive direct light and cast shadows but are not in the
// feedback atlas in this integration — documented limitation).
class ReferenceLegacyCornellScene final {
public:
    ReferenceLegacyCornellScene();

    ReferenceTraceHit trace(const glm::vec3& origin, const glm::vec3& direction,
                            float maxDistance) const;
    glm::vec3 getSkyLight(const glm::vec3& direction) const;  // black void
    glm::vec3 sunDirection() const { return {0.0f, 1.0f, 0.0f}; }
    glm::vec3 sunRadiance() const { return glm::vec3(0.0f); }  // disabled
    float referenceTime() const { return 0.0f; }

    const ReferenceSceneGpuData& gpuData() const { return gpuData_; }

private:
    ReferenceSceneGpuData gpuData_{};
};

#endif
