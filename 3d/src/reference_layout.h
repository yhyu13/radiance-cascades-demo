#ifndef REFERENCE_LAYOUT_H
#define REFERENCE_LAYOUT_H

#include <glm/glm.hpp>

#include <cstdint>

// CPU oracle for the Phase 3 parity layout kernel. Ports the exact decode from
// shader_toy/CubeA.glsl:62-152: hardcoded chart selection, probe coupling,
// square-ring hemisphere mapping, solid-angle/Lambert weights, and interval
// limits. The reference's theta literal 3.14192653 is preserved deliberately;
// the azimuth/weighting PI 3.141592653 is a distinct constant.

namespace reflayout {

constexpr uint32_t kCascadeCount = 6;
constexpr int kLogicalWidth = 1024;
constexpr int kLogicalHeight = 3072;
constexpr int kPhysicalWidth = 1024;
constexpr int kPhysicalHeight = 512;
constexpr int kBandHeight = 256;
constexpr int kPrimaryPageHeight = 1536;

constexpr float kThetaPi = 3.14192653f;
constexpr float kPi = 3.141592653f;
constexpr float kTexelScale = 1.0f / 256.0f;
constexpr float kC5Reach = 10000.0f;

struct ReferenceLayoutChart {
    uint32_t chartId;
    uint32_t materialId;
    glm::vec3 origin;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec3 normal;
    glm::vec2 resolution;
    glm::vec2 logicalBase;
};

struct ReferenceProbeDecode {
    bool active = false;
    uint32_t chartId = 0;
    uint32_t materialId = 0;
    uint32_t cascade = 0;
    uint32_t probeSize = 0;
    glm::vec3 probePosition{0.0f};
    glm::vec3 probeDirection{0.0f};
    float thetaIndex = 0.0f;
    float theta = 0.0f;
    float phi = 0.0f;
    float solidAngleWeight = 0.0f;
    float lambertWeight = 0.0f;
    float maxTraceDistance = 0.0f;
    // Continuous physical half-texel coordinate in the per-cascade 1024x512
    // texture; (-1,-1) when the global address is inactive for this cascade.
    glm::vec2 physicalUv{-1.0f, -1.0f};
    int directionBinCount = 0;
    int directionBinIndex = -1;
};

const ReferenceLayoutChart& chart(uint32_t chartId);
uint32_t selectChart(const glm::vec2& logicalUv);

uint32_t cascadeProbeSize(uint32_t cascade);
float cascadeReach(uint32_t cascade);
bool cascadeUnbounded(uint32_t cascade);

float mergeBase(uint32_t cascade);
void mergeTransition(uint32_t cascade, float& minDist, float& interval);
float mergeLerp(uint32_t cascade, float distance);

ReferenceProbeDecode decodeGlobalUv(const glm::vec2& logicalUv);

// Plan Section 7.3 mapping between the reference global logical domain and the
// physical 1024x512 per-cascade storage, in continuous half-texel coordinates.
// Inactive returns (-1,-1).
glm::vec2 globalToPhysical(uint32_t cascade, const glm::vec2& globalUv);
glm::vec2 physicalToGlobal(uint32_t cascade, const glm::vec2& physicalUv);

// Square-perimeter azimuth before normalization (integer-valued for texel
// centers); exposed for direction-bin indexing proofs.
float piecewisePhiUnnormalized(const glm::vec2& probeRel, float thetaIndex);

}  // namespace reflayout

#endif
