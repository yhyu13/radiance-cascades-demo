#include "chart_provider.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace chartprov {
namespace {

constexpr float kUvEps = 1.0e-4f;
constexpr float kAreaEps = 1.0e-12f;
constexpr int64_t kUvQuant = 100000;

ReferenceFloat4 f4(const glm::vec3& v, float w = 0.0f) {
    return {v.x, v.y, v.z, w};
}

ReferenceUint4 u4(uint32_t x, uint32_t y = 0, uint32_t z = 0, uint32_t w = 0) {
    return {x, y, z, w};
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0)
        return value;
    return (value + alignment - 1u) / alignment * alignment;
}

int64_t quantizeUv(float v) {
    return static_cast<int64_t>(std::llround(static_cast<double>(v) * kUvQuant));
}

uint64_t packPoint(int64_t x, int64_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(y));
}

uint64_t edgeKey(glm::vec2 a, glm::vec2 b) {
    const int64_t ax = quantizeUv(a.x);
    const int64_t ay = quantizeUv(a.y);
    const int64_t bx = quantizeUv(b.x);
    const int64_t by = quantizeUv(b.y);
    const uint64_t pa = packPoint(ax, ay);
    const uint64_t pb = packPoint(bx, by);
    const uint64_t lo = pa < pb ? pa : pb;
    const uint64_t hi = pa < pb ? pb : pa;
    return lo * 6364136223846793005ull + hi;
}

struct UnionFind {
    std::vector<int> parent;
    explicit UnionFind(int n) : parent(n) {
        for (int i = 0; i < n; ++i)
            parent[i] = i;
    }
    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void merge(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[b] = a;
    }
};

bool uvInside01(const glm::vec2& uv) {
    return uv.x >= -kUvEps && uv.x <= 1.0f + kUvEps &&
           uv.y >= -kUvEps && uv.y <= 1.0f + kUvEps;
}

bool aabbOverlapOpen(const glm::vec2& aMin, const glm::vec2& aMax,
                     const glm::vec2& bMin, const glm::vec2& bMax) {
    return aMin.x < bMax.x - kUvEps && bMin.x < aMax.x - kUvEps &&
           aMin.y < bMax.y - kUvEps && bMin.y < aMax.y - kUvEps;
}

void orthonormalize(glm::vec3& tangent, glm::vec3& bitangent, glm::vec3& normal) {
    const float nLen = glm::length(normal);
    if (nLen < 1.0e-8f)
        normal = glm::vec3(0, 1, 0);
    else
        normal /= nLen;

    tangent -= normal * glm::dot(tangent, normal);
    float tLen = glm::length(tangent);
    if (tLen < 1.0e-8f) {
        const glm::vec3 axis = std::abs(normal.y) < 0.9f ? glm::vec3(0, 1, 0)
                                                         : glm::vec3(1, 0, 0);
        tangent = glm::normalize(glm::cross(axis, normal));
        tLen = 1.0f;
    } else {
        tangent /= tLen;
    }
    bitangent = glm::cross(normal, tangent);
    const float bLen = glm::length(bitangent);
    if (bLen < 1.0e-8f)
        bitangent = glm::normalize(glm::cross(normal, tangent));
    else
        bitangent /= bLen;
    tangent = glm::cross(bitangent, normal);
}

