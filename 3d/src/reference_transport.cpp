#include "reference_transport.h"
#include "reference_layout.h"

#include <algorithm>
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

namespace {

float glslMod(float value, float divisor) {
    return value - divisor * std::floor(value / divisor);
}

glm::ivec2 texelOf(const glm::vec2& physical) {
    return {static_cast<int>(std::floor(physical.x)),
            static_cast<int>(std::floor(physical.y))};
}

}  // namespace

UpperMergeResult mergeUpper(const glm::vec2& uv, const LocalSample& local,
                            float localTraceDistance, const UpperFetch& fetchUpper) {
    UpperMergeResult result;
    result.mergedRgb = local.rgb;
    const auto lower = reflayout::decodeGlobalUv(uv);
    if (!lower.active || lower.cascade >= 5)
        return result;

    const auto& ch = reflayout::chart(lower.chartId);
    const float probeSize = static_cast<float>(lower.probeSize);
    const float upperProbeSize = probeSize * 2.0f;
    const glm::vec2 probePositions = ch.resolution / probeSize;
    const glm::vec2 upperPositions = probePositions * 0.5f;
    const glm::vec2 modUv(glslMod(uv.x, ch.resolution.x),
                          glslMod(uv.y, ch.resolution.y));
    const glm::vec2 upperOrigin = glm::floor(uv / ch.resolution) * ch.resolution +
                                  glm::vec2(0.0f, ch.resolution.y);
    const glm::vec2 directionOrigin = glm::floor(modUv / probePositions) * probePositions;
    const glm::vec2 p = glm::clamp(glm::mod(modUv, probePositions) * 0.5f,
                                   glm::vec2(0.5f), upperPositions - 0.5f);
    const glm::vec2 fraction = glm::fract(p - 0.5f);
    const glm::vec2 base = glm::floor(p - 0.5f) + 0.5f;
    const std::array<glm::vec2, 4> offsets = {
        glm::vec2(0,0), glm::vec2(1,0), glm::vec2(0,1), glm::vec2(1,1)};
    const std::array<float, 4> weights = {
        (1.0f-fraction.x)*(1.0f-fraction.y), fraction.x*(1.0f-fraction.y),
        (1.0f-fraction.x)*fraction.y, fraction.x*fraction.y};

    glm::vec3 numerator(0.0f);
    for (size_t i = 0; i < 4; ++i) {
        auto& candidate = result.candidates[i];
        candidate.bilinearWeight = weights[i];
        // Approved edge policy: clamp all candidate uses, including look-back.
        candidate.chartLocal = glm::clamp(base + offsets[i], glm::vec2(0.5f),
                                          upperPositions - 0.5f);
        candidate.worldPosition = ch.origin +
            ch.tangent * (candidate.chartLocal.x * upperProbeSize / 256.0f) +
            ch.bitangent * (candidate.chartLocal.y * upperProbeSize / 256.0f);
        const glm::vec3 relative = lower.probePosition - candidate.worldPosition;
        const float theta = (upperProbeSize*0.5f - 0.5f) /
                            (upperProbeSize*0.5f) * reflayout::kPi * 0.5f;
        const float phi = std::atan2(-glm::dot(relative, ch.tangent),
                                     -glm::dot(relative, ch.bitangent));
        const float count = 4.0f + 8.0f*(upperProbeSize*0.5f - 1.0f);
        const float phiI = std::floor((phi/reflayout::kPi*0.5f + 0.5f)*count) + 0.5f;
        const float phiLen = upperProbeSize - 1.0f;
        glm::vec2 phiUv;
        if (phiI < phiLen) phiUv = {upperProbeSize-0.5f, upperProbeSize-phiI};
        else if (phiI < phiLen*2.0f) phiUv = {upperProbeSize-(phiI-phiLen), 0.5f};
        else if (phiI < phiLen*3.0f) phiUv = {0.5f, phiI-phiLen*2.0f};
        else phiUv = {phiI-phiLen*3.0f, upperProbeSize-0.5f};

        candidate.lookBackGlobal = upperOrigin + directionOrigin +
                                   glm::floor(phiUv)*upperPositions + candidate.chartLocal;
        candidate.lookBackPhysical = reflayout::globalToPhysical(
            lower.cascade + 1, candidate.lookBackGlobal);
        candidate.lookBackDistance = fetchUpper(texelOf(candidate.lookBackPhysical)).a;
        const float cone = std::cos(reflayout::kPi*0.5f - theta);
        candidate.visible = candidate.lookBackDistance < -0.5f ||
            glm::length(relative) < candidate.lookBackDistance*cone + 0.01f;

        const glm::vec2 radBase = upperOrigin + directionOrigin + candidate.chartLocal;
        const std::array<glm::vec2, 4> radOffsets = {
            glm::vec2(0,0), glm::vec2(upperPositions.x,0),
            glm::vec2(0,upperPositions.y), upperPositions};
        for (size_t j = 0; j < 4; ++j) {
            candidate.radianceGlobal[j] = radBase + radOffsets[j];
            candidate.radiancePhysical[j] = reflayout::globalToPhysical(
                lower.cascade + 1, candidate.radianceGlobal[j]);
            candidate.radianceSum += glm::vec3(
                fetchUpper(texelOf(candidate.radiancePhysical[j])));
        }
        if (candidate.visible) {
            numerator += candidate.radianceSum * candidate.bilinearWeight;
            result.visibleWeight += candidate.bilinearWeight;
        }
    }
    result.upperRgb = numerator / std::max(0.01f, result.visibleWeight);
    result.localWeight = reflayout::mergeLerp(lower.cascade, localTraceDistance);
    result.mergedRgb = local.rgb*result.localWeight +
                       result.upperRgb*(1.0f-result.localWeight);
    return result;
}

}  // namespace reftransport
