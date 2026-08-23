#include "app3d.h"
#include "chart_provider_validation.h"
#include "legacy_demo3d_runtime.h"
#include "reference_cornell_scene.h"
#include "reference_layout_validation.h"
#include "reference_merge_validation.h"
#include "reference_feedback_validation.h"
#include "reference_final_validation.h"
#include "reference_pipeline.h"
#include "reference_pt.h"
#include "reference_legacy_pipeline.h"
#include "reference_legacy_validation.h"

#include "raylib.h"
#include "reference_transport_validation.h"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

enum class RuntimeShell {
    Legacy,
    App3D
};

struct StartupConfig {
    // Phase 9 cut-over: App3D is the default shell. Demo3D is reachable only
    // through the explicit --runtime-shell=legacy deprecation window.
    RuntimeShell shell = RuntimeShell::App3D;
    bool valid = true;
    bool hasUnknownArguments = false;
    bool validateReferenceScene = false;
    bool validateReferenceLayout = false;
    bool validateReferenceTransport = false;
    bool validateReferenceMerge = false;
    bool validateReferenceFeedback = false;
    bool validateReferenceFinal = false;
    bool validateReferenceLegacy = false;
    bool validateChartProvider = false;
    int legacyRenderFrames = -1;
    std::string_view legacyRenderShot;
    std::string_view legacyPtShot;
    std::string debugPixel;
    std::string debugC0Path;
    std::string_view referenceLegacyReport;
    int referenceRenderFrames = -1;  // >=0: interactive reference view for N frames
    std::string_view referenceRenderShot;
    std::string_view referencePtShot;
    int referencePtSpp = 64;
    int referencePtBounces = 5;
    bool referencePtReflectiveZero = false;
    std::string_view referenceSceneReport;
    std::string_view referenceLayoutReport;
    std::string_view referenceTransportReport;
    std::string_view referenceMergeReport;
    std::string_view referenceFeedbackReport;
    std::string_view referenceFinalReport;
    RcAtlasFilter atlasFilter = RcAtlasFilter::Linear;
    RcQualityProfile quality = RcQualityProfile::Parity;
    std::string_view occupancyJson;
    std::string_view chartProviderReport;
};