void fillIslandFrame(MeshIslandSource& island, const MeshAsset& mesh,
                     const LayoutBudget& budget) {
    glm::vec3 weightedNormal(0.0f);
    glm::vec3 centroid(0.0f);
    float areaSum = 0.0f;
    glm::vec2 uvMin(std::numeric_limits<float>::max());
    glm::vec2 uvMax(std::numeric_limits<float>::lowest());

    for (uint32_t ti : island.triangleIndices) {
        const MeshTriangle& tri = mesh.triangles[ti];
        const glm::vec3 e0 = tri.c[1].position - tri.c[0].position;
        const glm::vec3 e1 = tri.c[2].position - tri.c[0].position;
        const glm::vec3 n = glm::cross(e0, e1);
        const float area = 0.5f * glm::length(n);
        weightedNormal += n;
        const glm::vec3 triCentroid =
            (tri.c[0].position + tri.c[1].position + tri.c[2].position) / 3.0f;
        centroid += triCentroid * area;
        areaSum += area;
        for (int k = 0; k < 3; ++k) {
            uvMin = glm::min(uvMin, tri.c[k].uv2);
            uvMax = glm::max(uvMax, tri.c[k].uv2);
        }
    }
    island.area = areaSum;
    island.uv2Min = uvMin;
    island.uv2Max = uvMax;
    if (areaSum > kAreaEps)
        centroid /= areaSum;
    island.normal = weightedNormal;

    // UV2-derived tangent space (not PCA). A square island is isotropic in
    // 3D covariance, so PCA would pick an arbitrary in-plane axis and inflate
    // the AABB. dP/dU, dP/dV follow the authored parameterization.
    glm::vec3 tangent(0.0f);
    glm::vec3 bitangent(0.0f);
    for (uint32_t ti : island.triangleIndices) {
        const MeshTriangle& tri = mesh.triangles[ti];
        const glm::vec3 e1 = tri.c[1].position - tri.c[0].position;
        const glm::vec3 e2 = tri.c[2].position - tri.c[0].position;
        const float u1 = tri.c[1].uv2.x - tri.c[0].uv2.x;
        const float v1 = tri.c[1].uv2.y - tri.c[0].uv2.y;
        const float u2 = tri.c[2].uv2.x - tri.c[0].uv2.x;
        const float v2 = tri.c[2].uv2.y - tri.c[0].uv2.y;
        const float det = u1 * v2 - u2 * v1;
        if (std::abs(det) < 1.0e-12f)
            continue;
        const float r = 1.0f / det;
        const float area = 0.5f * glm::length(glm::cross(e1, e2));
        tangent += (e1 * v2 - e2 * v1) * r * area;
        bitangent += (e2 * u1 - e1 * u2) * r * area;
    }
    if (glm::length(tangent) < 1.0e-8f)
        tangent = glm::vec3(1, 0, 0);
    island.tangent = tangent;
    island.bitangent = glm::length(bitangent) < 1.0e-8f ? glm::vec3(0, 0, 1) : bitangent;
    orthonormalize(island.tangent, island.bitangent, island.normal);
    island.origin = centroid;

    float uMin = std::numeric_limits<float>::max();
    float uMax = std::numeric_limits<float>::lowest();
    float vMin = std::numeric_limits<float>::max();
    float vMax = std::numeric_limits<float>::lowest();
    double rmsAcc = 0.0;
    int rmsCount = 0;
    for (uint32_t ti : island.triangleIndices) {
        const MeshTriangle& tri = mesh.triangles[ti];
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 rel = tri.c[k].position - island.origin;
            uMin = std::min(uMin, glm::dot(rel, island.tangent));
            uMax = std::max(uMax, glm::dot(rel, island.tangent));
            vMin = std::min(vMin, glm::dot(rel, island.bitangent));
            vMax = std::max(vMax, glm::dot(rel, island.bitangent));
            rmsAcc += static_cast<double>(glm::dot(rel, island.normal)) *
                      static_cast<double>(glm::dot(rel, island.normal));
            ++rmsCount;
        }
    }
    island.planarRms = rmsCount > 0
                           ? static_cast<float>(std::sqrt(rmsAcc / rmsCount))
                           : 0.0f;

    const float du = std::max(0.0f, uMax - uMin);
    const float dv = std::max(0.0f, vMax - vMin);
    island.origin = island.origin + island.tangent * uMin + island.bitangent * vMin;
    island.extent = glm::vec2(du, dv);
    island.handedness =
        glm::dot(glm::cross(island.tangent, island.bitangent), island.normal) > 0.0f
            ? 1
            : -1;

    const uint32_t texelsU = std::max(
        1u, static_cast<uint32_t>(std::ceil(du / budget.texelScale - 1.0e-4f)));
    island.resolution.x = std::max(budget.probeAlign, alignUp(texelsU, budget.probeAlign));
    island.resolution.y = static_cast<uint32_t>(budget.bandHeight);
}

GpuReferenceChart toGpuChart(const MeshIslandSource& island, uint32_t chartId,
                             const LayoutBudget& budget) {
    const glm::vec2 chartExtent(static_cast<float>(island.resolution.x) * budget.texelScale,
                                static_cast<float>(island.resolution.y) * budget.texelScale);
    GpuReferenceChart chart{};
    chart.metadata = u4(chartId, island.materialId, 1u,
                        static_cast<uint32_t>(island.handedness > 0));
    chart.resolutionAndBase = u4(island.resolution.x, island.resolution.y,
                                 island.logicalBase.x, island.logicalBase.y);
    chart.originAndExtentU = f4(island.origin, chartExtent.x);
    chart.tangentAndExtentV = f4(island.tangent, chartExtent.y);
    chart.bitangentAndTexelScaleU = f4(island.bitangent, budget.texelScale);
    chart.normalAndTexelScaleV = f4(island.normal, budget.texelScale);
    return chart;
}

