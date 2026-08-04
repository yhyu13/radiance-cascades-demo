#include "reference_legacy_scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace reflegacy {
namespace {

// Legacy Cornell chart table (single primary page, 1/128 texel scale).
// Index 0 is the inactive chart. Charts 7/8 are the box top faces, charted so
// the box tops receive bounce light in addition to the declared directional
// sun. Box sides/fronts stay uncharted (documented limitation).
const LegacyChart kCharts[9] = {
    {0, 0, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0}, {0,0}},
    {1, 1, {-1.0f,-1.0f,-1.0f}, {1,0,0}, {0,0,1}, {0,1,0},  {256,256}, {0,0}},
    {2, 1, {-1.0f, 1.0f,-1.0f}, {1,0,0}, {0,0,1}, {0,-1,0}, {256,256}, {256,0}},
    {3, 2, {-1.0f,-1.0f,-1.0f}, {0,1,0}, {0,0,1}, {1,0,0},  {256,256}, {512,0}},
    {4, 3, { 1.0f,-1.0f,-1.0f}, {0,1,0}, {0,0,1}, {-1,0,0}, {256,256}, {768,0}},
    {5, 1, {-1.0f,-1.0f,-1.0f}, {0,1,0}, {1,0,0}, {0,0,1},  {256,256}, {1024,0}},
    {6, 4, {-0.25f, 0.98f,-0.25f}, {1,0,0}, {0,0,1}, {0,-1,0}, {64,64}, {1280,0}},
    {7, 1, {-0.6f, 0.0f,-0.35f}, {1,0,0}, {0,0,1}, {0,1,0}, {64,64}, {1344,0}},
    {8, 1, {0.15f,-0.4f,-0.05f}, {1,0,0}, {0,0,1}, {0,1,0}, {64,64}, {1408,0}},
};

float glslMod(float a, float b) {
    return a - b * std::floor(a / b);
}

}  // namespace

const LegacyChart& chart(uint32_t chartId) {
    return kCharts[chartId <= 8 ? chartId : 0];
}

uint32_t selectChart(const glm::vec2& uv) {
    if (uv.x < 0.0f || uv.x >= 1472.0f || uv.y < 0.0f || uv.y >= 1536.0f)
        return 0;
    if (uv.x < 256.0f) return 1;
    if (uv.x < 512.0f) return 2;
    if (uv.x < 768.0f) return 3;
    if (uv.x < 1024.0f) return 4;
    if (uv.x < 1280.0f) return 5;
    if (uv.x < 1344.0f) return 6;
    if (uv.x < 1408.0f) return 7;
    return 8;
}

uint32_t cascadeProbeSize(uint32_t cascade) {
    return 1u << (cascade + 1u);
}

float cascadeReach(uint32_t cascade) {
    if (cascade > 4)
        return kC5Reach;
    return static_cast<float>(cascadeProbeSize(cascade)) * 8.0f * kTexelScale;
}

LegacyProbeDecode decodeGlobalUv(const glm::vec2& uv) {
    LegacyProbeDecode out;
    const uint32_t chartId = selectChart(uv);
    if (chartId == 0)
        return out;
    const LegacyChart& ch = chart(chartId);
    const uint32_t cascade = static_cast<uint32_t>(std::floor(uv.y / 256.0f));
    const uint32_t probeSize = cascadeProbeSize(cascade);
    const glm::vec2 modUv(glslMod(uv.x, ch.resolution.x), glslMod(uv.y, ch.resolution.y));
    const glm::vec2 probePositions = ch.resolution / static_cast<float>(probeSize);
    const glm::vec3 probePos = ch.origin
        + glslMod(modUv.x, probePositions.x) * static_cast<float>(probeSize) * kTexelScale * ch.tangent
        + glslMod(modUv.y, probePositions.y) * static_cast<float>(probeSize) * kTexelScale * ch.bitangent;
    const glm::vec2 probeUv(
        std::floor(modUv.x / probePositions.x) + 0.5f,
        std::floor(modUv.y / probePositions.y) + 0.5f);
    const glm::vec2 probeRel = probeUv - static_cast<float>(probeSize) * 0.5f;
    const float thetai = std::max(std::abs(probeRel.x), std::abs(probeRel.y));
    const float theta = thetai / static_cast<float>(probeSize) * kThetaPi;
    float phiU = 0.0f;
    if (probeRel.x + 0.5f > thetai && probeRel.y - 0.5f > -thetai)
        phiU = probeRel.x - probeRel.y;
    else if (probeRel.y - 0.5f < -thetai && probeRel.x - 0.5f > -thetai)
        phiU = thetai * 2.0f - probeRel.y - probeRel.x;
    else if (probeRel.x - 0.5f < -thetai && probeRel.y + 0.5f < thetai)
        phiU = thetai * 4.0f - probeRel.x + probeRel.y;
    else if (probeRel.y + 0.5f > thetai && probeRel.x + 0.5f < thetai)
        phiU = thetai * 8.0f - (probeRel.y - probeRel.x);
    const float binCount = 4.0f + 8.0f * std::floor(thetai);
    const float phi = phiU * kPi * 2.0f / binCount;
    const glm::vec3 localDir(
        std::sin(phi) * std::sin(theta),
        std::cos(phi) * std::sin(theta),
        std::cos(theta));
    const glm::vec3 probeDir = localDir.x * ch.tangent + localDir.y * ch.bitangent +
                               localDir.z * ch.normal;
    out.active = true;
    out.chartId = chartId;
    out.cascade = cascade;
    out.probeSize = probeSize;
    out.probePosition = probePos;
    out.probeDirection = probeDir;
    out.thetaIndex = thetai;
    out.solidAngleWeight = (std::cos(theta - kPi / static_cast<float>(probeSize)) -
                            std::cos(theta + kPi / static_cast<float>(probeSize))) / binCount;
    out.lambertWeight = std::cos(theta);
    out.maxTraceDistance = cascadeReach(cascade);
    out.physicalUv = globalToPhysical(cascade, uv);
    return out;
}

