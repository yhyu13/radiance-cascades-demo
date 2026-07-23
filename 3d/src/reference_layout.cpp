#include "reference_layout.h"

#include <algorithm>
#include <cmath>

namespace reflayout {
namespace {

constexpr float kInteriorFrontZ = 0.47f - kTexelScale;
constexpr float kInteriorBackZ = 0.53f - kTexelScale;

// Hardcoded chart table from CubeA.glsl:68-121 / plan Section 4.0.
// Index 0 is the inactive chart.
const ReferenceLayoutChart kCharts[9] = {
    {0, 0, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0}, {0,0}},
    {1, 1, {0.0f,0.0f,0.0f}, {1,0,0}, {0,0,1}, {0,1,0},  {256,256}, {0,0}},
    {2, 1, {0.0f,0.5f,0.0f}, {1,0,0}, {0,0,1}, {0,-1,0}, {256,256}, {256,0}},
    {3, 2, {0.0f,0.0f,0.0f}, {0,1,0}, {0,0,1}, {1,0,0},  {128,256}, {512,0}},
    {4, 3, {1.0f,0.0f,0.0f}, {0,1,0}, {0,0,1}, {-1,0,0}, {128,256}, {640,0}},
    {5, 1, {0.0f,0.0f,0.0f}, {0,1,0}, {1,0,0}, {0,0,1},  {128,256}, {768,0}},
    {6, 1, {0.0f,0.0f,1.0f}, {0,1,0}, {1,0,0}, {0,0,-1}, {128,256}, {896,0}},
    {7, 4, {0.0f,0.0f,kInteriorFrontZ}, {0,1,0}, {1,0,0}, {0,0,-1}, {128,256}, {0,1536}},
    {8, 4, {0.0f,0.0f,kInteriorBackZ},  {0,1,0}, {1,0,0}, {0,0,1},  {128,256}, {128,1536}},
};

float glslMod(float a, float b) {
    return a - b * std::floor(a / b);
}

}  // namespace

const ReferenceLayoutChart& chart(uint32_t chartId) {
    return kCharts[chartId <= 8 ? chartId : 0];
}

uint32_t selectChart(const glm::vec2& uv) {
    if (uv.x < 0.0f || uv.x >= 1024.0f || uv.y < 0.0f || uv.y >= 3072.0f)
        return 0;
    if (uv.y < 1536.0f) {
        if (uv.x < 256.0f) return 1;
        if (uv.x < 512.0f) return 2;
        if (uv.x < 640.0f) return 3;
        if (uv.x < 768.0f) return 4;
        if (uv.x < 896.0f) return 5;
        return 6;
    }
    if (uv.x < 128.0f) return 7;
    if (uv.x < 256.0f) return 8;
    return 0;
}

uint32_t cascadeProbeSize(uint32_t cascade) {
    return 1u << (cascade + 1u);
}

float cascadeReach(uint32_t cascade) {
    if (cascade > 4)
        return kC5Reach;
    return static_cast<float>(cascadeProbeSize(cascade)) / 32.0f;
}

bool cascadeUnbounded(uint32_t cascade) {
    return cascade > 4;
}

float mergeBase(uint32_t cascade) {
    return kTexelScale * static_cast<float>(cascadeProbeSize(cascade)) * 1.5f;
}

void mergeTransition(uint32_t cascade, float& minDist, float& interval) {
    const float base = mergeBase(cascade);
    if (cascade == 0) {
        minDist = 0.0f;
        interval = 2.0f * base;
    } else {
        minDist = base;
        interval = base;
    }
}

float mergeLerp(uint32_t cascade, float distance) {
    float minDist = 0.0f;
    float interval = 1.0f;
    mergeTransition(cascade, minDist, interval);
    return 1.0f - std::clamp((distance - minDist) / interval, 0.0f, 1.0f);
}

float piecewisePhiUnnormalized(const glm::vec2& rel, float thetai) {
    // CubeA.glsl:137-145 exact branch conditions.
    if (rel.x + 0.5f > thetai && rel.y - 0.5f > -thetai)
        return rel.x - rel.y;
    if (rel.y - 0.5f < -thetai && rel.x - 0.5f > -thetai)
        return thetai * 2.0f - rel.y - rel.x;
    if (rel.x - 0.5f < -thetai && rel.y + 0.5f < thetai)
        return thetai * 4.0f - rel.x + rel.y;
    if (rel.y + 0.5f > thetai && rel.x + 0.5f < thetai)
        return thetai * 8.0f - (rel.y - rel.x);
    return 0.0f;
}

