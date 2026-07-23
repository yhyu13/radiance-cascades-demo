#include "app3d.h"
#include "legacy_demo3d_runtime.h"
#include "reference_cornell_scene.h"
#include "reference_layout_validation.h"
#include "reference_merge_validation.h"
#include "reference_feedback_validation.h"
#include "reference_final_validation.h"
#include "reference_pipeline.h"

#include "raylib.h"
#include "reference_transport_validation.h"

#include <iostream>
#include <memory>
#include <string_view>

namespace {

enum class RuntimeShell {
    Legacy,
    App3D
};

struct StartupConfig {
    RuntimeShell shell = RuntimeShell::Legacy;
    bool valid = true;
    bool validateReferenceScene = false;
    bool validateReferenceLayout = false;
    bool validateReferenceTransport = false;
    bool validateReferenceMerge = false;
    bool validateReferenceFeedback = false;
    bool validateReferenceFinal = false;
    int referenceRenderFrames = -1;  // >=0: interactive reference view for N frames
    std::string_view referenceSceneReport;
    std::string_view referenceLayoutReport;
    std::string_view referenceTransportReport;
    std::string_view referenceMergeReport;
    std::string_view referenceFeedbackReport;
    std::string_view referenceFinalReport;
};

class RuntimeBackend {
public:
    virtual ~RuntimeBackend() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual int run(int argc, char* argv[]) = 0;
};

class Demo3DBackend final : public RuntimeBackend {
public:
    std::string_view name() const noexcept override {
        return "demo3d-legacy";
    }

    int run(int argc, char* argv[]) override {
        return runLegacyDemo3DRuntime(
            argc, argv, {.shellName = "app3d", .runtimeBackendName = name()});
    }
};

StartupConfig parseStartupConfig(int argc, char* argv[]) {
    StartupConfig config;
    bool shellSeen = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
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
        constexpr std::string_view prefix = "--runtime-shell=";
        if (!argument.starts_with(prefix))
            continue;

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
    }

    return config;
}

}  // namespace

int runReferenceRenderInteractive(int frames);

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
        return runReferenceMergeValidation(std::string(config.referenceMergeReport)) ? 0 : 1;
    }

    if (config.validateReferenceFeedback) {
        if (config.shell != RuntimeShell::App3D || config.referenceFeedbackReport.empty()) {
            std::cerr << "[APP3D] Reference feedback validation requires app3d shell and report path.\n";
            return 2;
        }
        return runReferenceFeedbackValidation(std::string(config.referenceFeedbackReport)) ? 0 : 1;
    }

    if (config.validateReferenceFinal) {
        if (config.shell != RuntimeShell::App3D || config.referenceFinalReport.empty()) {
            std::cerr << "[APP3D] Reference final validation requires app3d shell and report path.\n";
            return 2;
        }
        return runReferenceFinalValidation(std::string(config.referenceFinalReport)) ? 0 : 1;
    }

    if (config.referenceRenderFrames >= 0) {
        if (config.shell != RuntimeShell::App3D) {
            std::cerr << "[APP3D] Reference render requires --runtime-shell=app3d.\n";
            return 2;
        }
        return runReferenceRenderInteractive(config.referenceRenderFrames);
    }

    if (config.shell == RuntimeShell::Legacy) {
        std::cout << "[APP3D] shell=legacy runtimeBackend=legacy-direct\n";
        return runLegacyDemo3DRuntime(
            argc, argv, {.shellName = "legacy", .runtimeBackendName = "legacy-direct"});
    }

    std::unique_ptr<RuntimeBackend> backend = std::make_unique<Demo3DBackend>();
    std::cout << "[APP3D] shell=app3d runtimeBackend=" << backend->name() << "\n";
    return backend->run(argc, argv);
}

int runReferenceRenderInteractive(int frames) {
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

    ReferenceRcPipeline pipeline;
    if (!pipeline.initialize()) {
        std::cerr << "[REFERENCE] pipeline init failed\n";
        CloseWindow();
        return 1;
    }
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
        DrawText(TextFormat("Reference C0 view | frame %d | gen %llu | ESC quit | F12 shot",
                            frame, (unsigned long long)pipeline.generation()),
                 8, 8, 10, LIME);
        EndDrawing();
        if (IsKeyPressed(KEY_F12))
            TakeScreenshot(TextFormat("reference_view_%d.png", frame));
        ++frame;
        if (frames > 0 && frame >= frames)
            break;
    }

    glDeleteTextures(1, &target);
    CloseWindow();
    return 0;
}
