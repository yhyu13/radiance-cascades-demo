/**
 * @file validation_helpers.cpp
 * @brief Phase 3 validation helpers for surface-attached RC chart mapping.
 */

#include "demo3d.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

struct ChartRoundTripStats {
    int chartID = 0;
    int tested = 0;
    int passed = 0;
    float maxUError = 0.0f;
    float maxVError = 0.0f;
};

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool ensureParentDirectory(const std::string& filename) {
    const std::filesystem::path path(filename);
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty())
        return true;
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

}  // namespace

bool Demo3D::validateUVRoundTrip(const std::string& metricsPath) {
    if (!surfaceRC) {
        std::cerr << "[Phase 3] ERROR: SurfaceRC is not initialized.\n";
        return false;
    }

    surfaceRC->updateScene(currentOBJPath, currentObjBmin, currentObjBmax, useOBJMesh);

    constexpr int testsPerChart = 100;
    constexpr float tolerance = 0.01f;
    constexpr unsigned seed = 1337;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uvDist(0.001f, 0.999f);

    int totalTests = 0;
    int passedTests = 0;
    std::vector<ChartRoundTripStats> chartStats;
    std::vector<std::string> failures;

    std::cout << "\n[Phase 3] UV round-trip validation\n";
    std::cout << "  scene=" << surfaceRC->getSceneLabel()
              << " activeCharts=" << surfaceRC->getActiveChartCount()
              << " testsPerChart=" << testsPerChart
              << " seed=" << seed << "\n";

    for (int chartID = 1; chartID <= surfaceRC->getMaxChartID(); ++chartID) {
        if (!surfaceRC->isChartActive(chartID))
            continue;

        ChartRoundTripStats stats;
        stats.chartID = chartID;

        for (int i = 0; i < testsPerChart; ++i) {
            const float u = uvDist(rng);
            const float v = uvDist(rng);
            const glm::vec3 worldPos = surfaceRC->chartToWorld(chartID, u, v);

            int recoveredChartID = 0;
            float recoveredU = 0.0f;
            float recoveredV = 0.0f;
            const bool mapped = surfaceRC->worldToChart(worldPos, recoveredChartID, recoveredU, recoveredV);

            ++stats.tested;
            ++totalTests;

            const float uError = std::abs(recoveredU - u);
            const float vError = std::abs(recoveredV - v);
            stats.maxUError = std::max(stats.maxUError, uError);
            stats.maxVError = std::max(stats.maxVError, vError);

            const bool pass = mapped &&
                              recoveredChartID == chartID &&
                              uError <= tolerance &&
                              vError <= tolerance;
            if (pass) {
                ++stats.passed;
                ++passedTests;
                continue;
            }

            if (failures.size() < 12) {
                failures.push_back(
                    "chart " + std::to_string(chartID) +
                    " uv=(" + std::to_string(u) + "," + std::to_string(v) + ")" +
                    " mapped=" + (mapped ? "true" : "false") +
                    " recoveredChart=" + std::to_string(recoveredChartID) +
                    " recoveredUV=(" + std::to_string(recoveredU) + "," + std::to_string(recoveredV) + ")");
            }
        }

        chartStats.push_back(stats);
        std::cout << "  chart " << chartID << ": " << stats.passed << "/" << stats.tested
                  << " maxErr=(" << stats.maxUError << "," << stats.maxVError << ")\n";
    }

    const float passRate = totalTests > 0
        ? static_cast<float>(passedTests) / static_cast<float>(totalTests)
        : 0.0f;
    const bool success = totalTests > 0 && passRate >= 0.99f;

    if (!failures.empty()) {
        std::cout << "  first failures:\n";
        for (const std::string& failure : failures)
            std::cout << "    " << failure << "\n";
    }

    std::cout << "  total=" << totalTests
              << " passed=" << passedTests
              << " failed=" << (totalTests - passedTests)
              << " passRate=" << (passRate * 100.0f) << "%"
              << " result=" << (success ? "PASS" : "FAIL") << "\n";

    if (!metricsPath.empty()) {
        if (!ensureParentDirectory(metricsPath)) {
            std::cerr << "[Phase 3] ERROR: could not create metrics parent directory for "
                      << metricsPath << "\n";
            return false;
        }

        std::ofstream out(metricsPath, std::ios::trunc);
        if (!out) {
            std::cerr << "[Phase 3] ERROR: could not write metrics JSON: "
                      << metricsPath << "\n";
            return false;
        }

        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"test\": \"surface_uv_roundtrip\",\n";
        out << "  \"scene\": \"" << escapeJson(surfaceRC->getSceneLabel()) << "\",\n";
        out << "  \"surface_rc_enabled\": " << (surfaceRC->isEnabled() ? "true" : "false") << ",\n";
        out << "  \"active_chart_count\": " << surfaceRC->getActiveChartCount() << ",\n";
        out << "  \"tests_per_chart\": " << testsPerChart << ",\n";
        out << "  \"total_tests\": " << totalTests << ",\n";
        out << "  \"passed\": " << passedTests << ",\n";
        out << "  \"failed\": " << (totalTests - passedTests) << ",\n";
        out << "  \"pass_rate\": " << passRate << ",\n";
        out << "  \"tolerance\": " << tolerance << ",\n";
        out << "  \"seed\": " << seed << ",\n";
        out << "  \"result\": \"" << (success ? "PASS" : "FAIL") << "\",\n";
        out << "  \"charts\": [\n";
        for (size_t i = 0; i < chartStats.size(); ++i) {
            const auto& stats = chartStats[i];
            const float chartPassRate = stats.tested > 0
                ? static_cast<float>(stats.passed) / static_cast<float>(stats.tested)
                : 0.0f;
            out << "    {\"chart_id\": " << stats.chartID
                << ", \"tested\": " << stats.tested
                << ", \"passed\": " << stats.passed
                << ", \"failed\": " << (stats.tested - stats.passed)
                << ", \"pass_rate\": " << chartPassRate
                << ", \"max_u_error\": " << stats.maxUError
                << ", \"max_v_error\": " << stats.maxVError << "}";
            if (i + 1 < chartStats.size())
                out << ",";
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";

        std::cout << "  metrics=" << metricsPath << "\n";
    }

    return success;
}

void Demo3D::measureMisclassificationRate(int numSamples) {
    if (!surfaceRC) {
        std::cerr << "[Phase 3] ERROR: SurfaceRC is not initialized.\n";
        return;
    }

    surfaceRC->updateScene(currentOBJPath, currentObjBmin, currentObjBmax, useOBJMesh);

    std::vector<int> activeCharts;
    for (int chartID = 1; chartID <= surfaceRC->getMaxChartID(); ++chartID) {
        if (surfaceRC->isChartActive(chartID))
            activeCharts.push_back(chartID);
    }

    if (activeCharts.empty() || numSamples <= 0) {
        std::cerr << "[Phase 3] No active charts or invalid sample count.\n";
        return;
    }

    struct Stats {
        int total = 0;
        int correct = 0;
        int missed = 0;
        std::map<int, int> perChartTotal;
        std::map<int, int> perChartMissed;
    } stats;

    std::mt19937 rng(7331);
    std::uniform_int_distribution<int> chartPick(0, static_cast<int>(activeCharts.size()) - 1);
    std::uniform_real_distribution<float> uvDist(0.02f, 0.98f);
    std::uniform_real_distribution<float> offsetDist(-0.01f, 0.01f);

    for (int i = 0; i < numSamples; ++i) {
        const int trueChartID = activeCharts[chartPick(rng)];
        const float u = uvDist(rng);
        const float v = uvDist(rng);
        const glm::vec3 worldPos = surfaceRC->chartToWorld(trueChartID, u, v) +
            glm::vec3(offsetDist(rng), offsetDist(rng), offsetDist(rng));

        int classifiedChartID = 0;
        float classifiedU = 0.0f;
        float classifiedV = 0.0f;
        const bool mapped = surfaceRC->worldToChart(worldPos, classifiedChartID, classifiedU, classifiedV);

        ++stats.total;
        ++stats.perChartTotal[trueChartID];
        if (mapped && classifiedChartID == trueChartID) {
            ++stats.correct;
        } else {
            ++stats.missed;
            ++stats.perChartMissed[trueChartID];
        }
    }

    const float missRate = stats.total > 0
        ? static_cast<float>(stats.missed) / static_cast<float>(stats.total)
        : 1.0f;

    std::cout << "\n[Phase 3] Misclassification probe\n";
    std::cout << "  total=" << stats.total
              << " correct=" << stats.correct
              << " missed=" << stats.missed
              << " missRate=" << (missRate * 100.0f) << "%\n";
}

void Demo3D::captureUnknownDistribution() {
    if (!surfaceRC) {
        std::cerr << "[Phase 3] ERROR: SurfaceRC is not initialized.\n";
        return;
    }

    setUseSurfaceRC(true);
    setSurfaceDebugTarget(2);
    setSurfaceRadianceDebugMode(8);
    setRenderMode(20);
    std::cout << "[Phase 3] Unknown/round-trip debug mode armed. Use --screenshot with --exit-frames to capture it.\n";
}