ReferenceProbeDecode decodeGlobalUv(const glm::vec2& uv) {
    ReferenceProbeDecode out;
    const uint32_t chartId = selectChart(uv);
    if (chartId == 0)
        return out;

    const ReferenceLayoutChart& ch = chart(chartId);
    const uint32_t cascade = static_cast<uint32_t>(
        std::floor(glslMod(uv.y, 1536.0f) / 256.0f));
    const uint32_t probeSize = cascadeProbeSize(cascade);

    const glm::vec2 modUv(glslMod(uv.x, ch.resolution.x), glslMod(uv.y, ch.resolution.y));
    const glm::vec2 probePositions = ch.resolution / static_cast<float>(probeSize);

    const glm::vec3 probePos = ch.origin
        + glslMod(modUv.x, probePositions.x) * static_cast<float>(probeSize) / 256.0f * ch.tangent
        + glslMod(modUv.y, probePositions.y) * static_cast<float>(probeSize) / 256.0f * ch.bitangent;

    const glm::vec2 probeUv(
        std::floor(modUv.x / probePositions.x) + 0.5f,
        std::floor(modUv.y / probePositions.y) + 0.5f);
    const glm::vec2 probeRel = probeUv - static_cast<float>(probeSize) * 0.5f;
    const float thetai = std::max(std::abs(probeRel.x), std::abs(probeRel.y));
    const float theta = thetai / static_cast<float>(probeSize) * kThetaPi;
    const float phiUnnormalized = piecewisePhiUnnormalized(probeRel, thetai);
    const float binCount = 4.0f + 8.0f * std::floor(thetai);
    const float phi = phiUnnormalized * kPi * 2.0f / binCount;

    const glm::vec3 localDir(
        std::sin(phi) * std::sin(theta),
        std::cos(phi) * std::sin(theta),
        std::cos(theta));
    const glm::vec3 probeDir = localDir.x * ch.tangent + localDir.y * ch.bitangent +
                               localDir.z * ch.normal;

    const float saw = (std::cos(theta - kPi / static_cast<float>(probeSize)) -
                       std::cos(theta + kPi / static_cast<float>(probeSize))) / binCount;
    const float lw = std::cos(theta);

    out.active = true;
    out.chartId = chartId;
    out.materialId = ch.materialId;
    out.cascade = cascade;
    out.probeSize = probeSize;
    out.probePosition = probePos;
    out.probeDirection = probeDir;
    out.thetaIndex = thetai;
    out.theta = theta;
    out.phi = phi;
    out.solidAngleWeight = saw;
    out.lambertWeight = lw;
    out.maxTraceDistance = cascadeReach(cascade);
    out.physicalUv = globalToPhysical(cascade, uv);
    out.directionBinCount = static_cast<int>(binCount);
    out.directionBinIndex = static_cast<int>(std::lround(phiUnnormalized)) %
                            static_cast<int>(binCount);
    return out;
}

glm::vec2 globalToPhysical(uint32_t cascade, const glm::vec2& globalUv) {
    const float bandY0 = 256.0f * static_cast<float>(cascade);
    if (globalUv.x < 0.0f || globalUv.x >= 1024.0f)
        return {-1.0f, -1.0f};
    if (globalUv.y >= bandY0 && globalUv.y < bandY0 + 256.0f)
        return {globalUv.x, globalUv.y - bandY0};
    // 256 + y - (1536 + 256c) == y - (1280 + 256c); the folded constant keeps
    // CPU and GPU float evaluation bit-identical (no reassociation hazard).
    const float interiorY0 = 1536.0f + bandY0;
    if (globalUv.y >= interiorY0 && globalUv.y < interiorY0 + 256.0f)
        return {globalUv.x, globalUv.y - (1280.0f + bandY0)};
    return {-1.0f, -1.0f};
}

glm::vec2 physicalToGlobal(uint32_t cascade, const glm::vec2& physicalUv) {
    const float bandY0 = 256.0f * static_cast<float>(cascade);
    if (physicalUv.y < 256.0f)
        return {physicalUv.x, physicalUv.y + bandY0};
    return {physicalUv.x, physicalUv.y + (1280.0f + bandY0)};
}

}  // namespace reflayout
