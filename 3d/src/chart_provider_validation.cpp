#include "chart_provider_validation.h"

#include "chart_provider.h"
#include "reference_layout.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

using chartprov::ChartProviderResult;
using chartprov::ChartProviderStatus;
using chartprov::ExtractResult;
using chartprov::LayoutBudget;
using chartprov::MeshAsset;
using chartprov::MeshIslandSource;
using chartprov::MeshLoadStats;

constexpr int kBandHeight = reflayout::kBandHeight;
constexpr int kLogicalWidth = reflayout::kLogicalWidth;
constexpr int kPrimaryPageHeight = reflayout::kPrimaryPageHeight;
constexpr uint32_t kProbeAlign = 64;
constexpr float kTexelScale = reflayout::kTexelScale;

struct Check {
    std::string name;
    bool pass = false;
    std::string detail;
};

struct Report {
    std::vector<Check> checks;
    MeshLoadStats sponza;
    uint32_t twoQuadIslands = 0;
    uint32_t twoQuadCharts = 0;
    uint32_t cornellPacked = 0;
    uint32_t fivePacked = 0;
    int usedPagesFive = 0;
    std::string sponzaPath;
    ChartProviderStatus twoQuadStatus = ChartProviderStatus::EmptyMesh;
    ChartProviderStatus tiledStatus = ChartProviderStatus::EmptyMesh;
    ChartProviderStatus foldedStatus = ChartProviderStatus::EmptyMesh;
    ChartProviderStatus overflowStatus = ChartProviderStatus::EmptyMesh;
    ChartProviderStatus sponzaStatus = ChartProviderStatus::EmptyMesh;

    void add(const std::string& name, bool pass, const std::string& detail) {
        checks.push_back({name, pass, detail});
    }

    bool passed() const {
        return std::all_of(checks.begin(), checks.end(),
                           [](const Check& c) { return c.pass; });
    }
};

std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') {
            o.push_back('\\');
            o.push_back(c);
        } else if (c == '\n') {
            o += "\\n";
        } else {
            o.push_back(c);
        }
    }
    return o;
}

bool framesOrthonormal(const MeshIslandSource& island, float eps = 1.0e-4f) {
    const float ln = std::abs(glm::length(island.normal) - 1.0f);
    const float lt = std::abs(glm::length(island.tangent) - 1.0f);
    const float lb = std::abs(glm::length(island.bitangent) - 1.0f);
    const float dtb = std::abs(glm::dot(island.tangent, island.bitangent));
    const float dtn = std::abs(glm::dot(island.tangent, island.normal));
    const float dbn = std::abs(glm::dot(island.bitangent, island.normal));
    return ln <= eps && lt <= eps && lb <= eps && dtb <= eps && dtn <= eps && dbn <= eps;
}

int r4Failures(const ChartProviderResult& packed, const LayoutBudget& budget) {
    int fails = 0;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const auto& island : packed.islands) {
        if (island.logicalBase.x + island.resolution.x >
            static_cast<uint32_t>(budget.logicalWidth))
            ++fails;
        if (island.logicalBase.y % static_cast<uint32_t>(budget.primaryPageHeight) != 0)
            ++fails;
        if (island.logicalBase.y / static_cast<uint32_t>(budget.primaryPageHeight) >=
            static_cast<uint32_t>(budget.pageCount))
            ++fails;
        if (island.resolution.x % budget.probeAlign != 0)
            ++fails;
        if (island.resolution.y != static_cast<uint32_t>(budget.bandHeight))
            ++fails;
        if (island.resolution.x == 0 ||
            island.logicalBase.x % island.resolution.x != 0)
            ++fails;
        spans.push_back({island.logicalBase.x, island.logicalBase.x + island.resolution.x});
    }
    for (size_t i = 0; i < packed.islands.size(); ++i) {
        for (size_t j = i + 1; j < packed.islands.size(); ++j) {
            if (packed.islands[i].logicalBase.y != packed.islands[j].logicalBase.y)
                continue;
            const uint32_t a0 = packed.islands[i].logicalBase.x;
            const uint32_t a1 = a0 + packed.islands[i].resolution.x;
            const uint32_t b0 = packed.islands[j].logicalBase.x;
            const uint32_t b1 = b0 + packed.islands[j].resolution.x;
            if (a0 < b1 && b0 < a1)
                ++fails;
        }
    }
    return fails;
}

