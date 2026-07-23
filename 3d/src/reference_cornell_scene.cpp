#include "reference_cornell_scene.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <type_traits>

namespace {

constexpr float kReferenceTime = 0.0f;
constexpr float kTexelScale = 1.0f / 256.0f;
constexpr float kInteriorFrontZ = 0.47f - kTexelScale;
constexpr float kInteriorBackZ = 0.53f - kTexelScale;
constexpr float kEpsilon = 1.0e-5f;

ReferenceFloat4 f4(const glm::vec3& value, float w = 0.0f) {
    return {value.x, value.y, value.z, w};
}

ReferenceUint4 u4(uint32_t x, uint32_t y = 0, uint32_t z = 0, uint32_t w = 0) {
    return {x, y, z, w};
}

bool close(float a, float b, float epsilon = kEpsilon) {
    return std::abs(a - b) <= epsilon;
}

bool close(const glm::vec2& a, const glm::vec2& b, float epsilon = kEpsilon) {
    return glm::length(a - b) <= epsilon;
}

bool close(const glm::vec3& a, const glm::vec3& b, float epsilon = kEpsilon) {
    return glm::length(a - b) <= epsilon;
}

glm::vec3 chartToWorld(const ReferenceSurfaceChart& chart, const glm::vec2& uv) {
    return chart.origin + chart.tangent * (uv.x * chart.extent.x) +
           chart.bitangent * (uv.y * chart.extent.y);
}

bool interiorOpening(const glm::vec3& point) {
    return glm::length(glm::vec2(point.x, point.y) - glm::vec2(0.5f, 0.0f)) < 0.25f ||
           glm::length(glm::vec2(point.x, point.y) - glm::vec2(0.87f, 0.25f)) < 0.12f;
}

glm::vec2 rotate2(const glm::vec2& point, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

glm::vec2 repeat2(const glm::vec2& point, float count) {
    const float angle = 2.0f * 3.141592653f / count;
    const float sector = std::floor(std::atan2(point.x, point.y) / angle + 0.5f);
    return rotate2(point, sector * angle);
}

float distanceBox2(const glm::vec2& point, const glm::vec2& size) {
    const glm::vec2 delta = glm::abs(point - size * 0.5f) - size * 0.5f;
    return std::min(std::max(delta.x, delta.y), 0.0f) +
           glm::length(glm::max(delta, glm::vec2(0.0f)));
}

bool animatedExclusion(const glm::vec3& point, float time) {
    const float nt = 1.0f + time * 0.2f;
    const glm::vec3 center(
        0.21f + (std::sin(nt) * 0.5f + 0.5f) * 0.58f,
        0.5f,
        0.21f + (std::cos(nt) * 0.5f + 0.5f) * 0.58f);
    const glm::vec3 relative = point - center;
    const glm::vec2 repeated = repeat2({relative.x, relative.z}, 8.0f);
    const float radius = glm::length(glm::vec2(relative.x, relative.z));
    return point.y > 0.49f && std::abs(point.z - 0.5f) > 0.04f &&
           radius < 0.2f && std::abs(radius - 0.1375f) > 0.01f &&
           distanceBox2({repeated.x + 0.01f, repeated.y - 0.015f}, {0.02f, 0.3f}) > 0.0f;
}

glm::vec3 tracePlaneNormal(ReferenceChartId id) {
    switch (id) {
        case ReferenceChartId::Floor:
        case ReferenceChartId::Ceiling: return {0, 1, 0};
        case ReferenceChartId::WallX0: return {1, 0, 0};
        case ReferenceChartId::WallX1: return {-1, 0, 0};
        case ReferenceChartId::WallZ0:
        case ReferenceChartId::WallZ1:
        case ReferenceChartId::InteriorFront:
        case ReferenceChartId::InteriorBack: return {0, 0, -1};
        default: return {0, 0, 0};
    }
}

void considerQuad(ReferenceTraceHit& best, const ReferenceSurfaceChart& chart,
                  const ReferenceMaterial& material, const glm::vec3& origin,
                  const glm::vec3& direction, bool applyAnimatedExclusion) {
    const glm::vec3 planeNormal = tracePlaneNormal(chart.id);
    const glm::vec3 planeRelativeOrigin = origin - chart.origin;
    const float denominator = glm::dot(planeNormal, direction);
    const float originDistance = glm::dot(planeNormal, planeRelativeOrigin);
    if (std::abs(denominator) < 1.0e-7f)
        return;
    if (std::signbit(denominator * originDistance) == false || denominator * originDistance == 0.0f)
        return;
    const float distance = -originDistance / denominator;
    if (distance <= -0.5f || distance >= best.distance)
        return;

    const glm::vec3 position = origin + direction * distance;
    const glm::vec3 relative = position - chart.origin;
    const glm::vec2 uv(glm::dot(relative, chart.tangent) / chart.extent.x,
                       glm::dot(relative, chart.bitangent) / chart.extent.y);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
        return;
    if (applyAnimatedExclusion && animatedExclusion(position, kReferenceTime))
        return;
    if ((chart.id == ReferenceChartId::InteriorFront ||
         chart.id == ReferenceChartId::InteriorBack) && interiorOpening(position))
        return;

    best = {distance, position, chart.normal, material.kind, material.response,
            chart.id, uv, true, true};
}

void considerCylinder(ReferenceTraceHit& best, const glm::vec3& origin,
                      const glm::vec3& direction, const glm::vec2& center,
                      float radius) {
    const glm::vec3 relative = origin - glm::vec3(center, 0.0f);
    const float directionLengthSquared = glm::dot(glm::vec2(direction), glm::vec2(direction));
    if (directionLengthSquared <= 1.0e-8f)
        return;
    const float a = (glm::dot(glm::vec2(relative), glm::vec2(relative)) - radius * radius) /
                    directionLengthSquared;
    const float b = 2.0f * glm::dot(glm::vec2(relative), glm::vec2(direction)) /
                    directionLengthSquared;
    const float discriminant = b * b * 0.25f - a;
    if (discriminant <= 0.0f)
        return;
    const float distance = -b * 0.5f + std::sqrt(discriminant);
    const float z = relative.z + direction.z * distance;
    if (distance <= 0.0f || distance >= best.distance ||
        z < kInteriorFrontZ || z > kInteriorBackZ)
        return;
    const glm::vec3 radial(relative.x + direction.x * distance,
                           relative.y + direction.y * distance, 0.0f);
    best = {distance, origin + direction * distance, -glm::normalize(radial),
            ReferenceMaterialKind::BlackUncharted, glm::vec3(0.0f),
            ReferenceChartId::Invalid, glm::vec2(-1.0f), false, true};
}

void considerSphere(ReferenceTraceHit& best, const glm::vec3& origin,
                    const glm::vec3& direction) {
    const glm::vec3 center(0.15f, 0.1005f, 0.3f);
    const glm::vec3 p = origin - center;
    const float b = 2.0f * glm::dot(p, direction);
    const float discriminant = b * b * 0.25f - (glm::dot(p, p) - 0.01f);
    if (glm::dot(p, direction) >= 0.0f || discriminant <= 0.0f)
        return;
    const float distance = -b * 0.5f - std::sqrt(discriminant);
    if (distance <= -0.5f || distance >= best.distance)
        return;
    const glm::vec3 position = origin + direction * distance;
    best = {distance, position, glm::normalize(position - center),
            ReferenceMaterialKind::Reflective, glm::vec3(0.0f),
            ReferenceChartId::Invalid, glm::vec2(-1.0f), false, true};
}

void considerBox(ReferenceTraceHit& best, const glm::vec3& origin,
                 const glm::vec3& direction) {
    const glm::vec3 minimum(0.78f, 0.06f, 0.78f);
    const glm::vec3 maximum(0.94f, 0.22f, 0.94f);
    const glm::vec3 inverse = 1.0f / direction;
    const glm::vec3 t0 = (minimum - origin) * inverse;
    const glm::vec3 t1 = (maximum - origin) * inverse;
    const glm::vec3 nearValues = glm::min(t0, t1);
    const glm::vec3 farValues = glm::max(t0, t1);
    const float nearDistance = std::max(nearValues.x, std::max(nearValues.y, nearValues.z));
    const float farDistance = std::min(farValues.x, std::min(farValues.y, farValues.z));
    if (nearDistance <= 0.0f || farDistance <= nearDistance || nearDistance >= best.distance)
        return;

    glm::vec3 normal(0.0f);
    if (nearValues.x > std::max(nearValues.y, nearValues.z))
        normal.x = direction.x > 0.0f ? -1.0f : 1.0f;
    else if (nearValues.y > nearValues.z)
        normal.y = direction.y > 0.0f ? -1.0f : 1.0f;
    else
        normal.z = direction.z > 0.0f ? -1.0f : 1.0f;

    best = {nearDistance, origin + direction * nearDistance, normal,
            ReferenceMaterialKind::Reflective, glm::vec3(0.0f),
            ReferenceChartId::Invalid, glm::vec2(-1.0f), false, true};
}

}  // namespace

static_assert(sizeof(ReferenceFloat4) == 16);
static_assert(sizeof(ReferenceUint4) == 16);
static_assert(sizeof(GpuReferenceSceneHeader) == 176);
static_assert(sizeof(GpuReferenceMaterial) == 48);
static_assert(sizeof(GpuReferenceChart) == 96);
static_assert(sizeof(GpuReferencePrimitive) == 80);
static_assert(offsetof(ReferenceSceneGpuData, materials) == 176);
static_assert(offsetof(ReferenceSceneGpuData, charts) == 560);
static_assert(offsetof(ReferenceSceneGpuData, primitives) == 1328);
static_assert(sizeof(ReferenceSceneGpuData) == 2368);
static_assert(std::is_standard_layout_v<ReferenceSceneGpuData>);
static_assert(std::is_trivially_copyable_v<ReferenceSceneGpuData>);

ReferenceCornellScene::ReferenceCornellScene() {
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(
        -std::sin(2.4f), 1.0f, -std::cos(2.4f)));

