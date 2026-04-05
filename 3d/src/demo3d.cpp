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
    /**
     * @brief Initialize cascade parameters and create OpenGL texture
     * 
     * Implementation Steps:
     * 1. Store configuration parameters
     * 2. Calculate interval start/end based on cascade index
     * 3. Create 3D texture with gl::createTexture3D()
     * 4. Set texture parameters (filtering, wrap mode)
     * 5. Attach to framebuffer if needed
     * 
     * @param res Resolution (e.g., 32, 64, 128)
     * @param cellSz World space size per voxel
     * @param org World space origin
     * @param rays Rays per probe
     */
    
    // TODO: Implement cascade initialization
    // - Allocate 3D texture for probe grid
    // - Configure mipmaps if needed
    // - Set up framebuffer attachment
}

void RadianceCascade3D::destroy() {
    /**
     * @brief Release OpenGL resources
     * 
     * Implementation Steps:
     * 1. Delete probe grid texture with glDeleteTextures()
     * 2. Reset state to default
     */
    
    // TODO: Implement resource cleanup
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
    , cascadeCount(MAX_CASCADES)
    , baseInterval(0.5f)
    , cascadeBilinear(true)
    , cascadeDisplayIndex(0)
    , disableCascadeMerging(false)
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
{
    /**
     * @brief Construct 3D demo and initialize all resources
     * 
     * Initialization Sequence:
     * 1. Configure camera with default position
     * 2. Initialize OpenGL context and extensions
     * 3. Load all shaders from res/shaders/
     * 4. Create volume textures and framebuffers
     * 5. Initialize radiance cascade hierarchy
     * 6. Set up initial scene geometry
     * 7. Configure ImGui for 3D controls
     * 
     * Error Handling:
     * - Check OpenGL version (4.3+ required)
     * - Verify shader compilation success
     * - Validate texture creation
     */
    
    // TODO: Implement constructor
    // Step 1: Camera setup
    resetCamera();
    
    // Step 2: OpenGL initialization
    // - Enable debug output if available
    // - Load GLEW extensions
    
    // Step 3: Shader loading
    // Note: Shaders are located in 3d/res/shaders/ directory
    // loadShader("voxelize.comp");
    // loadShader("sdf_3d.comp");
    // loadShader("radiance_3d.comp");
    // loadShader("inject_radiance.comp");
    // loadShader("raymarch.frag");
    
    // Step 4: Volume buffer creation
    createVolumeBuffers();
    
    // Step 5: Cascade initialization
    initCascades();
    
    // Step 6: Scene setup
    setScene(currentScene);
    
    // Step 7: ImGui configuration
    ImGui::GetIO().IniFilename = NULL;
    
    std::cout << "[Demo3D] Initialization complete." << std::endl;
    std::cout << "[Demo3D] Volume resolution: " << volumeResolution << "³" << std::endl;
    std::cout << "[Demo3D] Shader path: 3d/res/shaders/" << std::endl;

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
     * 
     * Input Mapping:
     * - WASD: Camera movement (XZ plane)
     * - QE: Camera height (Y axis)
     * - Mouse drag: Camera rotation
     * - Scroll: Brush size adjustment
     * - 1: Switch to voxelization mode
     * - 2: Switch to light placement mode
     * - Space: Toggle between modes
     * - F1: Toggle UI visibility
     * - F2: Take screenshot
     * - R: Reload shaders (hot-reload)
     * - C: Clear scene
     * - Escape: Exit application
     * 
     * Implementation Notes:
     * - Ignore input when ImGui wants capture
     * - Apply delta-time for smooth movement
     * - Clamp values to valid ranges
     */
    
    // TODO: Implement input processing
    // Check ImGui capture
    if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }
    
    // Keyboard handling
    // - Camera movement
    // - Mode switching
    // - Debug commands
    
    // Mouse handling
    // - Rotation (drag)
    // - Brush size (scroll)
    // - Voxel/light placement (click)
}