int r5Failures(const ChartProviderResult& packed, const LayoutBudget& budget) {
    int fails = 0;
    if (budget.minGutterTexels <= 0)
        return 0;
    for (size_t i = 0; i < packed.islands.size(); ++i) {
        for (size_t j = i + 1; j < packed.islands.size(); ++j) {
            if (packed.islands[i].logicalBase.y != packed.islands[j].logicalBase.y)
                continue;
            const int a0 = static_cast<int>(packed.islands[i].logicalBase.x);
            const int a1 = a0 + static_cast<int>(packed.islands[i].resolution.x);
            const int b0 = static_cast<int>(packed.islands[j].logicalBase.x);
            const int b1 = b0 + static_cast<int>(packed.islands[j].resolution.x);
            const int gap = a0 < b0 ? b0 - a1 : a0 - b1;
            if (gap < budget.minGutterTexels && gap >= 0)
                ++fails;
        }
    }
    return fails;
}

int r6Failures(const std::vector<MeshIslandSource>& islands, const LayoutBudget& budget) {
    int fails = 0;
    for (const auto& island : islands) {
        const uint32_t expected = std::max(
            budget.probeAlign,
            ((static_cast<uint32_t>(std::ceil(island.extent.x / budget.texelScale - 1.0e-4f)) +
              budget.probeAlign - 1u) /
             budget.probeAlign) *
                budget.probeAlign);
        if (island.resolution.x != expected)
            ++fails;
        if (island.resolution.y != static_cast<uint32_t>(budget.bandHeight))
            ++fails;
    }
    return fails;
}

std::string findSponzaPath() {
    const char* candidates[] = {
        "res/scene/sponza.obj",
        "../res/scene/sponza.obj",
        "3d/res/scene/sponza.obj"};
    for (const char* c : candidates) {
        if (std::filesystem::exists(c))
            return c;
    }
    return "res/scene/sponza.obj";
}