glm::vec2 globalToPhysical(uint32_t cascade, const glm::vec2& globalUv) {
    const float bandY0 = 256.0f * static_cast<float>(cascade);
    if (globalUv.x < 0.0f || globalUv.x >= 1472.0f)
        return {-1.0f, -1.0f};
    if (globalUv.y >= bandY0 && globalUv.y < bandY0 + 256.0f)
        return {globalUv.x, globalUv.y - bandY0};
    return {-1.0f, -1.0f};
}

glm::vec2 physicalToGlobal(uint32_t cascade, const glm::vec2& physicalUv) {
    const float bandY0 = 256.0f * static_cast<float>(cascade);
    return {physicalUv.x, physicalUv.y + bandY0};
}

}  // namespace reflegacy

// ---------------------------------------------------------------------------
// Scene trace (CPU oracle; mirrors Common.glsl conventions)
// ---------------------------------------------------------------------------

namespace {

constexpr float kEpsilon = 1.0e-5f;

glm::vec3 planeNormalForLegacyChart(uint32_t chartId) {
    // One-sided intersection plane normals (Common.glsl convention): ceiling
    // and the light quad flip relative to their chart (shading) normals.
    switch (chartId) {
        case 1: return {0.0f, 1.0f, 0.0f};
        case 2: return {0.0f, 1.0f, 0.0f};  // ceiling: plane +Y, chart -Y
        case 3: return {1.0f, 0.0f, 0.0f};
        case 4: return {-1.0f, 0.0f, 0.0f};
        case 5: return {0.0f, 0.0f, -1.0f}; // back: plane -Z, chart +Z
        case 6: return {0.0f, 1.0f, 0.0f};  // light: plane +Y, chart -Y
        case 7:
        case 8: return {0.0f, 0.0f, 1.0f};  // box fronts: plane +Z, chart +Z
        default: return {0.0f, 0.0f, 0.0f};
    }
}

void considerLegacyQuad(ReferenceTraceHit& best, const reflegacy::LegacyChart& ch,
                        uint32_t materialId, const glm::vec3& origin,
                        const glm::vec3& direction, bool tieOverride = false) {
    const glm::vec3 planeN = planeNormalForLegacyChart(ch.chartId);
    const glm::vec3 rel = origin - ch.origin;
    const float norDot = glm::dot(planeN, direction);
    const float pDot = glm::dot(planeN, rel);
    if (norDot * pDot >= 0.0f)
        return;
    const float t = -pDot / norDot;
    // Box-top quads share their plane with the box primitive's top face and
    // must win the tie so the hit lands on the charted surface.
    const float limit = tieOverride ? best.distance + 0.0005f : best.distance;
    if (t <= -0.5f || t > limit)
        return;
    const glm::vec3 hp = rel + direction * t;
    const glm::vec2 hit2(glm::dot(hp, ch.tangent), glm::dot(hp, ch.bitangent));
    const glm::vec2 extent(ch.resolution * reflegacy::kTexelScale);
    if (hit2.x < 0.0f || hit2.x > extent.x || hit2.y < 0.0f || hit2.y > extent.y)
        return;
    const glm::vec3 response =
        ch.materialId == 2 ? glm::vec3(0.8f, 0.2f, 0.2f)
        : ch.materialId == 3 ? glm::vec3(0.2f, 0.8f, 0.2f)
        : glm::vec3(0.8f);
    best.distance = t;
    best.position = origin + direction * t;
    best.normal = ch.normal;
    best.chartId = static_cast<ReferenceChartId>(ch.chartId);
    best.chartUv = hit2 / extent;
    best.chartValid = true;
    best.materialKind = ch.materialId == 4 ? ReferenceMaterialKind::Emissive
                                           : ReferenceMaterialKind::Diffuse;
    best.reflectanceOrEmission = ch.materialId == 4
        ? glm::vec3(6.0f, 5.7f, 5.1f)  // declared light emission
        : response;
    best.hit = true;
    (void)materialId;
}

void considerLegacyBox(ReferenceTraceHit& best, const glm::vec3& bmin,
                       const glm::vec3& bmax, const glm::vec3& origin,
                       const glm::vec3& direction) {
    const glm::vec3 inv = 1.0f / direction;
    const glm::vec3 t0 = (bmin - origin) * inv;
    const glm::vec3 t1 = (bmax - origin) * inv;
    const glm::vec3 nearV = glm::min(t0, t1);
    const glm::vec3 farV = glm::max(t0, t1);
    const float nearT = std::max(nearV.x, std::max(nearV.y, nearV.z));
    const float farT = std::min(farV.x, std::min(farV.y, farV.z));
    if (nearT <= 0.0f || farT <= nearT || nearT >= best.distance)
        return;
    glm::vec3 normal(0.0f);
    if (nearV.x > std::max(nearV.y, nearV.z))
        normal.x = direction.x > 0.0f ? -1.0f : 1.0f;
    else if (nearV.y > nearV.z)
        normal.y = direction.y > 0.0f ? -1.0f : 1.0f;
    else
        normal.z = direction.z > 0.0f ? -1.0f : 1.0f;
    best.distance = nearT;
    best.position = origin + direction * nearT;
    best.normal = normal;
    best.chartId = ReferenceChartId::Invalid;  // uncharted box
    best.chartUv = {-1.0f, -1.0f};
    best.chartValid = false;
    best.materialKind = ReferenceMaterialKind::Diffuse;
    best.reflectanceOrEmission = glm::vec3(0.8f);
    best.hit = true;
}

}  // namespace

