/**
 * @file demo3d.cpp
 * @brief Implementation of 3D Radiance Cascades demo class
 * 
 * This file contains the implementation outline for the Demo3D class.
 * Function bodies are intentionally left as stubs - only structure and
 * API documentation is provided.
 * 
 * Implementation Notes:
 * - All volume operations use compute shaders for GPU acceleration
 * - Sparse voxel octree reduces memory from O(n³) to O(surface area)
 * - Temporal reprojection reuses previous frame data for performance
 * - Multiple cascade levels provide LOD for distant lighting
 */

#include "demo3d.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// =============================================================================
// VoxelNode Implementation
// =============================================================================

VoxelNode::VoxelNode()
    : density(0.0f)
    , radiance(0.0f)
    , parent(-1)
    , position(0.0f)
    , size(1.0f)
{
    // Initialize all children to invalid index
    for (int i = 0; i < 8; ++i) {
        children[i] = -1;
    }
}

// =============================================================================
// RadianceCascade3D Implementation
// =============================================================================

RadianceCascade3D::RadianceCascade3D()
    : probeGridTexture(0)
    , resolution(0)
    , cellSize(1.0f)
    , origin(0.0f)
    , raysPerProbe(4)
    , intervalStart(0.0f)
    , intervalEnd(0.0f)
    , active(false)
{
}

void RadianceCascade3D::initialize(int res, float cellSz, const glm::vec3& org, int rays) {
    resolution   = res;
    cellSize     = cellSz;
    origin       = org;
    raysPerProbe = rays;
    active       = false;

    probeGridTexture = gl::createTexture3D(
        res, res, res,
        GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr
    );
    if (probeGridTexture != 0) active = true;
}

void RadianceCascade3D::destroy() {
    if (probeGridTexture) {
        glDeleteTextures(1, &probeGridTexture);
        probeGridTexture = 0;
    }
    active = false;
}

// =============================================================================
// Demo3D Implementation
// =============================================================================

Demo3D::Demo3D()
    : currentScene(0)
    , sceneDirty(true)
    , time(0.0f)
    , mouseDragging(false)
    , volumeResolution(DEFAULT_VOLUME_RESOLUTION)
    , volumeOrigin(-2.0f, -2.0f, -2.0f)  // Default volume origin (centered around origin)
    , volumeSize(4.0f, 4.0f, 4.0f)        // Default volume size (4x4x4 world units)
    , cascadeCount(1)
    , baseInterval(0.125f)  // 4.0 (volumeSize) / 32 (probeRes) = 0.125
    , cascadeBilinear(true)
    , disableCascadeMerging(false)
    , useCascadeGI(false)
    , selectedCascadeForRender(0)
    , activeShader(0)
    , userMode(Mode::VOXELIZE)
    , brushSize(0.5f)
    , brushColor(WHITE)
    , drawRainbow(false)
    , useTraditionalGI(false)
    , giRayCount(64)
    , giNoise(true)
    , ambientLight(false)
    , ambientColor(1.0f, 1.0f, 1.0f)
    , indirectMixFactor(0.7f)
    , indirectBrightness(1.3f)
    , useSparseVoxels(USE_SPARSE_VOXELS_DEFAULT)
    , useTemporalReprojection(true)
    , adaptiveStepSize(true)
    , raymarchSteps(256)
    , rayTerminationThreshold(0.95f)
    , raymarchRenderMode(0)
    , showDebugWindows(false)
    , showCascadeSlices(false)
    , showVoxelGrid(false)
    , showPerformanceMetrics(true)
    , skipUIRendering(false)
    , showImGuiDemo(false)
    , voxelizationTimeMs(0.0)
    , sdfTimeMs(0.0)
    , cascadeTimeMs(0.0)
    , raymarchTimeMs(0.0)
    , frameTimeMs(0.0)
    , activeVoxelCount(0)
    , memoryUsageMB(0.0f)
    , lastMousePos(0.0f)
    , analyticSDFEnabled(true)  // Phase 0: Enable analytic SDF by default
    , primitiveSSBO(0)
    , useOBJMesh(false)  // Default to analytic primitives, enable for OBJ loading
    , debugQuadVAO(0)
    , debugQuadVBO(0)
    , sdfSliceAxis(2)           // Default: Z-axis slice (XY plane)
    , sdfSlicePosition(0.5f)    // Default: middle of volume
    , sdfVisualizeMode(1)       // Default: surface-detection mode (more informative than grayscale)
    , showSDFDebug(false)       // Default: hidden, press 'D' to toggle
    , showRadianceDebug(false)
    , radianceSliceAxis(2)
    , radianceSlicePosition(0.5f)
    , radianceVisualizeMode(0)
    , radianceExposure(1.0f)
    , radianceIntensityScale(1.0f)
    , showRadianceGrid(false)
    , probeTotal(0)
    , probeCenterSample(0.0f)
    , probeBackwallSample(0.0f)
    , showLightingDebug(false)
    , lightingSliceAxis(2)
    , lightingSlicePosition(0.5f)
    , lightingDebugMode(0)
    , lightingExposure(1.0f)
    , lightingIntensityScale(1.0f)
    , voxelizationTimeQuery(0)
    , sdfTimeQuery(0)
    , cascadeTimeQuery(0)
    , raymarchTimeQuery(0)
{
    /**
     * @brief Construct 3D demo and initialize all resources
     */
    
    std::memset(probeNonZero, 0, sizeof(probeNonZero));
    std::memset(probeMaxLum,  0, sizeof(probeMaxLum));
    std::memset(probeMeanLum, 0, sizeof(probeMeanLum));

    std::cout << "========================================" << std::endl;
    std::cout << "[Demo3D] Initializing 3D Radiance Cascades" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Step 1: Camera setup
    resetCamera();
    
    // Step 2: Enable OpenGL debug output
    #ifdef DEBUG
    gl::enableDebugOutput();
    std::cout << "[Demo3D] Debug output enabled" << std::endl;
    #endif
    
    // Step 3: Create volume buffers
    createVolumeBuffers();
    
    // Step 4: Load shaders (minimal set for quick start)
    std::cout << "\n[Demo3D] Loading shaders..." << std::endl;
    loadShader("voxelize.comp");
    loadShader("sdf_3d.comp");
    loadShader("sdf_analytic.comp");  // Phase 0: Analytic SDF shader
    loadShader("radiance_3d.comp");
    loadShader("inject_radiance.comp");
    loadShader("sdf_debug.frag");     // Phase 0: SDF debug visualization (auto-loads .vert)
    loadShader("radiance_debug.frag"); // Phase 1: Radiance cascade debug (auto-loads .vert)
    loadShader("lighting_debug.frag"); // Phase 1: Lighting debug (auto-loads .vert)
    loadShader("raymarch.frag");       // Phase 1: Final raymarched image (auto-loads .vert)
    
    // Step 5: Initialize cascades
    initCascades();
    
    // Step 6: Initialize debug quad geometry
    initDebugQuad();
    
    // Step 7: Set up initial scene
    std::cout << "\n[Demo3D] Setting up initial scene..." << std::endl;
    setScene(0); // Empty room
    
    // Step 8: Initialize ImGui
    ImGui::GetIO().IniFilename = NULL;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "[Demo3D] Initialization complete!" << std::endl;
    std::cout << "[Demo3D] Volume resolution: " << volumeResolution << "³" << std::endl;
    std::cout << "[Demo3D] Memory usage: ~" << memoryUsageMB << " MB" << std::endl;
    std::cout << "[Demo3D] Shaders loaded: " << shaders.size() << std::endl;
    std::cout << "[Demo3D] SDF Debug View: Press 'D' to toggle" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

Demo3D::~Demo3D() {
    /**
     * @brief Destructor - release all resources
     * 
     * Cleanup Sequence:
     * 1. Destroy radiance cascades
     * 2. Delete volume textures
     * 3. Delete framebuffers
     * 4. Unload shaders
     * 5. Free query objects
     */
    
    // TODO: Implement destructor
    destroyCascades();
    destroyVolumeBuffers();
    
    // Delete shaders
    for (auto& [name, program] : shaders) {
        glDeleteProgram(program);
    }
    
    // Delete query objects
    glDeleteQueries(1, &voxelizationTimeQuery);
    glDeleteQueries(1, &sdfTimeQuery);
    glDeleteQueries(1, &cascadeTimeQuery);
    glDeleteQueries(1, &raymarchTimeQuery);
    
    std::cout << "[Demo3D] Resources cleaned up." << std::endl;
}

void Demo3D::processInput() {
    /**
     * @brief Process keyboard and mouse input
     */
    
    // Check ImGui capture
    if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }
    
    // Phase 0: SDF Debug Controls
    if (IsKeyPressed(KEY_D)) {
        showSDFDebug = !showSDFDebug;
        std::cout << "[Demo3D] SDF Debug View: " << (showSDFDebug ? "ON" : "OFF") << std::endl;
    }
    
    if (showSDFDebug) {
        // Change slice axis
        if (IsKeyPressed(KEY_ONE)) {
            sdfSliceAxis = 0;
            std::cout << "[Demo3D] SDF Slice: X-axis (YZ plane)" << std::endl;
        }
        if (IsKeyPressed(KEY_TWO)) {
            sdfSliceAxis = 1;
            std::cout << "[Demo3D] SDF Slice: Y-axis (XZ plane)" << std::endl;
        }
        if (IsKeyPressed(KEY_THREE)) {
            sdfSliceAxis = 2;
            std::cout << "[Demo3D] SDF Slice: Z-axis (XY plane)" << std::endl;
        }
        
        // Cycle visualization mode (0=Colorized SDF, 1=Surface, 2=Gradient, 3=Normals)
        if (IsKeyPressed(KEY_M)) {
            sdfVisualizeMode = (sdfVisualizeMode + 1) % 4;
            const char* modes[] = {"Colorized SDF", "Surface Detection", "Gradient Magnitude", "Surface Normals"};
            std::cout << "[Demo3D] SDF Visualize Mode: " << modes[sdfVisualizeMode] << std::endl;
        }
        
        // Adjust slice position with mouse wheel
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            sdfSlicePosition += wheel * 0.05f;
            sdfSlicePosition = glm::clamp(sdfSlicePosition, 0.0f, 1.0f);
        }
    }
    
    // Basic camera controls would go here
    // For quick start, rely on Raylib's built-in camera handling in main3d.cpp
}