void runChecks(Report& report) {
    LayoutBudget budget;
    budget.probeAlign = kProbeAlign;
    budget.texelScale = kTexelScale;
    budget.logicalWidth = kLogicalWidth;
    budget.bandHeight = kBandHeight;
    budget.primaryPageHeight = kPrimaryPageHeight;
    budget.pageCount = 2;
    budget.minGutterTexels = 0;
    budget.planarRmsRelativeLimit = 0.15f;

    const MeshAsset two = chartprov::makeTwoQuadUv2Mesh();
    const ChartProviderResult twoPacked = chartprov::buildCharts(two, budget);
    report.twoQuadStatus = twoPacked.status;
    report.twoQuadIslands = static_cast<uint32_t>(twoPacked.islands.size());
    report.twoQuadCharts = static_cast<uint32_t>(twoPacked.charts.size());

    report.add("two_quad_status_ok",
               twoPacked.status == ChartProviderStatus::Ok,
               chartprov::statusName(twoPacked.status));
    report.add("two_quad_island_count", twoPacked.islands.size() == 2,
               std::to_string(twoPacked.islands.size()));
    report.add("two_quad_chart_count", twoPacked.charts.size() == 2,
               std::to_string(twoPacked.charts.size()));
    report.add("two_quad_kind5_primitives",
               twoPacked.primitives.size() == 4,
               std::to_string(twoPacked.primitives.size()));

    bool kind0 = true;
    bool kind5 = true;
    uint32_t kind5Count = 0;
    for (const auto& p : twoPacked.primitives) {
        if (p.metadata.y == 0) {
            if (p.metadata.w == 0)
                kind0 = false;
        } else if (p.metadata.y == 5) {
            ++kind5Count;
            if (p.metadata.w == 0)
                kind5 = false;
        } else {
            kind0 = false;
        }
    }
    report.add("two_quad_kind0_and_kind5", kind0 && kind5 && kind5Count == 2,
               "kind5=" + std::to_string(kind5Count));

    bool framesOk = !twoPacked.islands.empty();
    for (const auto& island : twoPacked.islands)
        framesOk = framesOk && framesOrthonormal(island);
    report.add("two_quad_orthonormal_frames", framesOk, framesOk ? "ok" : "fail");

    const int r4Two = r4Failures(twoPacked, budget);
    const int r6Two = r6Failures(twoPacked.islands, budget);
    report.add("two_quad_R4_budget", r4Two == 0, "fails=" + std::to_string(r4Two));
    report.add("two_quad_R6_resolution", r6Two == 0, "fails=" + std::to_string(r6Two));
    report.add("two_quad_res_y_band",
               !twoPacked.islands.empty() &&
                   twoPacked.islands[0].resolution.y == static_cast<uint32_t>(kBandHeight) &&
                   twoPacked.islands[1].resolution.y == static_cast<uint32_t>(kBandHeight),
               twoPacked.islands.empty()
                   ? "empty"
                   : std::to_string(twoPacked.islands[0].resolution.y));
    report.add("two_quad_res_x_256",
               twoPacked.islands.size() == 2 && twoPacked.islands[0].resolution.x == 256 &&
                   twoPacked.islands[1].resolution.x == 256,
               twoPacked.islands.empty()
                   ? "empty"
                   : std::to_string(twoPacked.islands.empty() ? 0
                                                            : twoPacked.islands[0].resolution.x));
    report.add("two_quad_mod_uv_contract",
               twoPacked.islands.size() == 2 &&
                   twoPacked.islands[0].logicalBase.x % twoPacked.islands[0].resolution.x == 0 &&
                   twoPacked.islands[1].logicalBase.x % twoPacked.islands[1].resolution.x == 0,
               "base0=" +
                   std::to_string(twoPacked.islands.empty() ? 0
                                                          : twoPacked.islands[0].logicalBase.x));
    report.add("locked_layout_untouched",
               reflayout::kLogicalWidth == 1024 && reflayout::kLogicalHeight == 3072 &&
                   reflayout::kBandHeight == 256 && reflayout::kTexelScale == 1.0f / 256.0f &&
                   reflayout::kCascadeCount == 6,
               "ok");

    const MeshAsset tiled = chartprov::makeTiledUvMesh();
    const ChartProviderResult tiledPacked = chartprov::buildCharts(tiled, budget);
    report.tiledStatus = tiledPacked.status;
    report.add("tiled_uv_refused",
               tiledPacked.status == ChartProviderStatus::TiledUv &&
                   tiledPacked.charts.empty(),
               chartprov::statusName(tiledPacked.status));

    const MeshAsset folded = chartprov::makeFoldedUv2Mesh();
    const ChartProviderResult foldedPacked = chartprov::buildCharts(folded, budget);
    report.foldedStatus = foldedPacked.status;
    report.add("folded_uv2_planar_refused",
               foldedPacked.status == ChartProviderStatus::PlanarRmsTooHigh &&
                   foldedPacked.charts.empty(),
               chartprov::statusName(foldedPacked.status));

    LayoutBudget gutterBudget = budget;
    gutterBudget.minGutterTexels = 2;
    const ChartProviderResult gutterPacked =
        chartprov::packIslands(twoPacked.islands, two, gutterBudget);
    const int r5 = r5Failures(gutterPacked, gutterBudget);
    uint32_t gutterGap = 0;
    if (gutterPacked.islands.size() == 2 &&
        gutterPacked.islands[0].logicalBase.y == gutterPacked.islands[1].logicalBase.y) {
        const auto& a = gutterPacked.islands[0];
        const auto& b = gutterPacked.islands[1];
        const uint32_t a0 = a.logicalBase.x;
        const uint32_t a1 = a0 + a.resolution.x;
        const uint32_t b0 = b.logicalBase.x;
        const uint32_t b1 = b0 + b.resolution.x;
        gutterGap = a0 < b0 ? b0 - a1 : a0 - b1;
    }
    report.add("gutter_R5_no_bleed",
               gutterPacked.status == ChartProviderStatus::Ok && r5 == 0 &&
                   gutterPacked.islands.size() == 2 && gutterGap >= 2u,
               "gap=" + std::to_string(gutterGap));

    MeshAsset empty;
    empty.name = "empty";
    const ChartProviderResult emptyPacked = chartprov::buildCharts(empty, budget);
    report.add("empty_mesh_refused",
               emptyPacked.status == ChartProviderStatus::EmptyMesh &&
                   emptyPacked.charts.empty(),
               chartprov::statusName(emptyPacked.status));

    const auto cornellIslands = chartprov::makeCornellWidthIslands();
    MeshAsset cornellMesh;
    cornellMesh.name = "cornell-width";
    const ChartProviderResult cornellPacked =
        chartprov::packIslands(cornellIslands, cornellMesh, budget);
    report.cornellPacked = static_cast<uint32_t>(cornellPacked.islands.size());
    bool cornellBases = cornellPacked.status == ChartProviderStatus::Ok &&
                        cornellPacked.islands.size() == 6 &&
                        cornellPacked.islands[0].logicalBase.x == 0 &&
                        cornellPacked.islands[1].logicalBase.x == 256 &&
                        cornellPacked.islands[2].logicalBase.x == 512 &&
                        cornellPacked.islands[3].logicalBase.x == 640 &&
                        cornellPacked.islands[4].logicalBase.x == 768 &&
                        cornellPacked.islands[5].logicalBase.x == 896 &&
                        cornellPacked.islands[0].logicalBase.y == 0;
    report.add("cornell_width_pack_R4", cornellBases && r4Failures(cornellPacked, budget) == 0,
               cornellPacked.islands.empty()
                   ? "empty"
                   : "base5=" + std::to_string(cornellPacked.islands.back().logicalBase.x));

    const MeshAsset five = chartprov::makeFiveUnitUv2Mesh();
    const ChartProviderResult fivePacked = chartprov::buildCharts(five, budget);
    report.fivePacked = static_cast<uint32_t>(fivePacked.islands.size());
    int usedPages = 0;
    for (const auto& island : fivePacked.islands)
        usedPages = std::max(usedPages,
                             static_cast<int>(island.logicalBase.y / kPrimaryPageHeight) + 1);
    report.usedPagesFive = usedPages;
    report.add("five_unit_second_page",
               fivePacked.status == ChartProviderStatus::Ok && fivePacked.islands.size() == 5 &&
                   usedPages == 2 && r4Failures(fivePacked, budget) == 0,
               "pages=" + std::to_string(usedPages) +
                   " islands=" + std::to_string(fivePacked.islands.size()));

    LayoutBudget tight = budget;
    tight.pageCount = 1;
    const ChartProviderResult overflow = chartprov::buildCharts(five, tight);
    report.overflowStatus = overflow.status;
    report.add("overflow_R4_refused",
               overflow.status == ChartProviderStatus::BudgetExceeded &&
                   overflow.charts.empty(),
               chartprov::statusName(overflow.status));

    report.sponzaPath = findSponzaPath();
    MeshAsset sponza;
    const bool loaded = chartprov::loadMeshObj(report.sponzaPath, sponza, report.sponza);
    ChartProviderResult sponzaPacked;
    if (!loaded) {
        report.sponzaStatus = ChartProviderStatus::EmptyMesh;
        report.add("sponza_obj_readable", false, "missing " + report.sponzaPath);
    } else {
        sponzaPacked = chartprov::buildCharts(sponza, budget);
        report.sponzaStatus = sponzaPacked.status;
        const bool noAuthoredUv2 =
            sponzaPacked.status == ChartProviderStatus::TiledUv ||
            sponzaPacked.status == ChartProviderStatus::NoUv2;
        report.add("sponza_obj_readable",
                   report.sponza.loaded && report.sponza.triangleCount > 0,
                   "tris=" + std::to_string(report.sponza.triangleCount) +
                       " vt=" + std::to_string(report.sponza.texcoordCount));
        report.add("sponza_no_authored_uv2", noAuthoredUv2 && sponzaPacked.charts.empty(),
                   std::string(chartprov::statusName(sponzaPacked.status)) +
                       " uv_out=" + std::to_string(report.sponza.uvOutside01));
        report.add("sponza_not_sold_as_surface_rc",
                   sponzaPacked.charts.empty() && sponzaPacked.primitives.empty(),
                   "charts=" + std::to_string(sponzaPacked.charts.size()));
    }
}

