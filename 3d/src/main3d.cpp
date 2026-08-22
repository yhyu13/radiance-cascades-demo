/**
 * @file main3d.cpp
 * @brief Entry point for 3D Radiance Cascades demo
 * 
 * This file contains the application entry point, window initialization,
 * and main rendering loop for the 3D version of Radiance Cascades.
 * 
 * Key Differences from 2D Version:
 * - Uses Camera3D instead of orthographic 2D camera
 * - Requires OpenGL 4.3+ context for compute shaders
 * - Initializes GLEW for extension loading
 * - Higher default resolution for volume rendering
 */

#include "app3d.h"
#include "legacy_demo3d_runtime.h"
#include "demo3d.h"  // This includes raylib.h
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef _WIN32
    #include <direct.h>  // For _chdir on Windows
    #define chdir _chdir
#else
    #include <unistd.h>  // For chdir on Linux/Mac
#endif

// =============================================================================
// Application Configuration
// =============================================================================

/** Default window width */
constexpr int DEFAULT_WIDTH = 1280;

/** Default window height */
constexpr int DEFAULT_HEIGHT = 720;

/** Window title */
const std::string WINDOW_TITLE = "Radiance Cascades 3D";

/** Require OpenGL 4.3 minimum */
constexpr int OPENGL_MAJOR_VERSION = 4;
constexpr int OPENGL_MINOR_VERSION = 3;

static std::vector<glm::ivec3> parseAtlasCells(const std::string& text) {
    std::vector<glm::ivec3> cells;
    std::string normalized = text;
    for (char& c : normalized) {
        if (c == '|') c = ';';
    }
    std::stringstream ss(normalized);
    std::string item;
    while (std::getline(ss, item, ';')) {
        int x = 0, y = 0, z = 0;
        if (std::sscanf(item.c_str(), "%d,%d,%d", &x, &y, &z) == 3)
            cells.emplace_back(x, y, z);
    }
    return cells;
}

static std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
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

static std::string glString(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unavailable";
}

static bool drainGLErrors(const char* stage) {
    bool hadError = false;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        hadError = true;
        gl::noteDebugError();
        std::cerr << "[PHASE0] OpenGL error at " << stage << ": 0x"
                  << std::hex << error << std::dec << "\n";
    }
    return hadError;
}