StartupConfig parseStartupConfig(int argc, char* argv[]) {
    StartupConfig config;
    bool shellSeen = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--auto-rdoc") {
            // Fail-closed 2026-08-22: L2 timer + L3 qrenderdoc extract are obsolete.
            // Skill renderdoc-gpu-debug / rdc-cli is the capture+inspect path.
            // No escape hatch on this flag — copied docs/scripts must fail, not fire.
            std::cerr
                << "[APP3D] --auto-rdoc is obsolete (2026-08-22).\n"
                << "[APP3D] Use rdc-cli (skill renderdoc-gpu-debug), e.g.\n"
                << "  rdc capture --frame 24 --timeout 180 --json -- "
                   ".\\build\\RadianceCascades3D.exe --reference-render=40\n"
                << "  rdc open <file.rdc>\n"
                << "  rdc counters --name \"GPU Duration\" --json\n"
                << "[APP3D] Do not pass --wait-for-exit. G-key still saves a .rdc (no auto-extract).\n"
                << "[APP3D] See doc/journey.md Era 12.\n";
            config.valid = false;
            continue;
        }
        if (argument == "--validate-reference-cornell-scene") {
            config.validateReferenceScene = true;
            continue;
        }
        if (argument == "--validate-reference-layout") {
            config.validateReferenceLayout = true;
            continue;
        }
        if (argument == "--validate-reference-transport") {
            config.validateReferenceTransport = true;
            continue;
        }
        if (argument == "--validate-reference-merge") {
            config.validateReferenceMerge = true;
            continue;
        }
        if (argument == "--validate-reference-feedback") {
            config.validateReferenceFeedback = true;
            continue;
        }
        if (argument == "--validate-reference-final") {
            config.validateReferenceFinal = true;
            continue;
        }
        if (argument == "--validate-reference-legacy") {
            config.validateReferenceLegacy = true;
            continue;
        }
        if (argument == "--validate-chart-provider") {
            config.validateChartProvider = true;
            continue;
        }
        constexpr std::string_view legacyReportPrefix = "--reference-legacy-report=";
        if (argument.starts_with(legacyReportPrefix)) {
            config.referenceLegacyReport = argument.substr(legacyReportPrefix.size());
            continue;
        }
        constexpr std::string_view legacyRenderPrefix = "--legacy-render=";
        if (argument.starts_with(legacyRenderPrefix)) {
            config.legacyRenderFrames =
                std::max(0, std::atoi(std::string(argument.substr(legacyRenderPrefix.size())).c_str()));
            continue;
        }
        constexpr std::string_view legacyShotPrefix = "--legacy-render-shot=";
        if (argument.starts_with(legacyShotPrefix)) {
            config.legacyRenderShot = argument.substr(legacyShotPrefix.size());
            if (config.legacyRenderFrames < 0)
                config.legacyRenderFrames = 24;
            continue;
        }
        constexpr std::string_view legacyPtPrefix = "--legacy-pt-shot=";
        if (argument.starts_with(legacyPtPrefix)) {
            config.legacyPtShot = argument.substr(legacyPtPrefix.size());
            continue;
        }
        constexpr std::string_view dbgPixelPrefix = "--debug-legacy-pixel=";
        if (argument.starts_with(dbgPixelPrefix)) {
            config.debugPixel = std::string(argument.substr(dbgPixelPrefix.size()));
            continue;
        }
        constexpr std::string_view dbgC0Prefix = "--debug-legacy-c0=";
        if (argument.starts_with(dbgC0Prefix)) {
            config.debugC0Path = std::string(argument.substr(dbgC0Prefix.size()));
            continue;
        }
        constexpr std::string_view renderPrefix = "--reference-render=";
        if (argument.starts_with(renderPrefix)) {
            config.referenceRenderFrames =
                std::max(0, std::atoi(std::string(argument.substr(renderPrefix.size())).c_str()));
            continue;
        }
        if (argument == "--reference-render") {
            config.referenceRenderFrames = 0;  // run until ESC
            continue;
        }
        constexpr std::string_view renderShotPrefix = "--reference-render-shot=";
        if (argument.starts_with(renderShotPrefix)) {
            config.referenceRenderShot = argument.substr(renderShotPrefix.size());
            if (config.referenceRenderFrames < 0)
                config.referenceRenderFrames = 24;  // converge then capture
            continue;
        }
        constexpr std::string_view ptShotPrefix = "--reference-pt-shot=";
        if (argument.starts_with(ptShotPrefix)) {
            config.referencePtShot = argument.substr(ptShotPrefix.size());
            continue;
        }
        constexpr std::string_view ptSppPrefix = "--reference-pt-spp=";
        if (argument.starts_with(ptSppPrefix)) {
            config.referencePtSpp =
                std::max(1, std::atoi(std::string(argument.substr(ptSppPrefix.size())).c_str()));
            continue;
        }
        constexpr std::string_view ptBouncePrefix = "--reference-pt-bounces=";
        if (argument.starts_with(ptBouncePrefix)) {
            config.referencePtBounces =
                std::max(1, std::atoi(std::string(argument.substr(ptBouncePrefix.size())).c_str()));
            continue;
        }
        if (argument == "--reference-pt-reflective-zero") {
            config.referencePtReflectiveZero = true;
            continue;
        }
        constexpr std::string_view reportPrefix = "--reference-scene-report=";
        if (argument.starts_with(reportPrefix)) {
            config.referenceSceneReport = argument.substr(reportPrefix.size());
            continue;
        }
        constexpr std::string_view layoutReportPrefix = "--reference-layout-report=";
        if (argument.starts_with(layoutReportPrefix)) {
            config.referenceLayoutReport = argument.substr(layoutReportPrefix.size());
            continue;
        }
        constexpr std::string_view transportReportPrefix = "--reference-transport-report=";
        if (argument.starts_with(transportReportPrefix)) {
            config.referenceTransportReport = argument.substr(transportReportPrefix.size());
            continue;
        }
        constexpr std::string_view mergeReportPrefix = "--reference-merge-report=";
        if (argument.starts_with(mergeReportPrefix)) {
            config.referenceMergeReport = argument.substr(mergeReportPrefix.size());
            continue;
        }
        constexpr std::string_view feedbackReportPrefix = "--reference-feedback-report=";
        if (argument.starts_with(feedbackReportPrefix)) {
            config.referenceFeedbackReport = argument.substr(feedbackReportPrefix.size());
            continue;
        }
        constexpr std::string_view finalReportPrefix = "--reference-final-report=";
        if (argument.starts_with(finalReportPrefix)) {
            config.referenceFinalReport = argument.substr(finalReportPrefix.size());
            continue;
        }
        constexpr std::string_view atlasFilterPrefix = "--atlas-filter=";
        if (argument.starts_with(atlasFilterPrefix)) {
            const std::string_view value = argument.substr(atlasFilterPrefix.size());
            if (value == "linear") {
                config.atlasFilter = RcAtlasFilter::Linear;
            } else if (value == "nearest") {
                config.atlasFilter = RcAtlasFilter::Nearest;
            } else {
                std::cerr << "[APP3D] Invalid --atlas-filter '" << value
                          << "'; expected linear or nearest.\n";
                config.valid = false;
            }
            continue;
        }
        constexpr std::string_view qualityPrefix = "--rc-quality=";
        if (argument.starts_with(qualityPrefix)) {
            const std::string_view value = argument.substr(qualityPrefix.size());
            if (value == "parity") {
                config.quality = RcQualityProfile::Parity;
            } else if (value == "high-c0") {
                config.quality = RcQualityProfile::HighC0;
            } else {
                std::cerr << "[APP3D] Invalid --rc-quality '" << value
                          << "'; expected parity or high-c0.\n";
                config.valid = false;
            }
            continue;
        }
        constexpr std::string_view occupancyPrefix = "--occupancy-json=";
        if (argument.starts_with(occupancyPrefix)) {
            config.occupancyJson = argument.substr(occupancyPrefix.size());
            continue;
        }
        constexpr std::string_view chartProviderReportPrefix = "--chart-provider-report=";
        if (argument.starts_with(chartProviderReportPrefix)) {
            config.chartProviderReport = argument.substr(chartProviderReportPrefix.size());
            continue;
        }
        constexpr std::string_view prefix = "--runtime-shell=";
        if (argument.starts_with(prefix)) {
            if (shellSeen) {
                std::cerr << "[APP3D] Duplicate --runtime-shell selector.\n";
                config.valid = false;
                continue;
            }
            shellSeen = true;

            const std::string_view value = argument.substr(prefix.size());
            if (value == "legacy") {
                config.shell = RuntimeShell::Legacy;
            } else if (value == "app3d") {
                config.shell = RuntimeShell::App3D;
            } else {
                std::cerr << "[APP3D] Invalid --runtime-shell value '" << value
                          << "'; expected legacy or app3d.\n";
                config.valid = false;
            }
            continue;
        }

        // Unrecognized by the app3d shell. Legacy Demo3D flags are forwarded
        // only when --runtime-shell=legacy; otherwise they are reported.
        config.hasUnknownArguments = true;
    }

    return config;
}

}  // namespace

