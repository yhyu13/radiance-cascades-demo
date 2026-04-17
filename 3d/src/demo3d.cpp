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
    , lastMousePos(0.0f)
{
    /**
     * @brief Construct 3D demo and initialize all resources
     */
    
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
    loadShader("radiance_3d.comp");
    loadShader("inject_radiance.comp");
    // Note: raymarch.frag needs special handling - skip for now
    
    // Step 5: Initialize cascades
    initCascades();
    
    // Step 6: Set up initial scene
    std::cout << "\n[Demo3D] Setting up initial scene..." << std::endl;
    setScene(0); // Empty room
    
    // Step 7: Initialize ImGui
    ImGui::GetIO().IniFilename = NULL;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "[Demo3D] Initialization complete!" << std::endl;
    std::cout << "[Demo3D] Volume resolution: " << volumeResolution << "³" << std::endl;
    std::cout << "[Demo3D] Memory usage: ~" << memoryUsageMB << " MB" << std::endl;
    std::cout << "[Demo3D] Shaders loaded: " << shaders.size() << std::endl;
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
    
    // Basic camera controls would go here
    // For quick start, rely on Raylib's built-in camera handling in main3d.cpp
}

void Demo3D::update() {
    /**
     * @brief Update simulation state
     */
    
    time += GetFrameTime();
    
    // Check if scene needs update
    if (sceneDirty) {
        voxelizationPass();
    }
}

void Demo3D::render() {
    /**
     * @brief Execute complete rendering pipeline
     */
    
    // Pass 1: Voxelization (if needed)
    if (sceneDirty) {
        voxelizationPass();
    }
    
    // Pass 2: SDF Generation
    sdfGenerationPass();
    
    // Pass 3: Direct Lighting
    injectDirectLighting();
    
    // Pass 4: Radiance Cascades
    updateRadianceCascades();
    
    // Pass 5: Raymarching
    raymarchPass();
    
    // Calculate frame time
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

void Demo3D::sdfGenerationPass() {
    /**
     * @brief Generate 3D signed distance field using jump flooding
     * 
     * Quick start implementation: Skip full JFA, just create a simple placeholder
     */
    
    std::cout << "[Demo3D] SDF generation (placeholder - full JFA not yet implemented)" << std::endl;
    
    // For quick start, we'll skip the full 3D JFA algorithm
    // In production, this would:
    // 1. Initialize seed buffer from voxel grid
    // 2. Run log2(N) propagation passes
    // 3. Extract distances
    
    // TODO: Implement full 3D JFA when ready
}

void Demo3D::updateRadianceCascades() {
    /**
     * @brief Update all radiance cascade levels
     */
    
    std::cout << "[Demo3D] Updating radiance cascades (" << cascadeCount << " levels)" << std::endl;
    
    for (int i = 0; i < cascadeCount; ++i) {
        if (cascades[i].active) {
            updateSingleCascade(i);
        }
    }
}

void Demo3D::updateSingleCascade(int cascadeIndex) {
    /**
     * @brief Update single cascade level
     */
    
    if (cascadeIndex >= cascadeCount || !cascades[cascadeIndex].active) {
        return;
    }
    
    std::cout << "  Cascade " << cascadeIndex << ": resolution=" << cascades[cascadeIndex].resolution 
              << ", cellSize=" << cascades[cascadeIndex].cellSize << std::endl;
    
    // TODO: Implement actual cascade update with compute shader dispatch
    // For quick start, this is a placeholder
}

void Demo3D::injectDirectLighting() {
    /**
     * @brief Inject direct lighting into cascades
     */
    
    std::cout << "[Demo3D] Injecting direct lighting (placeholder)" << std::endl;
    
    // TODO: Implement direct lighting injection
    // For quick start, skip this
}

void Demo3D::raymarchPass() {
    /**
     * @brief Raymarching pass for final visualization
     * 
     * Quick start: Just clear to a gradient background since full raymarching needs SDF
     */
    
    // For quick start, we'll just render a simple test pattern
    // Full implementation would cast rays through the volume
    
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // TODO: Implement actual volume raymarching when SDF is ready
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
        // Fragment shader - need to create a simple program with vertex + fragment
        // For now, we'll skip this as we're using compute shaders primarily
        std::cerr << "[WARNING] Fragment shader loading not yet implemented: " << shaderName << std::endl;
        return false;
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
    loadShader("radiance_3d.comp");
    loadShader("inject_radiance.comp");
    
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
    
    ImGui::Checkbox("Show Performance Metrics", &showPerformanceMetrics);
    ImGui::Checkbox("Show Debug Windows", &showDebugWindows);
    
    ImGui::End();
}

void Demo3D::renderCascadePanel() {
    /**
     * @brief Render cascade visualization panel
     */
    
    ImGui::Begin("Cascades");
    
    ImGui::Text("Cascade Count: %d", cascadeCount);
    
    for (int i = 0; i < cascadeCount; ++i) {
        if (cascades[i].active) {
            ImGui::Text("Level %d: %d^3 probes, cell=%.2f", 
                       i, cascades[i].resolution, cascades[i].cellSize);
        }
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
    
    ImGui::Text("Current Features:");
    ImGui::BulletText("✓ Basic voxelization");
    ImGui::BulletText("✓ Volume textures created");
    ImGui::BulletText("✓ Shader loading");
    ImGui::BulletText("✗ SDF generation (placeholder)");
    ImGui::BulletText("✗ Full raymarching (placeholder)");
    
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
    
    camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
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
