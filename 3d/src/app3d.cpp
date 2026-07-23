#include "app3d.h"
#include "legacy_demo3d_runtime.h"
#include "reference_cornell_scene.h"
#include "reference_layout_validation.h"
#include "reference_merge_validation.h"
#include "reference_feedback_validation.h"
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
    std::string_view referenceSceneReport;
    std::string_view referenceLayoutReport;
    std::string_view referenceTransportReport;
    std::string_view referenceMergeReport;
    std::string_view referenceFeedbackReport;
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

    if (config.shell == RuntimeShell::Legacy) {
        std::cout << "[APP3D] shell=legacy runtimeBackend=legacy-direct\n";
        return runLegacyDemo3DRuntime(
            argc, argv, {.shellName = "legacy", .runtimeBackendName = "legacy-direct"});
    }

    std::unique_ptr<RuntimeBackend> backend = std::make_unique<Demo3DBackend>();
    std::cout << "[APP3D] shell=app3d runtimeBackend=" << backend->name() << "\n";
    return backend->run(argc, argv);
}
