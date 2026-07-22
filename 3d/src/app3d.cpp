#include "app3d.h"
#include "legacy_demo3d_runtime.h"

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

    if (config.shell == RuntimeShell::Legacy) {
        std::cout << "[APP3D] shell=legacy runtimeBackend=legacy-direct\n";
        return runLegacyDemo3DRuntime(
            argc, argv, {.shellName = "legacy", .runtimeBackendName = "legacy-direct"});
    }

    std::unique_ptr<RuntimeBackend> backend = std::make_unique<Demo3DBackend>();
    std::cout << "[APP3D] shell=app3d runtimeBackend=" << backend->name() << "\n";
    return backend->run(argc, argv);
}