    snapshot_.sceneId = 1;
    snapshot_.revision = 1;
    snapshot_.parityScene = {
        kReferenceTime,
        glm::vec3(0.0f),
        glm::vec3(1.0f, 0.5f, 1.0f),
        sunDirection,
        glm::vec3(2.5f, 2.25f, 1.625f),
        {{
            {ReferenceChartId::Floor, 1, {0,0,0}, {1,0,0}, {0,0,1}, {0,1,0}, {1,1}, {256,256}, {0,0}, -1},
            {ReferenceChartId::Ceiling, 1, {0,0.5f,0}, {1,0,0}, {0,0,1}, {0,-1,0}, {1,1}, {256,256}, {256,0}, 1},
            {ReferenceChartId::WallX0, 2, {0,0,0}, {0,1,0}, {0,0,1}, {1,0,0}, {0.5f,1}, {128,256}, {512,0}, 1},
            {ReferenceChartId::WallX1, 3, {1,0,0}, {0,1,0}, {0,0,1}, {-1,0,0}, {0.5f,1}, {128,256}, {640,0}, -1},
            {ReferenceChartId::WallZ0, 1, {0,0,0}, {0,1,0}, {1,0,0}, {0,0,1}, {0.5f,1}, {128,256}, {768,0}, -1},
            {ReferenceChartId::WallZ1, 1, {0,0,1}, {0,1,0}, {1,0,0}, {0,0,-1}, {0.5f,1}, {128,256}, {896,0}, 1},
            {ReferenceChartId::InteriorFront, 4, {0,0,kInteriorFrontZ}, {0,1,0}, {1,0,0}, {0,0,-1}, {0.5f,1}, {128,256}, {0,1536}, 1},
            {ReferenceChartId::InteriorBack, 4, {0,0,kInteriorBackZ}, {0,1,0}, {1,0,0}, {0,0,1}, {0.5f,1}, {128,256}, {128,1536}, -1}
        }},
        {{
            {1, ReferenceMaterialKind::Diffuse, {0.9f,0.9f,0.9f}, {0,0,0}},
            {2, ReferenceMaterialKind::Diffuse, {0.9f,0.1f,0.1f}, {0,0,0}},
            {3, ReferenceMaterialKind::Diffuse, {0.05f,0.95f,0.1f}, {0,0,0}},
            {4, ReferenceMaterialKind::Diffuse, {0.99f,0.99f,0.99f}, {0,0,0}},
            {5, ReferenceMaterialKind::BlackUncharted, {0,0,0}, {0,0,0}},
            {6, ReferenceMaterialKind::Reflective, {0,0,0}, {0,0,0}},
            {7, ReferenceMaterialKind::Emissive, {0,0,0}, {1,1,1}},
            {8, ReferenceMaterialKind::Sky, {0.7f,0.8f,1.0f}, {0,0,0}}
        }}
    };

