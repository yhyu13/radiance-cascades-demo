#ifndef LEGACY_DEMO3D_RUNTIME_H
#define LEGACY_DEMO3D_RUNTIME_H

#include <string_view>

struct LegacyRuntimeLaunch {
    std::string_view shellName;
    std::string_view runtimeBackendName;
};

int runLegacyDemo3DRuntime(int argc, char* argv[], const LegacyRuntimeLaunch& launch);

#endif
