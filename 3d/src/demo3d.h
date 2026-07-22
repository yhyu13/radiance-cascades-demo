/**
 * @file demo3d.h
 * @brief Main demo class for 3D Radiance Cascades implementation
 * 
 * This header defines the core rendering system for volumetric global illumination
 * using radiance cascades in 3D space. The implementation uses compute shaders
 * for voxel processing and fragment shaders for final raymarching.
 * 
 * Architecture Overview:
 * 1. Voxelization Pass: Convert 3D geometry to sparse voxel representation
 * 2. SDF Generation: Compute 3D signed distance field using jump flooding
 * 3. Radiance Cascades: Hierarchical probe grid for efficient light transport
 * 4. Raymarching: Volume rendering for final pixel color
 * 
 * Key Differences from 2D Version:
 * - Uses 3D volume textures instead of 2D render textures
 * - Compute shaders for parallel voxel processing
 * - Sparse Voxel Octree (SVO) for memory efficiency
 * - Temporal reprojection to reduce per-frame computation
 */

#ifndef DEMO3D_H
#define DEMO3D_H

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>

#ifdef _WIN32
#include "rdoc_helper.h"
#endif
#include "config.h"
#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include "gl_helpers.h"
#include "analytic_sdf.h"  // Analytic SDF primitives for Phase 0
#include "obj_loader.h"    // OBJ mesh loader for scene import
#include "surface_rc.h"    // ShaderToy2 surface-attached RC experimental path
#include <glm/glm.hpp>

// =============================================================================
// Constants and Configuration
// =============================================================================

/** Maximum number of cascade levels in the hierarchy */
constexpr int MAX_CASCADES = 6;

/** Default volume resolution (128^3 = 64 SDF samples per C0 probe cell, ~8 MB R32F) */
constexpr int DEFAULT_VOLUME_RESOLUTION = 128;

/** Maximum volume resolution supported */
constexpr int MAX_VOLUME_RESOLUTION = 512;


/** Enable/disable sparse voxel optimization */
constexpr bool USE_SPARSE_VOXELS_DEFAULT = true;

// =============================================================================
// Data Structures
// =============================================================================

/**
 * @brief Sparse Voxel Node for memory-efficient representation
 * 
 * Only voxels near surfaces are allocated, reducing memory from O(n³) to
 * O(surface area). Each node can have 8 children for hierarchical subdivision.
 */
struct VoxelNode {
    /** Packed density and material info */
    float density;
    
    /** Radiance stored in this voxel (RGB + alpha for emission) */
    glm::vec4 radiance;
    
    /** Child node indices in the voxel array (-1 if leaf) */
    int children[8];
    
    /** Parent node index (-1 for root) */
    int parent;
    
    /** World-space position of voxel center */
    glm::vec3 position;
    
    /** Size of voxel in world units */
    float size;
    
    /** Constructor for empty voxel */
    VoxelNode();
};

/**
 * @brief Radiance Cascade Level Configuration
 * 
 * Each cascade level has different resolution, cell size, and ray count.
 * Finer cascades near camera, coarser cascades for far field.
 */
struct RadianceCascade3D {
    /** OpenGL texture ID for 3D probe grid (isotropic average, written by reduction pass) */
    GLuint probeGridTexture;

    /** Phase 9: temporal history of probeGridTexture (same dims, RGBA16F) */
    GLuint probeGridHistory;

    /** Phase 5b: per-direction D×D tile atlas — (res*D)×(res*D)×res RGBA16F */
    GLuint probeAtlasTexture;

    /** Phase 9: temporal history of probeAtlasTexture (same dims, RGBA16F) */
    GLuint probeAtlasHistory;
    
    /** Resolution of probe grid (e.g., 32, 64, 128) */
    int resolution;
    
    /** Size of each probe cell in world units */
    float cellSize;
    
    /** World-space origin of cascade grid */
    glm::vec3 origin;
    
    /** Number of rays cast per probe */
    int raysPerProbe;
    
    /** Distance interval start for this cascade */
    float intervalStart;
    
    /** Distance interval end for this cascade */
    float intervalEnd;
    
    /** Whether this cascade is currently active */
    bool active;
    
    /** Constructor */
    RadianceCascade3D();
    
    /** Initialize cascade with parameters */
    void initialize(int res, float cellSz, const glm::vec3& org, int rays);
    
    /** Cleanup OpenGL resources */
    void destroy();
};

/**
 * @brief Camera configuration for 3D navigation
 */
struct Camera3DConfig {
    /** Camera position in world space */
    glm::vec3 position;
    
    /** Camera target point (look-at) */
    glm::vec3 target;
    
    /** Camera up vector */
    glm::vec3 up;
    
    /** Field of view in degrees */
    float fovy;
    
    /** Camera projection mode */
    CameraProjection projection;
    
    /** Movement speed */
    float moveSpeed;
    
    /** Rotation sensitivity */
    float rotationSpeed;
};

// =============================================================================
// Main Demo Class
// =============================================================================

/**
 * @brief Main demo controller for 3D Radiance Cascades
 * 
 * Manages the complete rendering pipeline from voxelization through final
 * raymarched output. Handles resource management, shader binding, and
 * UI interaction.
 * 
 * Usage Example:
 * @code
 *   Demo3D demo;
 *   while (!WindowShouldClose()) {
 *       demo.processInput();
 *       demo.update();
 *       demo.render();
 *   }
 * @endcode
 */
class Demo3D {
public:
    // =============================================================================
    // Construction & Initialization
    // =============================================================================
    
    /**
     * @brief Construct new 3D demo object
     * 
     * Initializes OpenGL resources, loads shaders, creates volume textures,
     * and sets up default scene configuration.
     */
    Demo3D();
    
    /**
     * @brief Destructor - cleanup all resources
     * 
     * Releases OpenGL textures, buffers, shaders, and other GPU resources.
     */
    ~Demo3D();
    
    // =============================================================================
    // Main Loop Functions
    // =============================================================================
    
    /**
     * @brief Process keyboard and mouse input
     * 
     * Handles camera movement, brush controls, and debug commands.
     * Called once per frame before update().
     */
    void processInput();
    
    /**
     * @brief Update simulation state
     * 
     * Processes dynamic elements, updates voxel data, and prepares
     * rendering state. Called once per frame after processInput().
     */
    void update();
    
    /**
     * @brief Render the current frame
     * 
     * Executes the complete rendering pipeline:
     * 1. Voxelization
     * 2. SDF generation
     * 3. Radiance cascade update
     * 4. Raymarching
     * 5. UI overlay
     */
    void render();
    
    // =============================================================================
    // Rendering Pipeline Stages
    // =============================================================================
    
    /**
     * @brief Voxelization pass - convert geometry to voxels
     * 
     * Renders scene from multiple viewpoints to create 3D voxel representation.
     * Uses geometry shader or transform feedback for voxel generation.
     */
    void voxelizationPass();
    
    /**
     * @brief Signed Distance Field generation
     *
     * Computes 3D SDF from voxel grid using jump flooding algorithm extended to 3D.
     * Runs as compute shader on GPU.
     * Step 3 (codex 07 F1): returns true on success, false on mesh-bake failure.
     * Caller (render loop) only flips its `sdfReady` flag on true.
     */
    bool sdfGenerationPass();
    
    /**
     * @brief Update all radiance cascade levels
     * 
     * Iterates through cascade hierarchy from fine to coarse, computing
     * radiance at each probe location.
     */
    void updateRadianceCascades();
    
    /**
     * @brief Update single cascade level
     * 
     * @param cascadeIndex Index of cascade to update (0 = finest)
     */
    void updateSingleCascade(int cascadeIndex);
    
    /**
     * @brief Inject direct lighting into cascades
     * 
     * Adds emission from light sources to appropriate cascade probes.
     */
    void injectDirectLighting();
    
    /**
     * @brief Raymarching pass for final visualization
     *
     * Casts rays through volume to produce final pixel colors.
     * Supports both perspective and orthographic projections.
     */
    void raymarchPass();

    /** Create or recreate the GI blur FBO at size w x h. */
    void initGIBlur(int w, int h);

    /** Free GI blur FBO and textures. */
    void destroyGIBlur();

    /** Apply bilateral blur to the GI FBO color output; writes to default framebuffer. */
    void giBlurPass();

    // Phase 7: PT reference dispatch + helpers (doc/7/pt_reference_plan.md).
    void ptDispatchReference();
    void ptEnsureAccumAllocated(int viewportW, int viewportH);

    // Hybrid correction dispatch + helpers (doc/7/hybrid_rc_pixel_correction_plan.md).
    void hybridDispatchCorrection();
    void hybridEnsureAccumAllocated(int viewportW, int viewportH);

    /**
     * @brief Debug visualization of intermediate buffers
     * 
     * Renders slices through volume textures for debugging.
     */
    void renderDebugVisualization();
    
    // =============================================================================
    // Resource Management
    // =============================================================================
    
    /**
     * @brief Load shader from file
     * 
     * @param shaderName Name of shader file (without path)
     * @return true if loaded successfully
     */
    bool loadShader(const std::string& shaderName);
    
    /**
     * @brief Reload all shaders (for hot-reloading)
     */
    void reloadShaders();
    
    /**
     * @brief Create all volume textures and framebuffers
     */
    void createVolumeBuffers();
    
    /**
     * @brief Destroy all volume textures and framebuffers
     */
    void destroyVolumeBuffers();
    
    /**
     * @brief Initialize radiance cascade hierarchy
     */
    void initCascades();
    
    /**
     * @brief Destroy radiance cascade resources
     */
    void destroyCascades();
    
    /**
     * @brief Set up scene geometry
     * 
     * @param sceneType Type of scene to load (-1 = clear, 0+ = preset scenes)
     */
    void setScene(int sceneType);
    
    /**
     * @brief Upload analytic primitives to GPU SSBO
     * 
     * Transfers primitive data from CPU to GPU for parallel evaluation.
     */
    void uploadPrimitivesToGPU();
    
