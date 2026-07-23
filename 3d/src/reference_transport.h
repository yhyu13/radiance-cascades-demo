#ifndef REFERENCE_TRANSPORT_H
#define REFERENCE_TRANSPORT_H

#include "reference_cornell_scene.h"

#include <glm/glm.hpp>

#include <cstdint>

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

}  // namespace reftransport

#endif