GpuReferencePrimitive toKind0Primitive(const MeshIslandSource& island, uint32_t primId,
                                       uint32_t chartId, const LayoutBudget& budget) {
    const glm::vec2 chartExtent(static_cast<float>(island.resolution.x) * budget.texelScale,
                                static_cast<float>(island.resolution.y) * budget.texelScale);
    GpuReferencePrimitive prim{};
    prim.metadata = u4(primId, 0, island.materialId, chartId);
    prim.data0 = f4(island.origin);
    prim.data1 = f4(island.tangent, chartExtent.x);
    prim.data2 = f4(island.bitangent, chartExtent.y);
    prim.data3 = f4(island.normal);
    return prim;
}

GpuReferencePrimitive toKind5Primitive(const MeshIslandSource& island, uint32_t primId,
                                       uint32_t chartId) {
    const uint32_t first =
        island.triangleIndices.empty() ? 0u : island.triangleIndices.front();
    GpuReferencePrimitive prim{};
    prim.metadata = u4(primId, 5, island.materialId, chartId);
    prim.data0 = {static_cast<float>(first),
                  static_cast<float>(island.triangleIndices.size()), 0.0f, 0.0f};
    prim.data1 = {};
    prim.data2 = {};
    prim.data3 = {};
    return prim;
}

void appendQuad(MeshAsset& mesh, const glm::vec3& o, const glm::vec3& t,
                const glm::vec3& b, const glm::vec2& uv0, const glm::vec2& uv1,
                uint32_t materialId) {
    MeshTriangle a;
    a.materialId = materialId;
    a.c[0] = {o, uv0};
    a.c[1] = {o + t, {uv1.x, uv0.y}};
    a.c[2] = {o + t + b, uv1};
    MeshTriangle c;
    c.materialId = materialId;
    c.c[0] = {o, uv0};
    c.c[1] = {o + t + b, uv1};
    c.c[2] = {o + b, {uv0.x, uv1.y}};
    mesh.triangles.push_back(a);
    mesh.triangles.push_back(c);
}

}  // namespace

bool loadMeshObj(const std::string& path, MeshAsset& mesh, MeshLoadStats& stats) {
    stats = {};
    stats.path = path;
    mesh = {};
    mesh.name = path;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::unordered_map<std::string, uint32_t> materialIds;
    uint32_t currentMaterial = 1;
    mesh.materials.push_back({u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
                              f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))});
    materialIds[""] = 1;

    glm::vec2 uvMin(std::numeric_limits<float>::max());
    glm::vec2 uvMax(std::numeric_limits<float>::lowest());
    uint32_t uvOutside = 0;
    uint32_t missingUv = 0;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            uvs.push_back(uv);
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
            if (!uvInside01(uv))
                ++uvOutside;
        } else if (tag == "usemtl") {
            std::string name;
            iss >> name;
            auto it = materialIds.find(name);
            if (it == materialIds.end()) {
                const uint32_t id = static_cast<uint32_t>(mesh.materials.size()) + 1u;
                materialIds[name] = id;
                mesh.materials.push_back(
                    {u4(id, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
                     f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))});
                currentMaterial = id;
            } else {
                currentMaterial = it->second;
            }
        } else if (tag == "f") {
            std::vector<int> pv;
            std::vector<int> tv;
            std::string tok;
            while (iss >> tok) {
                int v = 0, t = -1;
                const size_t slash = tok.find('/');
                if (slash == std::string::npos) {
                    v = std::atoi(tok.c_str());
                } else {
                    v = std::atoi(tok.substr(0, slash).c_str());
                    const size_t slash2 = tok.find('/', slash + 1);
                    const std::string tpart =
                        slash2 == std::string::npos
                            ? tok.substr(slash + 1)
                            : tok.substr(slash + 1, slash2 - slash - 1);
                    if (!tpart.empty())
                        t = std::atoi(tpart.c_str());
                }
                if (v < 0)
                    v = static_cast<int>(positions.size()) + v + 1;
                if (t < 0 && t != -1)
                    t = static_cast<int>(uvs.size()) + t + 1;
                pv.push_back(v - 1);
                tv.push_back(t == -1 ? -1 : t - 1);
            }
            const int npos = static_cast<int>(positions.size());
            const int nuv = static_cast<int>(uvs.size());
            for (size_t k = 1; k + 1 < pv.size(); ++k) {
                const int i0 = pv[0], i1 = pv[k], i2 = pv[k + 1];
                if (i0 < 0 || i0 >= npos || i1 < 0 || i1 >= npos || i2 < 0 || i2 >= npos)
                    continue;
                MeshTriangle tri;
                tri.materialId = currentMaterial;
                const int ts[3] = {tv[0], tv[k], tv[k + 1]};
                const int is[3] = {i0, i1, i2};
                bool missing = false;
                for (int c = 0; c < 3; ++c) {
                    tri.c[c].position = positions[is[c]];
                    if (ts[c] < 0 || ts[c] >= nuv) {
                        tri.c[c].uv2 = glm::vec2(-1.0f);
                        missing = true;
                    } else {
                        tri.c[c].uv2 = uvs[ts[c]];
                    }
                }
                if (missing)
                    ++missingUv;
                mesh.triangles.push_back(tri);
            }
        }
    }

    stats.loaded = true;
    stats.vertexCount = static_cast<uint32_t>(positions.size());
    stats.texcoordCount = static_cast<uint32_t>(uvs.size());
    stats.triangleCount = static_cast<uint32_t>(mesh.triangles.size());
    stats.uvOutside01 = uvOutside;
    stats.facesMissingUv = missingUv;
    if (!uvs.empty()) {
        stats.uvMin = uvMin;
        stats.uvMax = uvMax;
    }
    return true;
}

