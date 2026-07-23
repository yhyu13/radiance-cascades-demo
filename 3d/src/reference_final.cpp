#include "reference_final.h"

namespace reffinal {

FinalSample shadeFinalView(const ReferenceCornellScene& scene,
                           const glm::vec3& origin, const glm::vec3& direction,
                           const reftransport::C0Fetch& fetchC0,
                           bool referenceEnabled,
                           const LightingViewPolicy& policy,
                           const glm::vec2& chartUvOffset) {
    const glm::vec3 dir = glm::normalize(direction);
    const ReferenceTraceHit hit = scene.trace(origin, dir, 10000.0f);
    FinalSample out;
    out.hit = hit.hit;
    out.chartId = hit.chartId;
    out.chartUv = hit.chartUv;
    out.sky = !hit.hit;

    if (!hit.hit) {
        out.rgb = scene.getSkyLight(dir);
        return out;
    }

    switch (hit.materialKind) {
        case ReferenceMaterialKind::Reflective:
        case ReferenceMaterialKind::BlackUncharted:
            out.rgb = glm::vec3(0.0f);
            return out;
        case ReferenceMaterialKind::Emissive:
            out.rgb = hit.reflectanceOrEmission;
            return out;
        case ReferenceMaterialKind::Diffuse:
            break;
        case ReferenceMaterialKind::Sky:
            out.rgb = scene.getSkyLight(dir);
            return out;
    }

    glm::vec3 normal = hit.normal;
    if (glm::dot(normal, dir) >= 0.0f)
        normal = -normal;  // view-side normal for back-facing charts

    glm::vec3 irradiance(0.0f);
    if (referenceEnabled && policy.reconstructFourBins) {
        ReferenceTraceHit offsetHit = hit;
        offsetHit.chartUv += chartUvOffset;
        irradiance = reftransport::feedbackB(offsetHit, fetchC0);
    }

    glm::vec3 direct(0.0f);
    if (policy.compositeDirectSeparately) {
        const float ndl = glm::dot(normal, scene.sunDirection());
        if (ndl > 0.0f) {
            const ReferenceTraceHit shadow =
                scene.trace(hit.position + normal * reftransport::kShadowBias,
                            scene.sunDirection(), reftransport::kShadowMaxDistance);
            if (!shadow.hit)
                direct = scene.sunRadiance() * ndl;
        }
    }

    const glm::vec3 response = policy.applyVisibleSurfaceAlbedo
        ? hit.reflectanceOrEmission
        : glm::vec3(1.0f);
    const float normalization = policy.applyOneOverPi ? (1.0f / 3.141592653f) : 1.0f;
    out.rgb = response * (irradiance + direct) * normalization;
    return out;
}

}  // namespace reffinal