    auto& gpu = gpuData_;
    gpu.header.identity = u4(1, snapshot_.sceneId, 1, 0);
    gpu.header.counts = u4(13, 8, 8, 2);
    gpu.header.lightIds = u4(1, 2);
    gpu.header.roomBoundsMin = f4(snapshot_.parityScene.roomBoundsMin);
    gpu.header.roomBoundsMax = f4(snapshot_.parityScene.roomBoundsMax);
    gpu.header.referenceConstants = {kReferenceTime, kTexelScale, kInteriorFrontZ, kInteriorBackZ};
    gpu.header.sunDirection = f4(sunDirection);
    gpu.header.sunRadiance = f4(snapshot_.parityScene.sunRadiance);
    gpu.header.skyParameters = {0.7f, 0.8f, 1.0f, 0.5f};
    gpu.header.largeOpening = {0.5f, 0.0f, 0.25f, 0.0f};
    gpu.header.smallOpening = {0.87f, 0.25f, 0.12f, 0.0f};

    for (size_t i = 0; i < snapshot_.parityScene.materials.size(); ++i) {
        const auto& material = snapshot_.parityScene.materials[i];
        gpu.materials[i] = {u4(material.id, static_cast<uint32_t>(material.kind)),
                            f4(material.response), f4(material.emission)};
    }
    for (size_t i = 0; i < snapshot_.parityScene.charts.size(); ++i) {
        const auto& chart = snapshot_.parityScene.charts[i];
        gpu.charts[i] = {
            u4(static_cast<uint32_t>(chart.id), chart.materialId, 1, static_cast<uint32_t>(chart.handedness > 0)),
            u4(chart.resolution.x, chart.resolution.y, chart.logicalBase.x, chart.logicalBase.y),
            f4(chart.origin, chart.extent.x),
            f4(chart.tangent, chart.extent.y),
            f4(chart.bitangent, kTexelScale),
            f4(chart.normal, kTexelScale)
        };
        gpu.primitives[i] = {
            u4(static_cast<uint32_t>(i + 1), 0, chart.materialId, static_cast<uint32_t>(chart.id)),
            f4(chart.origin), f4(chart.tangent, chart.extent.x),
            f4(chart.bitangent, chart.extent.y), f4(chart.normal)
        };
    }