ExtractResult extractIslands(const MeshAsset& mesh, const LayoutBudget& budget) {
    ExtractResult out;
    if (mesh.triangles.empty()) {
        out.status = ChartProviderStatus::EmptyMesh;
        return out;
    }

    const int n = static_cast<int>(mesh.triangles.size());
    uint32_t uvOutside = 0;
    uint32_t missingUv = 0;
    UnionFind uf(n);
    std::unordered_map<uint64_t, std::pair<int, uint32_t>> edgeOwner;
    edgeOwner.reserve(static_cast<size_t>(n) * 3u);

    std::unordered_map<uint64_t, glm::vec3> uvToWorld;
    uvToWorld.reserve(static_cast<size_t>(n) * 3u);
    bool uvNotInjective = false;
    for (int i = 0; i < n; ++i) {
        const MeshTriangle& tri = mesh.triangles[i];
        for (int k = 0; k < 3; ++k) {
            if (tri.c[k].uv2.x == -1.0f && tri.c[k].uv2.y == -1.0f)
                ++missingUv;
            else if (!uvInside01(tri.c[k].uv2))
                ++uvOutside;
            const uint64_t pkey =
                packPoint(quantizeUv(tri.c[k].uv2.x), quantizeUv(tri.c[k].uv2.y));
            auto it = uvToWorld.find(pkey);
            if (it == uvToWorld.end()) {
                uvToWorld.emplace(pkey, tri.c[k].position);
            } else if (glm::length(it->second - tri.c[k].position) > 1.0e-3f) {
                uvNotInjective = true;
            }
        }
        for (int e = 0; e < 3; ++e) {
            const glm::vec2 a = tri.c[e].uv2;
            const glm::vec2 b = tri.c[(e + 1) % 3].uv2;
            const uint64_t key = edgeKey(a, b) ^ (static_cast<uint64_t>(tri.materialId) << 1);
            auto it = edgeOwner.find(key);
            if (it == edgeOwner.end()) {
                edgeOwner.emplace(key, std::make_pair(i, tri.materialId));
            } else if (it->second.second == tri.materialId) {
                uf.merge(it->second.first, i);
            }
        }
    }
    out.uvOutside01 = uvOutside;
    if (missingUv > 0) {
        out.status = ChartProviderStatus::NoUv2;
        return out;
    }
    if (uvOutside > 0 || uvNotInjective) {
        out.status = ChartProviderStatus::TiledUv;
        return out;
    }

    std::unordered_map<int, std::vector<uint32_t>> groups;
    for (int i = 0; i < n; ++i)
        groups[uf.find(i)].push_back(static_cast<uint32_t>(i));

    out.islands.reserve(groups.size());
    uint32_t index = 0;
    for (auto& [root, tris] : groups) {
        (void)root;
        MeshIslandSource island;
        island.islandIndex = index++;
        island.triangleIndices = std::move(tris);
        island.materialId = mesh.triangles[island.triangleIndices.front()].materialId;
        fillIslandFrame(island, mesh, budget);
        if (island.area <= kAreaEps || island.extent.x <= kUvEps ||
            island.extent.y <= kUvEps) {
            out.status = ChartProviderStatus::DegenerateIsland;
            out.islands.clear();
            return out;
        }
        out.islands.push_back(std::move(island));
    }

    std::sort(out.islands.begin(), out.islands.end(),
              [](const MeshIslandSource& a, const MeshIslandSource& b) {
                  if (a.resolution.x != b.resolution.x)
                      return a.resolution.x > b.resolution.x;
                  return a.islandIndex < b.islandIndex;
              });
    for (uint32_t i = 0; i < out.islands.size(); ++i)
        out.islands[i].islandIndex = i;

    for (size_t i = 0; i < out.islands.size(); ++i) {
        const float span = std::max(out.islands[i].extent.x, out.islands[i].extent.y);
        if (span > kUvEps &&
            out.islands[i].planarRms / span > budget.planarRmsRelativeLimit) {
            out.status = ChartProviderStatus::PlanarRmsTooHigh;
            return out;
        }
        for (size_t j = i + 1; j < out.islands.size(); ++j) {
            if (aabbOverlapOpen(out.islands[i].uv2Min, out.islands[i].uv2Max,
                                out.islands[j].uv2Min, out.islands[j].uv2Max))
                ++out.overlappingIslandPairs;
        }
    }
    if (out.overlappingIslandPairs > 0) {
        out.status = ChartProviderStatus::TiledUv;
        return out;
    }

    out.status = ChartProviderStatus::Ok;
    return out;
}