void Demo3D::update() {
    /**
     * @brief Update simulation state
     */

    time += GetFrameTime();
    // Scene update (voxelization + SDF regen) is handled entirely in render()
    // so that the sdfReady flag reset is properly chained after voxelizationPass.
    // Calling voxelizationPass() here would clear sceneDirty before render() sees it,
    // preventing sdfReady from being reset and leaving the SDF texture stale.
}

void Demo3D::render() {
    /**
     * @brief Execute complete rendering pipeline
     */
    
    // Pass 1: Voxelization (if needed)
    static bool sdfReady = false;
    if (sceneDirty) {
        double t0 = GetTime();
        voxelizationPass();
        voxelizationTimeMs = (GetTime() - t0) * 1000.0;
        sdfReady = false;
    }

    // Pass 2: SDF Generation (only when scene changed or first run)
    static bool cascadeReady = false;
    static bool lastMergeFlag = false;
    if (!sdfReady) {
        double t0 = GetTime();
        sdfGenerationPass();
        sdfTimeMs    = (GetTime() - t0) * 1000.0;
        sdfReady     = true;
        cascadeReady = false;  // SDF changed → cascade stale
    }
    if (disableCascadeMerging != lastMergeFlag) {
        lastMergeFlag = disableCascadeMerging;
        cascadeReady  = false;  // merge toggle changed → recompute all levels
    }

    // Pass 3: Radiance Cascades (only when SDF or merge flag changes)
    static bool probeDumped = false;
    if (!cascadeReady) {
        probeDumped = false;   // ensure readback triggers after this update
        double t0 = GetTime();
        updateRadianceCascades();
        cascadeTimeMs = (GetTime() - t0) * 1000.0;
        cascadeReady  = true;
    }

    // Probe readback: once per cascade update, sample all active levels
    if (!probeDumped && cascadeReady) {
        probeDumped = true;
        int res = cascades[0].resolution;
        probeTotal  = res * res * res;
        std::vector<float> buf(static_cast<size_t>(probeTotal) * 4);

        for (int ci = 0; ci < cascadeCount; ++ci) {
            if (!cascades[ci].active || cascades[ci].probeGridTexture == 0) continue;
            glBindTexture(GL_TEXTURE_3D, cascades[ci].probeGridTexture);
            glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
            glBindTexture(GL_TEXTURE_3D, 0);

            float maxLum = 0.0f, sumLum = 0.0f;
            int nonZero = 0;
            for (int i = 0; i < probeTotal; ++i) {
                float r = buf[i*4+0], g = buf[i*4+1], b = buf[i*4+2];
                float lum = (r + g + b) / 3.0f;
                if (lum > 1e-4f) ++nonZero;
                sumLum += lum;
                maxLum = std::max(maxLum, lum);
            }
            probeNonZero[ci] = nonZero;
            probeMaxLum[ci]  = maxLum;
            probeMeanLum[ci] = sumLum / static_cast<float>(probeTotal);
        }

        // Center and backwall spot-samples taken from C0
        auto idx = [res](int x, int y, int z){ return (z*res*res + y*res + x)*4; };
        int cx = res/2, cy = res/2, cz = res/2;
        glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
        glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
        glBindTexture(GL_TEXTURE_3D, 0);
        probeCenterSample   = glm::vec3(buf[idx(cx,cy,cz)+0], buf[idx(cx,cy,cz)+1], buf[idx(cx,cy,cz)+2]);
        probeBackwallSample = glm::vec3(buf[idx(16,16,1)+0],  buf[idx(16,16,1)+1],  buf[idx(16,16,1)+2]);
    }

    // Pass 4: Raymarching
    {
        double t0 = GetTime();
        raymarchPass();
        raymarchTimeMs = (GetTime() - t0) * 1000.0;
    }

    // Pass 5: SDF Debug Visualization (Phase 0)
    renderSDFDebug();

    // Pass 6: Radiance Cascade Slice Viewer (Phase 1)
    renderRadianceDebug();

    frameTimeMs = GetFrameTime() * 1000.0;
}

void Demo3D::voxelizationPass() {
    /**
     * @brief Convert 3D geometry to voxel representation
     * 
     * For quick start: We already have voxels from addVoxelBox(),
     * so this pass just ensures the texture is properly bound.
     */
    
    if (!sceneDirty) {
        return; // No need to re-voxelize if scene hasn't changed
    }
    
    std::cout << "[Demo3D] Voxelization pass (scene already voxelized via addVoxelBox)" << std::endl;
    
    // For a full implementation, we would:
    // 1. Clear voxel grid
    // 2. Render geometry to voxels using compute shader
    // 3. But for quick start, addVoxelBox already populated the texture
    
    sceneDirty = false;
}

void Demo3D::uploadPrimitivesToGPU() {
    /**
     * @brief Upload analytic SDF primitives to GPU SSBO
     */
    
    if (!analyticSDFEnabled) {
        return;
    }
    
    const auto& primitives = analyticSDF.getPrimitives();
    size_t count = primitives.size();
    
    if (count == 0) {
        std::cout << "[Demo3D] No primitives to upload" << std::endl;
        return;
    }
    
    // Create or resize SSBO
    if (primitiveSSBO == 0) {
        glGenBuffers(1, &primitiveSSBO);
    }
    
    // GPU struct layout (std430 with vec4 to avoid vec3 padding ambiguity):
    // int type + 3 floats padding (16 bytes), then vec4 position (16), scale (16), color (16)
    // Total: 64 bytes per primitive
    struct GPUPrimitive {
        int   type;
        float pad[3];        // padding so position starts at offset 16
        float position[4];   // vec4 (.xyz used)
        float scale[4];      // vec4 (.xyz used)
        float color[4];      // vec4 (.xyz used)
    };
    static_assert(sizeof(GPUPrimitive) == 64, "GPUPrimitive must be 64 bytes");

    size_t bufferSize = count * sizeof(GPUPrimitive);

    std::vector<GPUPrimitive> gpuData(count);
    for (size_t i = 0; i < count; ++i) {
        gpuData[i].type       = static_cast<int>(primitives[i].type);
        gpuData[i].pad[0]     = 0.0f;
        gpuData[i].pad[1]     = 0.0f;
        gpuData[i].pad[2]     = 0.0f;
        gpuData[i].position[0] = primitives[i].position.x;
        gpuData[i].position[1] = primitives[i].position.y;
        gpuData[i].position[2] = primitives[i].position.z;
        gpuData[i].position[3] = 0.0f;
        gpuData[i].scale[0]   = primitives[i].scale.x;
        gpuData[i].scale[1]   = primitives[i].scale.y;
        gpuData[i].scale[2]   = primitives[i].scale.z;
        gpuData[i].scale[3]   = 0.0f;
        gpuData[i].color[0]   = primitives[i].color.x;
        gpuData[i].color[1]   = primitives[i].color.y;
        gpuData[i].color[2]   = primitives[i].color.z;
        gpuData[i].color[3]   = 0.0f;
    }
    
    // Upload to GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, primitiveSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, gpuData.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    std::cout << "[Demo3D] Uploaded " << count << " primitives to GPU (" 
              << bufferSize << " bytes)" << std::endl;
}

