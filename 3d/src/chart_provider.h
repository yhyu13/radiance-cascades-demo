#ifndef CHART_PROVIDER_H
#define CHART_PROVIDER_H

#include "reference_cornell_scene.h"
#include "reference_layout.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Phase 11 M1 — CPU UV2 island extractor + atlas packer.
// Does not touch the GPU, shaders, or locked parity constants in
// reference_layout.h. Consumes a mesh with a unique UV2 channel and emits
// ChartProviderResult for later kind-5 integration (M2). Tiled UV0 is
// refused; meshlet derivation is not a silent fallback.

namespace chartprov {

enum class ChartProviderStatus : uint32_t {
    Ok = 0,
    EmptyMesh = 1,
    NoUv2 = 2,
    TiledUv = 3,
    PlanarRmsTooHigh = 4,
    BudgetExceeded = 5,
    DegenerateIsland = 6
};

inline const char* statusName(ChartProviderStatus s) {
    switch (s) {
        case ChartProviderStatus::Ok: return "Ok";
        case ChartProviderStatus::EmptyMesh: return "EmptyMesh";
        case ChartProviderStatus::NoUv2: return "NoUv2";
        case ChartProviderStatus::TiledUv: return "TiledUv";
        case ChartProviderStatus::PlanarRmsTooHigh: return "PlanarRmsTooHigh";
        case ChartProviderStatus::BudgetExceeded: return "BudgetExceeded";
        case ChartProviderStatus::DegenerateIsland: return "DegenerateIsland";
    }
    return "Unknown";
}

struct LayoutBudget {
    int logicalWidth = reflayout::kLogicalWidth;
    int bandHeight = reflayout::kBandHeight;
    int primaryPageHeight = reflayout::kPrimaryPageHeight;
    int pageCount = 2;
    float texelScale = reflayout::kTexelScale;
    uint32_t probeAlign = 64;  // C5 probeSize = 2^(5+1)
    int minGutterTexels = 0;
    float planarRmsRelativeLimit = 0.15f;
};

struct MeshCorner {
    glm::vec3 position{0.0f};
    glm::vec2 uv2{0.0f};
};

struct MeshTriangle {
    MeshCorner c[3];
    uint32_t materialId = 1;
};

struct MeshAsset {
    std::string name;
    std::vector<MeshTriangle> triangles;
    std::vector<GpuReferenceMaterial> materials;
};

struct MeshLoadStats {
    bool loaded = false;
    uint32_t vertexCount = 0;
    uint32_t texcoordCount = 0;
    uint32_t triangleCount = 0;
    uint32_t uvOutside01 = 0;
    uint32_t facesMissingUv = 0;
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{0.0f};
    std::string path;
};

struct MeshIslandSource {
    uint32_t islandIndex = 0;
    uint32_t materialId = 1;
    std::vector<uint32_t> triangleIndices;
    glm::vec3 origin{0.0f};
    glm::vec3 tangent{1, 0, 0};
    glm::vec3 bitangent{0, 0, 1};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 extent{0.0f};
    glm::uvec2 resolution{0, 0};
    glm::uvec2 logicalBase{0, 0};
    int handedness = 1;
    glm::vec2 uv2Min{0.0f};
    glm::vec2 uv2Max{1.0f};
    float area = 0.0f;
    float planarRms = 0.0f;
};

struct ExtractResult {
    ChartProviderStatus status = ChartProviderStatus::EmptyMesh;
    std::vector<MeshIslandSource> islands;
    uint32_t uvOutside01 = 0;
    uint32_t overlappingIslandPairs = 0;
};

struct ChartProviderResult {
    ChartProviderStatus status = ChartProviderStatus::EmptyMesh;
    std::vector<GpuReferenceChart> charts;
    std::vector<GpuReferencePrimitive> primitives;
    std::vector<GpuReferenceMaterial> materials;
    GpuReferenceSceneHeader header{};
    std::vector<MeshIslandSource> islands;
};

bool loadMeshObj(const std::string& path, MeshAsset& mesh, MeshLoadStats& stats);

ExtractResult extractIslands(const MeshAsset& mesh, const LayoutBudget& budget);
ChartProviderResult packIslands(const std::vector<MeshIslandSource>& islands,
                                const MeshAsset& mesh, const LayoutBudget& budget);
ChartProviderResult buildCharts(const MeshAsset& mesh, const LayoutBudget& budget);

MeshAsset makeTwoQuadUv2Mesh();
MeshAsset makeTiledUvMesh();
MeshAsset makeFoldedUv2Mesh();
MeshAsset makeFiveUnitUv2Mesh();
std::vector<MeshIslandSource> makeCornellWidthIslands();

}  // namespace chartprov

#endif