ChartProviderResult packIslands(const std::vector<MeshIslandSource>& islands,
                                const MeshAsset& mesh, const LayoutBudget& budget) {
    ChartProviderResult result;
    result.materials = mesh.materials;
    if (result.materials.empty()) {
        result.materials.push_back(
            {u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
             f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))});
    }
    if (islands.empty()) {
        result.status = ChartProviderStatus::EmptyMesh;
        return result;
    }

    std::vector<MeshIslandSource> packed = islands;
    uint32_t cursor = 0;
    int page = 0;
    const uint32_t logicalW = static_cast<uint32_t>(budget.logicalWidth);

    for (auto& island : packed) {
        if (island.resolution.y == 0)
            island.resolution.y = static_cast<uint32_t>(budget.bandHeight);
        if (island.resolution.x == 0) {
            const uint32_t texelsU = std::max(
                1u, static_cast<uint32_t>(
                        std::ceil(island.extent.x / budget.texelScale - 1.0e-4f)));
            island.resolution.x =
                std::max(budget.probeAlign, alignUp(texelsU, budget.probeAlign));
        }
        if (island.resolution.x > logicalW) {
            result.status = ChartProviderStatus::BudgetExceeded;
            result.islands = std::move(packed);
            return result;
        }

        const uint32_t width = island.resolution.x;
        uint32_t placed = alignUp(cursor, width);
        if (cursor > 0 && budget.minGutterTexels > 0) {
            const uint32_t withGutter = cursor + static_cast<uint32_t>(budget.minGutterTexels);
            placed = alignUp(withGutter, width);
        }
        if (placed + width > logicalW) {
            ++page;
            cursor = 0;
            placed = 0;
            if (page >= budget.pageCount) {
                result.status = ChartProviderStatus::BudgetExceeded;
                result.islands = std::move(packed);
                return result;
            }
        }
        island.logicalBase.x = placed;
        island.logicalBase.y = static_cast<uint32_t>(page * budget.primaryPageHeight);
        cursor = placed + width;
    }

    result.islands = packed;
    result.charts.reserve(packed.size());
    result.primitives.reserve(packed.size() * 2);
    for (uint32_t i = 0; i < packed.size(); ++i) {
        const uint32_t chartId = i + 1;
        result.charts.push_back(toGpuChart(packed[i], chartId, budget));
        result.primitives.push_back(toKind0Primitive(packed[i], i + 1, chartId, budget));
    }
    for (uint32_t i = 0; i < packed.size(); ++i) {
        const uint32_t chartId = i + 1;
        const uint32_t primId = static_cast<uint32_t>(packed.size()) + i + 1;
        result.primitives.push_back(toKind5Primitive(packed[i], primId, chartId));
    }

    result.header.identity = u4(1, 2, 1, 0);
    result.header.counts = u4(static_cast<uint32_t>(result.primitives.size()),
                              static_cast<uint32_t>(result.charts.size()),
                              static_cast<uint32_t>(result.materials.size()), 0);
    result.header.referenceConstants = {0.0f, budget.texelScale, 0.0f, 0.0f};
    result.status = ChartProviderStatus::Ok;
    return result;
}