void Demo3D::initDebugQuad() {
    /**
     * @brief Initialize full-screen quad for SDF debug visualization
     */
    
    // Create VAO
    glGenVertexArrays(1, &debugQuadVAO);
    glBindVertexArray(debugQuadVAO);
    
    // Create VBO with quad vertices (two triangles covering clip space)
    float vertices[] = {
        // Positions (clip space)
        -1.0f, -1.0f,  // Bottom-left
         1.0f, -1.0f,  // Bottom-right
         1.0f,  1.0f,  // Top-right
         1.0f,  1.0f,  // Top-right
        -1.0f,  1.0f,  // Top-left
        -1.0f, -1.0f   // Bottom-left
    };
    
    glGenBuffers(1, &debugQuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, debugQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Set up vertex attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    std::cout << "[Demo3D] Debug quad initialized" << std::endl;
}

void Demo3D::renderSDFDebug() {
    /**
     * @brief Render SDF cross-section debug view (OpenGL part only)
     * @note ImGui UI must be rendered in renderUI() method, not here
     */
    
    if (!showSDFDebug) return;
    
    // Check if SDF shader is loaded
    auto it = shaders.find("sdf_debug.frag");
    if (it == shaders.end()) {
        std::cerr << "[ERROR] SDF debug shader not loaded!" << std::endl;
        return;
    }
    
    // Save current viewport
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    // Set viewport to small window in top-left corner (400x400)
    int debugSize = 400;
    glViewport(0, viewport[3] - debugSize, debugSize, debugSize);
    
    // Clear with dark background
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Use debug shader
    glUseProgram(it->second);
    
    // Bind SDF texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glUniform1i(glGetUniformLocation(it->second, "sdfVolume"), 0);
    
    // Set uniforms
    glUniform1i(glGetUniformLocation(it->second, "sliceAxis"), sdfSliceAxis);
    glUniform1f(glGetUniformLocation(it->second, "slicePosition"), sdfSlicePosition);
    glUniform3fv(glGetUniformLocation(it->second, "volumeOrigin"), 1, &volumeOrigin[0]);
    glUniform3fv(glGetUniformLocation(it->second, "volumeSize"), 1, &volumeSize[0]);
    glUniform1f(glGetUniformLocation(it->second, "visualizeMode"), static_cast<float>(sdfVisualizeMode));
    
    // Render quad
    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);  // Disable depth test for overlay
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);   // Re-enable for rest of rendering
    glBindVertexArray(0);
    
    // Restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void Demo3D::renderRadianceDebug() {
    if (!showRadianceDebug) return;

    int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
    if (!cascades[selC].active || cascades[selC].probeGridTexture == 0) return;

    auto it = shaders.find("radiance_debug.frag");
    if (it == shaders.end()) return;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    int debugSize = 400;
    // Place in top-right corner so it doesn't overlap the SDF debug (top-left)
    glViewport(viewport[2] - debugSize, viewport[3] - debugSize, debugSize, debugSize);

    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(it->second);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, cascades[selC].probeGridTexture);
    glUniform1i(glGetUniformLocation(it->second, "uRadianceTexture"), 0);

    int res = cascades[selC].resolution;
    glUniform3i(glGetUniformLocation(it->second, "uVolumeSize"), res, res, res);
    glUniform1i(glGetUniformLocation(it->second, "uSliceAxis"),       radianceSliceAxis);
    glUniform1f(glGetUniformLocation(it->second, "uSlicePosition"),   radianceSlicePosition);
    glUniform1i(glGetUniformLocation(it->second, "uVisualizeMode"),   radianceVisualizeMode);
    glUniform1f(glGetUniformLocation(it->second, "uExposure"),        radianceExposure);
    glUniform1i(glGetUniformLocation(it->second, "uShowGrid"),        showRadianceGrid ? 1 : 0);
    glUniform1f(glGetUniformLocation(it->second, "uIntensityScale"),  radianceIntensityScale);

    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void Demo3D::renderSDFDebugUI() {
    /**
     * @brief Render SDF debug UI overlay (ImGui part only)
     * @note Must be called between rlImGuiBegin() and rlImGuiEnd()
     */

    if (!showSDFDebug) return;
    
    int debugSize = 400;
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    // Add text label with border indication via ImGui
    ImGui::SetNextWindowPos(ImVec2(10, viewport[3] - debugSize - 60));
    ImGui::Begin("SDF Debug Info", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | 
                 ImGuiWindowFlags_NoBackground);
    
    // Draw colored border indicator
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImVec2(debugSize + 20, debugSize + 70);
    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                       IM_COL32(255, 255, 0, 255), 0.0f, ImDrawFlags_None, 2.0f);
    
    ImGui::Text("SDF Cross-Section Viewer");
    ImGui::Separator();
    ImGui::Text("Slice Axis: %s", (sdfSliceAxis == 0) ? "X (YZ plane)" : 
                                (sdfSliceAxis == 1) ? "Y (XZ plane)" : "Z (XY plane)");
    ImGui::Text("Slice Position: %.2f", sdfSlicePosition);
    const char* sdfModeNames[] = {"Colorized SDF", "Surface Detection", "Gradient Magnitude", "Surface Normals"};
    ImGui::Text("Mode: %s", sdfModeNames[sdfVisualizeMode % 4]);
    ImGui::Text("Controls:");
    ImGui::Text("  [D] Toggle debug view");
    ImGui::Text("  [1/2/3] Change slice axis");
    ImGui::Text("  [Mouse Wheel] Adjust position");
    ImGui::Text("  [M] Cycle visualize mode");
    ImGui::End();
}

void Demo3D::renderRadianceDebugUI() {
    /**
     * @brief Render radiance cascade debug UI overlay
     * @note Must be called between rlImGuiBegin() and rlImGuiEnd()
     */
    
    if (!showRadianceDebug) return;
    
    int debugSize = 400;
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    ImGui::SetNextWindowPos(ImVec2(10, viewport[3] - debugSize - 60));
    ImGui::Begin("Radiance Debug Info", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | 
                 ImGuiWindowFlags_NoBackground);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImVec2(debugSize + 20, debugSize + 70);
    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                       IM_COL32(0, 255, 255, 255), 0.0f, ImDrawFlags_None, 2.0f);
    
    ImGui::Text("Radiance Cascade Viewer");
    ImGui::Separator();
    ImGui::Text("Slice Axis: %s", (radianceSliceAxis == 0) ? "X" : 
                                (radianceSliceAxis == 1) ? "Y" : "Z");
    ImGui::Text("Slice Position: %.2f", radianceSlicePosition);
    ImGui::Text("Mode: %s", (radianceVisualizeMode == 0) ? "Slice" : 
                             (radianceVisualizeMode == 1) ? "Max Projection" : "Average");
    ImGui::Text("Exposure: %.2f", radianceExposure);
    ImGui::Text("Intensity Scale: %.2f", radianceIntensityScale);
    ImGui::Text("Controls:");
    ImGui::Text("  [4/5/6] Change slice axis");
    ImGui::Text("  [Mouse Wheel] Adjust position");
    ImGui::Text("  [F] Cycle visualize mode");
    ImGui::Text("  [+/-] Adjust exposure");
    ImGui::End();
}

void Demo3D::renderLightingDebugUI() {
    /**
     * @brief Render per-light debug UI overlay
     * @note Must be called between rlImGuiBegin() and rlImGuiEnd()
     */
    
    if (!showLightingDebug) return;
    
    int debugSize = 400;
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    ImGui::SetNextWindowPos(ImVec2(10, viewport[3] - debugSize - 60));
    ImGui::Begin("Lighting Debug Info", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | 
                 ImGuiWindowFlags_NoBackground);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImVec2(debugSize + 20, debugSize + 70);
    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                       IM_COL32(255, 165, 0, 255), 0.0f, ImDrawFlags_None, 2.0f);
    
    ImGui::Text("Per-Light Contribution Viewer");
    ImGui::Separator();
    ImGui::Text("Slice Axis: %s", (lightingSliceAxis == 0) ? "X" : 
                                  (lightingSliceAxis == 1) ? "Y" : "Z");
    ImGui::Text("Slice Position: %.2f", lightingSlicePosition);
    ImGui::Text("Mode: %s", (lightingDebugMode == 0) ? "Combined" : 
                             (lightingDebugMode == 1) ? "Light 0" :
                             (lightingDebugMode == 2) ? "Light 1" :
                             (lightingDebugMode == 3) ? "Light 2" :
                             (lightingDebugMode == 4) ? "Normals" : "Albedo");
    ImGui::Text("Exposure: %.2f", lightingExposure);
    ImGui::Text("Intensity Scale: %.2f", lightingIntensityScale);
    ImGui::Text("Controls:");
    ImGui::Text("  [7/8/9] Change slice axis");
    ImGui::Text("  [Mouse Wheel] Adjust position");
    ImGui::Text("  [H] Cycle visualize mode");
    ImGui::Text("  [+/-] Adjust exposure");
    ImGui::End();
}

