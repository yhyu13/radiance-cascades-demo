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

#include "demo3d.h"  // This includes raylib.h
#include <iostream>
#include <cstdlib>

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

// =============================================================================
// Function Declarations
// =============================================================================

/**
 * @brief Initialize application and OpenGL context
 * 
 * Sets up window, OpenGL context, GLEW, and debug output.
 * 
 * @return true if initialization successful
 */
bool initializeApplication();

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

int main(int argc, char* argv[]) {
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

    // Step 2: Initialize application
    if (!initializeApplication()) {
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
    Demo3D* demo = new Demo3D();

    // --auto-analyze:  burst capture + AI analysis then exit
    // --auto-sequence: sequence capture (N frames) + AI analysis then exit
    // --auto-rdoc:     RenderDoc GPU capture after 8s warm-up (stays open; G also works)
    // --load-obj=NAME: load OBJ mesh once at startup (Step 2/3 testing).
    //   Step 6: NAME accepts cornell | cornell-orig | sponza | sponza-master.
    // --exit-frames=N: quit after rendering N frames (CI-friendly Step 2 verification)
    bool        autoAnalyze   = false;
    std::string loadObjName;
    std::string screenshotPath;
    int         exitAfterFrames = 0;
    int         switchToScene   = -999;   // codex 09 F1 verification: after --load-obj, switch to analytic scene N
    bool        testResetHelper = false;  // codex 11 F1/F2 verification: programmatically test resetCameraToScenePreset
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
            demo->setAutoRdocMode(8.0f);
            std::cout << "[MAIN] --auto-rdoc: will capture RenderDoc frame after 8s warm-up.\n";
        } else if (arg.rfind("--load-obj=", 0) == 0) {
            loadObjName = arg.substr(11);
            std::cout << "[MAIN] --load-obj=" << loadObjName << ": will load at startup.\n";
        } else if (arg.rfind("--exit-frames=", 0) == 0) {
            exitAfterFrames = std::atoi(arg.substr(14).c_str());
            std::cout << "[MAIN] --exit-frames=" << exitAfterFrames << ": will quit after N frames.\n";
        } else if (arg.rfind("--screenshot=", 0) == 0) {
            screenshotPath = arg.substr(13);
            std::cout << "[MAIN] --screenshot=" << screenshotPath << ": will capture last frame.\n";
        } else if (arg.rfind("--render-mode=", 0) == 0) {
            int m = std::atoi(arg.substr(14).c_str());
            demo->setRenderMode(m);
            std::cout << "[MAIN] --render-mode=" << m << "\n";
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
        }
    }

    if (!loadObjName.empty()) {
        std::string path;
        if      (loadObjName == "sponza")         path = "res/scene/sponza.obj";
        else if (loadObjName == "cornell")        path = "res/scene/cornell_box.obj";
        else if (loadObjName == "cornell-orig")   path = "res/scene/CornellBox-Original/CornellBox-Original.obj";
        else if (loadObjName == "sponza-master")  path = "res/scene/Sponza-master/sponza.obj";
        else {
            std::cerr << "[MAIN] --load-obj=" << loadObjName
                      << ": unknown name (expected sponza|cornell|cornell-orig|sponza-master). Aborting.\n";
            delete demo;
            CloseWindow();
            return 1;
        }
        if (!demo->loadOBJMesh(path)) {
            std::cerr << "[MAIN] --load-obj failed for " << path << "\n";
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

            // Phase 6a: capture after 3D, before ImGui (clean 3D-only frame)
            demo->takeScreenshot(/*launchAiAnalysis=*/true);

            // codex 09 F4: clean --screenshot capture happens HERE, before UI draw.
            if (wantCleanScreenshot) {
                TakeScreenshot(screenshotPath.c_str());
                std::cout << "[MAIN] --screenshot saved (clean 3D, no UI): "
                          << screenshotPath << "\n";
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

        if (exitAfterFrames > 0 && ++frameCounter >= exitAfterFrames) {
            std::cout << "[MAIN] --exit-frames reached (" << frameCounter << "), quitting.\n";
            break;
        }

        // Phase 6b: end RenderDoc frame capture and launch analysis
        demo->endRdocFrameIfPending();
    }

    // Step 7: Cleanup
    std::cout << "[MAIN] Cleaning up..." << std::endl;
    delete demo;
    
    rlImGuiShutdown();
    CloseWindow();
    
    std::cout << "[MAIN] Application terminated successfully." << std::endl;
    return 0;
}

// =============================================================================
// Application Initialization
// =============================================================================

bool initializeApplication() {
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
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, WINDOW_TITLE.c_str());
    
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