    /**
     * @brief Load and voxelize OBJ mesh file
     * @param filename Path to .obj file
     * @return true if successful
     */
    bool loadOBJMesh(const std::string& filename);
    
    /**
     * @brief Initialize debug quad geometry for SDF visualization
     */
    void initDebugQuad();
    
    /**
     * @brief Render SDF cross-section debug view (OpenGL part)
     * 
     * Displays a 2D slice of the 3D SDF volume as grayscale.
     * Useful for verifying SDF generation correctness.
     */
    void renderSDFDebug();

    /**
     * @brief Render radiance cascade slice viewer (OpenGL part only)
     */
    void renderRadianceDebug();

    /**
     * @brief Render SDF debug UI overlay (ImGui part)
     *
     * Must be called between rlImGuiBegin() and rlImGuiEnd().
     */
    void renderSDFDebugUI();

    /**
     * @brief Render radiance cascade debug UI overlay (Phase 1)
     */
    void renderRadianceDebugUI();

    /** Render ShaderToy2 surface-attached debug atlas UI overlay. */
    void renderSurfaceDebugUI();
    
    /**
     * @brief Render lighting debug UI overlay (Phase 1)
     */
    void renderLightingDebugUI();
    
    /**
     * @brief Helper to add a box of voxels to the volume
     * 
     * @param center Box center in world space
     * @param size Box dimensions (width, height, depth)
     * @param color RGB color (0-1 range)
     * @param emissive Whether the box is emissive (light source)
     */
    void addVoxelBox(
        const glm::vec3& center,
        const glm::vec3& size,
        const glm::vec3& color,
        bool emissive = false
    );

    // =============================================================================
    // UI Rendering
    // =============================================================================
    
    /**
     * @brief Render ImGui interface
     * 
     * Displays controls for lighting, cascades, and debug options.
     */
    void renderUI();
    
    /**
     * @brief Render settings panel
     */
    void renderSettingsPanel();
    
    /**
     * @brief Render cascade visualization panel
     */
    void renderCascadePanel();
    
    /**
     * @brief Render tutorial/information panel
     */
    void renderTutorialPanel();
    
    // =============================================================================
    // Utility Functions
    // =============================================================================
    
    /**
     * @brief Handle window resize event
     * 
     * Recreates volume buffers at new resolution.
     */
    void onResize();
    
    /**
     * @brief Take screenshot of current frame (3D scene only, no ImGui).
     * Call between EndMode3D() and rlImGuiBegin(). If launchAiAnalysis is
     * true and pendingScreenshot is set, spawns analyze_screenshot.py.
     */
    void takeScreenshot(bool launchAiAnalysis = false);
    
    /**
     * @brief Reset camera to default position
     */
    void resetCamera();
    
    /**
     * @brief Get Raylib Camera3D from internal camera config
     * 
     * Converts the internal Camera3DConfig to Raylib's Camera3D type
     * for use with BeginMode3D/EndMode3D.
     * 
     * @return Camera3D Raylib camera structure
     */
    Camera3D getRaylibCamera() const;
    
    /**
     * @brief Calculate optimal work group size for compute shader
     * 
     * @param dimX Total work items in X
     * @param dimY Total work items in Y
     * @param dimZ Total work items in Z
     * @param localSize Local work group size from shader
     * @return glm::ivec3 Work group count (x, y, z)
     */
    glm::ivec3 calculateWorkGroups(
        int dimX, int dimY, int dimZ,
        int localSize = 8
    );

    // codex 07 F2/F3 — let main3d.cpp set initial render mode for headless captures.
    // Range now covers modes 0-25, including Phase 2F modes 21-25.
    // No clamp; preserves existing shader fallthrough on out-of-range.
    void setRenderMode(int m) {
        if (m < 0 || m > 25) {
            std::cerr << "[Demo3D] WARN: render mode " << m
                      << " out of range [0,25]; rendering as default\n";
        }
        raymarchRenderMode = m;
    }

    // Step 10 — Camera state CLI/UI setters. Apply AFTER any auto-fit/preset paths.
    void setCameraPosition(const glm::vec3& p);
    void setCameraTarget(const glm::vec3& t);
    void setCameraFovy(float f);