void Demo3D::sdfGenerationPass() {
    /**
     * @brief Generate 3D signed distance field
     * 
     * Phase 0 implementation: Use analytic SDF for quick validation.
     * Future: Replace with voxel-based JFA when mesh loading is ready.
     */
    
    if (analyticSDFEnabled) {
        std::cout << "[Demo3D] Generating analytic SDF..." << std::endl;
        
        // Check if we have a valid shader
        auto it = shaders.find("sdf_analytic.comp");
        if (it == shaders.end()) {
            std::cerr << "[ERROR] Analytic SDF shader not loaded!" << std::endl;
            return;
        }
        
        // Upload primitives to GPU
        uploadPrimitivesToGPU();
        
        // Bind compute shader
        glUseProgram(it->second);
        activeShader = it->second;
        
        // Set uniforms
        glUniform3fv(glGetUniformLocation(activeShader, "volumeOrigin"), 
                     1, &volumeOrigin[0]);
        glUniform3fv(glGetUniformLocation(activeShader, "volumeSize"), 
                     1, &volumeSize[0]);
        glUniform1i(glGetUniformLocation(activeShader, "primitiveCount"), 
                    static_cast<GLint>(analyticSDF.getPrimitives().size()));
        
        // Bind primitive SSBO
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, primitiveSSBO);
        
        // Bind output textures (GL_TRUE = layered, required for image3D write in compute)
        glBindImageTexture(0, sdfTexture,    0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
        glBindImageTexture(1, albedoTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // Dispatch compute shader
        glm::ivec3 workGroups = calculateWorkGroups(
            volumeResolution, volumeResolution, volumeResolution, 8);
        glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
        
        // Ensure writes are visible
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        std::cout << "[Demo3D] Analytic SDF generation complete." << std::endl;
    } else {
        std::cout << "[Demo3D] SDF generation skipped (analytic SDF disabled, JFA not implemented)" << std::endl;
        
        // TODO: Implement full 3D JFA when ready
        // For now, just clear the SDF texture
        glBindTexture(GL_TEXTURE_3D, sdfTexture);
        glClearTexImage(sdfTexture, 0, GL_RED, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
}

void Demo3D::updateRadianceCascades() {
    /**
     * @brief Update all radiance cascade levels
     */
    
    // Coarse→fine: each level reads the already-written level above it for misses
    for (int i = cascadeCount - 1; i >= 0; --i) {
        if (cascades[i].active) {
            updateSingleCascade(i);
        }
    }
}

void Demo3D::updateSingleCascade(int cascadeIndex) {
    if (cascadeIndex >= cascadeCount || !cascades[cascadeIndex].active) return;
    auto& c = cascades[cascadeIndex];
    if (c.resolution == 0 || c.probeGridTexture == 0) return;

    auto it = shaders.find("radiance_3d.comp");
    if (it == shaders.end()) return;

    GLuint prog = it->second;
    glUseProgram(prog);

    glUniform1i(glGetUniformLocation(prog, "uCascadeIndex"),  cascadeIndex);
    glUniform1i(glGetUniformLocation(prog, "uCascadeCount"),  cascadeCount);
    glUniform1f(glGetUniformLocation(prog, "uBaseInterval"),  c.cellSize);

    glm::ivec3 volRes(c.resolution);
    glUniform3iv(glGetUniformLocation(prog, "uVolumeSize"), 1, glm::value_ptr(volRes));
    glUniform3fv(glGetUniformLocation(prog, "uGridSize"),   1, glm::value_ptr(volumeSize));
    glUniform3fv(glGetUniformLocation(prog, "uGridOrigin"), 1, glm::value_ptr(volumeOrigin));
    glUniform1i(glGetUniformLocation(prog, "uRaysPerProbe"), c.raysPerProbe);
    glUniform3f(glGetUniformLocation(prog, "uLightPos"),   0.0f, 0.8f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "uLightColor"), 1.0f, 0.95f, 0.85f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glUniform1i(glGetUniformLocation(prog, "uSDF"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 1);

    // Upper cascade for merge: when a ray misses this level's interval, sample the
    // coarser level at the same probe position to get the far-field contribution.
    // Disabled when disableCascadeMerging is set so the user can compare merged vs raw.
    int upperIdx = cascadeIndex + 1;
    if (!disableCascadeMerging &&
        upperIdx < cascadeCount && cascades[upperIdx].active && cascades[upperIdx].probeGridTexture != 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, cascades[upperIdx].probeGridTexture);
        glUniform1i(glGetUniformLocation(prog, "uUpperCascade"), 2);
        glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 1);
    } else {
        glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 0);
    }

    glBindImageTexture(0, c.probeGridTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    glm::ivec3 wg = calculateWorkGroups(c.resolution, c.resolution, c.resolution, 4);
    glDispatchCompute(wg.x, wg.y, wg.z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void Demo3D::injectDirectLighting() {
    // Phase 2: inject_radiance.comp is frozen; updateSingleCascade() handles lighting.
    return;
    
    // Use first cascade level for direct lighting
    GLuint radianceTexture = cascades[0].probeGridTexture;
    
    // Get shader program
    auto it = shaders.find("inject_radiance.comp");
    if (it == shaders.end()) {
        std::cerr << "[ERROR] inject_radiance.comp shader not loaded!" << std::endl;
        return;
    }
    
    glUseProgram(it->second);
    
    // Set uniforms
    glUniform3iv(glGetUniformLocation(it->second, "uVolumeSize"), 1, &volumeResolution);
    glUniform3fv(glGetUniformLocation(it->second, "uGridSize"), 1, &volumeSize[0]);
    glUniform3fv(glGetUniformLocation(it->second, "uGridOrigin"), 1, &volumeOrigin[0]);
    
    // Setup multi-light configuration (Phase 1: 3-light Cornell Box setup)
    // Light 1: Ceiling light (white, main illumination)
    // Light 2: Fill light (subtle, from side)
    // Light 3: Accent light (colored, for color bleeding test)
    
    struct PointLight {
        glm::vec3 position;
        glm::vec3 color;
        float radius;
        float intensity;
    };
    
    std::vector<PointLight> lights = {
        // Ceiling light - main white light
        {glm::vec3(0.0f, 1.8f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 5.0f, 1.0f},
        // Fill light - subtle from left
        {glm::vec3(-1.5f, 1.0f, 0.0f), glm::vec3(0.8f, 0.8f, 0.9f), 4.0f, 0.3f},
        // Accent light - warm from right
        {glm::vec3(1.5f, 0.8f, 0.0f), glm::vec3(1.0f, 0.9f, 0.7f), 3.5f, 0.4f}
    };
    
    glUniform1i(glGetUniformLocation(it->second, "uPointLightCount"), static_cast<GLint>(lights.size()));
    
    // Upload light data via UBO or individual uniforms
    // For simplicity, use array uniforms (can optimize to UBO later)
    for (size_t i = 0; i < lights.size(); ++i) {
        std::string prefix = "lightBuffer.pointLights[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(it->second, (prefix + ".position").c_str()), 1, &lights[i].position[0]);
        glUniform3fv(glGetUniformLocation(it->second, (prefix + ".color").c_str()), 1, &lights[i].color[0]);
        glUniform1f(glGetUniformLocation(it->second, (prefix + ".radius").c_str()), lights[i].radius);
        glUniform1f(glGetUniformLocation(it->second, (prefix + ".intensity").c_str()), lights[i].intensity);
    }
    
    // Ambient lighting
    glUniform3fv(glGetUniformLocation(it->second, "uAmbientColor"), 1, &glm::vec3(0.05f)[0]);
    glUniform1f(glGetUniformLocation(it->second, "uAmbientIntensity"), 0.1f);
    
    // Bind radiance texture as image for writing (binding 0)
    glBindImageTexture(0, radianceTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    
    // Bind SDF texture for normal computation (binding 1)
    glBindImageTexture(1, sdfTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
    
    // Dispatch compute shader
    glm::ivec3 workGroups = glm::ivec3(volumeResolution / 8) + 1;
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    // Ensure completion
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    std::cout << "[Demo3D] Direct lighting injected with " << lights.size() << " lights" << std::endl;
}

void Demo3D::raymarchPass() {
    auto it = shaders.find("raymarch.frag");
    if (it == shaders.end()) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    GLuint prog = it->second;
    glUseProgram(prog);

    // Build camera matrices
    glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
    glm::mat4 proj = glm::perspective(glm::radians(camera.fovy), aspect, 0.01f, 100.0f);
    glm::mat4 invVP = glm::inverse(proj * view);

    glUniformMatrix4fv(glGetUniformLocation(prog, "uViewMatrix"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(prog, "uProjMatrix"), 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(glGetUniformLocation(prog, "uInvVPMatrix"), 1, GL_FALSE, glm::value_ptr(invVP));
    glUniform3fv(glGetUniformLocation(prog, "uCameraPos"), 1, glm::value_ptr(camera.position));

    // Volume bounds
    glm::vec3 volumeMax = volumeOrigin + volumeSize;
    glUniform3fv(glGetUniformLocation(prog, "uVolumeMin"), 1, glm::value_ptr(volumeOrigin));
    glUniform3fv(glGetUniformLocation(prog, "uVolumeMax"), 1, glm::value_ptr(volumeMax));
    glm::ivec3 volRes(volumeResolution);
    glUniform3iv(glGetUniformLocation(prog, "uVolumeSize"), 1, glm::value_ptr(volRes));

    // Raymarching parameters
    glUniform1i(glGetUniformLocation(prog, "uSteps"), raymarchSteps);
    glUniform1f(glGetUniformLocation(prog, "uTerminationThreshold"), rayTerminationThreshold);
    glUniform1f(glGetUniformLocation(prog, "uTime"), time);
    glUniform1i(glGetUniformLocation(prog, "uRenderMode"), raymarchRenderMode);

    // Direct light: near the ceiling (Cornell Box inner room spans y=[-1,1], ceiling at y=1.0)
    glm::vec3 lightPos(0.0f, 0.8f, 0.0f);
    glm::vec3 lightColor(1.0f, 0.95f, 0.85f);
    glUniform3fv(glGetUniformLocation(prog, "uLightPos"), 1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(prog, "uLightColor"), 1, glm::value_ptr(lightColor));

    // SDF texture (sampler binding 0)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glUniform1i(glGetUniformLocation(prog, "uSDF"), 0);

    // Cascade indirect lighting — bind the user-selected cascade level so each can be
    // inspected independently; uUseCascade only controls blending in mode 0
    int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
    if (cascades[selC].active && cascades[selC].probeGridTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, cascades[selC].probeGridTexture);
        glUniform1i(glGetUniformLocation(prog, "uRadiance"), 1);
    }
    glUniform1i(glGetUniformLocation(prog, "uUseCascade"), useCascadeGI ? 1 : 0);

    // Albedo volume (sampler binding 2) — always bound, needed for direct shading color
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 2);

    // Reuse the existing fullscreen quad VAO
    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void Demo3D::renderDebugVisualization() {
    /**
     * @brief Debug visualization of intermediate buffers
     */
    
    // Placeholder - no debug viz yet
}

bool Demo3D::loadShader(const std::string& shaderName) {
    /**
     * @brief Load shader from file
     */
    
    // Determine shader path
    std::string shaderPath = "res/shaders/" + shaderName;
    
    std::cout << "[Demo3D] Loading shader: " << shaderPath << std::endl;
    
    GLuint program = 0;
    
    // Check if it's a compute shader (.comp) or fragment shader (.frag)
    if (shaderName.find(".comp") != std::string::npos) {
        // Compute shader
        program = gl::loadComputeShader(shaderPath);
    } else if (shaderName.find(".frag") != std::string::npos) {
        // Fragment shader - need corresponding vertex shader
        // Extract base name (e.g., "sdf_debug" from "sdf_debug.frag")
        std::string baseName = shaderName.substr(0, shaderName.find(".frag"));
        std::string vertPath = "res/shaders/" + baseName + ".vert";
        
        // Load and compile vertex shader
        GLuint vertShader = gl::compileShader(GL_VERTEX_SHADER, vertPath);
        if (vertShader == 0) {
            std::cerr << "[ERROR] Failed to compile vertex shader: " << vertPath << std::endl;
            return false;
        }
        
        // Load and compile fragment shader
        GLuint fragShader = gl::compileShader(GL_FRAGMENT_SHADER, shaderPath);
        if (fragShader == 0) {
            std::cerr << "[ERROR] Failed to compile fragment shader: " << shaderPath << std::endl;
            glDeleteShader(vertShader);
            return false;
        }
        
        // Link program
        program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);
        
        // Check link status
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            GLchar infoLog[1024];
            glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
            std::cerr << "[ERROR] Shader program link failed:\n" << infoLog << std::endl;
            glDeleteProgram(program);
            glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            return false;
        }
        
        // Clean up shaders (they're linked into program now)
        glDetachShader(program, vertShader);
        glDetachShader(program, fragShader);
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        
    } else {
        std::cerr << "[ERROR] Unknown shader type: " << shaderName << std::endl;
        return false;
    }
    
    if (program == 0) {
        std::cerr << "[ERROR] Failed to load shader: " << shaderPath << std::endl;
        return false;
    }
    
    shaders[shaderName] = program;
    std::cout << "[Demo3D] Shader loaded successfully: " << shaderName << std::endl;
    return true;
}

void Demo3D::reloadShaders() {
    /**
     * @brief Reload all shaders for hot-swapping
     */
    
    std::cout << "[Demo3D] Reloading shaders..." << std::endl;
    
    for (auto const& [name, program] : shaders) {
        glDeleteProgram(program);
    }
    shaders.clear();
    
    loadShader("voxelize.comp");
    loadShader("sdf_3d.comp");
    loadShader("sdf_analytic.comp");
    loadShader("radiance_3d.comp");
    loadShader("inject_radiance.comp");
    loadShader("sdf_debug.frag");
    loadShader("radiance_debug.frag");
    loadShader("lighting_debug.frag");
    loadShader("raymarch.frag");

    std::cout << "[Demo3D] Shaders reloaded" << std::endl;
}

void Demo3D::createVolumeBuffers() {
    /**
     * @brief Create all volume textures and FBOs
     * 
     * Textures Created:
     * 1. voxelGridTexture (RGBA8) - Voxel occupancy and color
     * 2. sdfTexture (R32F) - Signed distance field
     * 3. directLightingTexture (RGBA16F) - Direct illumination
     * 4. prevFrameTexture (RGBA16F) - Previous frame for temporal
     * 5. currentRadianceTexture (RGBA16F) - Current radiance output
     * 
     * Framebuffers Created:
     * 1. voxelizationFBO - For geometry → voxel conversion
     * 2. sdfFBO - For SDF computation
     * 3. cascadeFBO - For cascade rendering
     * 
     * Memory Calculation:
     * Total = 5 × (128³ × 8 bytes) ≈ 800 MB
     * Reduce resolution or use sparse textures to save memory
     */
    
    std::cout << "[Demo3D] Creating volume buffers at resolution " << volumeResolution << "^3" << std::endl;
    
    // Create 3D textures using gl::createTexture3D()
    voxelGridTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );
    
    sdfTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_R32F, GL_RED, GL_FLOAT, nullptr
    );

    albedoTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );

    directLightingTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr
    );
    
    prevFrameTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr
    );
    
    currentRadianceTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr
    );
    
    // Create framebuffers (minimal - we'll use compute shaders mostly)
    glGenFramebuffers(1, &voxelizationFBO);
    glGenFramebuffers(1, &sdfFBO);
    glGenFramebuffers(1, &cascadeFBO);
    
    // Calculate memory usage (approximate)
    // RGBA8 = 4 bytes, R32F = 4 bytes, RGBA16F = 8 bytes
    float voxelMem = volumeResolution * volumeResolution * volumeResolution * 4 / (1024.0f * 1024.0f);
    float sdfMem = volumeResolution * volumeResolution * volumeResolution * 4 / (1024.0f * 1024.0f);
    float radianceMem = volumeResolution * volumeResolution * volumeResolution * 8 * 3 / (1024.0f * 1024.0f);
    memoryUsageMB = voxelMem + sdfMem + radianceMem;
    
    std::cout << "[Demo3D] Memory usage: ~" << memoryUsageMB << " MB" << std::endl;
    std::cout << "[Demo3D] Volume buffers created successfully" << std::endl;
}