int runReferenceRenderInteractive(int frames, const std::string& screenshotPath,
                                  RcAtlasFilter atlasFilter, RcQualityProfile quality,
                                  const std::string& occupancyJson);
int runReferencePtCapture(const std::string& screenshotPath, int samplesPerPixel,
                          int maxBounces, bool reflectiveZero);
int runLegacyRenderInteractive(int frames, const std::string& screenshotPath);
int runLegacyPtCapture(const std::string& screenshotPath, int samplesPerPixel);

int App3D::run(int argc, char* argv[]) {
    const StartupConfig config = parseStartupConfig(argc, argv);
    if (!config.valid)
        return 2;

    if (config.validateReferenceScene) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Reference scene validation requires --runtime-shell=app3d.\n";
            return 2;
        }
        if (config.referenceSceneReport.empty()) {
            std::cerr << "[APP3D] --reference-scene-report is required for validation.\n";
            return 2;
        }
        const ReferenceCornellScene scene;
        const bool passed = scene.validateAndWriteReport(std::string(config.referenceSceneReport));
        std::cout << "[PHASE2] reference scene report=" << config.referenceSceneReport
                  << " result=" << (passed ? "PASS" : "FAIL") << "\n";
        return passed ? 0 : 1;
    }

    if (config.validateReferenceLayout) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Reference layout validation requires --runtime-shell=app3d.\n";
            return 2;
        }
        if (config.referenceLayoutReport.empty()) {
            std::cerr << "[APP3D] --reference-layout-report is required for validation.\n";
            return 2;
        }
        const bool passed =
            runReferenceLayoutValidation(std::string(config.referenceLayoutReport));
        return passed ? 0 : 1;
    }

    if (config.validateReferenceTransport) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Reference transport validation requires --runtime-shell=app3d.\n";
            return 2;
        }
        if (config.referenceTransportReport.empty()) {
            std::cerr << "[APP3D] --reference-transport-report is required for validation.\n";
            return 2;
        }
        const bool passed =
            runReferenceTransportValidation(std::string(config.referenceTransportReport));
        return passed ? 0 : 1;
    }

    if (config.validateReferenceMerge) {
        if (config.shell != RuntimeShell::App3D || config.referenceMergeReport.empty()) {
            std::cerr << "[APP3D] Reference merge validation requires app3d shell and report path.\n";
            return 2;
        }
        if (config.quality != RcQualityProfile::Parity) {
            std::cerr << "[APP3D] G6 validation is parity-only; refuse --rc-quality=high-c0.\n";
            return 2;
        }
        setDefaultRcAtlasFilter(config.atlasFilter);
        return runReferenceMergeValidation(std::string(config.referenceMergeReport)) ? 0 : 1;
    }

    if (config.validateReferenceFeedback) {
        if (config.shell != RuntimeShell::App3D || config.referenceFeedbackReport.empty()) {
            std::cerr << "[APP3D] Reference feedback validation requires app3d shell and report path.\n";
            return 2;
        }
        if (config.quality != RcQualityProfile::Parity) {
            std::cerr << "[APP3D] G7 validation is parity-only; refuse --rc-quality=high-c0.\n";
            return 2;
        }
        setDefaultRcAtlasFilter(config.atlasFilter);
        return runReferenceFeedbackValidation(std::string(config.referenceFeedbackReport)) ? 0 : 1;
    }

    if (config.validateChartProvider) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Chart-provider validation requires --runtime-shell=app3d.\n";
            return 2;
        }
        if (config.chartProviderReport.empty()) {
            std::cerr << "[APP3D] --chart-provider-report is required for validation.\n";
            return 2;
        }
        const bool passed =
            runChartProviderValidation(std::string(config.chartProviderReport));
        std::cout << "[PHASE11-M1] chart provider report=" << config.chartProviderReport
                  << " result=" << (passed ? "PASS" : "FAIL") << "\n";
        return passed ? 0 : 1;
    }

    if (config.validateReferenceFinal) {
        if (config.shell != RuntimeShell::App3D || config.referenceFinalReport.empty()) {
            std::cerr << "[APP3D] Reference final validation requires app3d shell and report path.\n";
            return 2;
        }
        if (config.quality != RcQualityProfile::Parity) {
            std::cerr << "[APP3D] G9 validation is parity-only; refuse --rc-quality=high-c0.\n";
            return 2;
        }
        setDefaultRcAtlasFilter(config.atlasFilter);
        return runReferenceFinalValidation(std::string(config.referenceFinalReport)) ? 0 : 1;
    }

    if (!config.debugC0Path.empty()) {
        SetConfigFlags(0);
        InitWindow(64, 64, "legacy-c0-debug");
        glewExperimental = GL_TRUE;
        glewInit();
        ReferenceLegacyPipeline pipeline;
        if (!pipeline.initialize()) {
            std::cerr << "[DEBUG-C0] pipeline init failed\n";
            CloseWindow();
            return 1;
        }
        for (int f = 0; f < 8; ++f)
            pipeline.runFrame();
        std::vector<float> c0(static_cast<size_t>(reflegacy::kPhysicalWidth) *
                              reflegacy::kPhysicalHeight * 4);
        // Per-cascade chart-region energy dump for diagnosis.
        for (uint32_t c = 0; c < 6; ++c) {
            glBindTexture(GL_TEXTURE_2D, pipeline.atlases().readTexture(c));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, c0.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            auto regionSum = [&](int x0, int x1) {
                double r = 0, g = 0, b = 0, sky = 0;
                for (int y = 0; y < reflegacy::kPhysicalHeight; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const size_t o = (static_cast<size_t>(y) * reflegacy::kPhysicalWidth + x) * 4;
                        if (c0[o + 3] < -0.5f) { sky += 1.0; continue; }
                        r += c0[o]; g += c0[o + 1]; b += c0[o + 2];
                    }
                }
                return std::make_tuple(r, g, b, sky);
            };
            const struct { const char* name; int x0; int x1; } regions[] = {
                {"floor", 0, 256}, {"red", 512, 768}, {"green", 768, 1024},
                {"light", 1280, 1344}, {"talltop", 1344, 1408}, {"shorttop", 1408, 1472},
            };
            for (const auto& rg : regions) {
                auto [r, g, b, sky] = regionSum(rg.x0, rg.x1);
                std::cout << "[DEBUG-C0] C" << c << " " << rg.name
                          << " rgb=(" << r << "," << g << "," << b << ") skyTexels=" << sky << "\n";
            }
        }
        glBindTexture(GL_TEXTURE_2D, pipeline.atlases().readTexture(0));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, c0.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        // CPU oracle final view for direct comparison with the GPU path.
        {
            const int W = 160, H = 120;
            std::vector<unsigned char> cpuBytes(static_cast<size_t>(W) * H * 4);
            auto fetch = [&](const glm::ivec2& p) -> glm::vec4 {
                if (p.x < 0 || p.y < 0 || p.x >= reflegacy::kPhysicalWidth ||
                    p.y >= reflegacy::kPhysicalHeight)
                    return glm::vec4(0.0f);
                const size_t o = (static_cast<size_t>(p.y) * reflegacy::kPhysicalWidth +
                                  static_cast<size_t>(p.x)) * 4;
                return {c0[o], c0[o + 1], c0[o + 2], c0[o + 3]};
            };
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const glm::vec2 ndc((static_cast<float>(x) + 0.5f) / W * 2.0f - 1.0f,
                                        (static_cast<float>(y) + 0.5f) / H * 2.0f - 1.0f);
                    glm::vec3 c(0.0f);
                    const auto hit = pipeline.scene().trace(pipeline.camera().position,
                                                            pipeline.camera().ray(ndc), 10000.0f);
                    if (hit.hit) {
                        if (hit.materialKind == ReferenceMaterialKind::Emissive) {
                            c = hit.reflectanceOrEmission;
                        } else if (hit.materialKind == ReferenceMaterialKind::Diffuse) {
                            glm::vec3 irr(0.0f);
                            if (hit.chartId != ReferenceChartId::Invalid && hit.chartUv.x >= 0.0f) {
                                const auto& ch = reflegacy::chart(static_cast<uint32_t>(hit.chartId));
                                const glm::vec2 halfRes = ch.resolution * 0.5f;
                                const glm::vec2 suv = glm::clamp(hit.chartUv * halfRes,
                                                                 glm::vec2(0.5f), halfRes - 0.5f) + ch.logicalBase;
                                const glm::vec2 offs[4] = {{0,0},{halfRes.x,0},{0,halfRes.y},halfRes};
                                for (const auto& off : offs)
                                    irr += glm::vec3(fetch(glm::ivec2(suv + off)));
                            }
                            glm::vec3 n = hit.normal;
                            if (glm::dot(n, glm::normalize(pipeline.camera().ray(ndc))) >= 0.0f) n = -n;
                            c = hit.reflectanceOrEmission * irr;
                        }
                    }
                    const size_t o = (static_cast<size_t>(y) * W + x) * 4;
                    for (int k = 0; k < 3; ++k)
                        cpuBytes[o + k] = static_cast<unsigned char>(
                            std::clamp(c[k] * 2.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                    cpuBytes[o + 3] = 255;
                }
            }
            std::string cpuPath = config.debugC0Path;
            const size_t dot = cpuPath.rfind(".png");
            if (dot != std::string::npos) cpuPath.insert(dot, "_cpu");
            Image cpuImg;
            cpuImg.data = cpuBytes.data();
            cpuImg.width = W;
            cpuImg.height = H;
            cpuImg.mipmaps = 1;
            cpuImg.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            ExportImage(cpuImg, cpuPath.c_str());
        }
        std::vector<unsigned char> bytes(c0.size());
        for (size_t i = 0; i < c0.size(); i += 4) {
            for (int k = 0; k < 3; ++k)
                bytes[i + k] = static_cast<unsigned char>(
                    std::clamp(c0[i + k] * 2.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
            bytes[i + 3] = c0[i + 3] < -0.5f ? 0 : 255;  // alpha>=0 -> white tag
        }
        Image image;
        image.data = bytes.data();
        image.width = reflegacy::kPhysicalWidth;
        image.height = reflegacy::kPhysicalHeight;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        const bool ok = ExportImage(image, config.debugC0Path.c_str());
        std::cout << "[DEBUG-C0] wrote=" << config.debugC0Path
                  << " result=" << (ok ? "PASS" : "FAIL") << "\n";
        CloseWindow();
        return ok ? 0 : 1;
    }

    if (!config.debugPixel.empty()) {
        const size_t comma = config.debugPixel.find(',');
        const int px = std::atoi(config.debugPixel.substr(0, comma).c_str());
        const int py = std::atoi(config.debugPixel.substr(comma + 1).c_str());
        const ReferenceLegacyCornellScene scene;
        const ReferenceCamera camera(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f),
                                     60.0f, 4.0f / 3.0f);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = px + dx, y = py + dy;
                const glm::vec2 ndc((static_cast<float>(x) + 0.5f) / 320.0f * 2.0f - 1.0f,
                                    (static_cast<float>(y) + 0.5f) / 240.0f * 2.0f - 1.0f);
                const auto hit = scene.trace(camera.position, camera.ray(ndc), 10000.0f);
                std::cout << "  (" << x << "," << y << ") hit=" << (hit.hit ? 1 : 0)
                          << " chart=" << static_cast<uint32_t>(hit.chartId)
                          << " kind=" << static_cast<uint32_t>(hit.materialKind)
                          << " t=" << hit.distance
                          << " uv=(" << hit.chartUv.x << "," << hit.chartUv.y << ")\n";
            }
        }
        return 0;
    }

    if (config.validateReferenceLegacy) {
        if (config.shell != RuntimeShell::App3D || config.referenceLegacyReport.empty()) {
            std::cerr << "[APP3D] Legacy validation requires app3d shell and report path.\n";
            return 2;
        }
        return runReferenceLegacyValidation(std::string(config.referenceLegacyReport)) ? 0 : 1;
    }

    if (!config.legacyPtShot.empty()) {
        return runLegacyPtCapture(std::string(config.legacyPtShot), config.referencePtSpp);
    }

    if (config.legacyRenderFrames >= 0) {
        return runLegacyRenderInteractive(config.legacyRenderFrames,
                                          std::string(config.legacyRenderShot));
    }

    if (!config.referencePtShot.empty()) {
        return runReferencePtCapture(std::string(config.referencePtShot),
                                     config.referencePtSpp, config.referencePtBounces,
                                     config.referencePtReflectiveZero);
    }

    if (config.referenceRenderFrames >= 0) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Reference render requires --runtime-shell=app3d.\n";
            return 2;
        }
        return runReferenceRenderInteractive(config.referenceRenderFrames,
                                             std::string(config.referenceRenderShot),
                                             config.atlasFilter, config.quality,
                                             std::string(config.occupancyJson));
    }

    if (config.shell == RuntimeShell::Legacy) {
        // Phase 9 deprecation window: Demo3D is no longer the default runtime.
        // It remains reachable only through this explicit selector.
        std::cout << "[APP3D] shell=legacy runtimeBackend=legacy-direct (deprecation window)\n";
        return runLegacyDemo3DRuntime(
            argc, argv, {.shellName = "legacy", .runtimeBackendName = "legacy-direct"});
    }

    // App3D is the default shell. Legacy Demo3D flags no longer silently fall
    // back to old global state; they require the explicit legacy selector.
    if (config.hasUnknownArguments) {
        std::cerr << "[APP3D] Unknown arguments in the app3d shell.\n"
                  << "[APP3D] Legacy Demo3D flags require --runtime-shell=legacy (deprecation window).\n"
                  << "[APP3D] Supported app3d commands:\n"
                  << "[APP3D]   --reference-render[=N] [--reference-render-shot=PATH]\n"
                  << "[APP3D]   --reference-pt-shot=PATH [--reference-pt-spp=N] [--reference-pt-bounces=N]\n"
                  << "[APP3D]   --legacy-render=N [--legacy-render-shot=PATH]   --legacy-pt-shot=PATH\n"
                  << "[APP3D]   --validate-reference-{cornell-scene,layout,transport,merge,feedback,final,legacy}\n"
                  << "[APP3D]       with the matching --reference-*-report=PATH\n"
                  << "[APP3D]   --validate-chart-provider --chart-provider-report=PATH\n"
                  << "[APP3D]   --atlas-filter=linear|nearest   --rc-quality=parity|high-c0\n"
                  << "[APP3D]   --occupancy-json=PATH\n"
                  << "[APP3D]   --debug-legacy-pixel=X,Y   --debug-legacy-c0=PATH\n";
        return 2;
    }

    // Default runtime: the reference surface-RC interactive view.
    std::cout << "[APP3D] backend=reference-surface-rc-default\n";
    return runReferenceRenderInteractive(0, "", config.atlasFilter, config.quality,
                                         std::string(config.occupancyJson));
}