void Demo3D::update() {
    /**
     * @brief Update simulation state
     * 
     * Update Tasks:
     * 1. Accumulate time for animations
     * 2. Check for dynamic scene changes
     * 3. Update sparse voxel structure if needed
     * 4. Mark scene as dirty if modifications occurred
     * 
     * Performance Optimization:
     * - Only update voxels that changed
     * - Use dirty flags to skip unnecessary passes
     * - Batch updates for efficiency
     */
    
    // TODO: Implement update logic
    time += GetFrameTime();
    
    // Check if scene needs update
    if (sceneDirty) {
        // Trigger voxelization
        sceneDirty = false;
    }
    
    // Update sparse voxel structure
    if (useSparseVoxels) {
        // TODO: Update SVO based on camera position
    }
}

void Demo3D::render() {
    /**
     * @brief Execute complete rendering pipeline
     * 
     * Rendering Order:
     * 1. Voxelization Pass (if scene changed)
     * 2. SDF Generation (compute shader)
     * 3. Direct Lighting Injection
     * 4. Radiance Cascade Update (all levels)
     * 5. Raymarching Pass (final composite)
     * 6. UI Overlay (ImGui)
     * 7. Debug Visualization (optional)
     * 
     * Performance Measurement:
     * - Start timer queries before each pass
     * - End queries after completion
     * - Accumulate timing statistics
     */
    
    // TODO: Implement main render loop
    
    // Pass 1: Voxelization
    gl::beginTimeQuery(voxelizationTimeQuery);
    voxelizationPass();
    voxelizationTimeMs = gl::endTimeQuery(voxelizationTimeQuery) / 1e6;
    
    // Pass 2: SDF Generation
    gl::beginTimeQuery(sdfTimeQuery);
    sdfGenerationPass();
    sdfTimeMs = gl::endTimeQuery(sdfTimeQuery) / 1e6;
    
    // Pass 3: Direct Lighting
    injectDirectLighting();
    
    // Pass 4: Radiance Cascades
    gl::beginTimeQuery(cascadeTimeQuery);
    updateRadianceCascades();
    cascadeTimeMs = gl::endTimeQuery(cascadeTimeQuery) / 1e6;
    
    // Pass 5: Raymarching
    gl::beginTimeQuery(raymarchTimeQuery);
    raymarchPass();
    raymarchTimeMs = gl::endTimeQuery(raymarchTimeQuery) / 1e6;
    
    // Calculate total frame time
    frameTimeMs = GetFrameTime() * 1000.0;
    
    // Pass 6: UI
    if (!skipUIRendering) {
        rlImGuiBegin();
        renderUI();
        rlImGuiEnd();
    }
    
    // Pass 7: Debug visualization
    if (showDebugWindows) {
        renderDebugVisualization();
    }
}