void Demo3D::destroyVolumeBuffers() {
    /**
     * @brief Delete all volume textures and FBOs
     * 
     * Cleanup Order:
     * 1. Delete framebuffers first
     * 2. Then delete textures
     * 3. Reset handles to 0
     */
    
    // TODO: Implement buffer destruction
    glDeleteFramebuffers(1, &voxelizationFBO);
    glDeleteFramebuffers(1, &sdfFBO);
    glDeleteFramebuffers(1, &cascadeFBO);
    
    glDeleteTextures(1, &voxelGridTexture);
    glDeleteTextures(1, &sdfTexture);
    glDeleteTextures(1, &albedoTexture);
    glDeleteTextures(1, &directLightingTexture);
    glDeleteTextures(1, &prevFrameTexture);
    glDeleteTextures(1, &currentRadianceTexture);
}

void Demo3D::initCascades() {
    // 4 cascades, all 32^3 probes at the same world positions.
    // Probe positions are identical across levels; cascade index determines
    // the ray interval band each probe samples.
    //   Cascade 0: [0,        d   ]  d = cellSize = 4/32 = 0.125
    //   Cascade 1: [d,        4d  ]  = [0.125, 0.5]
    //   Cascade 2: [4d,       16d ]  = [0.5,   2.0]
    //   Cascade 3: [16d,      64d ]  = [2.0,   8.0]  (beyond volume diagonal)
    cascadeCount = 4;

    const int   probeRes = 32;
    const float cellSz   = volumeSize.x / float(probeRes);  // 0.125

    for (int i = 0; i < cascadeCount; ++i) {
        cascades[i].initialize(probeRes, cellSz, volumeOrigin, 8);
        std::cout << "[Demo3D] Cascade " << i << ": " << probeRes
                  << "^3 probes, cellSize=" << cellSz
                  << ", active=" << cascades[i].active << std::endl;
    }
}

void Demo3D::destroyCascades() {
    /**
     * @brief Release all cascade resources
     * 
     * Iterates through cascade array and calls destroy() on each.
     */
    
    // TODO: Implement cascade cleanup
    for (int i = 0; i < cascadeCount; ++i) {
        cascades[i].destroy();
    }
}

