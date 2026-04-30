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
#include <vector>
#include <memory>
#include "config.h"
#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include "gl_helpers.h"
#include "analytic_sdf.h"  // Analytic SDF primitives for Phase 0
#include "obj_loader.h"    // OBJ mesh loader for scene import
#include <glm/glm.hpp>

// =============================================================================
// Constants and Configuration
// =============================================================================

/** Maximum number of cascade levels in the hierarchy */
constexpr int MAX_CASCADES = 6;

/** Default volume resolution (64^3 = fast SDF generation, ~6 voxels per wall at 4-unit volume) */
constexpr int DEFAULT_VOLUME_RESOLUTION = 64;

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

    /** Phase 5b: per-direction D×D tile atlas — (res*D)×(res*D)×res RGBA16F */
    GLuint probeAtlasTexture;
    
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
     */
    void sdfGenerationPass();
    
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
    
private:
    // =============================================================================
    // Phase 6a — Screenshot + AI Analysis
    // =============================================================================

    bool        pendingScreenshot = false;
    std::string screenshotDir     = "doc/cluade_plan/AI/screenshots";
    std::string analysisDir       = "doc/cluade_plan/AI/analysis";

    void launchAnalysis(const std::string& imagePath);

    // =============================================================================
    // Scene State
    // =============================================================================

    /** Current scene type/index */
    int currentScene;
    
    /** Whether scene has been modified */
    bool sceneDirty;
    
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
    
    /** Total frame time (ms) */
    double frameTimeMs;
    
    /** Active voxel count (for sparse representation) */
    int activeVoxelCount;
    
    /** Memory usage in MB */
    float memoryUsageMB;
};

#endif // DEMO3D_H
