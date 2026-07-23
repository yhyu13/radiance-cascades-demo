#ifndef REFERENCE_TRANSPORT_H
#define REFERENCE_TRANSPORT_H

#include "reference_cornell_scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <functional>

// Phase 4: local single-cascade transport over the locked ParitySceneSpec.
// Ports CubeA.glsl:150-192 with temporal feedback B(hit) disabled (zero) and
// no upper merge. Payload schema ReferenceSurfaceTexelV1:
//   RGB = weighted directional irradiance contribution (finite, nonnegative)
//   A   = first-hit distance in world units; A < 0 marks a sky miss.

namespace reftransport {

constexpr const char* kPayloadSchema = "ReferenceSurfaceTexelV1";
constexpr float kShadowBias = 0.001f;
constexpr float kShadowMaxDistance = 10000.0f;

struct LocalSample {
    glm::vec3 rgb{0.0f};
    float alpha = -1.0f;
};

// solidAngleWeight * lambertWeight for a probe direction, using the locked
// theta literal 3.14192653 and PI 3.141592653.
float weight(float thetaIndex, uint32_t probeSize);

// Shade an already-traced hit (also covers the synthetic emissive category).
LocalSample shadeHit(const ReferenceCornellScene& scene,
                     const ReferenceTraceHit& hit,
                     const glm::vec3& probeDirection,
                     float thetaIndex, uint32_t probeSize);

// Trace the locked scene and shade the result.
LocalSample traceAndShade(const ReferenceCornellScene& scene,
                          const glm::vec3& origin, const glm::vec3& direction,
                          float maxDistance, float thetaIndex, uint32_t probeSize);

struct UpperCandidate {
    glm::vec2 chartLocal{0.0f};
    glm::vec3 worldPosition{0.0f};
    glm::vec2 lookBackGlobal{-1.0f};
    glm::vec2 lookBackPhysical{-1.0f};
    std::array<glm::vec2, 4> radianceGlobal{};
    std::array<glm::vec2, 4> radiancePhysical{};
    float bilinearWeight = 0.0f;
    float lookBackDistance = -1.0f;
    bool visible = false;
    glm::vec3 radianceSum{0.0f};
};

struct UpperMergeResult {
    std::array<UpperCandidate, 4> candidates{};
    glm::vec3 upperRgb{0.0f};
    glm::vec3 mergedRgb{0.0f};
    float visibleWeight = 0.0f;
    float localWeight = 1.0f;
};

using UpperFetch = std::function<glm::vec4(const glm::ivec2&)>;

// Reproduces CubeA.glsl WeightedSample and merge with the approved chart-edge
// policy: candidate coordinates are clamped to the owning chart before both
// look-back distance and radiance address construction. This prevents the
// source snapshot's edge reads from crossing into another chart/cascade.
UpperMergeResult mergeUpper(const glm::vec2& lowerGlobalUv,
                            const LocalSample& local,
                            float localTraceDistance,
                            const UpperFetch& fetchUpper);

}  // namespace reftransport

#endif