void Demo3D::setScene(int sceneType) {
    /**
     * @brief Load preset scene configuration
     * 
     * Scene Types:
     * -1: Clear all voxels
     *  0: Empty room (box with walls)
     *  1: Cornell Box (classic rendering test)
     *  2: Sponza atrium (simplified)
     *  3: Maze
     *  4: Pillars hall
     *  5: Procedural city
     * 
     * @param sceneType Scene index to load
     */
    
    currentScene = sceneType;
    sceneDirty = true;
    
    // Phase 0: Set up analytic SDF if enabled
    if (analyticSDFEnabled) {
        analyticSDF.clear();
        
        switch (sceneType) {
            case -1:
                // Empty scene
                std::cout << "[Demo3D] Cleared scene (analytic SDF)" << std::endl;
                break;
                
            case 0: {
                // Empty room: same walls as Cornell Box but all-gray, no interior objects
                std::cout << "[Demo3D] Loading: Empty Room (analytic SDF)" << std::endl;
                const float hs = 1.0f, wt = 0.2f, ext = hs + wt;
                const glm::vec3 gray(0.7f, 0.7f, 0.7f);
                analyticSDF.addBox(glm::vec3(0.0f, 0.0f, -(hs+wt)), glm::vec3(2*ext, 2*ext, 2*wt), gray);
                analyticSDF.addBox(glm::vec3(0.0f, -(hs+wt), 0.0f), glm::vec3(2*ext, 2*wt, 2*ext), gray);
                analyticSDF.addBox(glm::vec3(0.0f,  hs+wt,   0.0f), glm::vec3(2*ext, 2*wt, 2*ext), gray);
                analyticSDF.addBox(glm::vec3(-(hs+wt), 0.0f, 0.0f), glm::vec3(2*wt, 2*ext, 2*ext), gray);
                analyticSDF.addBox(glm::vec3( hs+wt,   0.0f, 0.0f), glm::vec3(2*wt, 2*ext, 2*ext), gray);
                break;
            }

            case 1: {
                // Cornell Box using analytic primitives (red/green walls + two boxes)
                std::cout << "[Demo3D] Loading: Cornell Box (analytic SDF)" << std::endl;
                analyticSDF.createCornellBox();
                break;
            }
            
            case 2: {
                // Simplified Sponza with boxes
                std::cout << "[Demo3D] Loading: Simplified Sponza (analytic SDF)" << std::endl;
                
                // Floor
                analyticSDF.addBox(
                    glm::vec3(0.0f, -2.0f, 0.0f),
                    glm::vec3(6.0f, 0.2f, 16.0f),
                    glm::vec3(0.7f, 0.6f, 0.5f)
                );
                
                // Ceiling
                analyticSDF.addBox(
                    glm::vec3(0.0f, 2.0f, 0.0f),
                    glm::vec3(6.0f, 0.2f, 16.0f),
                    glm::vec3(0.7f, 0.7f, 0.7f)
                );
                
                // Pillars
                for (int i = 0; i < 6; ++i) {
                    float z = -7.0f + i * 2.8f;
                    
                    // Left pillar
                    analyticSDF.addBox(
                        glm::vec3(-2.5f, 0.0f, z),
                        glm::vec3(0.4f, 4.0f, 0.8f),
                        glm::vec3(0.8f, 0.8f, 0.8f)
                    );
                    
                    // Right pillar
                    analyticSDF.addBox(
                        glm::vec3(2.5f, 0.0f, z),
                        glm::vec3(0.4f, 4.0f, 0.8f),
                        glm::vec3(0.8f, 0.8f, 0.8f)
                    );
                }
                break;
            }
            
            default:
                std::cout << "[Demo3D] Scene " << sceneType << " not implemented for analytic SDF" << std::endl;
                break;
        }
        
        std::cout << "[Demo3D] Analytic SDF: " << analyticSDF.getPrimitiveCount() 
                  << " primitives loaded" << std::endl;
    }
    
    // Clear existing voxel grid by binding it and clearing via shader or CPU
    // For simplicity in this stub-filled implementation, we rely on addVoxelBox overwriting
    // In a real implementation, we'd clear the texture here first.
    glBindImageTexture(0, voxelGridTexture, 0, false, 0, GL_WRITE_ONLY, GL_RGBA8);
    
    switch (sceneType) {
        case -1: {
            // Clear scene - just empty volume
            std::cout << "[Demo3D] Cleared scene" << std::endl;
            // Ideally clear texture here
            break;
        }
        
        case 0: {
            // Empty room - create a simple box
            std::cout << "[Demo3D] Loading: Empty Room" << std::endl;
            
            // Define room dimensions
            float roomWidth = 4.0f;
            float roomHeight = 3.0f;
            float roomDepth = 4.0f;
            
            // Create floor
            addVoxelBox(
                glm::vec3(-roomWidth/2, -roomHeight/2, -roomDepth/2),
                glm::vec3(roomWidth, 0.1f, roomDepth),
                glm::vec3(0.6f, 0.6f, 0.6f)
            );
            
            // Create ceiling
            addVoxelBox(
                glm::vec3(-roomWidth/2, roomHeight/2 - 0.1f, -roomDepth/2),
                glm::vec3(roomWidth, 0.1f, roomDepth),
                glm::vec3(0.8f, 0.8f, 0.8f)
            );
            
            // Create back wall
            addVoxelBox(
                glm::vec3(-roomWidth/2, -roomHeight/2, roomDepth/2 - 0.1f),
                glm::vec3(roomWidth, roomHeight, 0.1f),
                glm::vec3(0.7f, 0.7f, 0.7f)
            );
            
            break;
        }
        
        case 1: {
            // Cornell Box
            std::cout << "[Demo3D] Loading: Cornell Box" << std::endl;
            
            float boxSize = 3.0f;
            float wallHeight = 2.5f;
            
            // Floor
            addVoxelBox(
                glm::vec3(-boxSize/2, -wallHeight/2, -boxSize/2),
                glm::vec3(boxSize, 0.1f, boxSize),
                glm::vec3(0.8f, 0.8f, 0.8f)
            );
            
            // Ceiling
            addVoxelBox(
                glm::vec3(-boxSize/2, wallHeight/2 - 0.1f, -boxSize/2),
                glm::vec3(boxSize, 0.1f, boxSize),
                glm::vec3(0.8f, 0.8f, 0.8f)
            );
            
            // Back wall
            addVoxelBox(
                glm::vec3(-boxSize/2, -wallHeight/2, boxSize/2 - 0.1f),
                glm::vec3(boxSize, wallHeight, 0.1f),
                glm::vec3(0.8f, 0.8f, 0.8f)
            );
            
            // Left wall (red)
            addVoxelBox(
                glm::vec3(-boxSize/2, -wallHeight/2, -boxSize/2),
                glm::vec3(0.1f, wallHeight, boxSize),
                glm::vec3(0.9f, 0.2f, 0.2f)
            );
            
            // Right wall (green)
            addVoxelBox(
                glm::vec3(boxSize/2 - 0.1f, -wallHeight/2, -boxSize/2),
                glm::vec3(0.1f, wallHeight, boxSize),
                glm::vec3(0.2f, 0.9f, 0.2f)
            );
            
            // Tall box (left)
            addVoxelBox(
                glm::vec3(-boxSize/4, -wallHeight/2 + 0.5f, -boxSize/4),
                glm::vec3(0.6f, 1.0f, 0.6f),
                glm::vec3(0.9f, 0.9f, 0.9f)
            );
            
            // Short box (right)
            addVoxelBox(
                glm::vec3(boxSize/4, -wallHeight/2 + 0.3f, boxSize/4),
                glm::vec3(0.6f, 0.6f, 0.6f),
                glm::vec3(0.9f, 0.9f, 0.9f)
            );
            
            // Light source (ceiling)
            addVoxelBox(
                glm::vec3(-0.5f, wallHeight/2 - 0.15f, -0.5f),
                glm::vec3(1.0f, 0.05f, 1.0f),
                glm::vec3(1.0f, 1.0f, 0.9f),
                true // Emissive
            );
            
            break;
        }
        
        case 2: {
            // Simplified Sponza Atrium
            std::cout << "[Demo3D] Loading: Simplified Sponza" << std::endl;
            
            // Long corridor with pillars
            float length = 8.0f;
            float width = 3.0f;
            float height = 4.0f;
            
            // Floor
            addVoxelBox(
                glm::vec3(-width/2, -height/2, -length/2),
                glm::vec3(width, 0.1f, length),
                glm::vec3(0.7f, 0.6f, 0.5f)
            );
            
            // Ceiling
            addVoxelBox(
                glm::vec3(-width/2, height/2 - 0.1f, -length/2),
                glm::vec3(width, 0.1f, length),
                glm::vec3(0.7f, 0.7f, 0.7f)
            );
            
            // Pillars along the corridor
            int pillarCount = 6;
            for (int i = 0; i < pillarCount; ++i) {
                float z = -length/2 + (i + 0.5f) * length / pillarCount;
                
                // Left pillar
                addVoxelBox(
                    glm::vec3(-width/2 + 0.3f, -height/2, z - 0.2f),
                    glm::vec3(0.2f, height, 0.4f),
                    glm::vec3(0.8f, 0.8f, 0.8f)
                );
                
                // Right pillar
                addVoxelBox(
                    glm::vec3(width/2 - 0.5f, -height/2, z - 0.2f),
                    glm::vec3(0.2f, height, 0.4f),
                    glm::vec3(0.8f, 0.8f, 0.8f)
                );
            }
            
            break;
        }
        
        case 3: {
            // Maze
            std::cout << "[Demo3D] Loading: Maze" << std::endl;
            
            float mazeSize = 6.0f;
            float wallHeight = 2.0f;
            float cellSize = 1.0f;
            
            // Simple maze pattern (1 = wall, 0 = path)
            int maze[6][6] = {
                {1, 1, 1, 1, 1, 1},
                {1, 0, 0, 1, 0, 1},
                {1, 0, 1, 0, 0, 1},
                {1, 0, 1, 1, 0, 1},
                {1, 0, 0, 0, 0, 1},
                {1, 1, 1, 1, 1, 1}
            };
            
            for (int x = 0; x < 6; ++x) {
                for (int z = 0; z < 6; ++z) {
                    if (maze[x][z] == 1) {
                        addVoxelBox(
                            glm::vec3(-mazeSize/2 + x * cellSize, -wallHeight/2, -mazeSize/2 + z * cellSize),
                            glm::vec3(cellSize, wallHeight, cellSize),
                            glm::vec3(0.6f, 0.5f, 0.4f)
                        );
                    }
                }
            }
            
            break;
        }
        
        case 4: {
            // Pillars Hall
            std::cout << "[Demo3D] Loading: Pillars Hall" << std::endl;
            
            float hallWidth = 5.0f;
            float hallLength = 10.0f;
            float hallHeight = 4.0f;
            
            // Floor
            addVoxelBox(
                glm::vec3(-hallWidth/2, -hallHeight/2, -hallLength/2),
                glm::vec3(hallWidth, 0.1f, hallLength),
                glm::vec3(0.75f, 0.7f, 0.65f)
            );
            
            // Ceiling
            addVoxelBox(
                glm::vec3(-hallWidth/2, hallHeight/2 - 0.1f, -hallLength/2),
                glm::vec3(hallWidth, 0.1f, hallLength),
                glm::vec3(0.8f, 0.8f, 0.8f)
            );
            
            // Two rows of pillars
            int pillarRows = 5;
            for (int i = 0; i < pillarRows; ++i) {
                float z = -hallLength/2 + (i + 0.5f) * hallLength / pillarRows;
                
                // Left row
                addVoxelBox(
                    glm::vec3(-hallWidth/3, -hallHeight/2, z - 0.25f),
                    glm::vec3(0.5f, hallHeight, 0.5f),
                    glm::vec3(0.85f, 0.8f, 0.75f)
                );
                
                // Right row
                addVoxelBox(
                    glm::vec3(hallWidth/3 - 0.5f, -hallHeight/2, z - 0.25f),
                    glm::vec3(0.5f, hallHeight, 0.5f),
                    glm::vec3(0.85f, 0.8f, 0.75f)
                );
            }
            
            break;
        }
        
        case 5: {
            // Procedural City (simple blocks)
            std::cout << "[Demo3D] Loading: Procedural City" << std::endl;
            
            float citySize = 8.0f;
            int gridSize = 4;
            
            // Ground plane
            addVoxelBox(
                glm::vec3(-citySize/2, -1.5f, -citySize/2),
                glm::vec3(citySize, 0.1f, citySize),
                glm::vec3(0.3f, 0.3f, 0.35f)
            );
            
            // Random buildings
            srand(42); // Fixed seed for reproducibility
            
            for (int x = 0; x < gridSize; ++x) {
                for (int z = 0; z < gridSize; ++z) {
                    float buildingWidth = 0.6f * (citySize / gridSize);
                    float buildingHeight = 0.5f + float(rand() % 100) / 100.0f * 2.0f;
                    
                    float posX = -citySize/2 + (x + 0.5f) * (citySize / gridSize);
                    float posZ = -citySize/2 + (z + 0.5f) * (citySize / gridSize);
                    
                    // Random gray color
                    float gray = 0.4f + float(rand() % 40) / 100.0f;
                    
                    addVoxelBox(
                        glm::vec3(posX - buildingWidth/2, -1.5f + buildingHeight/2, posZ - buildingWidth/2),
                        glm::vec3(buildingWidth, buildingHeight, buildingWidth),
                        glm::vec3(gray, gray, gray)
                    );
                }
            }
            
            break;
        }
        
        default:
            std::cerr << "[Demo3D] Unknown scene type: " << sceneType << std::endl;
            break;
    }
    
    // Mark scene as needing update
    sceneDirty = true;
    std::cout << "[Demo3D] Scene loaded successfully" << std::endl;
}