void Demo3D::voxelizationPass() {
    /**
     * @brief Convert 3D geometry to voxel representation
     * 
     * Algorithm:
     * 1. Bind voxelization framebuffer
     * 2. Clear voxel grid to empty state
     * 3. Render scene from 6 faces (cubic voxels)
     * 4. Use geometry shader to emit voxels
     * 5. Write to 3D texture via image store
     * 
     * Alternative Approaches:
     * - Conservative rasterization
     * - Transform feedback
     * - Compute shader voxelization
     * 
     * Optimization:
     * - Only voxelize dynamic objects
     * - Use instancing for repeated geometry
     * - Frustum culling for visible region
     */
    
    // TODO: Implement voxelization
    // Bind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, voxelizationFBO);
    
    // Clear volume
    glClearTexImage(voxelGridTexture, 0, GL_RGBA, GL_UNSIGNED_BYTE, glm::vec4(0.0f));
    
    // Set viewport to volume dimensions
    glViewport(0, 0, volumeResolution, volumeResolution);
    
    // Activate voxelization shader
    glUseProgram(shaders["voxelize.comp"]);
    
    // Render geometry
    // For each object in scene:
    //   - Set model matrix
    //   - Draw with appropriate primitive type
    
    // Unbind
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Demo3D::sdfGenerationPass() {
    /**
     * @brief Generate 3D signed distance field using jump flooding
     * 
     * 3D JFA Algorithm:
     * 1. Initialize: Mark surface voxels with distance 0, others with infinity
     * 2. For step = max_distance down to 1 (halving each iteration):
     *    a. Launch compute shader over 3x3x3 neighborhood
     *    b. Find minimum distance in neighborhood
     *    c. Store position of closest surface
     * 3. Final pass: Calculate exact Euclidean distance
     * 
     * Compute Shader Configuration:
     * - Local size: 8×8×8 work groups
     * - Global size: volume_resolution³
     * - Shared memory for caching neighborhood
     * 
     * Memory Barrier:
     * - Required between JFA iterations
     * - GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
     */
    
    // TODO: Implement 3D JFA
    GLuint sdfShader = shaders["sdf_3d.comp"];
    glUseProgram(sdfShader);
    
    // Bind input/output textures as images
    gl::bindImageTexture(0, voxelGridTexture, 0, false, 0, GL_READ_ONLY, GL_RGBA8);
    gl::bindImageTexture(1, sdfTexture, 0, false, 0, GL_WRITE_ONLY, GL_R32F);
    
    // Set uniform: initial step size
    int maxStep = volumeResolution / 2;
    glUniform1i(glGetUniformLocation(sdfShader, "uMaxStep"), maxStep);
    
    // JFA iterations
    for (int step = maxStep; step >= 1; step /= 2) {
        glUniform1i(glGetUniformLocation(sdfShader, "uStepSize"), step);
        
        // Dispatch compute shader
        auto workGroups = calculateWorkGroups(volumeResolution, volumeResolution, volumeResolution, 8);
        glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
        
        // Memory barrier between iterations
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
    
    glUseProgram(0);
}

void Demo3D::updateRadianceCascades() {
    /**
     * @brief Update all cascade levels from fine to coarse
     * 
     * Cascade Update Order:
     * 1. Start with finest cascade (index 0)
     * 2. Inject direct lighting into probes
     * 3. Cast rays within interval range
     * 4. Accumulate radiance from hits
     * 5. Merge with next coarser cascade
     * 6. Repeat for all levels
     * 
     * Temporal Reprojection:
     * - Reproject previous frame's radiance
     * - Blend with current frame (10% new, 90% old)
     * - Reduces flickering and noise
     * 
     * LOD Strategy:
     * - Finer cascades: Higher spatial resolution, fewer rays
     * - Coarser cascades: Lower resolution, more rays
     */
    
    // TODO: Implement cascade hierarchy update
    for (int i = 0; i < cascadeCount; ++i) {
        if (cascades[i].active) {
            updateSingleCascade(i);
        }
    }
}

void Demo3D::updateSingleCascade(int cascadeIndex) {
    /**
     * @brief Update single cascade level
     * 
     * Per-Cascade Operations:
     * 1. Get cascade configuration
     * 2. Bind compute shader for radiance injection
     * 3. For each probe in grid:
     *    a. Calculate world position
     *    b. Determine ray directions
     *    c. Raymarch in assigned interval
     *    d. Accumulate radiance from hits
     *    e. Sample coarser cascade if no hit
     * 4. Write results to 3D texture
     * 
     * @param cascadeIndex Index of cascade to update (0 = finest)
     */
    
    // TODO: Implement single cascade update
    RadianceCascade3D& cascade = cascades[cascadeIndex];
    
    // Activate radiance shader
    GLuint radianceShader = shaders["radiance_3d.comp"];
    glUseProgram(radianceShader);
    
    // Bind cascade texture
    glBindImageTexture(0, cascade.probeGridTexture, 0, false, 0, GL_WRITE_ONLY, GL_RGBA16F);
    
    // Set uniforms
    glUniform1i(glGetUniformLocation(radianceShader, "uCascadeIndex"), cascadeIndex);
    glUniform1i(glGetUniformLocation(radianceShader, "uCascadeAmount"), cascadeCount);
    glUniform1f(glGetUniformLocation(radianceShader, "uBaseInterval"), baseInterval);
    
    // Dispatch compute shader
    auto workGroups = calculateWorkGroups(cascade.resolution, cascade.resolution, cascade.resolution);
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    // Memory barrier
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    
    glUseProgram(0);
}

void Demo3D::injectDirectLighting() {
    /**
     * @brief Add emission from light sources to cascades
     * 
     * Light Types Supported:
     * - Point lights (omnidirectional)
     * - Directional lights (sun/moon)
     * - Area lights (rectangular)
     * - Spot lights (cone-shaped)
     * 
     * Injection Process:
     * 1. Identify which cascade level contains light
     * 2. Find affected probes (within light radius)
     * 3. Add emissive term to probe radiance
     * 4. Handle multiple overlapping lights
     * 
     * Optimization:
     * - Use light culling (frustum, distance)
     * - Cluster lights by spatial locality
     * - Batch similar light types
     */
    
    // TODO: Implement direct lighting injection
    GLuint injectShader = shaders["inject_radiance.comp"];
    glUseProgram(injectShader);
    
    // Bind finest cascade for injection
    glBindImageTexture(0, cascades[0].probeGridTexture, 0, false, 0, GL_READ_WRITE, GL_RGBA16F);
    
    // Set light parameters
    // glUniform3fv(glGetUniformLocation(injectShader, "uLightPos"), 1, &lightPos);
    // glUniform3fv(glGetUniformLocation(injectShader, "uLightColor"), 1, &lightColor);
    // glUniform1f(glGetUniformLocation(injectShader, "uLightRadius"), lightRadius);
    
    // Dispatch
    auto workGroups = calculateWorkGroups(cascades[0].resolution, cascades[0].resolution, cascades[0].resolution);
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glUseProgram(0);
}

void Demo3D::raymarchPass() {
    /**
     * @brief Volume raymarching for final pixel colors
     * 
     * Raymarching Algorithm:
     * 1. For each pixel, construct primary ray from camera
     * 2. March through volume in steps:
     *    a. Sample radiance from 3D texture
     *    b. Accumulate color with front-to-back blending
     *    c. Track accumulated alpha
     *    d. Early termination if alpha > threshold
     * 3. Apply tone mapping and gamma correction
     * 4. Write final color to framebuffer
     * 
     * Step Size Strategies:
     * - Fixed: Constant step count (simplest)
     * - Adaptive: Larger steps in empty space
     * - Hierarchical: Use SDF for safe step size
     * 
     * Quality Settings:
     * - raymarchSteps: Number of steps per ray
     * - rayTerminationThreshold: Alpha cutoff (0.95 typical)
     * 
     * @note This runs as fragment shader, not compute shader
     */
    
    // TODO: Implement raymarching
    // Bind default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Set viewport to screen size
    glViewport(0, 0, GetScreenWidth(), GetScreenHeight());
    
    // Activate raymarch shader
    GLuint raymarchShader = shaders["raymarch.frag"];
    glUseProgram(raymarchShader);
    
    // Bind volume textures
    glUniform1i(glGetUniformLocation(raymarchShader, "uSDF"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    
    glUniform1i(glGetUniformLocation(raymarchShader, "uRadiance"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, currentRadianceTexture);
    
    // Set camera uniforms
    glUniformMatrix4fv(glGetUniformLocation(raymarchShader, "uViewMatrix"), 1, GL_FALSE, &camera.view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(raymarchShader, "uProjMatrix"), 1, GL_FALSE, &camera.projection[0][0]);
    
    // Set raymarching parameters
    glUniform1i(glGetUniformLocation(raymarchShader, "uSteps"), raymarchSteps);
    glUniform1f(glGetUniformLocation(raymarchShader, "uTerminationThreshold"), rayTerminationThreshold);
    
    // Render fullscreen quad
    // (or use modern approach with glDrawArrays(GL_TRIANGLES, ...))
    
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void Demo3D::renderDebugVisualization() {
    /**
     * @brief Display debug windows for intermediate buffers
     * 
     * Debug Views Available:
     * 1. Voxel Grid Slices (XY, XZ, YZ planes)
     * 2. Distance Field Visualization (heatmap)
     * 3. Individual Cascade Levels
     * 4. Probe Density Map
     * 5. Ray Hit Visualization
     * 6. Performance Metrics Graph
     * 
     * Implementation:
     * - Use ImGui windows with embedded textures
     * - Render specific Z slices of 3D textures
     * - Color-code distance values
     * - Show ray directions and hits
     * 
     * Performance Impact:
     * - Minimal (just sampling existing textures)
     * - Can be toggled off for benchmarking
     */
    
    // TODO: Implement debug visualization
    if (showCascadeSlices) {
        // Render XY slice at current Z
        // ImGui::Image((ImTextureID)(intptr_t)cascades[cascadeDisplayIndex].probeGridTexture, ...)
    }
    
    if (showVoxelGrid) {
        // Visualize voxel structure as wireframe
        // Use line rendering or point sprites
    }
    
    if (showPerformanceMetrics) {
        // Display timing graph
        ImGui::Begin("Performance Metrics");
        ImGui::Text("Frame Time: %.2f ms (%.1f FPS)", frameTimeMs, 1000.0f / frameTimeMs);
        ImGui::Text("Voxelization: %.2f ms", voxelizationTimeMs);
        ImGui::Text("SDF Generation: %.2f ms", sdfTimeMs);
        ImGui::Text("Cascade Update: %.2f ms", cascadeTimeMs);
        ImGui::Text("Raymarching: %.2f ms", raymarchTimeMs);
        ImGui::Text("Active Voxels: %d", activeVoxelCount);
        ImGui::Text("Memory Usage: %.1f MB", memoryUsageMB);
        ImGui::End();
    }
}

bool Demo3D::loadShader(const std::string& shaderName) {
    /**
     * @brief Load and compile shader from file
     * 
     * @param shaderName Name of shader file (e.g., "voxelize.comp")
     * @return true if successful, false otherwise
     * 
     * Loading Process:
     * 1. Read file from 3d/res/shaders/ directory
     * 2. Detect shader type from extension (.comp, .frag, .vert)
     * 3. Compile with glCreateShader/glCompileShader
     * 4. Link into program with glCreateProgram/glLinkProgram
     * 5. Store in shaders map
     * 6. Output compilation log on error
     * 
     * Hot-Reloading:
     * - Delete existing program if present
     * - Allows runtime shader editing
     * 
     * Note: Shader files are now located in 3d/res/shaders/ instead of res/shaders/
     */
    
    // TODO: Implement shader loading
    std::string filepath = "3d/res/shaders/" + shaderName;
    
    // Read file content
    // Compile based on type
    // Link program
    // Check for errors
    // Store in map
    
    return true; // Placeholder
}

void Demo3D::reloadShaders() {
    /**
     * @brief Reload all shaders for hot-swapping
     * 
     * Use Case:
     * - Edit shader files while application runs
     * - Press R to see changes immediately
     * - No need to restart application
     * 
     * Implementation:
     * 1. Iterate through shaders map
     * 2. Delete each program
     * 3. Call loadShader() for each
     * 4. Report success/failure
     */
    
    // TODO: Implement hot-reload
    std::cout << "[Demo3D] Reloading shaders..." << std::endl;
    
    for (auto const& [name, program] : shaders) {
        glDeleteProgram(program);
        loadShader(name);
    }
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
    
    // TODO: Implement buffer creation
    // Create 3D textures using gl::createTexture3D()
    // Set up framebuffers with attachments
    // Verify completeness
    
    // Example:
    voxelGridTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );
    
    sdfTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_R32F, GL_RED, GL_FLOAT, nullptr
    );
    
    // ... create other textures
    
    // Calculate memory usage
    memoryUsageMB = (volumeResolution * volumeResolution * volumeResolution * 5 * 8) / (1024.0f * 1024.0f);
    std::cout << "[Demo3D] Allocated " << memoryUsageMB << " MB for volume buffers" << std::endl;
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
    glDeleteTextures(1, &directLightingTexture);
    glDeleteTextures(1, &prevFrameTexture);
    glDeleteTextures(1, &currentRadianceTexture);
}

void Demo3D::initCascades() {
    /**
     * @brief Initialize radiance cascade hierarchy
     * 
     * Cascade Configuration (default):
     * - Cascade 0: 32³ probes, 0.1 unit cells, 4 rays
     * - Cascade 1: 64³ probes, 0.5 unit cells, 4 rays
     * - Cascade 2: 128³ probes, 2.0 unit cells, 4 rays
     * - Cascade 3: 64³ probes, 8.0 unit cells, 4 rays
     * - Cascade 4: 32³ probes, 32.0 unit cells, 4 rays
     * 
     * Design Rationale:
     * - Finer cascades near camera for detail
     * - Coarser cascades for large-scale lighting
     * - Exponential cell size increase (×4 each level)
     */
    
    // TODO: Implement cascade initialization
    int baseRes = 32;
    float baseCellSize = 0.1f;
    
    for (int i = 0; i < cascadeCount; ++i) {
        int resolution = baseRes * (i % 3 == 0 ? 1 : (i % 3 == 1 ? 2 : 4));
        float cellSize = baseCellSize * powf(4.0f, i);
        
        cascades[i].initialize(
            resolution,
            cellSize,
            glm::vec3(0.0f), // Origin at world center
            BASE_RAY_COUNT
        );
        
        cascades[i].active = true;
    }
    
    std::cout << "[Demo3D] Initialized " << cascadeCount << " cascade levels" << std::endl;
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
    
    if (showImGuiDemo) {
        ImGui::ShowDemoWindow(&showImGuiDemo);
    }
}

void Demo3D::renderSettingsPanel() {
    /**
     * @brief Render settings and controls panel
     * 
     * Controls Grouped By:
     * - General (scene selection, reset)
     * - Algorithm (RC vs traditional GI)
     * - Quality (steps, resolution)
     * - Performance (sparse voxels, temporal)
     * - Debug (visualization toggles)
     */
    
    ImGui::Begin("Settings");
    
    // Header: General Settings
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Scene Selection");
        
        const char* sceneNames[] = {
            "Empty Room", "Cornell Box", "Sponza", 
            "Maze", "Pillars Hall", "Procedural City"
        };
        
        int currentSceneIndex = currentScene + 1; // Offset for -1 (clear)
        if (ImGui::Combo("Scene", &currentSceneIndex, sceneNames, 6)) {
            setScene(currentSceneIndex - 1);
        }
        
        if (ImGui::Button("Clear Scene")) {
            setScene(-1);
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Reload Shaders")) {
            reloadShaders();
        }
        
        ImGui::Separator();
    }
    
    // Header: Algorithm Settings
    if (ImGui::CollapsingHeader("Algorithm", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Global Illumination Method");
        
        ImGui::RadioButton("Radiance Cascades", &useTraditionalGI, false);
        ImGui::SameLine();
        ImGui::RadioButton("Traditional GI", &useTraditionalGI, true);
        
        if (!useTraditionalGI) {
            ImGui::SliderInt("Cascade Count", &cascadeCount, 1, MAX_CASCADES);
            ImGui::Checkbox("Bilinear Filtering", &cascadeBilinear);
            ImGui::Checkbox("Disable Merging", &disableCascadeMerging);
        } else {
            ImGui::SliderInt("GI Rays", &giRayCount, 16, 256);
            ImGui::Checkbox("GI Noise", &giNoise);
        }
        
        ImGui::Separator();
    }
    
    // Header: Quality Settings
    if (ImGui::CollapsingHeader("Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Rendering Quality");
        
        ImGui::SliderInt("Raymarch Steps", &raymarchSteps, 64, 512);
        ImGui::SliderFloat("Termination Threshold", &rayTerminationThreshold, 0.8f, 0.99f, "%.2f");
        
        ImGui::Separator();
        
        ImGui::SliderFloat("Indirect Mix", &indirectMixFactor, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Indirect Brightness", &indirectBrightness, 0.5f, 3.0f, "%.2f");
        
        ImGui::Separator();
        
        ImGui::Checkbox("Ambient Light", &ambientLight);
        if (ambientLight) {
            ImGui::ColorEdit3("Ambient Color", &ambientColor.x);
        }
    }
    
    // Header: Performance Options
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Optimization Features");
        
        ImGui::Checkbox("Sparse Voxels", &useSparseVoxels);
        ImGui::Checkbox("Temporal Reprojection", &useTemporalReprojection);
        ImGui::Checkbox("Adaptive Step Size", &adaptiveStepSize);
        
        ImGui::Separator();
        
        ImGui::Text("Volume Resolution: %d³", volumeResolution);
        ImGui::Text("Active Voxels: %d", activeVoxelCount);
        ImGui::Text("Memory Usage: %.1f MB", memoryUsageMB);
    }
    
    // Header: Debug Options
    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::SeparatorText("Visualization");
        
        ImGui::Checkbox("Show Debug Windows", &showDebugWindows);
        ImGui::Checkbox("Show Cascade Slices", &showCascadeSlices);
        ImGui::Checkbox("Show Voxel Grid", &showVoxelGrid);
        ImGui::Checkbox("Show Performance", &showPerformanceMetrics);
        ImGui::Checkbox("Show ImGui Demo", &showImGuiDemo);
        
        ImGui::Separator();
        
        ImGui::SliderInt("Display Cascade", &cascadeDisplayIndex, 0, cascadeCount - 1);
    }
    
    ImGui::End();
}

void Demo3D::renderCascadePanel() {
    /**
     * @brief Render cascade visualization and control panel
     * 
     * Displays:
     * - Cascade hierarchy tree
     * - Per-cascade parameters
     * - Live preview of selected cascade
     * - Merging toggle
     */
    
    ImGui::Begin("Radiance Cascades");
    
    ImGui::SeparatorText("Cascade Hierarchy");
    
    for (int i = 0; i < cascadeCount; ++i) {
        std::string header = "Cascade " + std::to_string(i);
        
        if (i == 0) {
            header += " (Finest)";
        } else if (i == cascadeCount - 1) {
            header += " (Coarsest)";
        }
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
        if (i == cascadeDisplayIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        if (ImGui::TreeNodeEx(header.c_str(), flags)) {
            ImGui::Indent();
            
            ImGui::Text("Resolution: %d³", cascades[i].resolution);
            ImGui::Text("Cell Size: %.3f units", cascades[i].cellSize);
            ImGui::Text("Rays/Probe: %d", cascades[i].raysPerProbe);
            ImGui::Text("Interval: [%.2f, %.2f]", 
                       cascades[i].intervalStart, 
                       cascades[i].intervalEnd);
            ImGui::Text("Active: %s", cascades[i].active ? "Yes" : "No");
            
            // Calculate memory for this cascade
            float cascadeMem = (cascades[i].resolution * cascades[i].resolution * 
                               cascades[i].resolution * 8) / (1024.0f * 1024.0f);
            ImGui::Text("Memory: %.1f MB", cascadeMem);
            
            ImGui::Unindent();
            ImGui::TreePop();
        }
    }
    
    ImGui::Separator();
    
    ImGui::SeparatorText("Controls");
    
    ImGui::Checkbox("Disable Cascade Merging", &disableCascadeMerging);
    
    if (ImGui::Button("Reset Cascades")) {
        destroyCascades();
        initCascades();
    }
    
    ImGui::Separator();
    
    ImGui::SeparatorText("Information");
    
    ImGui::TextWrapped(
        "Radiance Cascades use a hierarchical probe grid to efficiently "
        "compute global illumination. Finer cascades capture near-field details, "
        "while coarser cascades handle far-field lighting."
    );
    
    ImGui::End();
}

void Demo3D::renderTutorialPanel() {
    /**
     * @brief Render tutorial and information panel
     * 
     * Content Sections:
     * - How Radiance Cascades Work
     * - Parameter Explanations
     * - Performance Tips
     * - Keyboard Shortcuts
     */
    
    ImGui::Begin("Tutorial");
    
    if (ImGui::CollapsingHeader("How Radiance Cascades Work", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped(
            "Radiance Cascades is a real-time global illumination algorithm that uses "
            "a hierarchical structure of light probes to efficiently compute indirect lighting."
        );
        
        ImGui::Separator();
        
        ImGui::BulletText("1. Voxelization: Convert geometry to voxels");
        ImGui::BulletText("2. SDF Generation: Compute distance field using JFA");
        ImGui::BulletText("3. Cascade Injection: Cast rays from probe grid");
        ImGui::BulletText("4. Hierarchical Merge: Combine cascade levels");
        ImGui::BulletText("5. Raymarching: Visualize final radiance volume");
    }
    
    if (ImGui::CollapsingHeader("Controls & Shortcuts")) {
        ImGui::Text("Camera Movement:");
        ImGui::BulletText("WASD - Move horizontally");
        ImGui::BulletText("Q/E - Move up/down");
        ImGui::BulletText("Mouse Drag - Rotate view");
        ImGui::BulletText("Scroll - Adjust brush size");
        
        ImGui::Separator();
        
        ImGui::Text("Commands:");
        ImGui::BulletText("1 - Voxelization mode");
        ImGui::BulletText("2 - Light placement mode");
        ImGui::BulletText("Space - Toggle mode");
        ImGui::BulletText("F1 - Toggle UI");
        ImGui::BulletText("F2 - Screenshot");
        ImGui::BulletText("R - Reload shaders");
        ImGui::BulletText("C - Clear scene");
        ImGui::BulletText("Escape - Exit");
    }
    
    if (ImGui::CollapsingHeader("Performance Tips")) {
        ImGui::BulletText("Use fewer cascades for better performance");
        ImGui::BulletText("Reduce volume resolution for faster rendering");
        ImGui::BulletText("Enable sparse voxels for large scenes");
        ImGui::BulletText("Use temporal reprojection to reduce noise");
        ImGui::BulletText("Adjust raymarch steps based on quality needs");
    }
    
    if (ImGui::CollapsingHeader("About")) {
        ImGui::TextWrapped(
            "This is a 3D implementation of the Radiance Cascades algorithm, "
            "originally developed by Alexander Sannikov. The technique provides "
            "real-time global illumination suitable for games and interactive applications."
        );
        
        ImGui::Separator();
        
        ImGui::Text("Current Implementation:");
        ImGui::BulletText("Volume Resolution: %d³", volumeResolution);
        ImGui::BulletText("Cascade Levels: %d", cascadeCount);
        ImGui::BulletText("OpenGL 4.3+ Required");
    }
    
    ImGui::End();
}

void Demo3D::onResize() {
    /**
     * @brief Handle window resize event
     * 
     * Actions:
     * 1. Update viewport dimensions
     * 2. Recalculate projection matrix
     * 3. Optionally recreate volume buffers at new resolution
     */
    
    // TODO: Implement resize handler
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    glViewport(0, 0, width, height);
    
    // Update projection matrix
    // camera.projection = glm::perspective(...)
}

void Demo3D::takeScreenshot() {
    /**
     * @brief Capture current frame to PNG file
     * 
     * Implementation:
     * 1. Read pixels from framebuffer with glReadPixels()
     * 2. Flip vertically (OpenGL coordinates are bottom-up)
     * 3. Save as PNG using stb_image_write
     * 4. Display confirmation message
     */
    
    // TODO: Implement screenshot
    std::cout << "[Demo3D] Screenshot captured." << std::endl;
}

void Demo3D::resetCamera() {
    /**
     * @brief Reset camera to default viewing position
     * 
     * Default Position:
     * - Position: (0, 0, -5)
     * - Target: (0, 0, 0)
     * - Up: (0, 1, 0)
     * - FOV: 45 degrees
     */
    
    // TODO: Implement camera reset
    camera.position = glm::vec3(0.0f, 0.0f, -5.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.moveSpeed = 5.0f;
    camera.rotationSpeed = 0.5f;
}

glm::ivec3 Demo3D::calculateWorkGroups(int dimX, int dimY, int dimZ, int localSize) {
    /**
     * @brief Calculate optimal work group count for compute shader
     * 
     * Formula:
     * workGroups = ceil(globalSize / localSize)
     * 
     * @param dimX Total work items in X
     * @param dimY Total work items in Y
     * @param dimZ Total work items in Z
     * @param localSize Local work group size (from shader layout)
     * @return glm::ivec3 Work group count (x, y, z)
     * 
     * Example:
     * Global size: 128³, Local size: 8³
     * Work groups: (16, 16, 16)
     */
    
    // TODO: Implement calculation
    return glm::ivec3(
        (dimX + localSize - 1) / localSize,
        (dimY + localSize - 1) / localSize,
        (dimZ + localSize - 1) / localSize
    );
}