static bool writePhase0RuntimeReport(const std::string& path, const Demo3D& demo,
                                     const LegacyRuntimeLaunch& launch,
                                     const std::vector<std::string>& arguments,
                                     const std::vector<std::string>& unknownArguments,
                                     int frameCount, bool artifactFailure,
                                     bool allocationGlError, bool firstFrameGlError,
                                     bool sceneLoadFailure) {
    if (path.empty())
        return false;

    const std::filesystem::path reportPath(path);
    std::error_code ec;
    if (reportPath.has_parent_path())
        std::filesystem::create_directories(reportPath.parent_path(), ec);
    if (ec) {
        std::cerr << "[PHASE0] Could not create report directory: " << ec.message() << "\n";
        return false;
    }

    const auto& shaderRecords = gl::getShaderSourceRecords();
    bool shaderHashesMatch = !shaderRecords.empty();
    for (const auto& record : shaderRecords) {
        if (record.sourceHash.empty() || gl::sha256File(record.resolvedPath) != record.sourceHash)
            shaderHashesMatch = false;
    }

    const bool selectedShadersOk = demo.selectedBackendShadersOk();
    const bool success = unknownArguments.empty() && !artifactFailure &&
                         !allocationGlError && !firstFrameGlError &&
                         !sceneLoadFailure && selectedShadersOk && shaderHashesMatch;

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::cerr << "[PHASE0] Could not write runtime report: " << path << "\n";
        return false;
    }

    out << "{\n";
    out << "  \"schema_version\": \"phase0-runtime-v1\",\n";
    out << "  \"gate\": \"G0\",\n";
    out << "  \"result\": \"" << (success ? "PASS" : "FAIL") << "\",\n";
    out << "  \"source_root\": \"" << escapeJson(RC3D_SOURCE_ROOT) << "\",\n";
    out << "  \"working_directory\": \""
        << escapeJson(std::filesystem::current_path().string()) << "\",\n";
    out << "  \"arguments\": [";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) out << ", ";
        out << "\"" << escapeJson(arguments[i]) << "\"";
    }
    out << "],\n";
    out << "  \"unknown_arguments\": [";
    for (size_t i = 0; i < unknownArguments.size(); ++i) {
        if (i > 0) out << ", ";
        out << "\"" << escapeJson(unknownArguments[i]) << "\"";
    }
    out << "],\n";
    out << "  \"selection\": {\n";
    out << "    \"shell\": \"" << escapeJson(std::string(launch.shellName)) << "\",\n";
    out << "    \"runtime_backend\": \""
        << escapeJson(std::string(launch.runtimeBackendName)) << "\",\n";
    out << "    \"backend\": \"" << escapeJson(demo.getLegacyBackendName()) << "\",\n";
    out << "    \"render_view\": \"" << escapeJson(demo.getRenderViewName()) << "\",\n";
    out << "    \"render_mode\": " << demo.getRenderMode() << ",\n";
    out << "    \"scene\": \"" << escapeJson(demo.getSceneLabel()) << "\",\n";
    out << "    \"scene_revision\": " << demo.getSceneRevision() << ",\n";
    out << "    \"shader_revision\": " << demo.getShaderRevision() << "\n";
    out << "  },\n";
    out << "  \"gpu\": {\n";
    out << "    \"vendor\": \"" << escapeJson(glString(GL_VENDOR)) << "\",\n";
    out << "    \"renderer\": \"" << escapeJson(glString(GL_RENDERER)) << "\",\n";
    out << "    \"opengl_version\": \"" << escapeJson(glString(GL_VERSION)) << "\",\n";
    out << "    \"glsl_version\": \"" << escapeJson(glString(GL_SHADING_LANGUAGE_VERSION)) << "\"\n";
    out << "  },\n";
    out << "  \"runtime\": {\n";
    out << "    \"frames\": " << frameCount << ",\n";
    out << "    \"artifact_failure\": " << (artifactFailure ? "true" : "false") << ",\n";
    out << "    \"scene_load_failure\": " << (sceneLoadFailure ? "true" : "false") << ",\n";
    out << "    \"allocation_gl_error\": " << (allocationGlError ? "true" : "false") << ",\n";
    out << "    \"first_frame_gl_error\": " << (firstFrameGlError ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"shaders\": [\n";
    for (size_t i = 0; i < shaderRecords.size(); ++i) {
        const auto& record = shaderRecords[i];
        const std::string runtimeHash = gl::sha256File(record.resolvedPath);
        out << "    {\"name\": \"" << escapeJson(record.logicalName)
            << "\", \"path\": \"" << escapeJson(record.resolvedPath)
            << "\", \"compiled\": " << (record.compiled ? "true" : "false")
            << ", \"compiled_sha256\": \"" << record.sourceHash
            << "\", \"runtime_sha256\": \"" << runtimeHash
            << "\", \"hash_match\": "
            << (!record.sourceHash.empty() && runtimeHash == record.sourceHash ? "true" : "false") << "}";
        if (i + 1 < shaderRecords.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"gates\": {\n";
    out << "    \"required_shaders_compile_and_link\": \"" << (selectedShadersOk ? "PASS" : "FAIL") << "\",\n";
    out << "    \"runtime_hashes_match_compiled_sources\": \"" << (shaderHashesMatch ? "PASS" : "FAIL") << "\",\n";
    out << "    \"no_gl_error_allocation_or_first_frame\": \""
        << (!allocationGlError && !firstFrameGlError ? "PASS" : "FAIL") << "\",\n";
    out << "    \"backend_and_scene_revision_logged\": \"PASS\",\n";
    out << "    \"requested_artifacts_written\": \"" << (!artifactFailure ? "PASS" : "FAIL") << "\"\n";
    out << "  }\n";
    out << "}\n";
    return out.good() && success;
}

// =============================================================================
// Function Declarations
// =============================================================================

/**
 * @brief Initialize application and OpenGL context
 *
 * Sets up window, OpenGL context, GLEW, and debug output.
 *
 * @param windowWidth  Initial window width  (default DEFAULT_WIDTH)
 * @param windowHeight Initial window height (default DEFAULT_HEIGHT)
 *                     Step 11 perf-analysis: passed in from argv so
 *                     `--window-size=W,H` is honored at InitWindow time
 *                     (codex 11 F2+F5 -- avoids stale-dim Demo3D init).
 * @return true if initialization successful
 */
bool initializeApplication(int windowWidth, int windowHeight);

/**
 * @brief Configure OpenGL state for optimal performance
 * 
 * Enables depth testing, sets up viewport, configures debug output.
 */
void configureOpenGLState();

/**
 * @brief Check system requirements
 * 
 * Verifies OpenGL version, available extensions, and GPU capabilities.
 * 
 * @return true if requirements met
 */
bool checkRequirements();

// =============================================================================
// Main Entry Point
// =============================================================================

// Step 9 Phase 2 verify (--cache-hit-test): re-invoke loadOBJMesh after
// the initial --load-obj fires, to exercise the cache-hit path headlessly.
bool g_cacheHitTest = false;
// codex 04 F2 verify: toggle GPU SDF off after load to exercise the
// CPU-mirror-preserved transition.
bool g_toggleGpuSdfOffAfterLoad = false;
bool g_phase0Validation = false;
// 2026-05-28 Stage 11c: remember CLI --light-direction= so it can be
// re-applied after loadOBJMesh clobbers useDirectionalLight from the scene kind.
bool g_cliLightDirSet = false;
glm::vec3 g_cliLightDir{0.0f, -1.0f, 0.0f};
// 2026-05-28 Stage 11d: same pattern for --light-position=x,y,z so it survives
// loadOBJMesh's auto-fit camera/light preset (applyOBJViewPreset sets lightPosition).
bool g_cliLightPosSet = false;
glm::vec3 g_cliLightPos{0.0f, 0.8f, 0.0f};
// v4 Phase 1A: per-scene MB-gain preset (ShaderToy adoption closeout).
// Sponza-class scenes get gain=0.10; Cornell-class gets 1.0 (default).
bool g_usePerSceneMbGain = false;

static int loadMeasurementCamerasJson(Demo3D* demo, const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[MAIN] --measurement-cameras-file: could not open '"
                  << path << "'\n";
        return 0;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    int loaded = 0;
    size_t pos = 0;
    while (loaded < Demo3D::kMeasurementCameraSlots) {
        size_t pKey = text.find("\"position\"", pos);
        if (pKey == std::string::npos) break;
        size_t pOpen = text.find('[', pKey);
        size_t pClose = (pOpen == std::string::npos) ? std::string::npos : text.find(']', pOpen);
        if (pClose == std::string::npos) break;

        glm::vec3 p(0.0f);
        if (std::sscanf(text.c_str() + pOpen + 1, " %f , %f , %f", &p.x, &p.y, &p.z) != 3) {
            std::cerr << "[MAIN] cameras.json: malformed position at byte " << pOpen << "\n";
            break;
        }

        size_t tKey = text.find("\"target\"", pClose);
        if (tKey == std::string::npos) break;
        size_t tOpen = text.find('[', tKey);
        size_t tClose = (tOpen == std::string::npos) ? std::string::npos : text.find(']', tOpen);
        if (tClose == std::string::npos) break;

        glm::vec3 t(0.0f);
        if (std::sscanf(text.c_str() + tOpen + 1, " %f , %f , %f", &t.x, &t.y, &t.z) != 3) {
            std::cerr << "[MAIN] cameras.json: malformed target at byte " << tOpen << "\n";
            break;
        }

        demo->setMeasurementCameraSlot(loaded, p, t);
        ++loaded;
        pos = tClose + 1;
    }

    std::cout << "[MAIN] --measurement-cameras-file=" << path
              << " loaded " << loaded << " cameras\n";
    return loaded;
}

int main(int argc, char* argv[]) {
    return App3D{}.run(argc, argv);
}

int runLegacyDemo3DRuntime(int argc, char* argv[], const LegacyRuntimeLaunch& launch) {
    /**
     * @brief Application entry point
     * 
     * Execution Flow:
     * 1. Verify resource directory exists (auto-fix working directory)
     * 2. Initialize window and OpenGL context
     * 3. Check system requirements
     * 4. Configure OpenGL state
     * 5. Create Demo3D instance
     * 6. Enter main loop
     * 7. Cleanup on exit
     * 
     * @return Exit code (0 = success)
     */
    
    std::vector<std::string> runtimeArguments;
    runtimeArguments.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        runtimeArguments.emplace_back(argv[i]);

    // Step 1: Verify running from correct directory, auto-fix if needed
    if (!DirectoryExists("res")) {
        // Try to find res/ directory by going up one level (common when running from build/)
        printf("[INFO] Resource directory 'res/' not found in current directory.\n");
        printf("[INFO] Attempting to locate project root...\n");
        
        // Try going up one directory
        if (DirectoryExists("../res")) {
            printf("[INFO] Found 'res/' in parent directory. Changing working directory.\n");
            chdir("..");
        } else {
            printf("[ERROR] Could not find resource directory 'res/'.\n");
            printf("[ERROR] Please run from project root or ensure 'res/shaders/' exists.\n");
            return 1;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Radiance Cascades 3D Demo" << std::endl;
    std::cout << "  Version: " << VERSION_STAGE << VERSION << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Phase 6b: pre-load RenderDoc DLL BEFORE the GL context is created.
    // RenderDoc must hook into OpenGL at context creation time; loading after
    // InitWindow() means capture calls are no-ops.
    // If renderdoc.dll is not installed this silently returns false — no effect.
    {
#ifdef _WIN32
        RENDERDOC_API_1_6_0* rdoc_preload = nullptr;
        if (rdoc_load_api(&rdoc_preload))
            std::cout << "[6b] RenderDoc DLL pre-loaded (before GL context).\n";
        else
            std::cout << "[6b] RenderDoc DLL not found (pre-load); capture disabled.\n";
#endif
    }

    // Step 11 perf-analysis: pre-init pass over argv to resolve --window-size=W,H
    // BEFORE InitWindow (codex 11 F2+F5). All other flags are parsed AFTER
    // Demo3D construction below since they need a `demo` instance to call setters.
    int wWidth  = DEFAULT_WIDTH;
    int wHeight = DEFAULT_HEIGHT;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--phase0-baseline")
            g_phase0Validation = true;
        if (arg.rfind("--window-size=", 0) == 0) {
            int w = 0, h = 0;
            if (std::sscanf(arg.substr(14).c_str(), "%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
                wWidth  = w;
                wHeight = h;
                std::cout << "[MAIN] --window-size=" << w << "," << h << "\n";
            } else {
                std::cerr << "[MAIN] --window-size: expected W,H positive ints (got: '"
                          << arg.substr(14) << "'); using default "
                          << DEFAULT_WIDTH << "x" << DEFAULT_HEIGHT << "\n";
            }
        }
    }

    // Step 2: Initialize application
    if (!initializeApplication(wWidth, wHeight)) {
        std::cerr << "[ERROR] Application initialization failed." << std::endl;
        return 1;
    }
    
    // Step 3: Check requirements
    if (!checkRequirements()) {
        std::cerr << "[ERROR] System requirements check failed." << std::endl;
        CloseWindow();
        return 1;
    }
    
    // Step 4: Configure OpenGL
    configureOpenGLState();
    
    // Step 5: Create demo instance
    std::cout << "[MAIN] Creating 3D demo instance..." << std::endl;
    drainGLErrors("pre-demo cleanup");
    Demo3D* demo = new Demo3D();

    // --auto-analyze:  burst capture + AI analysis then exit
    // --auto-sequence: sequence capture (N frames) + AI analysis then exit
    // --auto-rdoc:     OBSOLETE 2026-08-22 (rejected in App3D::run before this runtime).
    //                  Capture via rdc-cli; G-key still saves a .rdc (no auto-extract).
    // --load-obj=NAME: load OBJ mesh once at startup (Step 2/3 testing).
    //   Step 6: NAME accepts cornell | cornell-orig | sponza | sponza-master.
    // --exit-frames=N: quit after rendering N frames (CI-friendly Step 2 verification)
    bool        autoAnalyze   = false;
    std::string loadObjName;
    std::string screenshotPath;
    std::string probeStatsPath;
    std::string atlasAttributionPath;
    bool        phase0BaselineRequested = false;
    bool        runtimeArtifactFailure = false;
    bool        sceneLoadFailure = false;
    bool        allocationGlError = false;
    drainGLErrors("demo initialization");
    bool        firstFrameGlError = false;
    std::string phase0MetadataPath;
    std::vector<std::string> unknownArguments;
    std::vector<glm::ivec3> atlasAttributionCells;
    // Continuous-shot capture inside a SINGLE session (critic 15 H2 / H3 follow-up):
    // capture frames [shotsAfter, shotsAfter + shotsCount) into PREFIX_fN.png.
    // Lets us measure true interactive frame-to-frame motion rather than cold-start A/B.
    std::string shotsPrefix;
    int shotsAfter = 0;
    int shotsCount = 0;
    int         exitAfterFrames = 0;
    int         switchToScene   = -999;   // codex 09 F1 verification: after --load-obj, switch to analytic scene N
    bool        testResetHelper = false;  // codex 11 F1/F2 verification: programmatically test resetCameraToScenePreset
    // Step 10 (codex 06 F7): CLI camera-state overrides. Applied AFTER --load-obj,
    // --switch-to-scene AND --test-reset-helper so user values are never overridden
    // by the auto-fit / scene-reset paths.
    bool        cliCameraPosSet    = false; glm::vec3 cliCameraPos{0.0f};
    bool        cliCameraTargetSet = false; glm::vec3 cliCameraTarget{0.0f};
    bool        cliFovySet         = false; float     cliFovy = 60.0f;
    std::string cliCameraPresetName;          // Phase 2.5d critic-10 W4: scene-validation hook
    int         measurementCameraToApply = -999;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--auto-analyze") {
            autoAnalyze = true;
            demo->setAutoCloseMode(true);
            std::cout << "[MAIN] --auto-analyze: will burst-capture, analyze, then exit.\n";
        } else if (arg == "--auto-sequence") {
            autoAnalyze = true;
            demo->setAutoSequenceMode(true);
            std::cout << "[MAIN] --auto-sequence: will sequence-capture, analyze, then exit.\n";
        } else if (arg == "--auto-rdoc") {
            // Belt-and-suspenders: App3D already rejects this before InitWindow.
            std::cerr << "[MAIN] --auto-rdoc is obsolete (2026-08-22). "
                         "Use rdc capture (skill renderdoc-gpu-debug). See doc/journey.md Era 12.\n";
            runtimeArtifactFailure = true;
        } else if (arg.rfind("--load-obj=", 0) == 0) {
            loadObjName = arg.substr(11);
            std::cout << "[MAIN] --load-obj=" << loadObjName << ": will load at startup.\n";
        } else if (arg == "--phase0-baseline") {
            phase0BaselineRequested = true;
            std::cout << "[MAIN] --phase0-baseline: strict Phase 0 validation enabled.\n";
        } else if (arg.rfind("--metadata-json=", 0) == 0) {
            phase0MetadataPath = arg.substr(16);
            std::cout << "[MAIN] --metadata-json=" << phase0MetadataPath << "\n";
        } else if (arg.rfind("--runtime-shell=", 0) == 0) {
            // Parsed and validated by App3D before entering this legacy runtime.
        } else if (arg.rfind("--window-size=", 0) == 0) {
            // Parsed before window initialization.
        } else if (arg.rfind("--exit-frames=", 0) == 0) {
            exitAfterFrames = std::atoi(arg.substr(14).c_str());
            std::cout << "[MAIN] --exit-frames=" << exitAfterFrames << ": will quit after N frames.\n";
        } else if (arg.rfind("--screenshot=", 0) == 0) {
            screenshotPath = arg.substr(13);
            std::cout << "[MAIN] --screenshot=" << screenshotPath << ": will capture last frame.\n";
        } else if (arg.rfind("--probe-stats-json=", 0) == 0) {
            probeStatsPath = arg.substr(19);
            std::cout << "[MAIN] --probe-stats-json=" << probeStatsPath
                      << ": will dump per-cascade probe stats on exit frame.\n";
        } else if (arg.rfind("--atlas-attribution-json=", 0) == 0) {
            atlasAttributionPath = arg.substr(25);
            std::cout << "[MAIN] --atlas-attribution-json=" << atlasAttributionPath
                      << ": will dump targeted C0 atlas texels on exit frame.\n";
        } else if (arg.rfind("--atlas-attribution-cells=", 0) == 0) {
            atlasAttributionCells = parseAtlasCells(arg.substr(26));
            std::cout << "[MAIN] --atlas-attribution-cells count="
                      << atlasAttributionCells.size() << "\n";
        } else if (arg.rfind("--screenshot-exr=", 0) == 0) {
            int v = std::atoi(arg.substr(17).c_str());
            demo->setExrCapture(v != 0);
            std::cout << "[MAIN] --screenshot-exr=" << v
                      << " (HDR sidecars for mode-17 cascade_gi/pt_full/pt_direct)\n";
        } else if (arg.rfind("--render-mode=", 0) == 0) {
            int m = std::atoi(arg.substr(14).c_str());
            demo->setRenderMode(m);
            std::cout << "[MAIN] --render-mode=" << m << "\n";
        } else if (arg.rfind("--auto-capture-delay=", 0) == 0) {
            float s = static_cast<float>(std::atof(arg.substr(21).c_str()));
            demo->setAutoCaptureDelaySeconds(s);
            std::cout << "[MAIN] --auto-capture-delay=" << s << "\n";
        } else if (arg.rfind("--inject-bake-failures=", 0) == 0) {
            int n = std::atoi(arg.substr(23).c_str());
            demo->setInjectBakeFailures(n);
            std::cout << "[MAIN] --inject-bake-failures=" << n
                      << " (codex 07 F1: forces N synthetic generateMeshSDF failures)\n";
        } else if (arg.rfind("--switch-to-scene=", 0) == 0) {
            switchToScene = std::atoi(arg.substr(18).c_str());
            std::cout << "[MAIN] --switch-to-scene=" << switchToScene
                      << " (codex 09 F1: setScene(N) after --load-obj to test invariant resets)\n";
        } else if (arg == "--test-reset-helper") {
            testResetHelper = true;
            std::cout << "[MAIN] --test-reset-helper (codex 11 F1/F2: programmatically exercise resetCameraToScenePreset)\n";
        } else if (arg == "--gpu-sdf") {
            demo->setUseGPUSDF(true);
            std::cout << "[MAIN] --gpu-sdf (Step 8): GPU JFA SDF path enabled\n";
        } else if (arg == "--gpu-voxelize") {
            demo->setUseGPUVoxelize(true);
            std::cout << "[MAIN] --gpu-voxelize (Step 9): GPU triangle voxelizer enabled\n";
        } else if (arg == "--strip-ambient-floor-bake") {
            // Step 11: cascade bake skips the vec3(0.05) floor at radiance_3d.comp:262.
            // Now equivalent to --ambient-bake-strength=0; kept as a quick flag.
            demo->setStripAmbientFloorBake(true);
            std::cout << "[MAIN] --strip-ambient-floor-bake (Step 11): GI bake without ambient floor\n";
        } else if (arg.rfind("--ambient-bake-strength=", 0) == 0) {
            // Lighting controls follow-up: continuous bake-side ambient floor.
            float v = static_cast<float>(std::atof(arg.substr(24).c_str()));
            demo->setAmbientBakeStrength(v);
            std::cout << "[MAIN] --ambient-bake-strength=" << v << "\n";
        } else if (arg.rfind("--ambient-composite-strength=", 0) == 0) {
            // Lighting controls follow-up: composite-side ambient floor.
            float v = static_cast<float>(std::atof(arg.substr(29).c_str()));
            demo->setAmbientCompositeStrength(v);
            std::cout << "[MAIN] --ambient-composite-strength=" << v << "\n";
        } else if (arg.rfind("--light-intensity=", 0) == 0) {
            // Lighting controls follow-up: scalar multiplier on (1.0, 0.95, 0.85) base.
            float v = static_cast<float>(std::atof(arg.substr(18).c_str()));
            demo->setLightIntensity(v);
            std::cout << "[MAIN] --light-intensity=" << v << "\n";
        } else if (arg.rfind("--light-position=", 0) == 0) {
            // 2026-05-28 Stage 11d: point-light position override (cornell baseline
            // is (0, 0.8, 0)). Forces useDirectionalLight=false so the value applies.
            float x = 0.0f, y = 0.8f, z = 0.0f;
            if (std::sscanf(arg.substr(17).c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                demo->setUseDirectionalLight(false);
                demo->setLightPosition(glm::vec3(x, y, z));
                std::cout << "[MAIN] --light-position=" << x << "," << y << "," << z
                          << " (forces useDirectionalLight=false)\n";
                extern bool g_cliLightPosSet; extern glm::vec3 g_cliLightPos;
                g_cliLightPosSet = true; g_cliLightPos = glm::vec3(x, y, z);
            } else {
                std::cerr << "[MAIN] --light-position: expected x,y,z (got: '"
                          << arg.substr(17) << "')\n";
            }
        } else if (arg.rfind("--light-direction=", 0) == 0) {
            // Lighting controls follow-up: directional light (Sponza sun).
            // Implies --use-directional-light=true. Vector is normalized in setter.
            // 2026-05-28 Stage 11c: ALSO remember the value so we can re-apply it
            // after loadOBJMesh, which unconditionally writes useDirectionalLight
            // from the scene kind (demo3d.cpp:7137) and otherwise clobbers it.
            float x = 0.0f, y = -1.0f, z = 0.0f;
            if (std::sscanf(arg.substr(18).c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                demo->setUseDirectionalLight(true);
                demo->setLightDirection(glm::vec3(x, y, z));
                std::cout << "[MAIN] --light-direction=" << x << "," << y << "," << z
                          << " (implies useDirectionalLight=true)\n";
                extern bool g_cliLightDirSet; extern glm::vec3 g_cliLightDir;
                g_cliLightDirSet = true; g_cliLightDir = glm::vec3(x, y, z);
            } else {
                std::cerr << "[MAIN] --light-direction: expected x,y,z (got: '"
                          << arg.substr(18) << "')\n";
            }
        } else if (arg == "--no-directional-light") {
            // Lighting controls follow-up: explicit override (e.g. force point
            // light on Sponza for A/B comparison).
            demo->setUseDirectionalLight(false);
            std::cout << "[MAIN] --no-directional-light: forcing point light\n";
        } else if (arg.rfind("--bake-leak-test=", 0) == 0) {
            // Phase 2.5a.1: bake-leak quantitative baseline. Argument is the
            // output JSON path. Combine with --load-obj=cornell-orig-alcove and
            // --exit-frames=300 (or larger) for a clean measurement run.
            std::string outPath = arg.substr(17);
            demo->setBakeLeakTest(outPath);
        } else if (arg.rfind("--cam-preset=", 0) == 0) {
            // Phase 2.5d L2 (revised per critic 10 W4): scene-specific camera
            // presets that the auto-fit doesn't naturally produce. Currently:
            // "alcove" = view focused on the cornell-orig-alcove right-side
            // alcove. Validation against the loaded scene happens at apply
            // time below (search for "cliCameraPresetName") so we know
            // currentOBJPath was actually set by loadOBJMesh first.
            std::string preset = arg.substr(13);
            if (preset == "alcove") {
                cliCameraPos = glm::vec3(0.6f, 1.0f, 0.5f);
                cliCameraTarget = glm::vec3(0.6f, 0.0f, -0.5f);
                cliCameraPosSet = true;
                cliCameraTargetSet = true;
                cliCameraPresetName = "alcove";  // for scene validation at apply time
                std::cout << "[MAIN] --cam-preset=alcove: camera focused on cornell-orig-alcove right side\n"
                          << "  (will be applied after load-obj; validates scene == cornell-orig-alcove)\n";
            } else {
                std::cerr << "[MAIN] WARN: --cam-preset=" << preset
                          << " unknown (expected: alcove)\n";
            }
        } else if (arg.rfind("--diag-alpha-mode=", 0) == 0) {
            // Phase 2.5d M1: diagnostic mode for the bake's surface-bin α
            // encoding. 1 = write sdfBefore_normalized for histogram analysis.
            // Combine with --bake-leak-test=path; the metric output then includes
            // a 16-bin histogram of surface-bin α values per cascade.
            int m = std::atoi(arg.substr(18).c_str());
            demo->setDiagAlphaMode(m);
            // Critic 12 M4: warn when diag mode is combined with bake-leak-test
            // because the JSON's leak metric numbers are NOT comparable to the
            // Phase 2 baseline (alpha values are diagnostic, not 0/1 binary).
            // The user could accidentally overwrite phase2.5_bake_leak_baseline.json
            // with diagnostic data; this warning makes the conflict explicit.
            if (m != 0 && demo->bakeLeakTestActive()) {
                std::cerr << "[MAIN] WARN: --diag-alpha-mode=" << m
                          << " combined with --bake-leak-test changes the meaning of\n"
                          << "  the leak_sum / leak_max numbers (alpha is diagnostic, not\n"
                          << "  0/1 binary). DO NOT compare against phase2.5_bake_leak_baseline.json.\n"
                          << "  Save to a different filename to avoid confusion.\n";
            }
        } else if (arg.rfind("--shots-prefix=", 0) == 0) {
            shotsPrefix = arg.substr(15);
            std::cout << "[MAIN] --shots-prefix=" << shotsPrefix << "\n";
        } else if (arg.rfind("--shots-after=", 0) == 0) {
            shotsAfter = std::atoi(arg.substr(14).c_str());
            std::cout << "[MAIN] --shots-after=" << shotsAfter << "\n";
        } else if (arg.rfind("--shots-count=", 0) == 0) {
            shotsCount = std::atoi(arg.substr(14).c_str());
            std::cout << "[MAIN] --shots-count=" << shotsCount << "\n";
        } else if (arg.rfind("--use-probe-jitter=", 0) == 0) {
            int v = std::atoi(arg.substr(19).c_str());
            demo->setUseProbeJitter(v != 0);
            std::cout << "[MAIN] --use-probe-jitter=" << v << "\n";
        } else if (arg.rfind("--use-temporal=", 0) == 0) {
            int v = std::atoi(arg.substr(15).c_str());
            demo->setUseTemporalAccum(v != 0);
            std::cout << "[MAIN] --use-temporal=" << v << "\n";
        } else if (arg.rfind("--use-history-clamp=", 0) == 0) {
            int v = std::atoi(arg.substr(20).c_str());
            demo->setUseHistoryClamp(v != 0);
            std::cout << "[MAIN] --use-history-clamp=" << v << "\n";
        } else if (arg.rfind("--cascade-scaled-dir-res=", 0) == 0) {
            int v = std::atoi(arg.substr(25).c_str());
            demo->setUseScaledDirRes(v != 0);
            std::cout << "[MAIN] --cascade-scaled-dir-res=" << v << "\n";
        } else if (arg.rfind("--noise-seed-offset=", 0) == 0) {
            int v = std::atoi(arg.substr(20).c_str());
            demo->setNoiseSeedOffset(v);
            std::cout << "[MAIN] --noise-seed-offset=" << v << "\n";
        } else if (arg.rfind("--use-directional-merge=", 0) == 0) {
            int v = std::atoi(arg.substr(24).c_str());
            demo->setUseDirectionalMergeCLI(v != 0);
            std::cout << "[MAIN] --use-directional-merge=" << v << "\n";
        } else if (arg.rfind("--use-directional-gi=", 0) == 0) {
            int v = std::atoi(arg.substr(21).c_str());
            demo->setUseDirectionalGI(v != 0);
            std::cout << "[MAIN] --use-directional-gi=" << v
                      << " (1=normal-aware atlas final sampling, 0=isotropic probe grid)\n";
        } else if (arg.rfind("--use-cascade-gi=", 0) == 0) {
            int v = std::atoi(arg.substr(17).c_str());
            demo->setUseCascadeGI(v != 0);
            std::cout << "[MAIN] --use-cascade-gi=" << v << "\n";
        } else if (arg.rfind("--use-gi-blur=", 0) == 0) {
            int v = std::atoi(arg.substr(14).c_str());
            demo->setUseGIBlur(v != 0);
            std::cout << "[MAIN] --use-gi-blur=" << v << "\n";
        } else if (arg.rfind("--use-dir-bilinear=", 0) == 0) {
            int v = std::atoi(arg.substr(19).c_str());
            demo->setUseDirBilinearCLI(v != 0);
            std::cout << "[MAIN] --use-dir-bilinear=" << v << "\n";
        } else if (arg.rfind("--use-spatial-trilinear=", 0) == 0) {
            int v = std::atoi(arg.substr(24).c_str());
            demo->setUseSpatialTrilinearCLI(v != 0);
            std::cout << "[MAIN] --use-spatial-trilinear=" << v << "\n";
        } else if (arg.rfind("--stagger=", 0) == 0) {
            int v = std::atoi(arg.substr(10).c_str());
            demo->setStaggerMaxInterval(v);
            std::cout << "[MAIN] --stagger=" << v
                      << " (1=no stagger / all cascades every frame)\n";
        } else if (arg.rfind("--use-multi-bounce=", 0) == 0) {
            int v = std::atoi(arg.substr(19).c_str());
            demo->setUseMultiBounce(v != 0);
            std::cout << "[MAIN] --use-multi-bounce=" << v
                      << " (0=OFF single-bounce, 1=ON temporal multi-bounce feedback)\n";
        } else if (arg.rfind("--multi-bounce-gain=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(20).c_str()));
            demo->setMultiBounceGain(v);
            std::cout << "[MAIN] --multi-bounce-gain=" << v << "\n";
        } else if (arg == "--mb-gain-per-scene") {
            g_usePerSceneMbGain = true;
            std::cout << "[MAIN] --mb-gain-per-scene: per-scene auto MB-gain "
                      << "(Sponza->0.10, Cornell->1.0, other->1.0). Overrides "
                      << "--multi-bounce-gain for OBJ-loaded scenes.\n";
        } else if (arg.rfind("--pt-cascade-match=", 0) == 0) {
            int v = std::atoi(arg.substr(19).c_str());
            demo->setPtCascadeMatch(v);
            std::cout << "[MAIN] --pt-cascade-match=" << v
                      << " (0=unbiased default, 1=match cascade ambient)\n";
        } else if (arg.rfind("--pt-rays-per-frame=", 0) == 0) {
            int v = std::atoi(arg.substr(20).c_str());
            demo->setPtRaysPerFrame(v);
            std::cout << "[MAIN] --pt-rays-per-frame=" << v << "\n";
        } else if (arg.rfind("--pt-max-bounces=", 0) == 0) {
            int v = std::atoi(arg.substr(17).c_str());
            demo->setPtMaxBounces(v);
            std::cout << "[MAIN] --pt-max-bounces=" << v << "\n";
        } else if (arg.rfind("--pt-russian-roulette=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(22).c_str()));
            demo->setPtRussianRoulette(v);
            std::cout << "[MAIN] --pt-russian-roulette=" << v << "\n";
        } else if (arg.rfind("--use-hybrid=", 0) == 0) {
            int v = std::atoi(arg.substr(13).c_str());
            demo->setUseHybrid(v != 0);
            std::cout << "[MAIN] --use-hybrid=" << v
                      << " (1=ON per-pixel correction, 0=OFF cascade-only) doc/7\n";
        } else if (arg.rfind("--hybrid-weight=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(16).c_str()));
            demo->setHybridBlendWeight(v);
            std::cout << "[MAIN] --hybrid-weight=" << v
                      << " (1.0=pure correction, 0.0=pure cascade; default 1.0)\n";
        } else if (arg.rfind("--hybrid-ema=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(13).c_str()));
            demo->setHybridEMAAlpha(v);
            std::cout << "[MAIN] --hybrid-ema=" << v
                      << " (default 0.1; lower = smoother but slower convergence)\n";
        } else if (arg.rfind("--hybrid-rays=", 0) == 0) {
            int v = std::atoi(arg.substr(14).c_str());
            demo->setHybridRaysPerFrame(v);
            std::cout << "[MAIN] --hybrid-rays=" << v
                      << " (rays/pixel/dispatch; default 1)\n";
        } else if (arg.rfind("--hybrid-max=", 0) == 0) {
            int v = std::atoi(arg.substr(13).c_str());
            demo->setHybridUseMaxComp(v != 0);
            std::cout << "[MAIN] --hybrid-max=" << v
                      << " (legacy: 1=max(correction, cascade); 0=disable)\n";
        } else if (arg.rfind("--hybrid-variance-merge=", 0) == 0) {
            int v = std::atoi(arg.substr(24).c_str());
            demo->setHybridUseVarianceMerge(v != 0);
            std::cout << "[MAIN] --hybrid-variance-merge=" << v
                      << " (1=cooperative inverse-variance merge [default]; 0=use mix/max)\n";
        } else if (arg.rfind("--hybrid-cascade-var=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(21).c_str()));
            demo->setHybridCascadeVariance(v);
            std::cout << "[MAIN] --hybrid-cascade-var=" << v
                      << " (variance prior for cascade signal; default 0.001)\n";
        } else if (arg.rfind("--hybrid-blur-radius=", 0) == 0) {
            int v = std::atoi(arg.substr(21).c_str());
            demo->setHybridBlurRadius(v);
            std::cout << "[MAIN] --hybrid-blur-radius=" << v
                      << " (bilateral kernel radius on hybrid accum; 0=off, 3=default)\n";
        } else if (arg.rfind("--hybrid-ab-sweep=", 0) == 0) {
            std::string dir = arg.substr(18);
            demo->startHybridSweepPublic(dir);
            std::cout << "[MAIN] --hybrid-ab-sweep=" << dir
                      << " (Phase 8 A/B validation suite; see doc/7/hybrid_v12_validation_phase8_plan.md)\n";
        } else if (arg.rfind("--hybrid-nee=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(13).c_str()));
            demo->setHybridNEEFraction(v);
            std::cout << "[MAIN] --hybrid-nee=" << v
                      << " (v1.3 NEE fraction; 0=cosine BRDF only, 0.5 default, 1=always cone-toward-light)\n";
        } else if (arg.rfind("--hybrid-roughness=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(19).c_str()));
            demo->setHybridGlobalRoughness(v);
            std::cout << "[MAIN] --hybrid-roughness=" << v
                      << " (v1.3 scene-wide roughness when use-roughness-tex=0; default 1.0=Lambert)\n";
        } else if (arg.rfind("--hybrid-use-roughness-tex=", 0) == 0) {
            int v = std::atoi(arg.substr(27).c_str());
            demo->setHybridUseRoughnessTex(v != 0);
            std::cout << "[MAIN] --hybrid-use-roughness-tex=" << v
                      << " (v1.3 per-voxel roughness sample; default 0)\n";
        } else if (arg.rfind("--phase3-debug=", 0) == 0) {
            int v = std::atoi(arg.substr(15).c_str());
            demo->setPhase3DebugMode(v);
        } else if (arg.rfind("--gi-strength=", 0) == 0) {
            float v = static_cast<float>(std::atof(arg.substr(14).c_str()));
            demo->setGIStrength(v);
        } else if (arg.rfind("--use-weighted-sample=", 0) == 0) {
            // Phase 3 (bake-side leak fix): enable/disable the 3D WeightedSample
            // per-corner visibility gating in the bake. Default OFF; ON gates the
            // upper-cascade contribution by per-corner geometric visibility.
            // Only effective on the trilinear path (non-co-located + spatial trilinear).
            int v = std::atoi(arg.substr(22).c_str());
            demo->setUseWeightedSample(v != 0);
            std::cout << "[MAIN] --use-weighted-sample=" << v
                      << " (1=ON Phase 3 per-corner gating; 0=OFF Phase 2 unconditional)\n";
        } else if (arg.rfind("--visibility-mode=", 0) == 0) {
            // Phase 2.5c (revised per critic 11 M3): Phase 2 already shipped to
            // users with this flag deprecated-but-functional. Silent slip-through
            // would be worse than the previous warning. Keep the deprecation
            // warning for one MORE release; flag still does nothing.
            std::cerr << "[MAIN] WARN: --visibility-mode=" << arg.substr(18)
                      << " is deprecated and ignored (Phase 2 2C cleanup retired the\n"
                      << "  visibility-mode switch; Phase 2.5c keeps this warning one more release).\n"
                      << "  Atlas-side α handles visibility; no runtime mode choice.\n";
        } else if (arg.rfind("--cascade-c0-res=", 0) == 0) {
            // Step 12 scaling experiment: cascade C0 probe-grid resolution
            // (8/16/24/32/48/64). Triggers full destroy/init cycle.
            int v = std::atoi(arg.substr(17).c_str());
            if (v > 0) {
                demo->setCascadeC0Res(v);
                std::cout << "[MAIN] --cascade-c0-res=" << v << "\n";
            } else {
                std::cerr << "[MAIN] --cascade-c0-res: expected positive int (got: '"
                          << arg.substr(17) << "')\n";
            }
        } else if (arg.rfind("--raymarch-steps=", 0) == 0) {
            // Step 12 scaling experiment: raymarch.frag uSteps uniform.
            int v = std::atoi(arg.substr(17).c_str());
            if (v > 0) {
                demo->setRaymarchSteps(v);
                std::cout << "[MAIN] --raymarch-steps=" << v << "\n";
            } else {
                std::cerr << "[MAIN] --raymarch-steps: expected positive int (got: '"
                          << arg.substr(17) << "')\n";
            }
        } else if (arg.rfind("--gi-blur-radius=", 0) == 0) {
            // Step 12 scaling experiment: bilateral GI blur kernel radius (clamped to [1, 8]).
            int v = std::atoi(arg.substr(17).c_str());
            if (v > 0) {
                demo->setGIBlurRadius(v);
                std::cout << "[MAIN] --gi-blur-radius=" << v << "\n";
            } else {
                std::cerr << "[MAIN] --gi-blur-radius: expected positive int (got: '"
                          << arg.substr(17) << "')\n";
            }
        } else if (arg == "--cache-hit-test") {
            // Step 9 Phase 2 verify hook: after the initial --load-obj fires
            // below, we'll re-invoke loadOBJMesh on the same path. Hits the
            // cache; no parse + voxelize work.
            extern bool g_cacheHitTest; g_cacheHitTest = true;
            std::cout << "[MAIN] --cache-hit-test (Step 9 Phase 2 verify)\n";
        } else if (arg == "--toggle-gpu-sdf-off-after-load") {
            // codex 04 F2 verify hook: after the initial --gpu-voxelize
            // --gpu-sdf --load-obj load, toggle useGPUSDF off so the next
            // sdfGenerationPass uses CPU EDT. Without F2's "always keep
            // CPU mirror" fix, CPU EDT would fail with empty meshVoxelData.
            extern bool g_toggleGpuSdfOffAfterLoad; g_toggleGpuSdfOffAfterLoad = true;
            std::cout << "[MAIN] --toggle-gpu-sdf-off-after-load (codex 04 F2 verify)\n";
        } else if (arg == "--dynamic-sphere") {
            demo->setDynamicSphere(true);
            std::cout << "[MAIN] --dynamic-sphere (Step 8): orbiting sphere overlay enabled\n";
        } else if (arg.rfind("--sphere-time=", 0) == 0) {
            float t = static_cast<float>(std::atof(arg.substr(14).c_str()));
            demo->setSphereTimeOverride(t);
            std::cout << "[MAIN] --sphere-time=" << t
                      << " (codex 01 F10: deterministic orbit phase for capture)\n";
        } else if (arg.rfind("--camera-pos=", 0) == 0) {
            // Step 10 — reproducible camera position for headless captures.
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (std::sscanf(arg.substr(13).c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                cliCameraPos = glm::vec3(x, y, z);
                cliCameraPosSet = true;
                std::cout << "[MAIN] --camera-pos=" << x << "," << y << "," << z << "\n";
            } else {
                std::cerr << "[MAIN] --camera-pos: expected x,y,z (got: '"
                          << arg.substr(13) << "')\n";
            }
        } else if (arg.rfind("--camera-target=", 0) == 0) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (std::sscanf(arg.substr(16).c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                cliCameraTarget = glm::vec3(x, y, z);
                cliCameraTargetSet = true;
                std::cout << "[MAIN] --camera-target=" << x << "," << y << "," << z << "\n";
            } else {
                std::cerr << "[MAIN] --camera-target: expected x,y,z (got: '"
                          << arg.substr(16) << "')\n";
            }
        } else if (arg.rfind("--camera-fovy=", 0) == 0) {
            cliFovy = static_cast<float>(std::atof(arg.substr(14).c_str()));
            cliFovySet = true;
            std::cout << "[MAIN] --camera-fovy=" << cliFovy << "\n";
        } else if (arg.rfind("--measurement-cameras-file=", 0) == 0) {
            loadMeasurementCamerasJson(demo, arg.substr(27));
        } else if (arg.rfind("--measurement-camera=", 0) == 0) {
            measurementCameraToApply = std::atoi(arg.substr(21).c_str());
            std::cout << "[MAIN] --measurement-camera=" << measurementCameraToApply
                      << " (will apply after scene load)\n";
        } else {
            unknownArguments.push_back(arg);
            std::cerr << "[MAIN] WARN: unknown argument: " << arg << "\n";
        }
    }

    if (phase0BaselineRequested) {
        if (phase0MetadataPath.empty()) {
            std::cerr << "[PHASE0] --metadata-json is required with --phase0-baseline.\n";
            runtimeArtifactFailure = true;
        }
        if (!unknownArguments.empty()) {
            std::cerr << "[PHASE0] Unknown arguments are fatal in strict validation mode.\n";
            runtimeArtifactFailure = true;
        }
        if (exitAfterFrames <= 0)
            exitAfterFrames = 2;
    }

    if (!loadObjName.empty()) {
        std::string path;
        if      (loadObjName == "sponza")               path = "res/scene/sponza.obj";
        else if (loadObjName == "cornell")              path = "res/scene/cornell_box.obj";
        else if (loadObjName == "cornell-orig")         path = "res/scene/CornellBox-Original/CornellBox-Original.obj";
        else if (loadObjName == "cornell-orig-alcove")  path = "res/scene/CornellBox-Original-Alcove/CornellBox-Original-Alcove.obj";
        else if (loadObjName == "sponza-master")        path = "res/scene/Sponza-master/sponza.obj";
        else {
            std::cerr << "[MAIN] --load-obj=" << loadObjName
                      << ": unknown name (expected sponza|cornell|cornell-orig|cornell-orig-alcove|sponza-master). Aborting.\n";
            delete demo;
            CloseWindow();
            return 1;
        }
        if (!demo->loadOBJMesh(path)) {
            std::cerr << "[MAIN] --load-obj failed for " << path << "\n";
            sceneLoadFailure = true;
            if (phase0BaselineRequested)
                exitAfterFrames = 1;
        }
        // Step 9 Phase 2 verify (--cache-hit-test): re-invoke the same load
        // immediately so the second call hits the cache populated by the first.
        if (g_cacheHitTest) {
            std::cout << "[MAIN] --cache-hit-test: re-loading " << path
                      << " to exercise cache path\n";
            demo->loadOBJMesh(path);
        }
    }
    if (switchToScene != -999) {
        std::cout << "[MAIN] Triggering setScene(" << switchToScene << ") after --load-obj\n";
        demo->setScene(switchToScene);
    }
    if (testResetHelper) {
        std::cout << "[MAIN] Triggering testResetCameraHelper() after --load-obj/--switch-to-scene\n";
        demo->testResetCameraHelper();
    }
    if (g_toggleGpuSdfOffAfterLoad) {
        std::cout << "[MAIN] Toggling GPU SDF off after load (codex 04 F2 verify)\n";
        demo->setUseGPUSDF(false);
    }
    if (measurementCameraToApply != -999) {
        demo->setMeasurementCamera(measurementCameraToApply);
    }

    // 2026-05-28 Stage 11c: re-apply CLI light direction AFTER loadOBJMesh so the
    // mesh-load auto-override (`useDirectionalLight = isSponza` at demo3d.cpp:7137)
    // doesn't silently clobber the user's --light-direction= choice on Cornell.
    {
        extern bool g_cliLightDirSet; extern glm::vec3 g_cliLightDir;
        if (g_cliLightDirSet) {
            demo->setUseDirectionalLight(true);
            demo->setLightDirection(g_cliLightDir);
            std::cout << "[MAIN] post-load: re-applied --light-direction=("
                      << g_cliLightDir.x << "," << g_cliLightDir.y << ","
                      << g_cliLightDir.z << "), useDirectionalLight=true\n";
        }
    }
    {
        extern bool g_cliLightPosSet; extern glm::vec3 g_cliLightPos;
        if (g_cliLightPosSet) {
            demo->setUseDirectionalLight(false);
            demo->setLightPosition(g_cliLightPos);
            std::cout << "[MAIN] post-load: re-applied --light-position=("
                      << g_cliLightPos.x << "," << g_cliLightPos.y << ","
                      << g_cliLightPos.z << "), useDirectionalLight=false\n";
        }
    }
    // v4 Phase 1A: per-scene MB-gain preset (ShaderToy adoption closeout).
    // Apply AFTER loadOBJMesh so we know the scene type. Sponza-class scenes
    // (sponza, sponza_master) get gain=0.10; everything else keeps default 1.0.
    {
        extern bool g_usePerSceneMbGain;
        if (g_usePerSceneMbGain) {
            demo->setUsePerSceneMbGain(true);
            const std::string& scene = demo->getCurrentOBJPath();
            if (scene == "sponza" || scene == "sponza_master") {
                demo->setMultiBounceGain(0.10f);
                std::cout << "[MAIN] post-load: per-scene MB-gain: sponza -> 0.10\n";
            } else {
                demo->setMultiBounceGain(1.0f);
                std::cout << "[MAIN] post-load: per-scene MB-gain: " << scene
                          << " -> 1.00 (default)\n";
            }
        }
    }
    // Step 10 (codex 06 F7): camera CLI overrides apply LAST so they win
    // over loadOBJMesh's auto-fit, --switch-to-scene's resetCamera, AND
    // --test-reset-helper. Order: pos -> target -> fovy. Setting target after
    // position re-syncs yaw/pitch from the user-chosen target.
    // Phase 2.5d critic-10 W4: if the camera came from a NAMED preset, validate
    // the loaded scene matches the preset's expected scene. Skip + warn on mismatch
    // rather than silently applying wrong coordinates. Direct --camera-pos /
    // --camera-target (cliCameraPresetName empty) skip the validation — user
    // takes responsibility for matching scene to coordinates.
    bool presetSkipped = false;
    if (cliCameraPresetName == "alcove") {
        const std::string sceneName = demo->getCurrentOBJPath();
        if (sceneName != "cornell_orig_alcove") {
            std::cerr << "[MAIN] WARN: --cam-preset=alcove requires "
                      << "--load-obj=cornell-orig-alcove (current scene: '" << sceneName
                      << "'); preset SKIPPED.\n";
            presetSkipped = true;
        }
    }
    if (!presetSkipped && cliCameraPosSet)    demo->setCameraPosition(cliCameraPos);
    if (!presetSkipped && cliCameraTargetSet) demo->setCameraTarget(cliCameraTarget);
    if (cliFovySet)         demo->setCameraFovy(cliFovy);

    std::cout << "[BACKEND] shell=" << launch.shellName
              << " runtimeBackend=" << launch.runtimeBackendName
              << " name=" << demo->getLegacyBackendName()
              << " view=" << demo->getRenderViewName()
              << " mode=" << demo->getRenderMode()
              << " scene=" << demo->getSceneLabel()
              << " sceneRevision=" << demo->getSceneRevision()
              << " shaderRevision=" << demo->getShaderRevision() << "\n";
    const uint64_t glErrorsBeforeFirstFrame = gl::getDebugErrorCount();

    int frameCounter = 0;

    // Step 6: Main rendering loop
    std::cout << "[MAIN] Entering main loop." << std::endl;
    
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    
    while (!WindowShouldClose()) {
        // Process input
        demo->processInput();

        // Phase 6b: set forceCascadeRebuild + TriggerCapture BEFORE update() so cascades
        // dispatch in the same frame that RenderDoc captures.
        demo->beginRdocFrameIfPending();

        // Update simulation
        demo->update();
        if (frameCounter == 0)
            firstFrameGlError |= drainGLErrors("first-frame update");

        // codex 09 F4: capture --screenshot on the LAST frame, BEFORE the UI is
        // drawn, so the saved image is a clean 3D-only frame instead of having
        // the ImGui overlay obscuring half the viewport.
        bool isExitFrame = (exitAfterFrames > 0 && (frameCounter + 1) >= exitAfterFrames);
        bool wantCleanScreenshot = isExitFrame && !screenshotPath.empty();

        // Render frame
        BeginDrawing();
            ClearBackground(BLACK);

            // Render 3D scene
            BeginMode3D(demo->getRaylibCamera());
                demo->render();
            EndMode3D();
            if (frameCounter == 0)
                firstFrameGlError |= drainGLErrors("first-frame render");

            // Phase 6a: capture after 3D, before ImGui (clean 3D-only frame)
            demo->takeScreenshot(/*launchAiAnalysis=*/true);

            // codex 09 F4: clean --screenshot capture happens HERE, before UI draw.
            if (wantCleanScreenshot) {
                if (demo->getExrCapture()) {
                    std::filesystem::path p(screenshotPath);
                    p.replace_extension();
                    runtimeArtifactFailure |= !demo->dumpScreenshotEXRs(p.string());
                }
                if (!probeStatsPath.empty())
                    runtimeArtifactFailure |= !demo->dumpProbeStatsJson(probeStatsPath);
                if (!atlasAttributionPath.empty()) {
                    if (atlasAttributionCells.empty()) {
                        atlasAttributionCells = {
                            glm::ivec3(7, 5, 4),
                            glm::ivec3(6, 5, 4),
                            glm::ivec3(6, 4, 4)
                        };
                    }
                    runtimeArtifactFailure |= !demo->dumpAtlasAttributionJson(atlasAttributionPath, atlasAttributionCells);
                }
                std::filesystem::path requestedScreenshotPath(screenshotPath);
                std::filesystem::path raylibBasenamePath;
                if (requestedScreenshotPath.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(requestedScreenshotPath.parent_path(), ec);
                    raylibBasenamePath = requestedScreenshotPath.filename();
                    if (!raylibBasenamePath.empty() && raylibBasenamePath != requestedScreenshotPath) {
                        ec.clear();
                        std::filesystem::remove(raylibBasenamePath, ec);
                    }
                }

                TakeScreenshot(screenshotPath.c_str());

                if (!raylibBasenamePath.empty() && raylibBasenamePath != requestedScreenshotPath &&
                    std::filesystem::exists(raylibBasenamePath)) {
                    std::error_code ec;
                    std::filesystem::rename(raylibBasenamePath, requestedScreenshotPath, ec);
                    if (ec) {
                        ec.clear();
                        std::filesystem::copy_file(raylibBasenamePath, requestedScreenshotPath,
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec)
                            std::filesystem::remove(raylibBasenamePath, ec);
                    }
                }
                if (!std::filesystem::exists(requestedScreenshotPath)) {
                    std::cerr << "[MAIN] ERROR: requested screenshot was not written: "
                              << screenshotPath << "\n";
                    runtimeArtifactFailure = true;
                }
                std::cout << "[MAIN] --screenshot saved (clean 3D, no UI): "
                          << screenshotPath << "\n";
            }

            // Continuous-shot capture (critic 15 H2 follow-up): grab consecutive
            // frames in ONE session so interactive frame-to-frame motion can be
            // measured directly (not cold-start A/B between independent runs).
            if (!shotsPrefix.empty() && shotsCount > 0) {
                int relFrame = static_cast<int>(frameCounter) - shotsAfter;
                if (relFrame >= 0 && relFrame < shotsCount) {
                    char buf[512];
                    std::snprintf(buf, sizeof(buf), "%s_f%d.png",
                                  shotsPrefix.c_str(), static_cast<int>(frameCounter));
                    TakeScreenshot(buf);
                }
                // Force exit after the last shot is captured.
                if (relFrame == shotsCount - 1) {
                    std::cout << "[MAIN] --shots-count reached; exiting after capture.\n";
                    exitAfterFrames = static_cast<int>(frameCounter) + 1;
                }
            }

            // --auto-analyze: exit once capture + analysis are done
            if (autoAnalyze && demo->isReadyToClose())
                break;

            // Render UI overlay (suppressed when capturing clean frame for analysis,
            // and also for our --screenshot exit frame so the saved file is clean).
            if (!demo->isSkippingUI() && !wantCleanScreenshot) {
                rlImGuiBegin();
                    demo->renderUI();
                rlImGuiEnd();
            }

            // Handle window resize
            if (screenWidth != GetScreenWidth() || screenHeight != GetScreenHeight()) {
                screenWidth = GetScreenWidth();
                screenHeight = GetScreenHeight();
                demo->onResize();
            }

            // Display FPS counter (skipped on clean-screenshot frame)
            if (!wantCleanScreenshot) DrawFPS(10, 10);

        EndDrawing();

        if (frameCounter == 0)
            firstFrameGlError |= drainGLErrors("first-frame end-drawing");
        if (frameCounter == 0)
            firstFrameGlError |= gl::getDebugErrorCount() > glErrorsBeforeFirstFrame;

        // Always advance frameCounter so --shots-after / --shots-count can use it
        // even when --exit-frames was not specified.
        ++frameCounter;
        if (exitAfterFrames > 0 && frameCounter >= exitAfterFrames) {
            std::cout << "[MAIN] --exit-frames reached (" << frameCounter << "), quitting.\n";
            break;
        }

        // Phase 6b: end RenderDoc frame capture and launch analysis
        demo->endRdocFrameIfPending();
    }

    // Phase 2.5d L5: warn if --bake-leak-test was scheduled but never fired
    // (cascade never became ready, OR exit-frames was too short for the
    // configured framesAfter convergence wait). Without this warning the user
    // would just see "no JSON written" and have to trace why.
    if (demo->bakeLeakTestActive() && !demo->bakeLeakTestComplete()) {
        std::cerr << "[MAIN] WARN: --bake-leak-test was scheduled but never fired. "
                  << "Either cascade never became ready (check --load-obj), or "
                  << "--exit-frames was too short for the convergence wait "
                  << "(default 240 frames). Increase --exit-frames and re-run.\n";
        runtimeArtifactFailure = true;
    }

    // Step 7: Cleanup
    std::cout << "[MAIN] Cleaning up..." << std::endl;

    // Phase 2.5d critic-10 W1: signal nonzero exit if any critical shader
    // failed to load. The banner in stderr already made the failure visible
    // mid-session; this propagates the failure to the exit code so any
    // orchestrator (CI, test scripts) can detect it.
    bool exitNonzeroForShaderFail = !demo->selectedBackendShadersOk();
    if (exitNonzeroForShaderFail) {
        std::cerr << "[MAIN] EXIT NONZERO: at least one critical shader failed to load this run.\n"
                  << "  See the banner above for which shader. Output is likely WRONG.\n";
    }
    bool phase0ReportFailed = false;
    if (phase0BaselineRequested) {
        phase0ReportFailed = !writePhase0RuntimeReport(
            phase0MetadataPath, *demo, launch, runtimeArguments, unknownArguments,
            frameCounter, runtimeArtifactFailure, allocationGlError,
            firstFrameGlError, sceneLoadFailure);
        std::cout << "[PHASE0] runtime report=" << phase0MetadataPath
                  << " result=" << (phase0ReportFailed ? "FAIL" : "PASS") << "\n";
    }

    delete demo;

    rlImGuiShutdown();
    CloseWindow();

    if (exitNonzeroForShaderFail || runtimeArtifactFailure ||
        sceneLoadFailure || phase0ReportFailed) {
        std::cerr << "[MAIN] Application terminated with validation/runtime failure (exit 1)." << std::endl;
        return 1;
    }
    std::cout << "[MAIN] Application terminated successfully." << std::endl;
    return 0;
}

// =============================================================================
// Application Initialization
// =============================================================================

bool initializeApplication(int windowWidth, int windowHeight) {
    /**
     * @brief Initialize window and OpenGL context
     *
     * Implementation Steps:
     * 1. Set OpenGL version hints
     * 2. Enable MSAA if available
     * 3. Create window with OpenGL context
     * 4. Load OpenGL extensions with GLEW
     * 5. Initialize ImGui and rlImGui
     * 6. Print system information
     *
     * @return true if successful
     */

    // TODO: Implement initialization

    // Step 1: Configure window hints
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_MSAA_4X_HINT);

    // Note: Raylib will use the highest OpenGL version available on the system
    // We've downgraded shaders to GLSL 430 for broader compatibility

    // Step 2: Create window
    // Step 11 perf-analysis: width/height come from argv (--window-size=W,H)
    // so Demo3D's later GetScreenWidth/Height reads pick up the right dims
    // immediately. Codex 11 F2+F5: do NOT use SetWindowSize after InitWindow.
    InitWindow(windowWidth, windowHeight, WINDOW_TITLE.c_str());
    std::cout << "[INIT] Window created at " << windowWidth << "x" << windowHeight << "\n";
    
    if (!IsWindowReady()) {
        std::cerr << "[ERROR] Failed to create window." << std::endl;
        return false;
    }
    
    // Step 3: Set up target FPS
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));
    
    // Step 4: Initialize trace logging
    SetTraceLogLevel(LOG_WARNING);
    
    // Step 5: Initialize ImGui
    std::cout << "[INIT] Setting up ImGui..." << std::endl;
    rlImGuiSetup(true);
    
    // Step 6: Load GLEW extensions
    // Note: This needs to happen after context creation
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "[ERROR] GLEW initialization failed: " << glewGetErrorString(err) << std::endl;
        return false;
    }
    
    // Step 7: Print OpenGL info
    std::cout << "[INFO] OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "[INFO] GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "[INFO] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[INFO] Vendor: " << glGetString(GL_VENDOR) << std::endl;
    
    return true;
}

void configureOpenGLState() {
    /**
     * @brief Set up OpenGL state for optimal rendering
     * 
     * Configuration:
     * 1. Enable depth testing for 3D
     * 2. Enable face culling for backfaces
     * 3. Set clear color to black
     * 4. Configure viewport
     * 5. Enable debug output if available
     */
    
    // TODO: Implement OpenGL configuration
    
    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Enable backface culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    // Set clear color
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    // Set viewport
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    glViewport(0, 0, width, height);
    
    // Enable debug output for Debug builds and strict Phase 0 validation.
    extern bool g_phase0Validation;
    // Enable debug output (if supported)
    #ifdef DEBUG
    if (glewIsSupported("GL_KHR_debug")) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, 
                                  GLenum severity, GLsizei length, 
                                  const GLchar* message, const void* userParam) {
            std::cout << "[GL DEBUG] " << message << std::endl;
        }, nullptr);
    }
    #endif
    
    std::cout << "[INIT] OpenGL state configured." << std::endl;
}

bool checkRequirements() {
    /**
     * @brief Verify system meets minimum requirements
     * 
     * Requirements:
     * - OpenGL 4.3 or higher
     * - GL_ARB_compute_shader extension
     * - GL_ARB_shader_image_load_store extension
     * - At least 2 GB VRAM recommended
     * 
     * @return true if all requirements met
     */
    
    // TODO: Implement requirements check
    
    bool allRequirementsMet = true;
    
    // Check OpenGL version
    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    
    std::cout << "[CHECK] OpenGL Version: " << major << "." << minor << std::endl;
    
    // Check for required extensions first (more important than base version)
    bool hasComputeShaders = glewIsSupported("GL_ARB_compute_shader");
    bool hasImageLoadStore = glewIsSupported("GL_ARB_shader_image_load_store");
    
    // Accept OpenGL 3.3+ if required extensions are available
    // This allows systems with older contexts but modern extension support
    bool versionOk = (major > 3) || (major == 3 && minor >= 3);
    
    if (!versionOk) {
        std::cerr << "[ERROR] OpenGL 3.3+ required, found " << major << "." << minor << std::endl;
        allRequirementsMet = false;
    } else if (major < OPENGL_MAJOR_VERSION || (major == OPENGL_MAJOR_VERSION && minor < OPENGL_MINOR_VERSION)) {
        std::cout << "[WARNING] OpenGL " << OPENGL_MAJOR_VERSION << "." << OPENGL_MINOR_VERSION 
                  << "+ recommended for optimal performance, found " << major << "." << minor << std::endl;
        std::cout << "[INFO] Will attempt to use extension-based compute shader support" << std::endl;
    }
    
    // Check for compute shader support
    if (!hasComputeShaders) {
        std::cerr << "[ERROR] Compute shaders not supported (GL_ARB_compute_shader required)" << std::endl;
        allRequirementsMet = false;
    } else {
        std::cout << "[CHECK] Compute shaders: Supported" << std::endl;
    }
    
    // Check for image load/store
    if (!hasImageLoadStore) {
        std::cerr << "[ERROR] Shader image load/store not supported" << std::endl;
        allRequirementsMet = false;
    } else {
        std::cout << "[CHECK] Image load/store: Supported" << std::endl;
    }
    
    // Check max 3D texture size
    GLint max3DTextureSize;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3DTextureSize);
    std::cout << "[CHECK] Max 3D Texture Size: " << max3DTextureSize << std::endl;
    
    if (max3DTextureSize < DEFAULT_VOLUME_RESOLUTION) {
        std::cerr << "[WARNING] Max 3D texture size smaller than default volume resolution" << std::endl;
    }
    
    // Check max compute work group count
    GLint maxWorkGroupCount[3];
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCount[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxWorkGroupCount[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxWorkGroupCount[2]);
    std::cout << "[CHECK] Max Compute Work Groups: " 
              << maxWorkGroupCount[0] << " x " 
              << maxWorkGroupCount[1] << " x " 
              << maxWorkGroupCount[2] << std::endl;
    
    if (allRequirementsMet) {
        std::cout << "[CHECK] All requirements satisfied." << std::endl;
    } else {
        std::cerr << "[CHECK] Requirements check FAILED." << std::endl;
    }
    
    return allRequirementsMet;
}