void Demo3D::addVoxelBox(
    const glm::vec3& center,
    const glm::vec3& size,
    const glm::vec3& color,
    bool emissive
) {
    /**
     * @brief Helper function to add a box of voxels
     * 
     * @param center Box center in world space
     * @param size Box dimensions
     * @param color RGB color
     * @param emissive Whether the box emits light
     */
    
    // Calculate voxel bounds
    glm::vec3 halfSize = size * 0.5f;
    glm::vec3 minPos = center - halfSize;
    glm::vec3 maxPos = center + halfSize;
    
    // Convert to voxel coordinates
    // Assuming unit grid size for demo purposes, scaled by resolution
    float voxelSize = 1.0f / float(volumeResolution); 
    
    // Assuming origin at 0,0,0 for this demo implementation
    glm::vec3 uGridOrigin(0.0f);
    
    glm::ivec3 minVoxel = glm::ivec3((minPos - uGridOrigin) / voxelSize);
    glm::ivec3 maxVoxel = glm::ivec3((maxPos - uGridOrigin) / voxelSize);
    
    // Clamp to volume bounds
    minVoxel = glm::clamp(minVoxel, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
    maxVoxel = glm::clamp(maxVoxel, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
    
    // Prepare voxel data
    std::vector<unsigned char> voxelData;
    std::vector<glm::ivec3> voxelPositions;
    
    for (int x = minVoxel.x; x <= maxVoxel.x; ++x) {
        for (int y = minVoxel.y; y <= maxVoxel.y; ++y) {
            for (int z = minVoxel.z; z <= maxVoxel.z; ++z) {
                // Add voxel
                unsigned char r = static_cast<unsigned char>(color.r * 255);
                unsigned char g = static_cast<unsigned char>(color.g * 255);
                unsigned char b = static_cast<unsigned char>(color.b * 255);
                unsigned char a = emissive ? 255 : 128;
                
                voxelData.push_back(r);
                voxelData.push_back(g);
                voxelData.push_back(b);
                voxelData.push_back(a);
                
                voxelPositions.push_back(glm::ivec3(x, y, z));
            }
        }
    }
    
    // Upload to GPU (batch update)
    // Note: For efficiency, should use staging buffer and single upload
    // This is a simplified implementation
    
    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    
    for (size_t i = 0; i < voxelPositions.size(); ++i) {
        glTexSubImage3D(
            GL_TEXTURE_3D,
            0,
            voxelPositions[i].x,
            voxelPositions[i].y,
            voxelPositions[i].z,
            1, 1, 1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            &voxelData[i * 4]
        );
    }
    
    glBindTexture(GL_TEXTURE_3D, 0);
}

void Demo3D::renderUI() {
    /**
     * @brief Render complete ImGui interface
     * 
     * UI Panels:
     * 1. Settings Panel (renderSettingsPanel)
     * 2. Cascade Control (renderCascadePanel)
     * 3. Tutorial/Info (renderTutorialPanel)
     * 4. Debug Windows (if enabled)
     * 
     * Layout:
     * - Dockable windows (ImGui docking)
     * - Collapsible headers
     * - Real-time value displays
     */
    
    // TODO: Implement UI rendering
    renderSettingsPanel();
    renderCascadePanel();
    renderTutorialPanel();
    
    // Render debug UI overlays
    renderSDFDebugUI();       // Phase 0: SDF visualization
    renderRadianceDebugUI();  // Phase 1: Radiance cascade visualization
    renderLightingDebugUI();  // Phase 1: Per-light contribution visualization
    
    if (showImGuiDemo) {
        ImGui::ShowDemoWindow(&showImGuiDemo);
    }
}

void Demo3D::renderSettingsPanel() {
    /**
     * @brief Render settings panel
     */
    
    ImGui::Begin("3D RC Settings");
    
    ImGui::Text("Volume Resolution: %d^3", volumeResolution);
    ImGui::Text("Memory Usage: %.1f MB", memoryUsageMB);
    ImGui::Text("Active Voxels: %d", activeVoxelCount);
    
    ImGui::Separator();
    
    if (ImGui::Button("Reload Shaders")) {
        reloadShaders();
    }
    
    if (ImGui::Button("Reset Camera")) {
        resetCamera();
    }
    
    ImGui::Separator();
    
    ImGui::Separator();
    ImGui::Text("Cascade GI (Phase 2):");
    ImGui::Checkbox("Cascade GI", &useCascadeGI);
    if (cascades[0].active)
        ImGui::Text("Probe grid: 32^3, rays/probe: %d", cascades[0].raysPerProbe);
    else
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cascade not initialized!");

    ImGui::Separator();
    ImGui::Text("Debug Render Mode:");
    ImGui::RadioButton("Final (0)",       &raymarchRenderMode, 0); ImGui::SameLine();
    ImGui::RadioButton("Normals (1)",     &raymarchRenderMode, 1); ImGui::SameLine();
    ImGui::RadioButton("Depth (2)",       &raymarchRenderMode, 2); ImGui::SameLine();
    ImGui::RadioButton("Indirect*5 (3)", &raymarchRenderMode, 3);
    ImGui::RadioButton("Direct only (4)", &raymarchRenderMode, 4); ImGui::SameLine();
    ImGui::RadioButton("Steps (5)",       &raymarchRenderMode, 5); ImGui::SameLine();
    ImGui::RadioButton("GI only (6)",     &raymarchRenderMode, 6);

    ImGui::Separator();
    ImGui::Checkbox("Show Performance Metrics", &showPerformanceMetrics);
    if (showPerformanceMetrics) {
        ImGui::Indent();
        ImGui::Text("Pass times (CPU-side, last run):");
        ImGui::Text("  Voxelize  %.2f ms", voxelizationTimeMs);
        ImGui::Text("  SDF       %.2f ms", sdfTimeMs);
        ImGui::Text("  Cascade   %.2f ms (%d levels)", cascadeTimeMs, cascadeCount);
        ImGui::Text("  Raymarch  %.2f ms", raymarchTimeMs);
        ImGui::Separator();
        ImGui::Text("  Frame     %.2f ms", frameTimeMs);
        ImGui::Unindent();
    }
    ImGui::Checkbox("Show Debug Windows", &showDebugWindows);

    ImGui::End();
}

void Demo3D::renderCascadePanel() {
    /**
     * @brief Render cascade visualization panel
     */
    
    ImGui::Begin("Cascades");
    
    ImGui::Text("Cascade Count: %d", cascadeCount);

    // Per-level interval table
    {
        const float d = (cascadeCount > 0) ? cascades[0].cellSize : 0.125f;
        for (int i = 0; i < cascadeCount; ++i) {
            if (!cascades[i].active) continue;
            float tMin = (i == 0) ? 0.02f : d * std::pow(4.0f, float(i - 1));
            float tMax = d * std::pow(4.0f, float(i));
            ImGui::Text("  C%d: %d^3  [%.3f, %.3f]", i, cascades[i].resolution, tMin, tMax);
        }
    }

    ImGui::Separator();

    // Merge toggle — triggers full cascade recompute when changed
    ImGui::Checkbox("Disable Merge (raw per-level)", &disableCascadeMerging);
    ImGui::SameLine();
    ImGui::TextDisabled(disableCascadeMerging ? "(each level independent)" : "(C3->C2->C1->C0)");

    ImGui::Separator();

    // Cascade selector for indirect lighting / debug views
    ImGui::Text("Render using cascade:");
    for (int i = 0; i < cascadeCount; ++i) {
        if (i > 0) ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof(label), "C%d", i);
        ImGui::RadioButton(label, &selectedCascadeForRender, i);
    }

    if (probeTotal > 0) {
        ImGui::Separator();
        ImGui::Text("Probe Readback (all levels):");

        // Per-cascade stats table
        const float d = (cascadeCount > 0) ? cascades[0].cellSize : 0.125f;
        for (int ci = 0; ci < cascadeCount; ++ci) {
            float pct = 100.0f * probeNonZero[ci] / float(probeTotal);
            // Colour-code: <10% red, 10-50% yellow, >50% green
            ImVec4 col = (pct < 10.0f) ? ImVec4(1,0.3f,0.3f,1)
                       : (pct < 50.0f) ? ImVec4(1,1,0.3f,1)
                                       : ImVec4(0.3f,1,0.3f,1);
            float tMin = (ci == 0) ? 0.02f : d * std::pow(4.0f, float(ci - 1));
            float tMax = d * std::pow(4.0f, float(ci));
            ImGui::TextColored(col, "  C%d [%.2f,%.2f]: %5.1f%%  max=%.3f  mean=%.4f",
                ci, tMin, tMax, pct, probeMaxLum[ci], probeMeanLum[ci]);
        }

        ImGui::Separator();
        ImGui::Text("C0 spot samples:");
        ImGui::Text("  Center:  (%.3f, %.3f, %.3f)",
                    probeCenterSample.r, probeCenterSample.g, probeCenterSample.b);
        ImGui::Text("  Backwall:(%.3f, %.3f, %.3f)",
                    probeBackwallSample.r, probeBackwallSample.g, probeBackwallSample.b);
    } else {
        ImGui::TextDisabled("  (cascade not yet sampled)");
    }

    ImGui::End();
}

void Demo3D::renderTutorialPanel() {
    /**
     * @brief Render tutorial/information panel
     */
    
    ImGui::Begin("3D Radiance Cascades - Quick Start");
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: Minimal Working Example");
    ImGui::Separator();
    
    ImGui::Text("This is a quick-start implementation.");
    ImGui::Text("Full features coming soon!");
    ImGui::NewLine();
    
    ImGui::Text("Controls:");
    ImGui::BulletText("WASD + Mouse: Navigate camera");
    ImGui::BulletText("Scroll: Adjust brush size");
    ImGui::BulletText("R: Reload shaders");
    ImGui::BulletText("F1: Toggle UI");
    ImGui::NewLine();
    
    // Scene Selection Controls
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Scene Selection:");
    ImGui::Separator();
    
    if (ImGui::Button(currentScene == 0 ? "[ACTIVE] Empty Room" : "Empty Room")) {
        setScene(0);
        std::cout << "[Demo3D] Switched to: Empty Room" << std::endl;
    }
    
    if (ImGui::Button(currentScene == 1 ? "[ACTIVE] Cornell Box" : "Cornell Box")) {
        setScene(1);
        std::cout << "[Demo3D] Switched to: Cornell Box (classic test scene)" << std::endl;
    }
    
    if (ImGui::Button(currentScene == 2 ? "[ACTIVE] Simplified Sponza" : "Simplified Sponza")) {
        setScene(2);
        std::cout << "[Demo3D] Switched to: Simplified Sponza" << std::endl;
    }

    if (ImGui::Button(useOBJMesh ? "[ACTIVE] Cornell Box (OBJ)" : "Cornell Box (OBJ)")) {
        if (loadOBJMesh("res/scene/cornell_box.obj")) {
            std::cout << "[Demo3D] Loaded real Cornell Box mesh from OBJ!" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load Cornell Box OBJ!" << std::endl;
        }
    }

    ImGui::NewLine();
    {
        const char* names[] = { "Empty Room", "Cornell Box", "Simplified Sponza" };
        if (useOBJMesh)
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active: Cornell Box (OBJ)");
        else if (currentScene >= 0 && currentScene < 3)
            ImGui::Text("Active: %s", names[currentScene]);
    }
    
    ImGui::NewLine();
    
    // Debug Visualization Controls
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Debug Visualizations:");
    ImGui::Separator();
    
    if (ImGui::Button(showSDFDebug ? "[ON] SDF Debug (D)" : "[OFF] SDF Debug (D)")) {
        showSDFDebug = !showSDFDebug;
        std::cout << "[Demo3D] SDF Debug View: " << (showSDFDebug ? "ON" : "OFF") << std::endl;
    }
    
    if (ImGui::Button(showRadianceDebug ? "[ON] Radiance Debug" : "[OFF] Radiance Debug")) {
        showRadianceDebug = !showRadianceDebug;
        std::cout << "[Demo3D] Radiance Debug View: " << (showRadianceDebug ? "ON" : "OFF") << std::endl;
    }
    if (ImGui::Button(showLightingDebug ? "[ON] Lighting Debug" : "[OFF] Lighting Debug")) {
        showLightingDebug = !showLightingDebug;
        std::cout << "[Demo3D] Lighting Debug View: " << (showLightingDebug ? "ON" : "OFF") << std::endl;
    }
    
    ImGui::NewLine();
    
    ImGui::Text("Phase 3 Status:");
    ImGui::BulletText("OK Analytic SDF + albedo volume (64^3)");
    ImGui::BulletText("OK SDF-guided primary raymarching");
    ImGui::BulletText("OK 4-level radiance cascade (C0-C3)");
    ImGui::BulletText("OK Cascade merge (C3->C2->C1->C0)");
    ImGui::BulletText("OK Merge toggle + per-level probe stats");
    ImGui::BulletText("OK 7 render modes (0-6) incl. GI-only");
    
    ImGui::End();
}

void Demo3D::onResize() {
    /**
     * @brief Handle window resize event
     */
    
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    glViewport(0, 0, width, height);
    
    std::cout << "[Demo3D] Window resized to " << width << "x" << height << std::endl;
}

void Demo3D::takeScreenshot() {
    /**
     * @brief Take screenshot of current frame
     */
    
    std::cout << "[Demo3D] Screenshot functionality not yet implemented" << std::endl;
}

void Demo3D::resetCamera() {
    /**
     * @brief Reset camera to default position
     */
    
    // Cornell Box is centered at origin in [-1,1]; camera looks in from +z
    camera.position = glm::vec3(0.0f, 0.0f, 4.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.moveSpeed = 5.0f;
    camera.rotationSpeed = 0.003f;
    
    std::cout << "[Demo3D] Camera reset to position: " 
              << camera.position.x << ", " << camera.position.y << ", " << camera.position.z << std::endl;
}

Camera3D Demo3D::getRaylibCamera() const {
    /**
     * @brief Convert internal Camera3DConfig to Raylib's Camera3D
     */
    
    Camera3D raylibCamera;
    raylibCamera.position = Vector3{camera.position.x, camera.position.y, camera.position.z};
    raylibCamera.target = Vector3{camera.target.x, camera.target.y, camera.target.z};
    raylibCamera.up = Vector3{camera.up.x, camera.up.y, camera.up.z};
    raylibCamera.fovy = camera.fovy;
    raylibCamera.projection = camera.projection;
    
    return raylibCamera;
}

glm::ivec3 Demo3D::calculateWorkGroups(int dimX, int dimY, int dimZ, int localSize) {
    /**
     * @brief Calculate optimal work group size for compute shader
     */
    
    return glm::ivec3(
        (dimX + localSize - 1) / localSize,
        (dimY + localSize - 1) / localSize,
        (dimZ + localSize - 1) / localSize
    );
}

bool Demo3D::loadOBJMesh(const std::string& filename) {
    std::cout << "\n[Demo3D] Loading OBJ mesh: " << filename << std::endl;
    
    // Try loading from multiple possible paths (handle different working directories)
    std::vector<std::string> searchPaths = {
        filename,                              // As provided
        "../" + filename,                      // One level up (from build/)
        "../../" + filename,                   // Two levels up
        "res/scene/" + filename.substr(filename.find_last_of("/\\") + 1),  // Just filename in res/scene/
        "../res/scene/" + filename.substr(filename.find_last_of("/\\") + 1)
    };
    
    bool loaded = false;
    std::string successfulPath;
    
    for (const auto& path : searchPaths) {
        if (objLoader.load(path)) {
            loaded = true;
            successfulPath = path;
            std::cout << "[Demo3D] Successfully loaded from: " << path << std::endl;
            break;
        }
    }
    
    if (!loaded) {
        std::cerr << "[ERROR] Failed to load OBJ from any location!" << std::endl;
        std::cerr << "[ERROR] Tried paths:" << std::endl;
        for (const auto& path : searchPaths) {
            std::cerr << "  - " << path << std::endl;
        }
        return false;
    }
    
    objLoader.normalize();
    
    std::vector<uint8_t> voxelData;
    objLoader.voxelize(volumeResolution, voxelData, volumeOrigin, volumeSize);
    
    if (voxelData.empty()) {
        std::cerr << "[ERROR] Empty voxelization!" << std::endl;
        return false;
    }
    
    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                    volumeResolution, volumeResolution, volumeResolution,
                    GL_RGBA, GL_UNSIGNED_BYTE, voxelData.data());
    
    sceneDirty = true;
    useOBJMesh = true;
    
    std::cout << "[Demo3D] OBJ mesh loaded and voxelized successfully!" << std::endl;
    return true;
}
