#include "reference_transport.h"

#include <cmath>

namespace reftransport {

float weight(float thetaIndex, uint32_t probeSize) {
    const float size = static_cast<float>(probeSize);
    const float theta = thetaIndex / size * 3.14192653f;
    const float binCount = 4.0f + 8.0f * std::floor(thetaIndex);
    const float solidAngle =
        (std::cos(theta - 3.141592653f / size) - std::cos(theta + 3.141592653f / size)) /
        binCount;
    return solidAngle * std::cos(theta);
}

LocalSample shadeHit(const ReferenceCornellScene& scene,
                     const ReferenceTraceHit& hit,
                     const glm::vec3& probeDirection,
                     float thetaIndex, uint32_t probeSize) {
    const float w = weight(thetaIndex, probeSize);
    LocalSample out;

    if (!hit.hit) {
        out.alpha = -1.0f;
        out.rgb = scene.getSkyLight(probeDirection) * w;
        return out;
    }

    out.alpha = hit.distance;
    glm::vec3 rgb(0.0f);
    switch (hit.materialKind) {
        case ReferenceMaterialKind::Reflective:
        case ReferenceMaterialKind::BlackUncharted:
            break;  // zero contribution in the parity kernel
        case ReferenceMaterialKind::Emissive:
            rgb = hit.reflectanceOrEmission;
            break;
        case ReferenceMaterialKind::Diffuse: {
            if (glm::dot(hit.normal, probeDirection) < 0.0f) {
                glm::vec3 direct(0.0f);
                const float ndl = glm::dot(hit.normal, scene.sunDirection());
                if (ndl > 0.0f) {
                    const glm::vec3 shadowOrigin =
                        hit.position + hit.normal * kShadowBias;
                    const ReferenceTraceHit shadow =
                        scene.trace(shadowOrigin, scene.sunDirection(),
                                    kShadowMaxDistance);
                    if (!shadow.hit)
                        direct = scene.sunRadiance() * ndl;
                }
                // B(hit) = 0: temporal feedback is disabled in Phase 4.
                rgb = direct * hit.reflectanceOrEmission;
            }
            break;
        }
        case ReferenceMaterialKind::Sky:
            out.alpha = -1.0f;
            rgb = scene.getSkyLight(probeDirection);
            break;
    }
    out.rgb = rgb * w;
    return out;
}

LocalSample traceAndShade(const ReferenceCornellScene& scene,
                          const glm::vec3& origin, const glm::vec3& direction,
                          float maxDistance, float thetaIndex, uint32_t probeSize) {
    const ReferenceTraceHit hit = scene.trace(origin, direction, maxDistance);
    return shadeHit(scene, hit, glm::normalize(direction), thetaIndex, probeSize);
}

}  // namespace reftransport