ChartProviderResult buildCharts(const MeshAsset& mesh, const LayoutBudget& budget) {
    const ExtractResult extracted = extractIslands(mesh, budget);
    if (extracted.status != ChartProviderStatus::Ok) {
        ChartProviderResult failed;
        failed.status = extracted.status;
        failed.materials = mesh.materials;
        failed.islands = extracted.islands;
        return failed;
    }
    return packIslands(extracted.islands, mesh, budget);
}

MeshAsset makeTwoQuadUv2Mesh() {
    MeshAsset mesh;
    mesh.name = "two-quad-uv2";
    mesh.materials = {
        {u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
         f4(glm::vec3(0.9f)), f4(glm::vec3(0.0f))},
        {u4(2, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
         f4(glm::vec3(0.2f, 0.8f, 0.2f)), f4(glm::vec3(0.0f))}};
    appendQuad(mesh, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0.00f, 0.00f}, {0.40f, 0.40f}, 1);
    appendQuad(mesh, {0, 0, 2}, {1, 0, 0}, {0, 0, 1}, {0.60f, 0.60f}, {1.00f, 1.00f}, 2);
    return mesh;
}

MeshAsset makeTiledUvMesh() {
    MeshAsset mesh;
    mesh.name = "tiled-uv";
    mesh.materials = {
        {u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
         f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))}};
    appendQuad(mesh, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1}, 1);
    appendQuad(mesh, {0, 0, 2}, {1, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1}, 1);
    return mesh;
}

MeshAsset makeFoldedUv2Mesh() {
    MeshAsset mesh;
    mesh.name = "folded-uv2";
    mesh.materials = {
        {u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
         f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))}};
    MeshTriangle t0;
    t0.materialId = 1;
    t0.c[0] = {{0, 0, 0}, {0, 0}};
    t0.c[1] = {{1, 0, 0}, {1, 0}};
    t0.c[2] = {{0, 1, 0}, {0, 1}};
    MeshTriangle t1;
    t1.materialId = 1;
    t1.c[0] = {{0, 0, 0}, {0, 0}};
    t1.c[1] = {{1, 0, 0}, {1, 0}};
    t1.c[2] = {{0, 0, 1}, {1, 1}};
    mesh.triangles.push_back(t0);
    mesh.triangles.push_back(t1);
    return mesh;
}

MeshAsset makeFiveUnitUv2Mesh() {
    MeshAsset mesh;
    mesh.name = "five-unit-uv2";
    mesh.materials = {
        {u4(1, static_cast<uint32_t>(ReferenceMaterialKind::Diffuse)),
         f4(glm::vec3(0.8f)), f4(glm::vec3(0.0f))}};
    for (int i = 0; i < 5; ++i) {
        const float u0 = static_cast<float>(i) * 0.18f;
        const float u1 = u0 + 0.16f;
        appendQuad(mesh, {static_cast<float>(i) * 2.0f, 0, 0}, {1, 0, 0}, {0, 0, 1},
                   {u0, 0.0f}, {u1, 0.16f}, 1);
    }
    return mesh;
}

std::vector<MeshIslandSource> makeCornellWidthIslands() {
    const float extentsU[6] = {1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<MeshIslandSource> islands;
    islands.reserve(6);
    for (uint32_t i = 0; i < 6; ++i) {
        MeshIslandSource island;
        island.islandIndex = i;
        island.materialId = 1;
        island.origin = glm::vec3(0.0f);
        island.tangent = glm::vec3(1, 0, 0);
        island.bitangent = glm::vec3(0, 0, 1);
        island.normal = glm::vec3(0, 1, 0);
        island.extent = glm::vec2(extentsU[i], 1.0f);
        island.resolution = glm::uvec2(i < 2 ? 256u : 128u, 256u);
        island.handedness = 1;
        island.area = extentsU[i];
        islands.push_back(island);
    }
    return islands;
}

}  // namespace chartprov
