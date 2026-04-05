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

#include "demo3d.h"
#include <iostream>
#include <cstdlib>

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

int main() {
    /**
     * @brief Application entry point
     * 
     * Execution Flow:
     * 1. Verify resource directory exists
     * 2. Initialize window and OpenGL context
     * 3. Check system requirements
     * 4. Configure OpenGL state
     * 5. Create Demo3D instance
     * 6. Enter main loop
     * 7. Cleanup on exit
     * 
     * @return Exit code (0 = success)
     */
    
    // Step 1: Verify running from correct directory
    if (!DirectoryExists("res")) {
        printf("[ERROR] Please run from project root directory.\n");
        printf("[ERROR] Resource directory 'res/' not found.\n");
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Radiance Cascades 3D Demo" << std::endl;
    std::cout << "  Version: " << VERSION_STAGE << VERSION << std::endl;
    std::cout << "========================================" << std::endl;
    
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
    
    // Step 6: Main rendering loop
    std::cout << "[MAIN] Entering main loop." << std::endl;
    
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    
    while (!WindowShouldClose()) {
        // Process input
        demo->processInput();
        
        // Update simulation
        demo->update();
        
        // Render frame
        BeginDrawing();
            ClearBackground(BLACK);
            
            // Render 3D scene
            BeginMode3D(demo->camera);
                demo->render();
            EndMode3D();
            
            // Render UI overlay
            rlImGuiBegin();
                demo->renderUI();
            rlImGuiEnd();
            
            // Handle window resize
            if (screenWidth != GetScreenWidth() || screenHeight != GetScreenHeight()) {
                screenWidth = GetScreenWidth();
                screenHeight = GetScreenHeight();
                demo->onResize();
            }
            
            // Display FPS counter
            DrawFPS(10, 10);
            
        EndDrawing();
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
    
    // Note: For core profile (required for compute shaders):
    // SetConfigFlags(FLAG_OPENGL_CORE_PROFILE);
    // This may need to be set before InitWindow in raylib
    
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
    
    if (major < OPENGL_MAJOR_VERSION || (major == OPENGL_MAJOR_VERSION && minor < OPENGL_MINOR_VERSION)) {
        std::cerr << "[ERROR] OpenGL 4.3+ required, found " << major << "." << minor << std::endl;
        allRequirementsMet = false;
    }
    
    // Check for compute shader support
    if (!glewIsSupported("GL_ARB_compute_shader")) {
        std::cerr << "[ERROR] Compute shaders not supported (GL_ARB_compute_shader required)" << std::endl;
        allRequirementsMet = false;
    } else {
        std::cout << "[CHECK] Compute shaders: Supported" << std::endl;
    }
    
    // Check for image load/store
    if (!glewIsSupported("GL_ARB_shader_image_load_store")) {
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
