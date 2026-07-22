#ifndef REFERENCE_CORNELL_SCENE_H
#define REFERENCE_CORNELL_SCENE_H

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>

enum class ReferenceMaterialKind : uint32_t {
    Diffuse = 0,
    BlackUncharted = 1,
    Reflective = 2,
    Emissive = 3,
    Sky = 4
};

enum class ReferenceChartId : uint32_t {
    Invalid = 0,
    Floor = 1,
    Ceiling = 2,
    WallX0 = 3,
    WallX1 = 4,
    WallZ0 = 5,
    WallZ1 = 6,
    InteriorFront = 7,
    InteriorBack = 8
};

struct ReferenceTraceHit {
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    ReferenceMaterialKind materialKind = ReferenceMaterialKind::Sky;
    glm::vec3 reflectanceOrEmission{0.0f};
    ReferenceChartId chartId = ReferenceChartId::Invalid;
    glm::vec2 chartUv{-1.0f};
    bool chartValid = false;
    bool hit = false;
};

struct ReferenceSurfaceChart {
    ReferenceChartId id;
    uint32_t materialId;
    glm::vec3 origin;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec3 normal;
    glm::vec2 extent;
    glm::uvec2 resolution;
    glm::uvec2 logicalBase;
    int handedness;
};

struct ReferenceMaterial {
    uint32_t id;
    ReferenceMaterialKind kind;
    glm::vec3 response;
    glm::vec3 emission;
};

struct ParitySceneSpec {
    float referenceTime;
    glm::vec3 roomBoundsMin;
    glm::vec3 roomBoundsMax;
    glm::vec3 sunDirection;
    glm::vec3 sunRadiance;
    std::array<ReferenceSurfaceChart, 8> charts;
    std::array<ReferenceMaterial, 8> materials;
};

struct ReferenceSceneSnapshot {
    uint32_t sceneId;
    uint64_t revision;
    ParitySceneSpec parityScene;
};

struct alignas(16) ReferenceFloat4 {
    float x, y, z, w;
};

struct alignas(16) ReferenceUint4 {
    uint32_t x, y, z, w;
};

struct alignas(16) GpuReferenceSceneHeader {
    ReferenceUint4 identity;
    ReferenceUint4 counts;
    ReferenceUint4 lightIds;
    ReferenceFloat4 roomBoundsMin;
    ReferenceFloat4 roomBoundsMax;
    ReferenceFloat4 referenceConstants;
    ReferenceFloat4 sunDirection;
    ReferenceFloat4 sunRadiance;
    ReferenceFloat4 skyParameters;
    ReferenceFloat4 largeOpening;
    ReferenceFloat4 smallOpening;
};

struct alignas(16) GpuReferenceMaterial {
    ReferenceUint4 metadata;
    ReferenceFloat4 response;
    ReferenceFloat4 emission;
};

struct alignas(16) GpuReferenceChart {
    ReferenceUint4 metadata;
    ReferenceUint4 resolutionAndBase;
    ReferenceFloat4 originAndExtentU;
    ReferenceFloat4 tangentAndExtentV;
    ReferenceFloat4 bitangentAndTexelScaleU;
    ReferenceFloat4 normalAndTexelScaleV;
};

struct alignas(16) GpuReferencePrimitive {
    ReferenceUint4 metadata;
    ReferenceFloat4 data0;
    ReferenceFloat4 data1;
    ReferenceFloat4 data2;
    ReferenceFloat4 data3;
};

struct alignas(16) ReferenceSceneGpuData {
    GpuReferenceSceneHeader header;
    std::array<GpuReferenceMaterial, 8> materials;
    std::array<GpuReferenceChart, 8> charts;
    std::array<GpuReferencePrimitive, 13> primitives;
};

class ReferenceCornellScene final {
public:
    ReferenceCornellScene();

    const ReferenceSceneSnapshot& snapshot() const noexcept { return snapshot_; }
    const ReferenceSceneGpuData& gpuData() const noexcept { return gpuData_; }

    ReferenceTraceHit trace(const glm::vec3& origin, const glm::vec3& direction,
                            float maxDistance) const;
    bool validateAndWriteReport(const std::string& path) const;

private:
    ReferenceSceneSnapshot snapshot_;
    ReferenceSceneGpuData gpuData_;
};

#endif