int runReferenceRenderInteractive(int frames, const std::string& screenshotPath,
                                  RcAtlasFilter atlasFilter, RcQualityProfile quality,
                                  const std::string& occupancyJson) {
    constexpr int kViewWidth = 640;
    constexpr int kViewHeight = 480;
    SetConfigFlags(0);
    InitWindow(kViewWidth, kViewHeight, "Reference Surface RC (Phase 7 final consumer)");
    if (!IsWindowReady()) {
        std::cerr << "[REFERENCE] window init failed\n";
        return 1;
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "[REFERENCE] glew init failed\n";
        CloseWindow();
        return 1;
    }
    while (glGetError() != GL_NO_ERROR) {}

    setDefaultRcAtlasFilter(atlasFilter);
    ReferenceRcPipeline pipeline(atlasFilter);
    pipeline.setQualityProfile(quality);
    if (!pipeline.initialize()) {
        std::cerr << "[REFERENCE] pipeline init failed\n";
        CloseWindow();
        return 1;
    }
    std::cout << "[REFERENCE] quality=" << rcQualityProfileName(quality)
              << " atlas_filter=" << rcAtlasFilterName(atlasFilter) << "\n";
    // Display-only mapping for human viewing (validated pixels stay linear).
    pipeline.setDisplayMapping(8.0f, 1.0f / 2.2f);

    GLuint target = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kViewWidth, kViewHeight, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    Texture2D view;
    view.id = target;
    view.width = kViewWidth;
    view.height = kViewHeight;
    view.mipmaps = 1;
    view.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;

    SetTargetFPS(30);
    int frame = 0;
    while (!WindowShouldClose()) {
        if (!pipeline.runFrame()) {
            std::cerr << "[REFERENCE] frame " << frame << " failed\n";
            break;
        }
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(view, 0, 0, WHITE);
        DrawText(TextFormat("Reference C0 | %s | %s | frame %d | gen %llu | ESC quit | F12 shot",
                            rcQualityProfileName(quality), rcAtlasFilterName(atlasFilter),
                            frame, (unsigned long long)pipeline.generation()),
                 8, 8, 10, LIME);
        EndDrawing();
        if (IsKeyPressed(KEY_F12))
            TakeScreenshot(TextFormat("reference_view_%d.png", frame));
        ++frame;
        if (frames > 0 && frame >= frames)
            break;
    }

    if (!screenshotPath.empty()) {
        // Linear companion first (validated display policy), then the mapped
        // panel for viewing. Both are written from the same frame's C0 state.
        pipeline.setDisplayMapping(1.0f, 1.0f);
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        std::vector<float> linear(static_cast<size_t>(kViewWidth) * kViewHeight * 4);
        glBindTexture(GL_TEXTURE_2D, target);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, linear.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        std::string linearPath = screenshotPath;
        const size_t dot = linearPath.rfind(".png");
        if (dot != std::string::npos)
            linearPath.insert(dot, "_linear");
        else
            linearPath += "_linear.png";
        {
            std::vector<unsigned char> bytes(linear.size());
            for (size_t i = 0; i < linear.size(); ++i)
                bytes[i] = static_cast<unsigned char>(
                    std::clamp(linear[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            Image image;
            image.data = bytes.data();
            image.width = kViewWidth;
            image.height = kViewHeight;
            image.mipmaps = 1;
            image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            if (!ExportImage(image, linearPath.c_str()))
                std::cerr << "[REFERENCE] linear write failed: " << linearPath << "\n";
        }

        pipeline.setDisplayMapping(8.0f, 1.0f / 2.2f);
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        // Read with glGetTexImage directly: raylib's LoadImageFromTexture
        // corrupts RGBA32F content (channel inflation and zeroed patches).
        std::vector<float> mapped(linear.size());
        glBindTexture(GL_TEXTURE_2D, target);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, mapped.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        // Manual row reversal instead of ImageFlipVertical (crashes on vector-backed Images).
        std::vector<unsigned char> mappedBytes(mapped.size());
        for (int y = 0; y < kViewHeight; ++y) {
            const int srcY = kViewHeight - 1 - y;
            for (int x = 0; x < kViewWidth; ++x) {
                const size_t src = (static_cast<size_t>(srcY) * kViewWidth + x) * 4;
                const size_t dst = (static_cast<size_t>(y) * kViewWidth + x) * 4;
                for (int k = 0; k < 3; ++k)
                    mappedBytes[dst + k] = static_cast<unsigned char>(
                        std::clamp(mapped[src + k], 0.0f, 1.0f) * 255.0f + 0.5f);
                mappedBytes[dst + 3] = 255;
            }
        }
        Image image;
        image.data = mappedBytes.data();
        image.width = kViewWidth;
        image.height = kViewHeight;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        if (ExportImage(image, screenshotPath.c_str()))
            std::cout << "[REFERENCE] screenshot=" << screenshotPath
                      << " linear=" << linearPath << " frames=" << frame << "\n";
        else
            std::cerr << "[REFERENCE] screenshot write failed: "
                      << screenshotPath << "\n";
    }

    if (!occupancyJson.empty()) {
        if (pipeline.writeOccupancyJson(occupancyJson))
            std::cout << "[REFERENCE] occupancy=" << occupancyJson << "\n";
        else
            std::cerr << "[REFERENCE] occupancy write failed: " << occupancyJson << "\n";
    }

    glDeleteTextures(1, &target);
    CloseWindow();
    return 0;
}

int runReferencePtCapture(const std::string& screenshotPath, int samplesPerPixel,
                          int maxBounces, bool reflectiveZero) {
    const ReferenceCornellScene scene;
    const ReferenceCamera camera;
    ReferencePtOptions options;
    options.width = 640;
    options.height = 480;
    options.samplesPerPixel = samplesPerPixel;
    options.maxBounces = maxBounces;
    options.reflectiveZero = reflectiveZero;
    const ReferencePtResult result = renderReferencePT(scene, camera, options);

    auto writePng = [&](const std::string& path, bool displayMapped) {
        std::vector<unsigned char> bytes(
            static_cast<size_t>(result.width) * result.height * 4);
        // result.pixels is bottom-up (row 0 = ndc.y=-1); PNG row 0 is the top.
        for (int y = 0; y < result.height; ++y) {
            const int srcY = result.height - 1 - y;
            for (int x = 0; x < result.width; ++x) {
                const size_t src = static_cast<size_t>(srcY) * result.width + x;
                const size_t dst = static_cast<size_t>(y) * result.width + x;
                glm::vec3 c = result.pixels[src];
                if (displayMapped) {
                    c = glm::pow(glm::max(c * 8.0f, glm::vec3(0.0f)),
                                 glm::vec3(1.0f / 2.2f));
                }
                for (int k = 0; k < 3; ++k)
                    bytes[dst * 4 + k] = static_cast<unsigned char>(
                        std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
                bytes[dst * 4 + 3] = 255;
            }
        }
        Image image;
        image.data = bytes.data();
        image.width = result.width;
        image.height = result.height;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        return ExportImage(image, path.c_str());
    };

    const bool okMapped = writePng(screenshotPath, true);
    std::string linearPath = screenshotPath;
    const size_t dot = linearPath.rfind(".png");
    if (dot != std::string::npos)
        linearPath.insert(dot, "_linear");
    else
        linearPath += "_linear.png";
    const bool okLinear = writePng(linearPath, false);
    std::cout << "[REFERENCE-PT] screenshot=" << screenshotPath
              << " linear=" << linearPath
              << " result=" << ((okMapped && okLinear) ? "PASS" : "FAIL") << "\n";
    return (okMapped && okLinear) ? 0 : 1;
}

int runLegacyPtCapture(const std::string& screenshotPath, int samplesPerPixel) {
    const ReferenceLegacyCornellScene scene;
    const ReferenceCamera camera(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f),
                                 60.0f, 4.0f / 3.0f);
    ReferencePtOptions options;
    options.width = 640;
    options.height = 480;
    options.samplesPerPixel = samplesPerPixel;
    const ReferencePtResult result = renderReferencePT(scene, camera, options);

    auto writePng = [&](const std::string& path, bool displayMapped) {
        std::vector<unsigned char> bytes(
            static_cast<size_t>(result.width) * result.height * 4);
        // result.pixels is bottom-up (row 0 = ndc.y=-1); PNG row 0 is the top.
        for (int y = 0; y < result.height; ++y) {
            const int srcY = result.height - 1 - y;
            for (int x = 0; x < result.width; ++x) {
                const size_t src = static_cast<size_t>(srcY) * result.width + x;
                const size_t dst = static_cast<size_t>(y) * result.width + x;
                glm::vec3 c = result.pixels[src];
                if (displayMapped)
                    c = glm::pow(glm::max(c * 8.0f, glm::vec3(0.0f)),
                                 glm::vec3(1.0f / 2.2f));
                for (int k = 0; k < 3; ++k)
                    bytes[dst * 4 + k] = static_cast<unsigned char>(
                        std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
                bytes[dst * 4 + 3] = 255;
            }
        }
        Image image;
        image.data = bytes.data();
        image.width = result.width;
        image.height = result.height;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        return ExportImage(image, path.c_str());
    };

    const bool okMapped = writePng(screenshotPath, true);
    std::string linearPath = screenshotPath;
    const size_t dot = linearPath.rfind(".png");
    if (dot != std::string::npos)
        linearPath.insert(dot, "_linear");
    else
        linearPath += "_linear.png";
    const bool okLinear = writePng(linearPath, false);
    std::cout << "[LEGACY-PT] screenshot=" << screenshotPath
              << " linear=" << linearPath
              << " result=" << ((okMapped && okLinear) ? "PASS" : "FAIL") << "\n";
    return (okMapped && okLinear) ? 0 : 1;
}

int runLegacyRenderInteractive(int frames, const std::string& screenshotPath) {
    constexpr int kViewWidth = 640;
    constexpr int kViewHeight = 480;
    SetConfigFlags(0);
    InitWindow(kViewWidth, kViewHeight, "Legacy Cornell - New Surface RC");
    if (!IsWindowReady()) {
        std::cerr << "[LEGACY] window init failed\n";
        return 1;
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "[LEGACY] glew init failed\n";
        CloseWindow();
        return 1;
    }
    while (glGetError() != GL_NO_ERROR) {}

    ReferenceLegacyPipeline pipeline;
    if (!pipeline.initialize()) {
        std::cerr << "[LEGACY] pipeline init failed\n";
        CloseWindow();
        return 1;
    }
    pipeline.setDisplayMapping(8.0f, 1.0f / 2.2f);

    GLuint target = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kViewWidth, kViewHeight, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    Texture2D view;
    view.id = target;
    view.width = kViewWidth;
    view.height = kViewHeight;
    view.mipmaps = 1;
    view.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;

    SetTargetFPS(30);
    int frame = 0;
    while (!WindowShouldClose()) {
        if (!pipeline.runFrame()) {
            std::cerr << "[LEGACY] frame " << frame << " failed\n";
            break;
        }
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(view, 0, 0, WHITE);
        DrawText(TextFormat("Legacy Cornell new RC | frame %d | ESC quit", frame),
                 8, 8, 10, LIME);
        EndDrawing();
        ++frame;
        if (frames > 0 && frame >= frames)
            break;
    }

    if (!screenshotPath.empty()) {
        pipeline.setDisplayMapping(1.0f, 1.0f);
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        std::vector<float> linear(static_cast<size_t>(kViewWidth) * kViewHeight * 4);
        glBindTexture(GL_TEXTURE_2D, target);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, linear.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        std::string linearPath = screenshotPath;
        const size_t dot = linearPath.rfind(".png");
        if (dot != std::string::npos)
            linearPath.insert(dot, "_linear");
        else
            linearPath += "_linear.png";
        {
            std::vector<unsigned char> bytes(linear.size());
            for (size_t i = 0; i < linear.size(); ++i)
                bytes[i] = static_cast<unsigned char>(
                    std::clamp(linear[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            Image image;
            image.data = bytes.data();
            image.width = kViewWidth;
            image.height = kViewHeight;
            image.mipmaps = 1;
            image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            ExportImage(image, linearPath.c_str());
        }
        pipeline.setDisplayMapping(8.0f, 1.0f / 2.2f);
        pipeline.renderFinalView(target, kViewWidth, kViewHeight, true);
        // Read with glGetTexImage directly: raylib's LoadImageFromTexture
        // corrupts RGBA32F content (channel inflation and zeroed patches).
        // Rows are written reversed (GL bottom-up origin) without ImageFlipVertical.
        std::vector<float> mapped(linear.size());
        glBindTexture(GL_TEXTURE_2D, target);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, mapped.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        std::vector<unsigned char> mappedBytes(mapped.size());
        for (int y = 0; y < kViewHeight; ++y) {
            const int srcY = kViewHeight - 1 - y;
            for (int x = 0; x < kViewWidth; ++x) {
                const size_t src = (static_cast<size_t>(srcY) * kViewWidth + x) * 4;
                const size_t dst = (static_cast<size_t>(y) * kViewWidth + x) * 4;
                for (int k = 0; k < 3; ++k)
                    mappedBytes[dst + k] = static_cast<unsigned char>(
                        std::clamp(mapped[src + k], 0.0f, 1.0f) * 255.0f + 0.5f);
                mappedBytes[dst + 3] = 255;
            }
        }
        Image image;
        image.data = mappedBytes.data();
        image.width = kViewWidth;
        image.height = kViewHeight;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        if (ExportImage(image, screenshotPath.c_str()))
            std::cout << "[LEGACY] screenshot=" << screenshotPath
                      << " linear=" << linearPath << " frames=" << frame << "\n";
        else
            std::cerr << "[LEGACY] mapped export failed\n";
    }

    glDeleteTextures(1, &target);
    CloseWindow();
    return 0;
}