    gpu.primitives[8] = {u4(9, 1, 5), {0.5f,0.0f,kInteriorFrontZ,kInteriorBackZ}, {0.25f,0,0,0}, {}, {}};
    gpu.primitives[9] = {u4(10, 1, 5), {0.87f,0.25f,kInteriorFrontZ,kInteriorBackZ}, {0.12f,0,0,0}, {}, {}};
    gpu.primitives[10] = {u4(11, 2, 6), {0.15f,0.1005f,0.3f,0.1f}, {}, {}, {}};
    gpu.primitives[11] = {u4(12, 3, 6), {0.78f,0.06f,0.78f,0}, {0.94f,0.22f,0.94f,0}, {}, {}};
    const float nt = 1.0f + kReferenceTime * 0.2f;
    gpu.primitives[12] = {
        u4(13, 4),
        {0.21f + (std::sin(nt)*0.5f+0.5f)*0.58f, 0.5f,
         0.21f + (std::cos(nt)*0.5f+0.5f)*0.58f, 8.0f},
        {0.49f,0.04f,0.2f,0.1375f}, {0.01f,0.02f,0.3f,0}, {0.01f,-0.015f,0,0}
    };
}

glm::vec3 ReferenceCornellScene::getSkyLight(const glm::vec3& direction) const {
    // Common.glsl:44-47: direction-dependent environment radiance.
    return glm::vec3(0.7f, 0.8f, 1.0f) * (1.0f - direction.y * 0.5f);
}