    // Lighting controls — continuous bake-side ambient floor strength.
    // Replaces the original `setStripAmbientFloorBake(bool)` toggle (which
    // was binary 0 vs 0.05). Strength=0 reproduces the strip behavior.
    // Lighting change → 4-line invalidation (no meshSDFReady; codex 08 F1).
    void setAmbientBakeStrength(float v) {
        if (ambientBakeStrength == v) return;
        ambientBakeStrength = v;
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] ambientBakeStrength=" << v
                  << " (cascade rebake triggered; SDF unchanged)\n";
    }

    // Backward-compat: `--strip-ambient-floor-bake` CLI flag still works
    // by calling this — equivalent to setAmbientBakeStrength(0.0f).
    void setStripAmbientFloorBake(bool v) {
        setAmbientBakeStrength(v ? 0.0f : 0.05f);
    }

    // Composite-side ambient floor strength (raymarch.frag's vec3(...) literal).
    // Independent of the bake-side floor — uniform-only update, no cascade rebake.
    void setAmbientCompositeStrength(float v) {
        ambientCompositeStrength = v;
        std::cout << "[Demo3D] ambientCompositeStrength=" << v << "\n";
    }

    // Lighting controls — directional light support (Sponza-style sun light).
    // When useDirectionalLight is true, the cascade bake + raymarch see a
    // far-away point light derived from `lightDirection` so the existing
    // point-light shaders naturally degenerate to directional behavior
    // (no shader changes needed). Sponza variants enable this on load.
    void setUseDirectionalLight(bool v) {
        useDirectionalLight = v;
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] useDirectionalLight=" << v << " (cascade rebake)\n";
    }
    void setLightDirection(const glm::vec3& d) {
        lightDirection = glm::length(d) > 1e-6f ? glm::normalize(d) : glm::vec3(0, -1, 0);
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] lightDirection=(" << lightDirection.x << ","
                  << lightDirection.y << "," << lightDirection.z << ")\n";
    }
    // 2026-05-28 Stage 11d: point-light position setter (was member-only before).
    void setLightPosition(const glm::vec3& p) {
        lightPosition       = p;
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] lightPosition=(" << lightPosition.x << ","
                  << lightPosition.y << "," << lightPosition.z << ") (cascade rebake)\n";
    }
    void setLightIntensity(float v) {
        lightIntensity = v;
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] lightIntensity=" << v << " (cascade rebake)\n";
    }

    // (setVisibilityMode() deprecation stub deleted in Phase 2.5c. The
    // visibility-mode switch was retired in Phase 2 2C; the deprecation
    // grace covered Phase 2's release; Phase 2.5 ships in the same release.
    // Any caller still using setVisibilityMode() will fail to compile —
    // intended outcome.)

    // Phase 2.5a.1: bake-leak baseline measurement. Set via --bake-leak-test=path.
    // After bakeLeakTestFramesAfter frames have rendered (default 240, ample for
    // EMA convergence at α=0.1), reads back C0 atlas via glGetTexImage and
    // computes the metric per visibility_phase2.5_plan §2.5a.1.
    void setBakeLeakTest(const std::string& outPath, int framesAfter = 240) {
        bakeLeakTestOutPath      = outPath;
        bakeLeakTestFramesAfter  = framesAfter;
        bakeLeakTestPending      = true;
        bakeLeakElapsedFrames    = 0;
        std::cout << "[Demo3D] --bake-leak-test=" << outPath
                  << " (compute after " << framesAfter << " elapsed frames + cascade ready)\n";
    }
    bool bakeLeakTestComplete() const { return bakeLeakTestDone; }
    bool bakeLeakTestActive()   const { return bakeLeakTestPending; }

    // Phase 2.5d critic-10 W1: true iff all CRITICAL shaders loaded OK at init.
    // Set by initialize() after the loadShader() chain. main3d uses this to set
    // a nonzero exit code when a shader-load failure has occurred — the app
    // continues running (so the *banner* in stderr is visible) but the exit
    // code signals "this run produced wrong output" to any orchestrator.
    bool criticalShaderLoadOk    = true;
    bool allCriticalShadersOk() const { return criticalShaderLoadOk; }
    bool selectedBackendShadersOk() const;

    std::string getLegacyBackendName() const;
    std::string getRenderViewName() const;
    std::string getSceneLabel() const;
    uint64_t getSceneRevision() const { return sceneRevision; }
    uint64_t getShaderRevision() const { return shaderRevision; }
    int getRenderMode() const { return raymarchRenderMode; }

    // Phase 2.5d critic-10 W4: scene-validation accessor for the
    // --cam-preset=NAME flag in main3d.cpp. Returns the current OBJ key
    // ("cornell", "cornell_orig", "cornell_orig_alcove", "sponza", "sponza_master")
    // or empty if no OBJ is loaded.
    const std::string& getCurrentOBJPath() const { return currentOBJPath; }
    // Computes metric for the current scene + writes JSON to bakeLeakTestOutPath.
    // Idempotent: sets bakeLeakTestDone, won't re-run.
    void computeBakeLeakMetric();

    // Phase 2.5d M1: enable diagnostic alpha mode. When 1, surface bins write
    // sdfBefore_normalized to alpha (see radiance_3d.comp). MUST also set a
    // bake-leak test path; the metric then doubles as a histogram source.
    // Triggers a cascade rebake (uniform changes affect bake output).
    void setDiagAlphaMode(int m) {
        // Critic 12 L3: early-return on no-op to avoid redundant cascade rebuild.
        if (m == diagAlphaMode) return;
        diagAlphaMode = m;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] diagAlphaMode=" << m
                  << " (1=write sdfBefore_normalized to surface-bin alpha; cascade rebake)\n";
    }

    // Phase 3 (bake-side leak fix): toggle for the 3D WeightedSample bake-time
    // visibility check. Triggers a full cascade rebake (changes baked atlas content).
    void setUseWeightedSample(bool v) {
        if (v == useWeightedSample) return;
        useWeightedSample = v;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] useWeightedSample=" << (v ? "ON" : "OFF")
                  << " (Phase 3 bake-side per-corner visibility; cascade rebake)\n";
    }
    bool getUseWeightedSample() const { return useWeightedSample; }

    void setPhase3DebugMode(int v) {
        if (v == phase3DebugMode) return;
        phase3DebugMode = v;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] phase3DebugMode=" << v << "\n";
    }
    void setGIStrength(float v) {
        if (v == giStrength) return;
        giStrength = v;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] giStrength=" << v << "\n";
    }

    // Phase MB (multi-bounce temporal feedback) setters — trigger cascade rebake
    // since atlas content depends on the toggle/gain.
    void setUseMultiBounce(bool v) {
        if (v == useMultiBounce) return;
        useMultiBounce = v;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] useMultiBounce=" << (v ? "ON" : "OFF") << "\n";
    }
    void setMultiBounceGain(float v) {
        if (v == multiBounceGain) return;
        multiBounceGain = v;
        cascadeReady = false;
        forceCascadeRebuild = true;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        std::cout << "[Demo3D] multiBounceGain=" << v << "\n";
    }
    // v4 Phase 1A: per-scene MB-gain preset. When true, the manual gain slider
    // is overridden by the scene-class lookup (Sponza→0.10, Cornell→1.0).
    // The setter does NOT trigger a cascade rebuild — the gain is set by
    // setMultiBounceGain in the same post-load hook.
    void setUsePerSceneMbGain(bool v) { usePerSceneMbGain = v; }

    // Hybrid correction setters + invalidation (doc/7)
    void resetHybridAccumulator() { hybridDirty = true; hybridSampleCount = 0; }
    void setUseHybrid(bool v) {
        if (v == useHybrid) return;
        useHybrid = v;
        resetHybridAccumulator();
        std::cout << "[Hybrid] " << (v ? "ON" : "OFF")
                  << " (display-path per-pixel correction; reset accumulator)\n";
    }
    void setHybridBlendWeight(float v) {
        if (v == hybridBlendWeight) return;
        hybridBlendWeight = v;
        resetHybridAccumulator();
    }
    void setHybridEMAAlpha(float v) {
        if (v == hybridEMAAlpha) return;
        hybridEMAAlpha = v;
        // No accumulator reset — EMA change just takes effect on subsequent samples.
    }
    void setHybridRaysPerFrame(int v) {
        if (v == hybridRaysPerFrame) return;
        hybridRaysPerFrame = v < 1 ? 1 : v;
        resetHybridAccumulator();
    }
    void setHybridUseMaxComp(bool v) {
        // Composition switch only — no accumulator reset needed; the cached correction
        // values are still valid, only the per-pixel combination changes.
        hybridUseMaxComp = v;
    }
    // v1.2 cooperative variance merge controls
    void setHybridUseVarianceMerge(bool v) { hybridUseVarianceMerge = v; }
    void setHybridCascadeVariance(float v) { hybridCascadeVariance = v < 1e-6f ? 1e-6f : v; }
    void setHybridConfidenceSamples(int v) { hybridConfidenceSamples = v < 1 ? 1 : v; }
    // Phase 8 A/B sweep entry point. Triggered by --hybrid-ab-sweep=<dir>; see demo3d.cpp impl.
    void startHybridSweepPublic(const std::string& outDir);
    void setHybridBlurRadius(int v)        { hybridBlurRadius = v < 0 ? 0 : (v > 6 ? 6 : v); }
    void setHybridBlurDepthSigma(float v)  { hybridBlurDepthSigma = v < 1e-4f ? 1e-4f : v; }
    void setHybridBlurNormalSigma(float v) { hybridBlurNormalSigma = v < 1e-4f ? 1e-4f : v; }
    void setHybridBlurLumSigma(float v)    { hybridBlurLumSigma = v < 0.0f ? 0.0f : v; }
    void setHybridAabbClamp(bool v)        { hybridAabbClamp = v; }
    void setHybridAabbSlack(float v)       { hybridAabbSlack = v < 1.0f ? 1.0f : v; }
    void setHybridAabbMinSpp(int v)        { hybridAabbMinSpp = v < 0 ? 0 : v; }
    // v1.3 importance sampling setters
    void setHybridNEEFraction(float v)      { hybridNEEFraction      = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); resetHybridAccumulator(); }
    void setHybridGlobalRoughness(float v)  { hybridGlobalRoughness  = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); resetHybridAccumulator(); }
    void setHybridUseRoughnessTex(bool v)   { hybridUseRoughnessTex  = v; resetHybridAccumulator(); }
    void setHybridNEEConeMin(float v)       { hybridNEEConeMin       = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); resetHybridAccumulator(); }
    void setHybridNEEConeMax(float v)       { hybridNEEConeMax       = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); resetHybridAccumulator(); }

    // Phase 7: PT reference setters + invalidation
    void resetPTAccumulator() { ptDirty = true; ptSampleCount = 0; }
    void setPtRaysPerFrame(int v) { ptRaysPerFrame = v < 1 ? 1 : v; resetPTAccumulator(); }
    void setPtMaxBounces(int v)   { ptMaxBounces   = v < 1 ? 1 : v; resetPTAccumulator(); }
    void setPtRussianRoulette(float v) { ptRussianRoulette = v; resetPTAccumulator(); }
    void setPtCascadeMatch(int v) { ptCascadeMatch = v; resetPTAccumulator(); }

    // Diagnostic CLI setters for temporal/jitter bisection.
    void setUseProbeJitter(bool v)   { useProbeJitter = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseTemporalAccum(bool v) { useTemporalAccum = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseHistoryClamp(bool v)  { useHistoryClamp = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseScaledDirRes(bool v)  { useScaledDirRes = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseDirectionalMergeCLI(bool v) { useDirectionalMerge = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseDirBilinearCLI(bool v)      { useDirBilinear = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseSpatialTrilinearCLI(bool v) { useSpatialTrilinear = v; cascadeReady = false; renderFrameIndex = 0; historyNeedsSeed = true; }
    void setUseDirectionalGI(bool v)       { useDirectionalGI = v; std::cout << "[Demo3D] useDirectionalGI=" << (v ? "ON" : "OFF") << " (display path)\n"; }

    void setAutoCaptureDelaySeconds(float s) { autoCaptureDelaySeconds = s < 0.0f ? 0.0f : s; }
    void setNoiseSeedOffset(int v) {
        if (v < 0) v = 0;
        noiseSeedOffset = v;
        cascadeReady = false;
        renderFrameIndex = 0;
        historyNeedsSeed = true;
        resetPTAccumulator();
        resetHybridAccumulator();
    }

    static constexpr int kMeasurementCameraSlots = 3;
    void setMeasurementCameraSlot(int idx, const glm::vec3& pos, const glm::vec3& target) {
        if (idx < 0 || idx >= kMeasurementCameraSlots) return;
        measurementCameraPositions[idx] = pos;
        measurementCameraTargets[idx]   = target;
        measurementCameraValid[idx]     = true;
        std::cout << "[Demo3D] measurementCameraSlot[" << idx << "] pos=("
                  << pos.x << "," << pos.y << "," << pos.z << ") target=("
                  << target.x << "," << target.y << "," << target.z << ")\n";
    }
    void setMeasurementCamera(int v) {
        if (v < -1) v = -1;
        if (v >= kMeasurementCameraSlots) v = kMeasurementCameraSlots - 1;
        measurementCamera = v;
        if (v >= 0 && measurementCameraValid[v]) {
            camera.position = measurementCameraPositions[v];
            camera.target   = measurementCameraTargets[v];
            syncCameraYawPitchFromTarget();
            cascadeReady = false;
            renderFrameIndex = 0;
            historyNeedsSeed = true;
            resetPTAccumulator();
            resetHybridAccumulator();
        }
        std::cout << "[Demo3D] measurementCamera=" << v << "\n";
    }

    void setExrCapture(bool v) {
        if (v == exrCapture) return;
        exrCapture = v;
        resetPTAccumulator();
        std::cout << "[Demo3D] exrCapture=" << (v ? "ON" : "OFF")
                  << " (mode 17 screenshot dumps cascade_gi/pt_full/pt_direct EXRs)\n";
    }
    bool getExrCapture() const { return exrCapture; }
    bool dumpScreenshotEXRs(const std::string& stem);
    bool dumpProbeStatsJson(const std::string& path) const;
    bool dumpAtlasAttributionJson(const std::string& path, const std::vector<glm::ivec3>& cells) const;
    bool dumpSurfaceC0ProducerJson(const std::string& path) const;

    // Diagnostic CLI for cascade-staggering hypothesis testing.
    void setStaggerMaxInterval(int v) {
        if (v < 1) v = 1;
        if (v == staggerMaxInterval) return;
        staggerMaxInterval = v;
        cascadeReady = false;
        renderFrameIndex = 0;
        std::cout << "[Demo3D] staggerMaxInterval=" << v
                  << " (1=no stagger; 2/4/8=Ci updates every min(2^i, max) frames)\n";
    }

    // Step 12 scaling experiment (codex 12 F2 + F8): public CLI setters for
    // the 3 scaling knobs (cascade probe-res, raymarch step count, GI blur
    // radius). cascadeC0Res requires destroy/init cycle (definition in cpp);
    // the other two are uniform-only and inline here.
    void setCascadeC0Res(int v);    // out-of-line; reallocates cascade textures
    void setRaymarchSteps(int v) {
        raymarchSteps = v;
        std::cout << "[Demo3D] raymarchSteps=" << v << "\n";
    }
    void setGIBlurRadius(int v) {
        // Match ImGui slider range [1, 8].
        if (v < 1) v = 1;
        if (v > 8) v = 8;
        giBlurRadius = v;
        std::cout << "[Demo3D] giBlurRadius=" << giBlurRadius << "\n";
    }

    // codex 07 F1 — let main3d.cpp inject bake failures via CLI for runtime test of the bool-return retry path
    void setInjectBakeFailures(int n) { injectBakeFailures = n; }

    // codex 11 F1/F2 — lets main3d.cpp programmatically trigger the scene-aware
    // reset path (proves the helper that R-key and ImGui button now share).
    void testResetCameraHelper() {
        std::cout << "[Demo3D] testResetCameraHelper before: pos=("
                  << camera.position.x << "," << camera.position.y << "," << camera.position.z
                  << ") fovy=" << camera.fovy << " light=("
                  << lightPosition.x << "," << lightPosition.y << "," << lightPosition.z << ")\n";
        // Move camera away from preset to prove reset actually does something.
        camera.position += glm::vec3(2.5f, 0.7f, 1.3f);
        camera.target   += glm::vec3(2.5f, 0.7f, 1.3f);
        std::cout << "[Demo3D] testResetCameraHelper after move: pos=("
                  << camera.position.x << "," << camera.position.y << "," << camera.position.z << ")\n";
        resetCameraToScenePreset();
        std::cout << "[Demo3D] testResetCameraHelper after reset: pos=("
                  << camera.position.x << "," << camera.position.y << "," << camera.position.z
                  << ") fovy=" << camera.fovy << " light=("
                  << lightPosition.x << "," << lightPosition.y << "," << lightPosition.z << ")\n";
    }

    // Phase 12a — CLI auto-close query / setter (used by main3d.cpp)
    bool isReadyToClose() const { return captureAndAnalysisDone; }
    void setAutoCloseMode(bool v)    { autoCloseAfterCapture = v; }
    void setAutoSequenceMode(bool v) { autoCloseAfterCapture = v; autoSequencePending = v; }

    // Phase 6b — RenderDoc GPU frame capture (called from main3d.cpp main loop)
    void beginRdocFrameIfPending();
    void endRdocFrameIfPending();
    void setAutoRdocMode(float delaySeconds) { autoRdocDelaySeconds = delaySeconds; }
    bool isSkippingUI() const { return skipUIRendering; }

    // v5 / ShaderToy2 Phase 0-1: experimental Cornell-only surface-attached
    // debug atlas path. Default OFF; volumetric cascade remains default.
    void setUseSurfaceRC(bool v) {
        if (!surfaceRC) return;
        surfaceRC->setEnabled(v);
    }
    bool getUseSurfaceRC() const { return surfaceRC && surfaceRC->isEnabled(); }
    void setSurfaceDebugTarget(int v) { if (surfaceRC) surfaceRC->setDebugTarget(v); }
    void setSurfaceDebugMode(int v)   { if (surfaceRC) surfaceRC->setDebugMode(v); }
    void setSurfaceRingDebugMode(int v) { if (surfaceRC) surfaceRC->setRingDebugMode(v); }
    void setSurfaceRadianceDebugMode(int v) { if (surfaceRC) surfaceRC->setRadianceDebugMode(v); }
    void setSurfaceRayBias(float v) { if (surfaceRC) surfaceRC->setRayBias(v); }
    
    // Phase 2D: Feedback system controls
    float getSurfaceFeedbackAlpha() const { return surfaceFeedbackAlpha; }
    void setSurfaceFeedbackAlpha(float v) { surfaceFeedbackAlpha = v; }
    bool getSurfaceResetFeedback() const { return surfaceResetFeedback; }
    void setSurfaceResetFeedback(bool v) { surfaceResetFeedback = v; }
    void resetSurfaceAtlases() { if (surfaceRC) surfaceRC->clearAtlases(); }
    
    // Phase 2E: Cascade hierarchy controls
    bool getCascadeHierarchyEnabled() const { return cascadeHierarchyEnabled; }
    void setCascadeHierarchyEnabled(bool v) { 
        cascadeHierarchyEnabled = v;
        if (v && surfaceRC) surfaceRC->clearCascadeAtlases();  // Reset on enable
    }
    float getCascadeInjectionWeight() const { return cascadeInjectionWeight; }
    void setCascadeInjectionWeight(float v) { cascadeInjectionWeight = v; }
    
    // Phase 2F: Raymarch integration controls
    void setUseCascadeGI(bool v) { useCascadeGI = v; }
    void setUseGIBlur(bool v) { useGIBlur = v; }
    bool getEnableSurfaceRCInRaymarch() const { return enableSurfaceRCInRaymarch; }
    void setEnableSurfaceRCInRaymarch(bool v) {
        enableSurfaceRCInRaymarch = v;
        if (v && surfaceRC) {
            surfaceRC->setEnabled(true);
            surfaceRC->setShowDebug(false);
        }
    }
    float getSurfaceGIScale() const { return surfaceGIScale; }
    void setSurfaceGIScale(float v) { surfaceGIScale = glm::clamp(v, 0.0f, 10.0f); }
    bool getBlendWithVolumetric() const { return blendWithVolumetric; }
    void setBlendWithVolumetric(bool v) { blendWithVolumetric = v; }
    float getBlendFactor() const { return blendFactor; }
    void setBlendFactor(float v) { blendFactor = glm::clamp(v, 0.0f, 1.0f); }
    
    // Phase 3A: Validation functions
    bool validateUVRoundTrip(const std::string& metricsPath = "tools/phase3_validation/uv_roundtrip_metrics.json");
    void measureMisclassificationRate(int numSamples = 1000);
    void captureUnknownDistribution();

private:
    // =============================================================================
    // Phase 6a — Screenshot + AI Analysis
    // =============================================================================

    bool        pendingScreenshot = false;
    std::string screenshotDir;   // resolved at construction time relative to exe
    std::string analysisDir;     // same directory as screenshotDir (both in tools/)
    std::string toolsScript;     // absolute path to analyze_screenshot.py

    void initToolsPaths();       // resolve absolute paths from exe location
    void launchAnalysis(const std::string& imagePath,
                        const std::string& statsPath = "");

    // =============================================================================
    // Phase 6b — RenderDoc In-Process GPU Capture
    // =============================================================================

#ifdef _WIN32
    RENDERDOC_API_1_6_0* rdoc = nullptr;
#endif
    bool        pendingRdocCapture      = false;  // set by G key / timer → triggers TriggerCapture
    bool        rdocCaptureWaiting     = false;  // set after TriggerCapture → poll for file
    bool        forceCascadeRebuild    = false;  // set by beginRdocFrameIfPending → forces cascade dispatch in captured frame
    uint32_t    rdocForceRebuildCount  = 0;     // sustain forceCascadeRebuild+renderFrameIndex=0 for N frames (covers TriggerCapture's 1-frame delay)
    uint32_t    rdocCaptureCountBefore  = 0;     // GetNumCaptures() snapshot before trigger
    std::string rdocCaptureDir;                  // "tools/captures" — where .rdc files are saved
    std::string rdocAnalysisDir;                 // "tools/analysis" — where extract PNGs / pipeline.md go
    float       autoRdocDelaySeconds  = 0.0f;  // 0=disabled; fires once after this many seconds
    bool        autoRdocFired         = false;  // latch: only fire once per session

    void initRenderDoc();
    void launchRdocAnalysis(const std::string& capturePath);

    // Phase 12a — Auto-capture + probe stats JSON
    float       autoCaptureDelaySeconds = 5.0f;  // 0.0 = disabled
    bool        pendingStatsDump        = false;  // write JSON alongside next screenshot
    std::string statsPathForAnalysis;             // path passed to launchBurstAnalysis()
    std::string lastAnalysisPath;                 // shown in settings panel

    // --auto-analyze CLI mode: block on analysis then signal the main loop to exit
    bool        autoCloseAfterCapture  = false;
    bool        captureAndAnalysisDone = false;
    bool        autoSequencePending    = false;  // --auto-sequence: start seq instead of burst

    // Phase 12b — burst state machine
    enum class BurstState { Idle, CapM0, CapM3, CapM6, Analyze };
    BurstState  burstState          = BurstState::Idle;
    int         savedRenderMode     = 0;
    std::string burstPaths[3];          // [0]=_m0  [1]=_m3  [2]=_m6
    std::string lastScreenshotPath;     // set by takeScreenshot() on each successful write
    std::string pendingScreenshotTag;   // suffix inserted before ".png" (_m0/_m3/_m6/"")

    int noiseSeedOffset = 0;
    int measurementCamera = -1;
    glm::vec3 measurementCameraPositions[kMeasurementCameraSlots] = {};
    glm::vec3 measurementCameraTargets[kMeasurementCameraSlots]   = {};
    bool      measurementCameraValid[kMeasurementCameraSlots]     = {};
    bool exrCapture = false;

    void launchBurstAnalysis();

    // Phase 14a — multi-frame sequence capture (temporal jitter analysis)
    enum class SeqCapState { Idle, Capturing };
    SeqCapState              seqCapState   = SeqCapState::Idle;
    int                      seqFrameCount = 8;   // frames to capture (one jitter cycle)
    int                      seqFrameIndex = 0;   // next frame index to request
    std::vector<std::string> seqPaths;            // collected frame paths

    void launchSequenceAnalysis();

    // =============================================================================
    // Phase 8 — Hybrid v1.2 validation A/B sweep (doc/7/hybrid_v12_validation_phase8_plan.md)
    // State machine that flips through 5 configurations (cascade-only, hybrid mix, hybrid max,
    // hybrid variance, PT reference) and dumps PNGs + metadata to disk for offline analysis.
    // Driven by --hybrid-ab-sweep=<dir> CLI flag.
    // =============================================================================
    enum class HybridSweepState {
        Idle,
        Warmup,                 // wait for warmup frames
        ConfigCascadeOnly,
        StabilizeCascadeOnly,
        CaptureCascadeOnly,
        ConfigHybridMix,
        StabilizeHybridMix,
        CaptureHybridMix,
        ConfigHybridMax,
        StabilizeHybridMax,
        CaptureHybridMax,
        ConfigHybridVariance,
        StabilizeHybridVariance,
        CaptureHybridVariance,
        ConfigPtRef,
        StabilizePtRef,
        CapturePtRef,
        WriteMetadata,
        Done
    };
    HybridSweepState hybridSweepState        = HybridSweepState::Idle;
    std::string      hybridSweepOutDir;        // base dir for outputs
    int              hybridSweepFrameCounter  = 0;
    int              hybridSweepStabilizeFrames = 90;  // post-config wait for accumulator convergence
    int              hybridSweepWarmupFrames  = 60;    // pre-sweep settle
    std::string      hybridSweepPaths[5];      // [0]=cascade [1]=mix [2]=max [3]=variance [4]=pt
    bool             hybridSweepQuitOnDone    = true;  // exit process after Done
    // Saved state for restore after sweep (idempotent if Idle).
    bool             hybridSweepSavedUseHybrid       = false;
    bool             hybridSweepSavedUseVarMerge     = false;
    bool             hybridSweepSavedUseMax          = false;
    float            hybridSweepSavedBlendWeight     = 1.0f;
    int              hybridSweepSavedRenderMode      = 0;

    void startHybridSweep(const std::string& outDir);
    void tickHybridSweep();

    // =============================================================================
    // Scene State
    // =============================================================================

    /** Current scene type/index */
    int currentScene;
    
    /** Whether scene has been modified */
    bool sceneDirty;

    /** Monotonic successful scene commit revision for Phase 0 evidence. */
    uint64_t sceneRevision = 0;

    /** Monotonic completed shader load generation. */
    uint64_t shaderRevision = 0;
    
    /** Time accumulator for animations */
    float time;
    
    // =============================================================================
    // Camera
    // =============================================================================
    
    /** 3D camera configuration */
    Camera3DConfig camera;
    
    /** Last mouse position for rotation */
    glm::vec2 lastMousePos;
    
    /** Whether mouse is being dragged */
    bool mouseDragging;
    
    // =============================================================================
    // Volume Textures (OpenGL)
    // =============================================================================
    
    /** Voxel grid storage (RGBA8) */
    GLuint voxelGridTexture;
    
    /** Signed distance field (R32F) */
    GLuint sdfTexture;

    /** Albedo/material color volume (RGBA8) — written alongside SDF by sdf_analytic.comp */
    GLuint albedoTexture;

    // Step 8 (codex 01 F9): GPU JFA SDF + dynamic-sphere overlay textures.
    /** RGBA32F Voronoi ping-pong A: .xyz = closest-seed voxel coord, .w = valid flag. */
    GLuint voronoiTextureA = 0;
    /** RGBA32F Voronoi ping-pong B (alternate target each JFA step). */
    GLuint voronoiTextureB = 0;
    /** RGBA8 cache of static OBJ voxels: written once at loadOBJMesh,
     *  copied into voxelGridTexture each frame the dynamic sphere is on so
     *  sphere injection doesn't destroy the static base layer. */
    GLuint meshVoxelBaseTexture = 0;

    // Step 9 Phase 3 (codex 03 F5): R32UI owner-index texture for the GPU
    // voxelizer's atomicMin pass. Stores winning triangle index per voxel
    // (0xFFFFFFFFu = no owner). Resolve pass reads this and writes RGBA8
    // color into voxelGridTexture. Separate texture so we don't alias RGBA8
    // as R32UI (which is fragile re: GL image-format-class compatibility).
    GLuint voxelOwnerTexture = 0;

    // Step 9 Phase 3: SSBO holding all triangles for the active OBJ
    // (positions + per-face Kd lookup). Built once per OBJ load via
    // OBJLoader::buildTriangles(). 64 bytes per triangle.
    GLuint triangleSSBO = 0;

    // =============================================================================
    // Analytic SDF (Phase 0 - Quick Validation)
    // =============================================================================
    
    /** Analytic SDF primitive system for quick testing */
    AnalyticSDF analyticSDF;
    
    /** Whether to use analytic SDF instead of voxel-based JFA */
    bool analyticSDFEnabled;
    
    /** SSBO for uploading primitives to GPU */
    GLuint primitiveSSBO;
    
    /** OBJ mesh loader for importing real geometry */
    OBJLoader objLoader;
    
    /** Whether to use loaded OBJ mesh instead of analytic primitives */
    bool useOBJMesh;

    /** Which OBJ was last loaded (4-way label): "cornell", "cornell_orig",
     *  "sponza", or "sponza_master". Step 7: pure UI/identity label only —
     *  camera/light preset is now derived from currentObjBmin/Bmax, not from
     *  this string. */
    std::string currentOBJPath;

    // Step 7 (auto-fit preset): post-normalize bounds of the active mesh.
    // Stored at loadOBJMesh time so applyOBJViewPreset() and the R-key reset
    // path can compute camera/light without re-querying the loader. Both
    // default to (0,0,0) until first OBJ load.
    glm::vec3 currentObjBmin = glm::vec3(0.0f);
    glm::vec3 currentObjBmax = glm::vec3(0.0f);

    // Step 2: Mesh SDF (CPU EDT bake from OBJ voxelization)
    /** OBJ surface voxels (RGBA8, volumeResolution^3); set by loadOBJMesh, consumed by generateMeshSDF. */
    std::vector<uint8_t> meshVoxelData;
    /** True after generateMeshSDF() successfully baked sdfTexture/albedoTexture for the current mesh. */
    bool meshSDFReady = false;

    // Step 8 (codex 01 F1): promoted from render() static locals so non-render
    // sites (UI toggles, dynamic-sphere update path) can invalidate the same
    // single source of truth. Render condition is now
    //   if (!sdfReady || (useOBJMesh && !meshSDFReady)) -> bake
    bool sdfReady     = false;
    bool cascadeReady = false;

    // Step 9 Phase 2 (codex 03 F1+F2): source-aware OBJ cache.
    // Key includes voxelizerKind (0=CPU, 1=GPU) so CPU and GPU bakes coexist
    // as separate entries -- toggling between paths after first load gives
    // two cache hits, not a re-voxelize. Cache always stores RGBA8 voxel
    // bytes regardless of which path produced them; GPU path pays one-shot
    // glGetTexImage on cache populate (~5-10ms per unique OBJ).
    // codex 04 F3: key is the CALLER-PROVIDED string (`requestedPath`),
    // NOT a canonicalized filesystem path. Two different aliases for the
    // same file (e.g. `cornell_box.obj` vs `res/scene/cornell_box.obj`)
    // produce two cache entries. Current callers (4 ImGui buttons + the
    // CLI mapping) all use stable strings so this isn't observed in
    // practice; renaming clears up the doc/code mismatch.
    struct MeshCacheKey {
        std::string requestedPath;
        int         voxelizerKind;   // 0 = CPU, 1 = GPU
        bool operator==(const MeshCacheKey& o) const {
            return requestedPath == o.requestedPath && voxelizerKind == o.voxelizerKind;
        }
    };
    struct MeshCacheKeyHash {
        size_t operator()(const MeshCacheKey& k) const {
            return std::hash<std::string>{}(k.requestedPath) ^ size_t(k.voxelizerKind);
        }
    };
    struct CachedMesh {
        std::vector<uint8_t> voxelBytes;
        glm::vec3 bmin, bmax;
    };
    std::unordered_map<MeshCacheKey, CachedMesh, MeshCacheKeyHash> meshCache;

    // Step 9 Phase 3 (codex 03 F1): set true after GPU voxelize completes.
    // sdfGenerationPass branches into the OBJ SDF path on either
    // !meshVoxelData.empty() (CPU path) OR gpuVoxelGridReady (GPU path).
    bool gpuVoxelGridReady = false;

    // Step 9 Phase 3 (codex 03 F10): runtime toggle picks GPU vs CPU
    // triangle voxelizer at OBJ load. Default OFF (CPU baseline). Flipping
    // re-invokes loadOBJMesh on the active OBJ so the user sees the effect
    // immediately (cache key includes voxelizerKind so both bakes coexist).
    bool useGPUVoxelize = false;
    /** Step 2 v2: bake conservative UDF from meshVoxelData into sdfTexture + propagated albedoTexture.
     *  Returns false on validation/upload failure; caller (Step 3) sets meshSDFReady on success. */
    bool generateMeshSDF();

    /** Step 8 (codex 01 F6/F7/F8): GPU JFA equivalent of generateMeshSDF().
     *  3-pass dispatch (init / log2(N) JFA steps / finalize) into sdfTexture +
     *  albedoTexture with conservative-band UDF matching the CPU path. Returns
     *  false if shader missing or dispatch fails. */
    bool generateMeshSDFGPU();

    /** Step 9 Phase 3 (codex 03 F4-F7): GPU triangle voxelizer. Builds
     *  triangle SSBO via OBJLoader::buildTriangles, dispatches voxelize.comp
     *  in 3 passes (init / atomicMin / resolve), copies result into
     *  meshVoxelBaseTexture, populates the mesh cache via glGetTexImage,
     *  sets gpuVoxelGridReady = true. Returns false on shader/handle/GL error. */
    bool voxelizeOBJ_GPU();

    /** Step 8 (codex 01 F1): runtime toggle picks GPU vs CPU mesh-SDF path.
     *  Default OFF (CPU baseline). Flipping invalidates meshSDFReady so the
     *  next render frame re-bakes via the new path. */
    bool useGPUSDF = false;

    /** Lighting controls — continuous ambient floor strengths (replace the
     *  Step 11 binary `stripAmbientFloorBake` toggle and the original
     *  hardcoded vec3(0.05) literals). Two independent knobs:
     *    - ambientBakeStrength: floor baked into cascade probe radiance
     *      (radiance_3d.comp). Affects GI bounce magnitude. 0 = strip behavior.
     *    - ambientCompositeStrength: floor added at the camera-visible surface
     *      shade (raymarch.frag direct + mode 4 + mode 10). Affects what the
     *      camera sees on unlit surfaces.
     *  Both default 0.05 to match prior baseline. */
    float ambientBakeStrength      = 0.05f;
    float ambientCompositeStrength = 0.05f;

    // Phase 2.5a.1 bake-leak test state. Triggered via setBakeLeakTest().
    // bakeLeakElapsedFrames is a dedicated counter (renderFrameIndex resets via
    // rdocForceRebuildCount + useTemporalAccum transitions, so it's unreliable).
    bool        bakeLeakTestPending     = false;
    bool        bakeLeakTestDone        = false;
    int         bakeLeakElapsedFrames   = 0;
    int         bakeLeakTestFramesAfter = 240;
    std::string bakeLeakTestOutPath;

    // Phase 2.5d M1 diagnostic: when set, the bake writes sdfBefore_normalized
    // to alpha for surface bins (instead of the binary 0). Combined with
    // setBakeLeakTest, the readback computes a histogram of sdfBefore values
    // to confirm/refute 2.5b's "SDF returns small for ALL hits" diagnosis.
    int         diagAlphaMode           = 0;

    /** Lighting controls — directional light support (Sponza-style sun light).
     *  When useDirectionalLight=true, cascade dispatch derives a far-away point
     *  light from `lightDirection` so existing point-light shaders naturally
     *  degenerate to directional behavior (no shader changes). Sponza variants
     *  enable this on load. lightDirection is normalized; default points
     *  downward + slight angle (typical sun). lightIntensity is a multiplier
     *  on the hardcoded vec3(1.0, 0.95, 0.85) base color. */
    bool      useDirectionalLight = false;
    glm::vec3 lightDirection      = glm::vec3(-0.3f, -1.0f, -0.4f);
    float     lightIntensity      = 1.0f;

    // visibilityMode member retired in Phase 2 2C cleanup. The atlas's α
    // channel now handles per-bin visibility natively via sampleProbeDir;
    // there are no longer multiple visibility modes to choose between. The
    // setVisibilityMode() setter above is preserved as a deprecation stub
    // for one release, then removed.

    // Step 8 Phase 2: dynamic sphere overlay state.
    bool       dynamicSphereEnabled    = false;    // ImGui + --dynamic-sphere
    bool       dynamicSphereWasEnabled = false;    // codex 02 F2: detect disable transition
    glm::vec3  dynamicSphereCenter     = glm::vec3(0.0f);
    float      sphereTime              = 0.0f;     // accumulated orbit phase
    float      sphereOrbitSpeed        = 1.0f;     // ImGui slider 0.1..5.0
    float      sphereTimeOverride      = -1.0f;    // codex 01 F10: -1 = real time, else snap

    /** Step 8 Phase 2b (codex 01 F3+F4): rasterize a solid sphere into voxelGridTexture
     *  with correct world->voxel math AND a single batched glTexSubImage3D upload. */
    void addVoxelSphere(const glm::vec3& center, float radius, const glm::vec3& color);
public:
    /** Step 8: CLI hook (`--gpu-sdf`) to enable GPU JFA SDF path at startup. */
    void setUseGPUSDF(bool v) { useGPUSDF = v; meshSDFReady = false; cascadeReady = false; }
    /** Step 8 (codex 01 F10): CLI hooks for dynamic-sphere demo. */
    void setDynamicSphere(bool v) { dynamicSphereEnabled = v; }
    void setSphereTimeOverride(float t) { sphereTimeOverride = t; }
    /** Step 9 (codex 03 F10): CLI hook (`--gpu-voxelize`) -- must be set
     *  BEFORE the first --load-obj so the initial bake uses the chosen path. */
    void setUseGPUVoxelize(bool v) { useGPUVoxelize = v; }
private:

    /** codex 07 F1 test hook: when > 0, generateMeshSDF returns false this many times
     *  before behaving normally. Used by --inject-bake-failures=N to verify the render
     *  loop's failure-retry path. Default 0 (no injection). */
    int  injectBakeFailures = 0;
    
    // =============================================================================
    // SDF Debug Visualization (Phase 0)
    // =============================================================================
    
    /** Quad VAO for debug visualization */
    GLuint debugQuadVAO;
    
    /** Quad VBO for debug visualization */
    GLuint debugQuadVBO;
    
    /** Which axis to slice (0=X, 1=Y, 2=Z) */
    int sdfSliceAxis;
    
    /** Normalized position along slice axis (0.0-1.0) */
    float sdfSlicePosition;
    
    /** Visualization mode (0=grayscale, 1=surface, 2=gradient) */
    int sdfVisualizeMode;
    
    /** Whether to show SDF debug view */
    bool showSDFDebug;
    
    // ========================================================================
    // Phase 1: Radiance Cascade Debug Controls
    // ========================================================================
    
    /** Whether to show radiance cascade debug view */
    bool showRadianceDebug;
    
    /** Radiance slice axis (0=X, 1=Y, 2=Z) */
    int radianceSliceAxis;
    
    /** Radiance slice position (0.0-1.0) */
    float radianceSlicePosition;
    
    /** Radiance visualization mode (0=Slice 1=MaxProj 2=Avg 3=Atlas 4=HitType 5=Bin) */
    int radianceVisualizeMode;

    /** Phase 5b: direction bin (dx, dy) selected for mode 5 Bin viewer */
    int atlasBinDx;
    int atlasBinDy;
    
    /** Radiance exposure for tone mapping */
    float radianceExposure;
    
    /** Radiance intensity scale */
    float radianceIntensityScale;
    
    /** Show voxel grid overlay on radiance debug */
    bool showRadianceGrid;

    /** v5 / ShaderToy2 Phase 0-1 surface-attached Cornell debug atlas. */
    std::unique_ptr<SurfaceRC> surfaceRC;
    
    // Phase 2D: Feedback system parameters
    float surfaceFeedbackAlpha = 0.1f;  // EMA blend factor for temporal accumulation
    bool surfaceResetFeedback = false;  // Flag to trigger atlas reset
    
    // Phase 2E: Cascade hierarchy parameters
    bool cascadeHierarchyEnabled = false;  // Enable/disable cascade hierarchy
    float cascadeInjectionWeight = 0.5f;   // Weight for injecting coarse into fine
    
    // Phase 2F: Raymarch integration parameters
    bool enableSurfaceRCInRaymarch = false;  // Enable surface RC GI in raymarch shader
    float surfaceGIScale = 1.0f;             // GI contribution scale
    bool blendWithVolumetric = false;        // Blend with volumetric RC
    float blendFactor = 0.5f;                // Blend factor (0.0=surface, 1.0=volumetric)

private:
    // Probe readback stats (populated once per cascade update, shown in Cascades panel)
    // Per-cascade probe readback stats (indexed 0..cascadeCount-1)
    int   probeNonZero[MAX_CASCADES];    // any contribution > 1e-4 (includes sky propagation)
    int   probeSurfaceHit[MAX_CASCADES]; // probes with at least one direct surface hit
    int   probeSkyHit[MAX_CASCADES];     // probes with at least one direct sky exit
    int   probeTotal;               // same for all levels (res^3)
    float probeMaxLum[MAX_CASCADES];
    float probeMeanLum[MAX_CASCADES];
    float probeVariance[MAX_CASCADES];   // luma variance across all probes (noise indicator)
    float probeHistogram[MAX_CASCADES][16]; // normalized 16-bin luma histogram (max bin = 1.0)
    glm::vec3 probeCenterSample;    // C0 center probe sample
    glm::vec3 probeBackwallSample;  // C0 backwall probe sample

    // ========================================================================
    // Phase 1: Lighting Debug Controls
    // ========================================================================
    
    /** Whether to show lighting debug view */
    bool showLightingDebug;
    
    /** Lighting debug slice axis */
    int lightingSliceAxis;
    
    /** Lighting debug slice position */
    float lightingSlicePosition;
    
    /** Lighting debug mode (0=Light0, 1=Light1, 2=Light2, 3=Combined, 4=Normals, 5=Albedo) */
    int lightingDebugMode;
    
    /** Lighting exposure */
    float lightingExposure;
    
    /** Lighting intensity scale */
    float lightingIntensityScale;
    
    /** Direct lighting buffer (RGBA16F) */
    GLuint directLightingTexture;
    
    /** Previous frame radiance for temporal reprojection (RGBA16F) */
    GLuint prevFrameTexture;
    
    /** Current frame radiance output (RGBA16F) */
    GLuint currentRadianceTexture;
    
    /** Volume resolution (isotropic) */
    int volumeResolution;
    
    /** Volume origin in world space */
    glm::vec3 volumeOrigin;
    
    /** Volume size in world space */
    glm::vec3 volumeSize;
    
    // =============================================================================
    // Radiance Cascades
    // =============================================================================
    
    /** Array of cascade levels */
    RadianceCascade3D cascades[MAX_CASCADES];
    
    /** Number of active cascades */
    int cascadeCount;
    
    /** Base interval size in voxels */
    float baseInterval;

    /** C0 probe grid resolution (powers of 2: 8/16/32/64). All other cascades derived from this.
     *  co-located: all cascades use cascadeC0Res^3.
     *  non-co-located: Ci uses (cascadeC0Res >> i)^3, halving per level.
     *  Also sets baseInterval = volumeSize / cascadeC0Res (C0 cell size = tMax_C0). */
    int cascadeC0Res;
    
    /** Whether to use bilinear filtering for cascades */
    bool cascadeBilinear;
    
    /** Disable cascade merging (debug) */
    bool disableCascadeMerging;

    /** Whether to blend cascade indirect lighting in the final image */
    bool useCascadeGI;

    /** Which cascade level to use for indirect lighting in the raymarch pass */
    int selectedCascadeForRender;

    /** 4a: Out-of-volume rays return skyColor instead of vec3(0). Default OFF. */
    bool useEnvFill;

    /** 4a: Sky color used when useEnvFill is true (very dim by default). */
    glm::vec3 skyColor;

    /** 4b: Base ray count for C0; Ci fires baseRaysPerProbe * 2^i rays. Default 8. */
    int baseRaysPerProbe;

    /** 4c: Blend zone as fraction of interval width. 0=binary (Phase 3), default 0.5. */
    float blendFraction;

    /** 5a: Octahedral direction bin resolution. D^2 rays per probe. Default 4 (16 bins). */
    int dirRes;

    /** 5c: Use per-direction texelFetch merge (true) or isotropic texture() fallback (false). */
    bool useDirectionalMerge;

    /** 5d: Co-located cascades (all 32^3, default) vs ShaderToy-style halving (32/16/8/4). */
    bool useColocatedCascades;

    /** 5d: Per-cascade probe count for fill-rate display (set during probe readback). */
    int  probeTotalPerCascade[MAX_CASCADES];

    /** 5e: Per-cascade D scaling A/B toggle. false=all D4 (default); true=C0=D2,C1=D4,C2=D8,C3=D16. */
    bool useScaledDirRes;
    /** 5e: Per-cascade directional resolution (D). Computed in initCascades(). */
    int  cascadeDirRes[MAX_CASCADES];

    /** 5f: Bilinear interpolation across 4 surrounding direction bins when reading upper cascade.
     *  true (default): smooth blend eliminates hard bin-boundary banding/bleeding.
     *  false: nearest-bin texelFetch (Phase 5c behaviour, useful for A/B comparison). */
    bool useDirBilinear;
    /** Phase 5d trilinear: 8-neighbor spatial interpolation when reading upper cascade
     *  in non-co-located mode. true=trilinear (default), false=nearest-parent (Phase 5d baseline).
     *  No effect in co-located mode (upper probe is at same position; trilinear is trivially exact). */
    bool useSpatialTrilinear;

    /** Phase 3 (bake-side leak fix via 3D WeightedSample): per-corner geometric visibility
     *  check at bake time. true = sampleUpperDirWeighted (gates upper merge by visibility);
     *  false (DEFAULT) = sampleUpperDirTrilinear (Phase 2 unconditional-trust merge,
     *  bit-exact preserved). Only active on the trilinear path (uUpperToCurrentScale==2 +
     *  uUseSpatialTrilinear). See doc/6/claude_plan/visibility_phase3_plan.md and
     *  res/shaders/radiance_3d.comp for the algorithm. */
    bool useWeightedSample;
    /** 2026-05-18 debug: instrument Phase 3 v2 to find WHY GI is still dimmed.
     *  0=normal, 1=force aFactor=1, 2=visualize aFactor, 3=visualize upperDir.a,
     *  4=force upperDir.rgb=trilinear.rgb (test if WeightedSample's renormalize differs). */
    int   phase3DebugMode;
    /** 2026-05-18 debug: multiplier on upper contribution in the bake (default 1.0). */
    float giStrength;

    /** 2026-05-18 leak-suspect heatmap (mode 14) sensitivity divisor.
     *  leak_potential >= this value saturates to red. Sqrt-scaled in shader so
     *  small reductions near saturation visibly shift color. Default 0.05. */
    float leakHeatmapDivisor;

    /** Phase MB (multi-bounce temporal feedback) — see doc/7/multi_bounce_temporal_plan.md.
     *  false (default): single-bounce only (current behavior, bit-exact preserved).
     *  true: bake-time surface hits sample previous-frame C0 atlas for indirect.
     *  Converges to multi-bounce equilibrium over ~5-10 frames. */
    bool  useMultiBounce;
    /** Multiplier on multi-bounce feedback term. Default 0.7 for stability margin.
     *  gain × albedo × hemi_factor < 1 required for stable geometric series. */
    float multiBounceGain;
    /** v4 Phase 1A: when true, MB gain is controlled by per-scene preset
     *  (Sponza→0.10, else→1.0) rather than the manual slider. Set by
     *  --mb-gain-per-scene CLI flag. */
    bool  usePerSceneMbGain = false;

    /** 2026-05-19 Mode 18 (cascade-vs-PT delta heatmap) sensitivity divisor.
     *  Signed bipolar colormap: |delta| >= divisor saturates. Default 0.2 picked for
     *  Cornell-scale scenes (typical radiance ~0.3). */
    float deltaHeatmapDivisor;

    // ========================================================================
    // Hybrid RC + Per-Pixel Correction (doc/7/hybrid_rc_pixel_correction_plan.md)
    // ========================================================================
    GLuint   hybridAccumTexture;       // RGBA32F, half-viewport (.rgb radiance, .a E[L^2])
    GLuint   hybridGBufferTexture;     // RGBA32F, half-viewport (.rgb normal*0.5+0.5, .a depth)
    GLuint   hybridFilteredTexture;    // RGBA32F, half-viewport — bilateral-blurred accum
    int      hybridAccumWidth, hybridAccumHeight;
    int      hybridSampleCount;        // total rays-per-pixel accumulated
    bool     useHybrid;                // false default (opt-in)
    float    hybridBlendWeight;        // 1.0 = pure correction (default per critic-05 H1)
    bool     hybridUseMaxComp;         // legacy: per-pixel max(correction, cascade)
    int      hybridRaysPerFrame;       // default 1 (stochastic, EMA averages)
    float    hybridEMAAlpha;           // default 0.1 (~10 frames to converge)
    bool     hybridDirty;              // reset accumulator next dispatch
    uint32_t hybridFrameSeed;          // RNG seed input
    glm::vec3 hybridLastCamPos;
    glm::vec3 hybridLastCamTarget;
    // 2026-05-19 v1.2 cooperative inverse-variance merge.
    bool     hybridUseVarianceMerge;   // ON = inverse-variance weighted merge with cascade
    float    hybridCascadeVariance;    // RELATIVE (CoV^2) prior for cascade (default 0.001)
    int      hybridConfidenceSamples;  // samples for full correction trust (default 8); J9 fix
    int      hybridBlurRadius;         // bilateral kernel radius on hybridAccum (default 3)
    float    hybridBlurDepthSigma;     // depth edge stop (default 0.05)
    float    hybridBlurNormalSigma;    // normal edge stop (default 0.3)
    float    hybridBlurLumSigma;       // v1.2.3 luminance edge stop (default 0.5; 0=off)
    bool     hybridAabbClamp;          // v1.2.4 firefly clamp (HIGH side only); default OFF
    float    hybridAabbSlack;          // AABB clamp slack multiplier (default 2.0; was 1.5)
    int      hybridAabbMinSpp;         // skip firefly clamp until spp >= this (default 4)
    // v1.3 importance sampling (NEE + roughness)
    float    hybridNEEFraction;        // P(NEE strategy) per ray; 0=cosine-only, 0.5 default
    float    hybridGlobalRoughness;    // [0,1] fallback roughness when texture disabled
    bool     hybridUseRoughnessTex;    // sample per-voxel roughnessTexture when true
    float    hybridNEEConeMin;         // cos(half-angle) at roughness=0 (narrow cone)
    float    hybridNEEConeMax;         // cos(half-angle) at roughness=1 (wide cone)
    GLuint   roughnessTexture;         // GL_R8 3D texture; voxel-aligned with sdf/albedo

    // ========================================================================
    // Phase 7: PT reference (doc/7/pt_reference_plan.md)
    // ========================================================================
    GLuint   ptAccumTexture;       // RGBA32F, half-viewport size — FULL PT (all bounces)
    // 2026-05-19 Mode 19: parallel accumulator for DIRECT-ONLY PT (max-bounces=1).
    // PT_GI = ptAccumTexture - ptDirectAccumTexture per pixel.
    GLuint   ptDirectAccumTexture;
    int      ptAccumWidth, ptAccumHeight;
    int      ptSampleCount;        // total rays-per-pixel accumulated since reset
    int      ptRaysPerFrame;       // dispatched per frame (default 1 for interactive)
    int      ptMaxBounces;         // hard cap on path length (default 8)
    float    ptRussianRoulette;    // survival probability (default 0.9)
    int      ptCascadeMatch;       // 0 = unbiased (default), 1 = match cascade ambient
    bool     ptDirty;              // true → reset accumulator next dispatch
    uint32_t ptFrameIndex;         // RNG seed input
    // Camera-change debounce (W5 of /loop discussion):
    glm::vec3 ptLastCamPos;
    glm::vec3 ptLastCamTarget;

    /** 5h: Cast shadow ray from surface hit to light in direct path.
     *  true (default): 32-step SDF march gives hard binary shadow in direct term.
     *  false: unshadowed direct (Phase 1-4 behaviour). Display-path only, no cascade rebuild. */
    bool useShadowRay;

    /** 5g: Cosine-weighted directional atlas sampling for indirect GI.
     *  false (default): reads isotropic probeGridTexture (same as pre-5g).
     *  true: samples C0 directional atlas with hemisphere-weighted integration over surface normal.
     *  Display-path only, no cascade rebuild. */
    bool useDirectionalGI;

    /** 5i: SDF cone soft shadow (IQ-style) in the final renderer's direct term.
     *  false (default): binary shadow from Phase 5h shadowRay().
     *  true: smooth penumbra via k*h/t accumulation. Display-path only, no cascade rebuild.
     *  Requires useShadowRay=true to have any effect. */
    bool useSoftShadow;

    /** 5i: SDF cone soft shadow applied inside the bake shader's inShadow() call.
     *  false (default): binary inShadow() — hard shadow baked into probe radiance.
     *  true: smooth shadow baked per probe, reducing Source 2/3 probe-grid banding.
     *  Requires cascade rebuild on toggle or k change. */
    bool useSoftShadowBake;

    /** 5i: Penumbra width for SDF cone soft shadow in both display and bake.
     *  Lower k = wider, softer penumbra. Range [1, 16]. Default 8.
     *  k change triggers cascade rebuild only when useSoftShadowBake is true. */
    float softShadowK;

    // =============================================================================
    // Phase 9: Temporal accumulation + probe jitter
    // =============================================================================

    /** Master toggle: blend each bake into history. Display reads history. */
    bool useTemporalAccum;

    /** EMA blend weight: history = mix(history, bake, alpha). 1.0=no accum. */
    float temporalAlpha;

    /** Per-frame probe jitter: shift probes by random [-0.5,0.5]^3 cell units.
     *  Only effective when combined with temporal accumulation. */
    bool useProbeJitter;

    /** Current frame's jitter vector (probe-cell units). Updated each rebuild. */
    glm::vec3 currentProbeJitter;

    /** Phase 9b: Halton sequence index — increments when jitter is ON, resets when disabled. */
    uint32_t probeJitterIndex;

    /** Jitter amplitude in probe-cell units. 0.25 → ±0.25 cell (was implicitly ±0.5). */
    float probeJitterScale;

    /** Wrap Halton index at this N. After N distinct positions the cycle repeats.
     *  Default 8 — Halton(2,3,5) at indices 0-7 gives good 3-D coverage. */
    int jitterPatternSize;

    /** Hold each jitter position for this many frames before advancing to the next.
     *  Lets the EMA integrate each position before moving on. Default 1. */
    int jitterHoldFrames;

    /** Internal: counts frames remaining in the current jitter hold period. */
    int jitterHoldCounter;

    /** Phase 9b: total temporal blend dispatches since temporal was last enabled (jitter or not). */
    uint32_t temporalRebuildCount;

    /** Phase 9b: clamp history to current-neighborhood AABB before EMA blend (TAA-style ghost rejection). */
    bool useHistoryClamp;

    /** Phase 9b: seed history textures = current bake on next warm-up rebuild (eliminates dark warmup). */
    bool historyNeedsSeed;

    /** Phase 10: monotonic frame counter for staggered cascade update gating. */
    uint32_t renderFrameIndex;

    /** Phase 10: max cascade update interval (1=no stagger, 2/4/8=stagger Ci every 2^i frames).
     *  Cascade i updates when renderFrameIndex % min(1<<i, staggerMaxInterval) == 0. */
    int staggerMaxInterval;

    // =============================================================================
    // Shaders
    // =============================================================================
    
    /** Map of shader name to program object */
    std::map<std::string, GLuint> shaders;
    
    /** Current active shader program */
    GLuint activeShader;
    
    // =============================================================================
    // User Interaction
    // =============================================================================
    
    /** Current editing mode (voxel placement vs light placement) */
    enum class Mode { VOXELIZE, LIGHT } userMode;
    
    /** Brush size in world units */
    float brushSize;
    
    /** Brush color for lights */
    Color brushColor;
    
    /** Whether to draw rainbow colors */
    bool drawRainbow;
    
    // =============================================================================
    // Lighting Settings
    // =============================================================================
    
    /** Use traditional GI algorithm instead of RC */
    bool useTraditionalGI;
    
    /** Number of rays for traditional GI */
    int giRayCount;
    
    /** Add noise to GI (dithering) */
    bool giNoise;
    
    /** Enable ambient lighting term */
    bool ambientLight;
    
    /** Ambient light color */
    glm::vec3 ambientColor;
    
    /** Indirect lighting mix factor (0-1) */
    float indirectMixFactor;
    
    /** Indirect lighting brightness multiplier */
    float indirectBrightness;

    /** Step 4 (4b ext): per-scene light position. Was hardcoded to (0, 0.8, 0)
     *  for Cornell Box. Sponza needs a light inside its [-0.795, 0.795] Y range
     *  (the hardcoded Y=0.8 was just above Sponza's ceiling -- explained the
     *  black mode-4 capture). loadOBJMesh() updates this per OBJ. */
    glm::vec3 lightPosition;

    // Step 5 (5b, codex 10 F6): maintained yaw/pitch scalars for mouse-look.
    // Avoids the cross-product singularity at world-up/down by reconstructing
    // forward from yaw/pitch directly. Synced from camera.target on scene load
    // via syncCameraYawPitchFromTarget(). cameraPitch clamped to ~+/-85 deg.
    float cameraYaw   = 0.0f;   // radians, 0 = +Z forward
    float cameraPitch = 0.0f;   // radians, clamped

    // Step 5 (5-helper, codex 10 F3): apply per-OBJ camera + light preset
    // without touching mesh data. Called by loadOBJMesh() after commit and
    // by R-key reset.
    //
    // Step 7 (auto-fit): now parameterless and bounds-driven — uses
    // currentObjBmin/currentObjBmax (set by loadOBJMesh) to compute the
    // camera + light position generically. No per-OBJ branches; any future
    // OBJ "just works" without editing this function.
    void applyOBJViewPreset();

    // Step 5 (codex 10 F6): initialize cameraYaw/cameraPitch from camera's
    // current forward vector. Called on scene load + camera reset.
    void syncCameraYawPitchFromTarget();

    // Step 5 (codex 11 F1): scene-aware camera reset. Calls applyOBJViewPreset()
    // for OBJ scenes, resetCamera() for analytic. Single helper consumed by both
    // the R key and the ImGui "Reset Camera" button so both paths agree.
    void resetCameraToScenePreset();

    // Step 10 (codex 06 F5): rebuild camera.target from cameraYaw/cameraPitch
    // around the current camera.position (preserves facing when only the
    // position changes). Mirrors the inline mouse-look math at demo3d.cpp:481-486.
    // Clamps cameraPitch to +/-85 deg (matches mouse-look:478) so a near-vertical
    // sync result doesn't yield a degenerate forward.
    void rebuildCameraTargetFromYawPitch();

    // Step 10 (codex 06 F11): standalone alpha-validation against meshVoxelData.
    // Extracted from applyOBJViewPreset() so setters and the ImGui edit handler
    // can reuse it. Logs inside/outside-volume status with originLabel
    // ("preset", "CLI", "ImGui edit", ...) to identify which trigger fired.
    void validateCameraPosition(const glm::vec3& pos, const char* originLabel);
    
    // =============================================================================
    // Performance & Quality
    // =============================================================================
    
    /** Use sparse voxel octree optimization */
    bool useSparseVoxels;
    
    /** Enable temporal reprojection */
    bool useTemporalReprojection;
    
    /** Adaptive step size for raymarching */
    bool adaptiveStepSize;
    
    /** Raymarching step count */
    int raymarchSteps;
    
    /** Early ray termination threshold */
    float rayTerminationThreshold;
    
    // =============================================================================
    // Debug Options
    // =============================================================================
    
    /** Render mode sent to raymarch.frag: 0=final, 1=normals, 2=SDF dist, 3=indirect*5 */
    int raymarchRenderMode;

    /** Phase 7 diagnostic: evaluate SDF analytically per-sample instead of texture lookup.
     *  Toggle in UI alongside mode 5 / mode 7 to isolate grid-quantization banding. */
    bool useAnalyticRaymarch;

    /** Show debug visualization windows */
    bool showDebugWindows;
    
    /** Display individual cascade slices */
    bool showCascadeSlices;
    
    /** Visualize voxel grid structure */
    bool showVoxelGrid;
    
    /** Show performance metrics */
    bool showPerformanceMetrics;
    
    /** Skip UI rendering (F1 toggle) */
    bool skipUIRendering;
    
    /** Show ImGui demo window */
    bool showImGuiDemo;
    
    // =============================================================================
    // Framebuffer Objects
    // =============================================================================
    
    /** FBO for voxelization pass */
    GLuint voxelizationFBO;

    /** FBO for SDF generation */
    GLuint sdfFBO;

    /** FBO for cascade rendering */
    GLuint cascadeFBO;

    // GI bilateral blur FBO (Phase 9d)
    // 6 color attachments: direct, gbuffer, indirect/GI, probe diag, contribution, bin
    // Only active when useGIBlur=true AND raymarchRenderMode==0.
    GLuint giFBO;
    GLuint giDirectTex;    // linear direct lighting (location=0 from raymarch.frag)
    GLuint giGBufferTex;   // normal*0.5+0.5 + linearDepth (location=1)
    GLuint giIndirectTex;  // linear indirect/GI (location=2 from raymarch.frag)
    GLuint giProbeDiagTex; // mode-17 probe coord + raw indirect luma (location=3)
    GLuint giProbeContribTex; // mode-17 top probe coord + share (location=4)
    GLuint giProbeBinTex;     // mode-17 top bin + share + reconstructed luma (location=5)
    int giLastW, giLastH;

    // GI blur settings
    bool  useGIBlur;
    int   giBlurRadius;
    float giBlurDepthSigma;
    float giBlurNormalSigma;
    float giBlurLumSigma;       // Phase 13b: luminance edge-stop (0.0 = disabled)
    float c0MinRange;           // Phase 14b: minimum C0 tMax in world units (0=legacy cellSize)
    float c1MinRange;           // Phase 14c: minimum C1 tMax in world units (0=legacy 0.5wu)
    
    // =============================================================================
    // Query Objects (Performance)
    // =============================================================================
    
    /** Timer query for voxelization pass */
    GLuint voxelizationTimeQuery;
    
    /** Timer query for SDF pass */
    GLuint sdfTimeQuery;
    
    /** Timer query for cascade update */
    GLuint cascadeTimeQuery;
    
    /** Timer query for raymarching */
    GLuint raymarchTimeQuery;
    
    // =============================================================================
    // Cached Metrics
    // =============================================================================
    
    /** Last frame voxelization time (ms) */
    double voxelizationTimeMs;
    
    /** Last frame SDF generation time (ms) */
    double sdfTimeMs;
    
    /** Last frame cascade update time (ms) */
    double cascadeTimeMs;
    
    /** Last frame raymarching time (ms) */
    double raymarchTimeMs;

    /** Phase 8 — Hybrid GPU timer (EMA-smoothed, GL_TIMESTAMP query). 0 when hybrid OFF.
     *  hybridCorrectionMs measures hybrid_correction.comp dispatch.
     *  hybridBlurMs       measures hybrid_blur.comp       dispatch. */
    double hybridCorrectionMs = 0.0;
    double hybridBlurMs       = 0.0;
    GLuint hybridTimerQueries[3] = {0, 0, 0};  // start / between / end
    bool   hybridTimerInflight   = false;       // true while last frame's query is unresolved
    
    /** Total frame time (ms) */
    double frameTimeMs;
    
    /** Active voxel count (for sparse representation) */
    int activeVoxelCount;
    
    /** Memory usage in MB */
    float memoryUsageMB;
};

#endif // DEMO3D_H