ReferenceLegacyCornellScene::ReferenceLegacyCornellScene() {
    // Fill the shared scene contract (SSBO layout reused from the parity
    // scene). Layout identity marks this as the legacy layout (sceneId 2).
    gpuData_.header.identity = {1, 2, 1, 1};
    gpuData_.header.counts = {13, 8, 8, 0};
    gpuData_.header.lightIds = {0, 0, 0, 0};
    gpuData_.header.roomBoundsMin = {-1.0f, -1.0f, -1.0f, 0.0f};
    gpuData_.header.roomBoundsMax = {1.0f, 1.0f, 1.0f, 0.0f};
    gpuData_.header.referenceConstants = {0.0f, reflegacy::kTexelScale, 0.0f, 0.0f};
    // Declared directional sun for the legacy Cornell scene: a warm overhead
    // sun with a slight -X/+Z angle. This is a scene-lighting choice (like the
    // emissive ceiling quad), NOT a kernel change. It gives the interior box
    // tops direct light via the kernel's directional-sun term, which the
    // emissive quad alone cannot (it contributes only through the merge chain,
    // which loses energy on small geometry at C0/C1).
    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, 1.0f, 0.2f));
    gpuData_.header.sunDirection = {sunDir.x, sunDir.y, sunDir.z, 0.0f};
    gpuData_.header.sunRadiance = {2.0f, 1.9f, 1.8f, 0.0f};
    gpuData_.header.skyParameters = {0.0f, 0.0f, 0.0f, 0.0f};
    gpuData_.header.largeOpening = {0, 0, 0, 0};
    gpuData_.header.smallOpening = {0, 0, 0, 0};

    const struct { uint32_t id; ReferenceMaterialKind kind; glm::vec3 response; glm::vec3 emission; } mats[4] = {
        {1, ReferenceMaterialKind::Diffuse, {0.8f, 0.8f, 0.8f}, {0, 0, 0}},
        {2, ReferenceMaterialKind::Diffuse, {0.8f, 0.2f, 0.2f}, {0, 0, 0}},
        {3, ReferenceMaterialKind::Diffuse, {0.2f, 0.8f, 0.2f}, {0, 0, 0}},
        {4, ReferenceMaterialKind::Emissive, {0, 0, 0}, {6.0f, 5.7f, 5.1f}},
    };
    for (size_t i = 0; i < 4; ++i) {
        gpuData_.materials[i] = {{mats[i].id, static_cast<uint32_t>(mats[i].kind), 0, 0},
                                 {mats[i].response.x, mats[i].response.y, mats[i].response.z, 0},
                                 {mats[i].emission.x, mats[i].emission.y, mats[i].emission.z, 0}};
    }
    for (size_t i = 4; i < 8; ++i)
        gpuData_.materials[i] = {};

    for (uint32_t c = 0; c < 8; ++c) {
        const auto& ch = reflegacy::chart(c + 1);
        const glm::vec2 extent = ch.resolution * reflegacy::kTexelScale;
        gpuData_.charts[c] = {
            {ch.chartId, ch.materialId, 1, 0},
            {static_cast<uint32_t>(ch.resolution.x), static_cast<uint32_t>(ch.resolution.y),
             static_cast<uint32_t>(ch.logicalBase.x), static_cast<uint32_t>(ch.logicalBase.y)},
            {ch.origin.x, ch.origin.y, ch.origin.z, extent.x},
            {ch.tangent.x, ch.tangent.y, ch.tangent.z, extent.y},
            {ch.bitangent.x, ch.bitangent.y, ch.bitangent.z, reflegacy::kTexelScale},
            {ch.normal.x, ch.normal.y, ch.normal.z, reflegacy::kTexelScale},
        };
        // metadata.w high bit 0x100: plane normal for the one-sided
        // intersection test is the negated chart normal (ceiling, back, light).
        const uint32_t flip = (ch.chartId == 2 || ch.chartId == 5 || ch.chartId == 6)
            ? 0x100u : 0u;
        gpuData_.primitives[c] = {
            {c + 1, 0, ch.materialId, ch.chartId | flip},
            {ch.origin.x, ch.origin.y, ch.origin.z, 0},
            {ch.tangent.x, ch.tangent.y, ch.tangent.z, extent.x},
            {ch.bitangent.x, ch.bitangent.y, ch.bitangent.z, extent.y},
            {ch.normal.x, ch.normal.y, ch.normal.z, 0},
        };
    }
    // Cylinder slots inert; the two box slots carry the interior boxes.
    // Tall box (diffuse, uncharted): center(-0.35,-0.5,-0.1) half(0.25,0.5,0.25)
    gpuData_.primitives[10] = {{11, 3, 1, 0},
                               {-0.6f, -1.0f, -0.35f, 0.0f},
                               {-0.1f, 0.0f, 0.15f, 0.0f}, {}, {}};
    // Short box (diffuse, uncharted): center(0.4,-0.7,0.2) half(0.25,0.3,0.25)
    gpuData_.primitives[11] = {{12, 3, 1, 0},
                               {0.15f, -1.0f, -0.05f, 0.0f},
                               {0.65f, -0.4f, 0.45f, 0.0f}, {}, {}};
    gpuData_.primitives[12] = {{13, 4, 0, 0}, {}, {}, {}, {}};
}