bool writeReport(const std::string& path, const Report& report) {
    const std::filesystem::path reportPath(path);
    std::error_code ec;
    if (reportPath.has_parent_path())
        std::filesystem::create_directories(reportPath.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (ec || !out)
        return false;

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"schema_version\": \"chart-provider-m1-report-v1\",\n";
    out << "  \"gate\": \"P11-M1-uv2-packer\",\n";
    out << "  \"result\": \"" << (report.passed() ? "PASS" : "FAIL") << "\",\n";
    out << "  \"gpu_touched\": false,\n";
    out << "  \"parity_layout_constants_unchanged\": true,\n";
    out << "  \"metrics\": {\n";
    out << "    \"two_quad_islands\": " << report.twoQuadIslands << ",\n";
    out << "    \"two_quad_charts\": " << report.twoQuadCharts << ",\n";
    out << "    \"cornell_packed\": " << report.cornellPacked << ",\n";
    out << "    \"five_packed\": " << report.fivePacked << ",\n";
    out << "    \"five_pages\": " << report.usedPagesFive << ",\n";
    out << "    \"two_quad_status\": \"" << chartprov::statusName(report.twoQuadStatus)
        << "\",\n";
    out << "    \"tiled_status\": \"" << chartprov::statusName(report.tiledStatus) << "\",\n";
    out << "    \"folded_status\": \"" << chartprov::statusName(report.foldedStatus)
        << "\",\n";
    out << "    \"overflow_status\": \"" << chartprov::statusName(report.overflowStatus)
        << "\",\n";
    out << "    \"sponza_status\": \"" << chartprov::statusName(report.sponzaStatus) << "\"\n";
    out << "  },\n";
    out << "  \"sponza\": {\n";
    out << "    \"path\": \"" << jsonEscape(report.sponzaPath) << "\",\n";
    out << "    \"loaded\": " << (report.sponza.loaded ? "true" : "false") << ",\n";
    out << "    \"vertex_count\": " << report.sponza.vertexCount << ",\n";
    out << "    \"texcoord_count\": " << report.sponza.texcoordCount << ",\n";
    out << "    \"triangle_count\": " << report.sponza.triangleCount << ",\n";
    out << "    \"uv_outside_01\": " << report.sponza.uvOutside01 << ",\n";
    out << "    \"faces_missing_uv\": " << report.sponza.facesMissingUv << ",\n";
    out << "    \"uv_min\": [" << report.sponza.uvMin.x << ", " << report.sponza.uvMin.y
        << "],\n";
    out << "    \"uv_max\": [" << report.sponza.uvMax.x << ", " << report.sponza.uvMax.y
        << "],\n";
    out << "    \"authored_uv2\": false,\n";
    out << "    \"q5\": \"OBJ has a single tiled albedo vt channel (range outside [0,1]); "
           "not unique UV2. Meshlet fallback is not used. Author a UV2 pass before M3.\"\n";
    out << "  },\n";
    out << "  \"checks\": [\n";
    for (size_t i = 0; i < report.checks.size(); ++i) {
        const auto& c = report.checks[i];
        out << "    {\"name\": \"" << jsonEscape(c.name) << "\", \"result\": \""
            << (c.pass ? "PASS" : "FAIL") << "\", \"detail\": \"" << jsonEscape(c.detail)
            << "\"}";
        out << (i + 1 == report.checks.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"notes\": [\n";
    out << "    \"M1 is CPU-only. No dispatch, no shader, no G0-G10 rerun required.\",\n";
    out << "    \"R4: charts pack into 1024 x 256 band templates on two pages (y=0 and "
           "y=1536).\",\n";
    out << "    \"R5: optional minGutterTexels; default 0 matches Cornell adjacency. LINEAR "
           "bleed is owned by unowned gutter texels.\",\n";
    out << "    \"R6: resolution.x = max(64, align_up(extent.x / texelScale, 64)); "
           "resolution.y locked to bandHeight 256 for probe coupling (R7).\",\n";
    out << "    \"decodeProbe uses mod(uv, gRes); logicalBase.x must be a multiple of "
           "resolution.x.\",\n";
    out << "    \"Sponza fail-closed: tiled UV0 is not UV2. Do not claim general surface "
           "RC.\"\n";
    out << "  ]\n";
    out << "}\n";
    return out.good();
}

}  // namespace

bool runChartProviderValidation(const std::string& reportPath) {
    Report report;
    runChecks(report);
    const bool written = writeReport(reportPath, report);
    return written && report.passed();
}