ReferenceTraceHit ReferenceCornellScene::trace(const glm::vec3& origin,
                                                const glm::vec3& direction,
                                                float maxDistance) const {
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    ReferenceTraceHit best;
    best.distance = maxDistance;
    best.position = origin + normalizedDirection * maxDistance;
    best.materialKind = ReferenceMaterialKind::Sky;
    best.reflectanceOrEmission = glm::vec3(0.7f, 0.8f, 1.0f) *
                                 (1.0f - normalizedDirection.y * 0.5f);

    for (const auto& chart : snapshot_.parityScene.charts) {
        const auto& material = snapshot_.parityScene.materials[chart.materialId - 1];
        considerQuad(best, chart, material, origin, normalizedDirection,
                     static_cast<uint32_t>(chart.id) <= static_cast<uint32_t>(ReferenceChartId::WallZ1));
    }
    considerCylinder(best, origin, normalizedDirection, {0.5f, 0.0f}, 0.25f);
    considerCylinder(best, origin, normalizedDirection, {0.87f, 0.25f}, 0.12f);
    considerSphere(best, origin, normalizedDirection);
    considerBox(best, origin, normalizedDirection);
    return best;
}

bool ReferenceCornellScene::validateAndWriteReport(const std::string& path) const {
    size_t orthonormalFailures = 0;
    size_t handednessFailures = 0;
    size_t cornerFailures = 0;
    size_t traceFailures = 0;
    float maxAxisError = 0.0f;
    float maxDotError = 0.0f;

    for (const auto& chart : snapshot_.parityScene.charts) {
        maxAxisError = std::max({maxAxisError, std::abs(glm::length(chart.tangent) - 1.0f),
                                 std::abs(glm::length(chart.bitangent) - 1.0f),
                                 std::abs(glm::length(chart.normal) - 1.0f)});
        maxDotError = std::max({maxDotError, std::abs(glm::dot(chart.tangent, chart.bitangent)),
                                std::abs(glm::dot(chart.tangent, chart.normal)),
                                std::abs(glm::dot(chart.bitangent, chart.normal))});
        orthonormalFailures += maxAxisError > kEpsilon || maxDotError > kEpsilon;
        const int sign = glm::dot(glm::cross(chart.tangent, chart.bitangent), chart.normal) > 0.0f ? 1 : -1;
        handednessFailures += sign != chart.handedness;

        const glm::vec3 expected11 = chart.origin + chart.tangent * chart.extent.x +
                                     chart.bitangent * chart.extent.y;
        cornerFailures += !close(chartToWorld(chart, {0,0}), chart.origin);
        cornerFailures += !close(chartToWorld(chart, {1,1}), expected11);
    }

    struct TraceFixture {
        glm::vec3 origin;
        glm::vec3 direction;
        float distance;
        ReferenceChartId chart;
        ReferenceMaterialKind material;
    };
    const std::array<TraceFixture, 10> fixtures = {{
        {{0.25f,0.25f,0.25f},{0,-1,0},0.25f,ReferenceChartId::Floor,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.25f},{0,1,0},0.25f,ReferenceChartId::Ceiling,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.25f},{-1,0,0},0.25f,ReferenceChartId::WallX0,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.25f},{1,0,0},0.75f,ReferenceChartId::WallX1,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.25f},{0,0,-1},0.25f,ReferenceChartId::WallZ0,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.75f},{0,0,1},0.25f,ReferenceChartId::WallZ1,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.25f},{0,0,1},kInteriorFrontZ-0.25f,ReferenceChartId::InteriorFront,ReferenceMaterialKind::Diffuse},
        {{0.25f,0.25f,0.75f},{0,0,-1},0.75f-kInteriorBackZ,ReferenceChartId::InteriorBack,ReferenceMaterialKind::Diffuse},
        {{0.15f,0.1005f,0.0f},{0,0,1},0.2f,ReferenceChartId::Invalid,ReferenceMaterialKind::Reflective},
        {{0.86f,0.14f,0.5f},{0,0,1},0.28f,ReferenceChartId::Invalid,ReferenceMaterialKind::Reflective}
    }};
    for (const auto& fixture : fixtures) {
        const ReferenceTraceHit hit = trace(fixture.origin, fixture.direction, 100.0f);
        traceFailures += !hit.hit || !close(hit.distance, fixture.distance) ||
                         hit.chartId != fixture.chart || hit.materialKind != fixture.material;
    }

    const bool passed = orthonormalFailures == 0 && handednessFailures == 0 &&
                        cornerFailures == 0 && traceFailures == 0;
    const std::filesystem::path reportPath(path);
    std::error_code ec;
    if (reportPath.has_parent_path())
        std::filesystem::create_directories(reportPath.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (ec || !out)
        return false;

    out << std::fixed << std::setprecision(9);
    out << "{\n";
    out << "  \"schema_version\": \"reference-cornell-scene-report-v1\",\n";
    out << "  \"gate\": \"G1-chart-contract\",\n";
    out << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\",\n";
    out << "  \"source\": {\"common\": \"shader_toy/Common.glsl\", \"cube\": \"shader_toy/CubeA.glsl\"},\n";
    out << "  \"scene_id\": " << snapshot_.sceneId << ",\n";
    out << "  \"scene_revision\": " << snapshot_.revision << ",\n";
    out << "  \"reference_time\": " << snapshot_.parityScene.referenceTime << ",\n";
    out << "  \"chart_count\": " << snapshot_.parityScene.charts.size() << ",\n";
    out << "  \"material_count\": " << snapshot_.parityScene.materials.size() << ",\n";
    out << "  \"gpu_contract\": {\"schema\": 1, \"bytes\": " << sizeof(ReferenceSceneGpuData)
        << ", \"materials_offset\": " << offsetof(ReferenceSceneGpuData, materials)
        << ", \"charts_offset\": " << offsetof(ReferenceSceneGpuData, charts)
        << ", \"primitives_offset\": " << offsetof(ReferenceSceneGpuData, primitives) << "},\n";
    out << "  \"metrics\": {\n";
    out << "    \"max_axis_length_error\": " << maxAxisError << ",\n";
    out << "    \"max_axis_dot_error\": " << maxDotError << ",\n";
    out << "    \"orthonormal_failures\": " << orthonormalFailures << ",\n";
    out << "    \"handedness_failures\": " << handednessFailures << ",\n";
    out << "    \"corner_fixture_failures\": " << cornerFailures << ",\n";
    out << "    \"trace_fixture_failures\": " << traceFailures << "\n";
    out << "  },\n";
    out << "  \"notes\": [\"referenceTime=0 is the deterministic project lock\", \"emissive is a synthetic material category; Common.glsl defines no emissive primitive\"]\n";
    out << "}\n";
    return out.good() && passed;
}