ReferenceTraceHit ReferenceLegacyCornellScene::trace(const glm::vec3& origin,
                                                      const glm::vec3& direction,
                                                      float maxDistance) const {
    const glm::vec3 dir = glm::normalize(direction);
    ReferenceTraceHit best;
    best.distance = maxDistance;
    best.position = origin + dir * maxDistance;
    best.materialKind = ReferenceMaterialKind::Sky;
    best.reflectanceOrEmission = glm::vec3(0.0f);
    best.chartId = ReferenceChartId::Invalid;

    for (uint32_t c = 1; c <= 6; ++c)
        considerLegacyQuad(best, reflegacy::chart(c), reflegacy::chart(c).materialId,
                           origin, dir);

    const auto& boxA = gpuData_.primitives[10];
    considerLegacyBox(best, {boxA.data0.x, boxA.data0.y, boxA.data0.z},
                      {boxA.data1.x, boxA.data1.y, boxA.data1.z}, origin, dir);
    const auto& boxB = gpuData_.primitives[11];
    considerLegacyBox(best, {boxB.data0.x, boxB.data0.y, boxB.data0.z},
                      {boxB.data1.x, boxB.data1.y, boxB.data1.z}, origin, dir);

    // Charted box front faces (+Z) override the coplanar box front within a
    // small tie margin so feedback lands on the charted surface.
    for (uint32_t c = 7; c <= 8; ++c)
        considerLegacyQuad(best, reflegacy::chart(c), reflegacy::chart(c).materialId,
                           origin, dir, true);

    return best;
}

glm::vec3 ReferenceLegacyCornellScene::getSkyLight(const glm::vec3& direction) const {
    (void)direction;
    return glm::vec3(0.0f);  // black void outside the open-front box
}
