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
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>
#include <random>

// raylib already compiles STB_IMAGE_WRITE_IMPLEMENTATION in rtextures.c;
// include the header here for declarations only (no redefinition).
#include "external/stb_image_write.h"
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
    , probeGridHistory(0)
    , probeAtlasTexture(0)
    , probeAtlasHistory(0)
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
    if (probeGridTexture)  { glDeleteTextures(1, &probeGridTexture);  probeGridTexture  = 0; }
    if (probeGridHistory)  { glDeleteTextures(1, &probeGridHistory);  probeGridHistory  = 0; }
    if (probeAtlasTexture) { glDeleteTextures(1, &probeAtlasTexture); probeAtlasTexture = 0; }
    if (probeAtlasHistory) { glDeleteTextures(1, &probeAtlasHistory); probeAtlasHistory = 0; }
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
    , useCascadeGI(true)
    , selectedCascadeForRender(0)
    , useEnvFill(false)
    , skyColor(0.02f, 0.03f, 0.05f)
    , baseRaysPerProbe(8)
    , blendFraction(0.5f)
    , dirRes(4)
    , useDirectionalMerge(true)
    , useColocatedCascades(false)   // non-colocated: better spatial coverage
    , useScaledDirRes(true)         // D4/D8/D16/D16: upper cascades get finer angular res
    , useDirBilinear(true)
    , useSpatialTrilinear(true)
    , useShadowRay(true)
    , useDirectionalGI(true)        // cosine-weighted directional atlas lookup
    , useSoftShadow(false)
    , useSoftShadowBake(false)
    , softShadowK(8.0f)
    , useTemporalAccum(false)
    , temporalAlpha(0.1f)
    , useProbeJitter(false)
    , currentProbeJitter(0.0f)
    , cascadeC0Res(32)
    , atlasBinDx(0)
    , atlasBinDy(0)
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
    , useAnalyticRaymarch(false)
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
    
    std::memset(probeTotalPerCascade, 0, sizeof(probeTotalPerCascade));
    for (int i = 0; i < MAX_CASCADES; ++i) cascadeDirRes[i] = dirRes;
    std::memset(probeNonZero,    0, sizeof(probeNonZero));
    std::memset(probeSurfaceHit, 0, sizeof(probeSurfaceHit));
    std::memset(probeSkyHit,     0, sizeof(probeSkyHit));
    std::memset(probeMaxLum,     0, sizeof(probeMaxLum));
    std::memset(probeMeanLum,    0, sizeof(probeMeanLum));
    std::memset(probeVariance,   0, sizeof(probeVariance));
    std::memset(probeHistogram,  0, sizeof(probeHistogram));

    // Phase 6a: resolve tool paths from exe location (works regardless of CWD)
    initToolsPaths();

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
    loadShader("reduction_3d.comp");    // Phase 5b-1: atlas → isotropic reduction
    loadShader("temporal_blend.comp"); // Phase 9: temporal probe accumulation
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
    setScene(1); // Cornell Box (default test scene)
    
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
    
    // Phase 1: Radiance Debug Controls
    if (IsKeyPressed(KEY_F)) {
        radianceVisualizeMode = (radianceVisualizeMode + 1) % 7;
        const char* radModes[] = { "Slice", "MaxProj", "Avg", "Atlas", "HitType", "Bin", "Bilinear" };
        std::cout << "[Demo3D] Radiance Debug Mode: " << radModes[radianceVisualizeMode] << std::endl;
    }

    // Phase 6a: P = screenshot + AI analysis
    if (IsKeyPressed(KEY_P)) {
        pendingScreenshot = true;
        std::cout << "[6a] Screenshot queued (captured at next render point)." << std::endl;
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
    static bool      lastEnvFill  = false;
    static glm::vec3 lastSkyColor(-1.0f);
    if (useEnvFill != lastEnvFill ||
        (useEnvFill && skyColor != lastSkyColor)) {
        lastEnvFill  = useEnvFill;
        lastSkyColor = skyColor;
        cascadeReady = false;
    }
    // Phase 5a: baseRaysPerProbe slider is retired -- D^2 fixed dispatch, slider is disabled in UI.
    static bool lastDirectionalMerge = true;
    if (useDirectionalMerge != lastDirectionalMerge) {
        lastDirectionalMerge = useDirectionalMerge;
        cascadeReady = false;  // directional/isotropic toggle → recompute all cascades
    }
    static bool lastDirBilinear = true;
    if (useDirBilinear != lastDirBilinear) {
        lastDirBilinear = useDirBilinear;
        cascadeReady = false;  // bilinear toggle changes merge result → recompute all cascades
        std::cout << "[5f] dir bilinear: " << (useDirBilinear ? "ON" : "OFF") << std::endl;
    }
    static bool lastSpatialTrilinear = true;
    if (useSpatialTrilinear != lastSpatialTrilinear) {
        lastSpatialTrilinear = useSpatialTrilinear;
        cascadeReady = false;
        std::cout << "[5d] spatial trilinear: "
                  << (useSpatialTrilinear ? "ON (8-neighbor)" : "OFF (nearest-parent)")
                  << std::endl;
    }
    static bool lastShadowRay = true;
    if (useShadowRay != lastShadowRay) {
        lastShadowRay = useShadowRay;
        // Display-path only -- no cascade rebuild needed
        std::cout << "[5h] shadow ray: " << (useShadowRay ? "ON" : "OFF (unshadowed)") << std::endl;
    }
    static bool lastDirectionalGI = false;
    if (useDirectionalGI != lastDirectionalGI) {
        lastDirectionalGI = useDirectionalGI;
        // Display-path only -- no cascade rebuild needed
        std::cout << "[5g] directional GI: " << (useDirectionalGI ? "ON (cosine-weighted atlas)" : "OFF (isotropic)") << std::endl;
    }
    // Phase 5i: soft shadow display toggle (display-path only)
    static bool lastSoftShadow = false;
    if (useSoftShadow != lastSoftShadow) {
        lastSoftShadow = useSoftShadow;
        std::cout << "[5i] soft shadow display: " << (useSoftShadow ? "ON" : "OFF (binary)") << std::endl;
    }
    // Phase 5i: soft shadow bake toggle or k change — requires cascade rebuild
    static bool  lastSoftShadowBake = false;
    static float lastSoftShadowK    = 8.0f;
    if (useSoftShadowBake != lastSoftShadowBake ||
            (useSoftShadowBake && softShadowK != lastSoftShadowK)) {
        lastSoftShadowBake = useSoftShadowBake;
        lastSoftShadowK    = softShadowK;
        cascadeReady = false;
        std::cout << "[5i] soft shadow bake: "
                  << (useSoftShadowBake ? "ON" : "OFF (binary inShadow)")
                  << "  k=" << softShadowK << std::endl;
    }
    // Phase 5d: co-located toggle changes texture dimensions -- must destroy+rebuild
    static bool lastColocated = true;
    if (useColocatedCascades != lastColocated) {
        lastColocated = useColocatedCascades;
        destroyCascades();
        initCascades();
        cascadeReady = false;  // this triggers probeDumped=false in the Pass 3 block below
        std::cout << "[5d] cascade layout: "
                  << (useColocatedCascades ? "co-located (all 32^3)" : "non-co-located (32/16/8/4)")
                  << std::endl;
    }
    // Phase 5e: D-scaling toggle changes atlas dimensions -- must destroy+rebuild
    static bool lastScaledDirRes = false;
    if (useScaledDirRes != lastScaledDirRes) {
        lastScaledDirRes = useScaledDirRes;
        destroyCascades();
        initCascades();
        cascadeReady = false;
        std::cout << "[5e] dir scaling: "
                  << (useScaledDirRes ? "scaled (D4/D8/D16/D16)" : "fixed (all D4)")
                  << std::endl;
    }

    // Phase 8: dirRes change — triggers full cascade rebuild (atlas layout depends on D)
    static int lastDirRes = 4;
    if (dirRes != lastDirRes) {
        lastDirRes = dirRes;
        destroyCascades();
        initCascades();
        cascadeReady = false;
        std::cout << "[Phase 8] dirRes: " << dirRes
                  << "  D^2=" << dirRes * dirRes << " bins/probe" << std::endl;
    }

    // C0 probe resolution slider — changes interval and atlas dimensions
    static int lastC0Res = 32;
    if (cascadeC0Res != lastC0Res) {
        lastC0Res = cascadeC0Res;
        destroyCascades();
        initCascades();
        cascadeReady = false;
        std::cout << "[C0] probe res: " << cascadeC0Res
                  << "^3  baseInterval=" << baseInterval << "m" << std::endl;
    }

    static float lastBlendFrac = -1.0f;
    if (blendFraction != lastBlendFrac) {
        std::cout << "[4c] blendFraction " << lastBlendFrac << " -> " << blendFraction
                  << (blendFraction < 0.01f ? "  (binary — Phase 3 mode)" : "  (blended)") << std::endl;
        lastBlendFrac = blendFraction;
        cascadeReady  = false;
    }

    // Phase 9: temporal accumulation rebuild triggers.
    // On first enable: warm up history (display switches to reading it immediately).
    // With jitter ON: rebuild every frame — each frame samples different world positions,
    // and the EMA integrates them into a wider spatial footprint over ~22 frames.
    // Without jitter: one warm-up rebuild is enough; accumulating identical samples
    // converges to the same biased result and wastes GPU time.
    static bool lastTemporalAccum = false;
    if (useTemporalAccum != lastTemporalAccum) {
        lastTemporalAccum = useTemporalAccum;
        if (useTemporalAccum) cascadeReady = false;  // warm-up on first enable
    }
    static bool lastProbeJitter = false;
    if (useProbeJitter != lastProbeJitter) {
        lastProbeJitter = useProbeJitter;
        cascadeReady = false;
    }
    if (useTemporalAccum && useProbeJitter) {
        cascadeReady = false;  // continuous rebuild to accumulate distinct jitter samples
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
        int resC0 = cascades[0].resolution;   // always 32
        probeTotal  = resC0 * resC0 * resC0;  // C0 total; kept for spot-sample and UI guards

        for (int ci = 0; ci < cascadeCount; ++ci) {
            if (!cascades[ci].active || cascades[ci].probeGridTexture == 0) continue;

            // Phase 5d: per-cascade resolution (32 when co-located; 32>>ci when non-co-located)
            int res     = cascades[ci].resolution;
            int ciTotal = res * res * res;
            int D       = cascadeDirRes[ci];   // Phase 5e: per-cascade D
            int atlasWH = res * D;
            probeTotalPerCascade[ci] = ciTotal;

            // RGB luminance stats from probeGridTexture (isotropic average, correct)
            std::vector<float> ciBuf(static_cast<size_t>(ciTotal) * 4);
            glBindTexture(GL_TEXTURE_3D, cascades[ci].probeGridTexture);
            glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, ciBuf.data());
            glBindTexture(GL_TEXTURE_3D, 0);

            // Surf/sky hit classification from probeAtlasTexture alpha.
            // Phase 5b-1 zeroes probeGridTexture.a, so packed-hit decode no longer works there.
            // Atlas stores hit.a per bin: >0 = surface, <0 = sky, =0 = miss.
            int surfHit = 0, skyHit = 0;
            if (cascades[ci].probeAtlasTexture != 0) {
                std::vector<float> atlasBuf(static_cast<size_t>(atlasWH) * atlasWH * res * 4);
                glBindTexture(GL_TEXTURE_3D, cascades[ci].probeAtlasTexture);
                glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, atlasBuf.data());
                glBindTexture(GL_TEXTURE_3D, 0);
                for (int pz = 0; pz < res; ++pz)
                for (int py = 0; py < res; ++py)
                for (int px = 0; px < res; ++px) {
                    bool hasSurf = false, hasSky = false;
                    for (int dy = 0; dy < D; ++dy)
                    for (int dx = 0; dx < D; ++dx) {
                        int ax = px * D + dx, ay = py * D + dy;
                        float a = atlasBuf[((pz * atlasWH + ay) * atlasWH + ax) * 4 + 3];
                        if (a > 0.0f) hasSurf = true;
                        if (a < 0.0f) hasSky  = true;
                    }
                    if (hasSurf) ++surfHit;
                    if (hasSky)  ++skyHit;
                }
            }

            float maxLum = 0.0f, sumLum = 0.0f, sumLum2 = 0.0f;
            int nonZero = 0;
            for (int i = 0; i < ciTotal; ++i) {
                float r = ciBuf[i*4+0], g = ciBuf[i*4+1], b = ciBuf[i*4+2];
                float lum = (r + g + b) / 3.0f;
                if (lum > 1e-4f) ++nonZero;
                sumLum  += lum;
                sumLum2 += lum * lum;
                maxLum = std::max(maxLum, lum);
            }
            probeNonZero[ci]    = nonZero;
            probeSurfaceHit[ci] = surfHit;
            probeSkyHit[ci]     = skyHit;
            probeMaxLum[ci]     = maxLum;
            float mean = sumLum / static_cast<float>(ciTotal);
            probeMeanLum[ci]    = mean;
            // Cascade-wide luminance distribution variance: E[X^2] - E[X]^2 over all res^3 probes.
            // This is NOT per-probe Monte Carlo variance — it captures scene spatial structure
            // (light gradients, wall colours) as well as sampling noise. Use as a heuristic only.
            probeVariance[ci]   = sumLum2 / static_cast<float>(ciTotal) - mean * mean;

            // 16-bin probe-luminance distribution -- spatial histogram across all res^3 probes.
            {
                constexpr int BINS = 16;
                float histMax = std::min(mean * 4.0f, maxLum);
                if (histMax < 1e-6f) histMax = 1e-6f;
                int rawBins[BINS] = {};
                for (int i = 0; i < ciTotal; ++i) {
                    float lum = (ciBuf[i*4+0] + ciBuf[i*4+1] + ciBuf[i*4+2]) / 3.0f;
                    int bin = static_cast<int>(lum / histMax * BINS);
                    bin = std::max(0, std::min(BINS - 1, bin));
                    ++rawBins[bin];
                }
                float binMax = 1.0f;
                for (int b = 0; b < BINS; ++b) binMax = std::max(binMax, static_cast<float>(rawBins[b]));
                for (int b = 0; b < BINS; ++b)
                    probeHistogram[ci][b] = static_cast<float>(rawBins[b]) / binMax;
            }
        }

        // Center and backwall spot-samples taken from C0 (always 32^3)
        std::vector<float> buf(static_cast<size_t>(probeTotal) * 4);
        auto idx = [resC0](int x, int y, int z){ return (z*resC0*resC0 + y*resC0 + x)*4; };
        int cx = resC0/2, cy = resC0/2, cz = resC0/2;
        glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
        glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
        glBindTexture(GL_TEXTURE_3D, 0);
        probeCenterSample   = glm::vec3(buf[idx(cx,cy,cz)+0], buf[idx(cx,cy,cz)+1], buf[idx(cx,cy,cz)+2]);
        probeBackwallSample = glm::vec3(buf[idx(16,16,1)+0],  buf[idx(16,16,1)+1],  buf[idx(16,16,1)+2]);

        // A/B log: mean lum per cascade after every bake — compare at blendFraction=0.0 vs 0.5
        std::cout << "[4c A/B] blend=" << blendFraction
                  << (blendFraction < 0.01f ? " (binary)" : " (blended)") << "  meanLum:";
        for (int ci = 0; ci < cascadeCount; ++ci)
            std::cout << "  C" << ci << "=" << std::fixed << std::setprecision(5) << probeMeanLum[ci];
        std::cout << std::defaultfloat << std::endl;
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
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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

    // Phase 5b: atlas for modes 3 (raw), 4 (HitType), 5 (Bin viewer)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, cascades[selC].probeAtlasTexture);
    glUniform1i(glGetUniformLocation(it->second, "uAtlasTexture"), 1);
    glUniform1i(glGetUniformLocation(it->second, "uAtlasDirRes"),  dirRes);
    glUniform2i(glGetUniformLocation(it->second, "uAtlasBin"),     atlasBinDx, atlasBinDy);

    int res = cascades[selC].resolution;
    glUniform3i(glGetUniformLocation(it->second, "uVolumeSize"), res, res, res);
    glUniform1i(glGetUniformLocation(it->second, "uSliceAxis"),       radianceSliceAxis);
    glUniform1f(glGetUniformLocation(it->second, "uSlicePosition"),   radianceSlicePosition);
    glUniform1i(glGetUniformLocation(it->second, "uVisualizeMode"),   radianceVisualizeMode);
    glUniform1f(glGetUniformLocation(it->second, "uExposure"),        radianceExposure);
    glUniform1i(glGetUniformLocation(it->second, "uShowGrid"),        showRadianceGrid ? 1 : 0);
    glUniform1f(glGetUniformLocation(it->second, "uIntensityScale"),  radianceIntensityScale);
    glUniform1i(glGetUniformLocation(it->second, "uRaysPerProbe"),    dirRes * dirRes);

    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    const char* radModeNames[] = { "Slice", "Max Projection", "Average", "Direct", "HitType" };
    int modeIdx = std::max(0, std::min(radianceVisualizeMode, 4));
    ImGui::Text("Mode: %s", radModeNames[modeIdx]);
    if (radianceVisualizeMode == 4)
        ImGui::TextColored(ImVec4(0.5f,1,0.5f,1), "  R=miss  G=surf  B=sky");
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
    // Phase 9: generate one jitter vector per rebuild cycle (same for all cascades).
    // With temporal accumulation, each rebuild samples at a different world position.
    if (useProbeJitter) {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        currentProbeJitter = glm::vec3(dist(rng), dist(rng), dist(rng));
    } else {
        currentProbeJitter = glm::vec3(0.0f);
    }

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
    // Phase 5d: split uBaseInterval (always C0's cellSize for tMin/tMax formula)
    // from uProbeCellSize (per-cascade, for probeToWorld()).
    // In co-located mode both are 0.125; in non-co-located they diverge.
    glUniform1f(glGetUniformLocation(prog, "uBaseInterval"),  cascades[0].cellSize);  // always 0.125
    glUniform1f(glGetUniformLocation(prog, "uProbeCellSize"), c.cellSize);             // per-cascade

    // Phase 5d: upper-cascade scale factor for upperProbePos = probePos / scale.
    // 0 = no upper cascade; 1 = co-located (same index); 2 = non-co-located (halved index).
    int upperIdx5d = cascadeIndex + 1;
    bool hasUpper5d = (!disableCascadeMerging && upperIdx5d < cascadeCount &&
                       cascades[upperIdx5d].active && cascades[upperIdx5d].probeAtlasTexture != 0);
    int  upperToCurrentScale = hasUpper5d ? (useColocatedCascades ? 1 : 2) : 0;
    glUniform1i(glGetUniformLocation(prog, "uUpperToCurrentScale"), upperToCurrentScale);
    float upperProbeCellSz = hasUpper5d ? cascades[upperIdx5d].cellSize : 0.0f;
    glUniform1f(glGetUniformLocation(prog, "uUpperProbeCellSize"), upperProbeCellSz);

    glm::ivec3 volRes(c.resolution);
    glUniform3iv(glGetUniformLocation(prog, "uVolumeSize"), 1, glm::value_ptr(volRes));
    glUniform3fv(glGetUniformLocation(prog, "uGridSize"),   1, glm::value_ptr(volumeSize));
    glUniform3fv(glGetUniformLocation(prog, "uGridOrigin"), 1, glm::value_ptr(volumeOrigin));
    glUniform1i(glGetUniformLocation(prog, "uDirRes"), cascadeDirRes[cascadeIndex]);  // Phase 5a/5e
    int upperCascDirRes = hasUpper5d ? cascadeDirRes[cascadeIndex + 1] : cascadeDirRes[cascadeIndex];
    glUniform1i(glGetUniformLocation(prog, "uUpperDirRes"), upperCascDirRes);  // Phase 5e
    // Phase 5d trilinear: upper cascade probe grid dimensions for 8-neighbor clamping
    int upperRes = hasUpper5d ? cascades[cascadeIndex + 1].resolution : 1;
    glm::ivec3 upperVolRes(upperRes);
    glUniform3iv(glGetUniformLocation(prog, "uUpperVolumeSize"), 1, glm::value_ptr(upperVolRes));
    glUniform1i(glGetUniformLocation(prog, "uUseSpatialTrilinear"), useSpatialTrilinear ? 1 : 0);
    glUniform3f(glGetUniformLocation(prog, "uLightPos"),   0.0f, 0.8f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "uLightColor"), 1.0f, 0.95f, 0.85f);
    glUniform1i(glGetUniformLocation(prog, "uUseEnvFill"), useEnvFill ? 1 : 0);
    glUniform3fv(glGetUniformLocation(prog, "uSkyColor"),  1, glm::value_ptr(skyColor));
    glUniform1f(glGetUniformLocation(prog, "uBlendFraction"), blendFraction);
    // 5i: soft shadow in bake shader
    glUniform1i(glGetUniformLocation(prog, "uUseSoftShadowBake"), useSoftShadowBake ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "uSoftShadowK"),        softShadowK);
    // Phase 9: probe jitter — offsets probe world positions by ±0.5 cell for temporal supersampling
    glUniform3fv(glGetUniformLocation(prog, "uProbeJitter"), 1, glm::value_ptr(currentProbeJitter));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glUniform1i(glGetUniformLocation(prog, "uSDF"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 1);

    // Phase 5c: upper cascade directional atlas for per-direction merge.
    // When a ray misses this level's interval, fetch the upper atlas at the exact
    // direction bin instead of the isotropic probe average.
    int upperIdx = cascadeIndex + 1;
    if (!disableCascadeMerging &&
        upperIdx < cascadeCount && cascades[upperIdx].active && cascades[upperIdx].probeAtlasTexture != 0) {
        // Unit 2: directional atlas (Phase 5c texelFetch path)
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, cascades[upperIdx].probeAtlasTexture);
        glUniform1i(glGetUniformLocation(prog, "uUpperCascadeAtlas"), 2);
        // Unit 3: isotropic probeGridTexture (Phase 4 fallback when toggle is OFF)
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, cascades[upperIdx].probeGridTexture);
        glUniform1i(glGetUniformLocation(prog, "uUpperCascade"), 3);
        glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 1);
    } else {
        glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 0);
    }
    glUniform1i(glGetUniformLocation(prog, "uUseDirectionalMerge"), useDirectionalMerge ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog, "uUseDirBilinear"),      useDirBilinear      ? 1 : 0);

    // Phase 5b: write per-direction radiance into the atlas (not the isotropic grid)
    glBindImageTexture(0, c.probeAtlasTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    glm::ivec3 wg = calculateWorkGroups(c.resolution, c.resolution, c.resolution, 4);
    glDispatchCompute(wg.x, wg.y, wg.z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Phase 5b-1: reduction — average D² atlas bins per probe → isotropic probeGridTexture
    // This keeps raymarch.frag's display path (texture(uRadiance, uvw).rgb) valid.
    auto red = shaders.find("reduction_3d.comp");
    if (red != shaders.end()) {
        glUseProgram(red->second);
        glUniform1i(glGetUniformLocation(red->second, "uDirRes"),      cascadeDirRes[cascadeIndex]);
        glUniform3iv(glGetUniformLocation(red->second, "uVolumeSize"), 1, glm::value_ptr(volRes));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, c.probeAtlasTexture);
        glUniform1i(glGetUniformLocation(red->second, "uAtlas"), 0);

        glBindImageTexture(0, c.probeGridTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

        glDispatchCompute(wg.x, wg.y, wg.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    // Phase 9: temporal blend — mix fresh bake into history buffers.
    // Atlas history: (res*D)x(res*D)x res — same work groups as bake.
    // Grid history:  res x res x res     — same work groups as reduction.
    // Display pass reads from history when useTemporalAccum is ON.
    auto tb = shaders.find("temporal_blend.comp");
    if (useTemporalAccum && tb != shaders.end() &&
        c.probeAtlasHistory != 0 && c.probeGridHistory != 0) {
        GLuint tbProg = tb->second;
        glUseProgram(tbProg);
        glUniform1f(glGetUniformLocation(tbProg, "uAlpha"), temporalAlpha);

        // Blend directional atlas
        int D    = cascadeDirRes[cascadeIndex];
        int axyz = c.resolution * D;
        glUniform3i(glGetUniformLocation(tbProg, "uSize"), axyz, axyz, c.resolution);
        glBindImageTexture(0, c.probeAtlasHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(1, c.probeAtlasTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
        glm::ivec3 wgA = calculateWorkGroups(axyz, axyz, c.resolution, 4);
        glDispatchCompute(wgA.x, wgA.y, wgA.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // Blend isotropic grid
        glUniform3i(glGetUniformLocation(tbProg, "uSize"), c.resolution, c.resolution, c.resolution);
        glBindImageTexture(0, c.probeGridHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(1, c.probeGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
        glDispatchCompute(wg.x, wg.y, wg.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
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

    // Phase 7: analytic SDF toggle — bind primitive SSBO so the fragment shader can
    // evaluate SDF continuously (no grid). SSBO binding 0 is shared with the compute
    // pass; safe here since compute is not running during the render draw call.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, primitiveSSBO);
    glUniform1i(glGetUniformLocation(prog, "uUseAnalyticSDF"),
                useAnalyticRaymarch ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog, "uPrimitiveCount"),
                static_cast<GLint>(analyticSDF.getPrimitiveCount()));

    // Cascade indirect lighting — bind the user-selected cascade level so each can be
    // inspected independently; uUseCascade only controls blending in mode 0
    int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
    if (cascades[selC].active && cascades[selC].probeGridTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        // Phase 9: read from temporal history when accumulation is active and history exists
        GLuint gridTex = (useTemporalAccum && cascades[selC].probeGridHistory != 0)
                         ? cascades[selC].probeGridHistory
                         : cascades[selC].probeGridTexture;
        glBindTexture(GL_TEXTURE_3D, gridTex);
        glUniform1i(glGetUniformLocation(prog, "uRadiance"), 1);
    }
    glUniform1i(glGetUniformLocation(prog, "uUseCascade"), useCascadeGI ? 1 : 0);

    // Albedo volume (sampler binding 2) — always bound, needed for direct shading color
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 2);

    // 5h: shadow ray toggle (display-path only, no cascade rebuild)
    glUniform1i(glGetUniformLocation(prog, "uUseShadowRay"), useShadowRay ? 1 : 0);
    // 5i: soft shadow in display path
    glUniform1i(glGetUniformLocation(prog, "uUseSoftShadow"), useSoftShadow ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "uSoftShadowK"),   softShadowK);

    // 5g: bind selected cascade's directional atlas on unit 3 (units 0-2 = SDF/Radiance/Albedo)
    // Use selC (not hardcoded 0) so the cascade debug selector works in mode 6.
    // atlasAvailable gates the shader toggle so uUseDirectionalGI=0 is always pushed
    // when the texture is missing -- even if the UI toggle is ON.
    bool atlasAvailable = false;
    if (cascadeCount > 0 && cascades[selC].active && cascades[selC].probeAtlasTexture != 0) {
        glActiveTexture(GL_TEXTURE3);
        // Phase 9: read from temporal history when accumulation is active and history exists
        GLuint atlasTex = (useTemporalAccum && cascades[selC].probeAtlasHistory != 0)
                          ? cascades[selC].probeAtlasHistory
                          : cascades[selC].probeAtlasTexture;
        glBindTexture(GL_TEXTURE_3D, atlasTex);
        glUniform1i(glGetUniformLocation(prog, "uDirectionalAtlas"), 3);
        glm::ivec3 atlasVolSize(cascades[selC].resolution);
        glUniform3iv(glGetUniformLocation(prog, "uAtlasVolumeSize"), 1, glm::value_ptr(atlasVolSize));
        glUniform3fv(glGetUniformLocation(prog, "uAtlasGridOrigin"), 1, glm::value_ptr(volumeOrigin));
        glUniform3fv(glGetUniformLocation(prog, "uAtlasGridSize"),   1, glm::value_ptr(volumeSize));
        glUniform1i(glGetUniformLocation(prog, "uAtlasDirRes"),      cascadeDirRes[selC]);
        atlasAvailable = true;
    }
    glUniform1i(glGetUniformLocation(prog, "uUseDirectionalGI"), (useDirectionalGI && atlasAvailable) ? 1 : 0);

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
    loadShader("reduction_3d.comp");
    loadShader("temporal_blend.comp");
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

    const int   baseRes    = cascadeC0Res;
    baseInterval           = volumeSize.x / float(baseRes);
    const float baseCellSz = baseInterval;

    for (int i = 0; i < cascadeCount; ++i) {
        // Phase 5d: co-located = all 32^3; non-co-located = 32>>i (32,16,8,4)
        int   probeRes = useColocatedCascades ? baseRes    : (baseRes >> i);
        float cellSz   = useColocatedCascades ? baseCellSz : volumeSize.x / float(probeRes);

        cascades[i].initialize(probeRes, cellSz, volumeOrigin, baseRaysPerProbe * (1 << i));

        // Phase 5e: per-cascade D. Scaled: C0=D4,C1=D8,C2=D16,C3=D16 (cap 16). Fixed: all D4.
        // D=2 is degenerate: all 4 bin centers land on the octahedral equatorial fold (z=0),
        // causing severe directional mismatch when reading upper cascade atlas. Min D is 4.
        int cascD = useScaledDirRes ? std::min(16, dirRes << i) : dirRes;
        cascadeDirRes[i] = cascD;

        // Phase 5b: per-direction atlas — (res*D)x(res*D)x res RGBA16F, must be GL_NEAREST
        int atlasXY = probeRes * cascD;
        cascades[i].probeAtlasTexture = gl::createTexture3D(
            atlasXY, atlasXY, probeRes,
            GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
        // gl::createTexture3D defaults to GL_LINEAR — override to prevent bin bleed
        glBindTexture(GL_TEXTURE_3D, cascades[i].probeAtlasTexture);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);

        // Phase 9: temporal history atlas — same layout as probeAtlasTexture (zero-initialized)
        cascades[i].probeAtlasHistory = gl::createTexture3D(
            atlasXY, atlasXY, probeRes,
            GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_3D, cascades[i].probeAtlasHistory);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);

        // Phase 9: temporal history for isotropic probe grid — same dims as probeGridTexture
        cascades[i].probeGridHistory = gl::createTexture3D(
            probeRes, probeRes, probeRes,
            GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);

        std::cout << "[Demo3D] Cascade " << i << ": " << probeRes
                  << "^3 probes, D=" << cascD << ", cellSize=" << cellSz
                  << ", atlas=" << atlasXY << "x" << atlasXY << "x" << probeRes
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
        ImGui::Text("Probe grid: %d^3  D=%d  rays/probe=%d (all cascades, Phase 5a)",
            cascadeC0Res, dirRes, dirRes * dirRes);
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
    ImGui::RadioButton("GI only (6)",     &raymarchRenderMode, 6); ImGui::SameLine();
    ImGui::RadioButton("RayDist (7)",     &raymarchRenderMode, 7);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Mode 7: ray travel distance heatmap (continuous float, green=near, red=far).\nCompare with Mode 5 (integer step count). If mode 7 is smooth but mode 5 is banded,\nthe banding is from integer step-count quantization, not SDF resolution.");

    ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("OFF (default): SDF read from 128^3 texture (trilinear, grid-quantized).\nON: SDF evaluated analytically per-sample — truly continuous, no voxel grid.\nDiagnostic: toggle in Mode 5 or 7. If banding disappears -> grid is the cause.\nIf banding stays -> it is the natural rectangular iso-contours of the Cornell Box.");

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
    // Reusable helper: grey "(?) " that shows a tooltip on hover.
    auto HelpMarker = [](const char* desc) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", desc);
    };

    ImGui::Begin("Cascades");

    // ── Cascade hierarchy ────────────────────────────────────────────────────
    {
        char layoutDesc[64];
        if (useColocatedCascades)
            snprintf(layoutDesc, sizeof(layoutDesc), "co-located, all %d^3", cascadeC0Res);
        else
            snprintf(layoutDesc, sizeof(layoutDesc), "non-co-located, %d/%d/%d/%d",
                cascadeC0Res, cascadeC0Res>>1, cascadeC0Res>>2, cascadeC0Res>>3);
        ImGui::Text("Cascade Count: %d  [%s]", cascadeCount, layoutDesc);
    }
    HelpMarker(
        "4-level hierarchical probe grid (C0-C3).\n"
        "Co-located mode: all cascades share the same 32^3 world-space grid.\n"
        "Non-co-located (ShaderToy): C0=32^3 C1=16^3 C2=8^3 C3=4^3.\n\n"
        "Each level samples a distinct distance band via ray marching:\n"
        "  C0  [~0,   0.125m]  near-field / direct contact\n"
        "  C1  [0.125, 0.5m]  short-range bounce\n"
        "  C2  [0.5,   2.0m]  mid-range\n"
        "  C3  [2.0,   8.0m]  far-field / ceiling / sky\n"
        "Merge pass feeds C3 data into C2, C2 into C1, C1 into C0\n"
        "so each level inherits far-field radiance from above.\n\n"
        "Toggle 'Co-located cascades' checkbox below to switch modes.\n"
        "Non-co-located mode rebuilds all cascade textures on toggle.");
    {
        const float d = (cascadeCount > 0) ? cascades[0].cellSize : 0.125f;
        for (int i = 0; i < cascadeCount; ++i) {
            if (!cascades[i].active) continue;
            float tMin = (i == 0) ? 0.02f : d * std::pow(4.0f, float(i - 1));
            float tMax = d * std::pow(4.0f, float(i));
            ImGui::Text("  C%d: %d^3  D=%d  cell=%.4fm  [%.3f, %.3f]m  D^2=%d rays",
                i, cascades[i].resolution, cascadeDirRes[i], cascades[i].cellSize, tMin, tMax,
                cascadeDirRes[i] * cascadeDirRes[i]);
        }
    }

    ImGui::Separator();

    // ── Merge toggle ─────────────────────────────────────────────────────────
    ImGui::Checkbox("Disable Merge (raw per-level)", &disableCascadeMerging);
    HelpMarker(
        "Merge ON (default): each cascade's miss rays pull radiance from\n"
        "the next coarser level (C3->C2->C1->C0).\n\n"
        "Merge OFF (debug): every cascade solved independently with no\n"
        "upper-cascade fallback. Useful to isolate a single level's\n"
        "direct contribution without any inter-level blending.");
    ImGui::SameLine();
    ImGui::TextDisabled(disableCascadeMerging ? "(each level independent)" : "(C3->C2->C1->C0)");

    // ── Phase 5c: directional merge toggle (A/B) ─────────────────────────────
    ImGui::Checkbox("Directional merge (Phase 5c)", &useDirectionalMerge);
    HelpMarker(
        "ON  (default): per-direction texelFetch from upper cascade atlas.\n"
        "     Each ray's miss pulls the upper cascade value for THAT exact\n"
        "     direction bin, not the probe average.\n\n"
        "OFF (Phase 4 fallback): isotropic texture() from upper cascade's\n"
        "     probeGridTexture -- same averaged value for all directions.\n\n"
        "Toggle to A/B compare. Expect: ON reduces banding at cascade\n"
        "boundaries; red/green walls show distinct directional color.");
    ImGui::SameLine();
    ImGui::TextDisabled(useDirectionalMerge ? "(directional)" : "(isotropic fallback)");

    // ── Phase 5f: directional bilinear interpolation ──────────────────────────
    ImGui::Checkbox("Directional bilinear merge (Phase 5f)", &useDirBilinear);
    HelpMarker(
        "ON  (default): when reading the upper cascade atlas, blends across\n"
        "     4 surrounding direction bins (bilinear in octahedral space).\n"
        "     Eliminates hard bin-boundary banding and wall-color bleeding.\n"
        "     Cost: 4x texelFetch calls per direction bin vs nearest-bin.\n\n"
        "OFF (Phase 5c nearest-bin): snaps to the single nearest bin via\n"
        "     floor(oct * D). At D=4 each bin is ~36 degrees wide, so bin\n"
        "     boundaries cause hard color steps -> banding and bleeding.\n\n"
        "No effect when Directional merge (5c) is OFF.\n"
        "Zero regression: at D=1 (not used) bilinear == nearest.\n\n"
        "Use atlas debug modes 5 (nearest) and 6 (bilinear) to compare.");
    ImGui::SameLine();
    ImGui::TextDisabled(useDirBilinear ? "(bilinear)" : "(nearest-bin)");

    // ── Phase 5d: co-located vs ShaderToy-style probe layout ─────────────────
    ImGui::Separator();
    ImGui::Text("Cascade Probe Layout (Phase 5d):");
    ImGui::Checkbox("Co-located cascades (all N^3)", &useColocatedCascades);
    HelpMarker(
        "ON (default): all 4 cascades share the same 32^3 probe grid.\n"
        "Upper probe for any cascade is at the same world position,\n"
        "so the directional merge reads the identical atlas texel.\n"
        "Phase 5d probe visibility check is a no-op (dist=0).\n\n"
        "OFF (ShaderToy-style, probe-resolution halving):\n"
        "C0=32^3  C1=16^3  C2=8^3  C3=4^3. Same world volume; upper probes ~0.108m away.\n"
        "Directional merge reads upper atlas with 8-neighbor spatial trilinear (Phase 5d\n"
        "trilinear, default ON -- see checkbox below). Spatial trilinear OFF falls back to\n"
        "nearest-parent (probePos/2), which is blocky but cheaper.\n"
        "Toggling destroys and rebuilds all cascade textures.");
    ImGui::SameLine();
    if (useColocatedCascades)
        ImGui::TextDisabled("(all %d^3)", cascadeC0Res);
    else
        ImGui::TextDisabled("(%d/%d/%d/%d)", cascadeC0Res, cascadeC0Res>>1, cascadeC0Res>>2, cascadeC0Res>>3);

    // C0 probe resolution slider
    {
        static const int kC0Options[] = { 8, 16, 32, 64 };
        static const char* kC0Labels[] = { "8^3  (fast, coarse)", "16^3", "32^3 (default)", "64^3  (slow)" };
        int curIdx = 2;
        for (int k = 0; k < 4; ++k) if (kC0Options[k] == cascadeC0Res) { curIdx = k; break; }
        ImGui::Text("C0 probe resolution:");
        HelpMarker(
            "Sets the C0 probe grid resolution. All other cascades derive from this:\n"
            "  co-located:     all cascades use the same N^3 grid.\n"
            "  non-co-located: Ci uses (N>>i)^3, halving per level.\n\n"
            "Changing this sets baseInterval = volumeSize / N (C0 cell size).\n"
            "The full cascade hierarchy is rebuilt on change.\n\n"
            "Non-co-located minimum: N=32 (gives C3=4^3). N=8 gives C3=1^3 (degenerate).\n"
            "N=64 co-located uses ~340 MB VRAM with D scaling ON.");
        if (ImGui::Combo("##C0Res", &curIdx, kC0Labels, 4))
            cascadeC0Res = kC0Options[curIdx];
        ImGui::SameLine();
        ImGui::TextDisabled("baseInterval=%.4fm", volumeSize.x / float(cascadeC0Res));
    }

    // Spatial trilinear merge — only meaningful in non-co-located mode
    {
        bool disabled = useColocatedCascades;
        if (disabled) ImGui::BeginDisabled();
        ImGui::Checkbox("Spatial trilinear merge (Phase 5d)", &useSpatialTrilinear);
        HelpMarker(
            "Non-co-located mode only. Has no effect when co-located is ON.\n\n"
            "ON  (default): when a lower cascade misses, blends the 8 surrounding\n"
            "     upper probes using trilinear interpolation weighted by the lower\n"
            "     probe's fractional position within the upper cell.\n"
            "     Cost: 8x directional reads per miss vs 1x (static bake only).\n\n"
            "OFF: reads only the single nearest upper probe (Phase 5d baseline).\n"
            "     All 8 lower probes in a 2x2x2 block read the same parent -- blocky.\n\n"
            "The -0.5 offset in the trilinear formula maps upper probe centers to\n"
            "integers, matching the same center-to-center convention as Phase 5f\n"
            "directional bilinear (sampleUpperDir's octScaled = oct*D - 0.5).");
        if (disabled) ImGui::EndDisabled();
        ImGui::SameLine();
        if (useColocatedCascades)
            ImGui::TextDisabled("(co-located: no effect)");
        else
            ImGui::TextDisabled(useSpatialTrilinear ? "(8-neighbor)" : "(nearest-parent)");
    }

    // ── Phase 5h: shadow ray in direct path ─────────────────────────────────
    ImGui::Separator();
    ImGui::Checkbox("Shadow ray in direct path (Phase 5h)", &useShadowRay);
    HelpMarker(
        "ON  (default): casts a 32-step SDF shadow ray from each surface hit\n"
        "     toward the light. Uses a normal-offset origin (normal*0.02 +\n"
        "     ldir*0.01) -- better than the bake shader's fixed t=0.05 bias\n"
        "     because the surface normal is known in the final renderer.\n"
        "     Gives hard binary shadow in the direct term.\n\n"
        "OFF (Phase 1-4 baseline): no shadow check in direct path. Shadow only\n"
        "     appears via the cascade indirect contribution, diluted ~8x by the\n"
        "     isotropic reduction.\n\n"
        "Toggle does NOT re-bake cascades -- display path only.");
    ImGui::SameLine();
    ImGui::TextDisabled(useShadowRay ? "(shadow ON)" : "(unshadowed)");

    // ── Phase 5g: directional atlas GI sampling ──────────────────────────────
    ImGui::Separator();
    ImGui::Checkbox("Directional GI sampling (Phase 5g)", &useDirectionalGI);
    HelpMarker(
        "OFF (default): reads the isotropic average probeGridTexture.\n"
        "     Shadow signal diluted ~8x by direction averaging.\n\n"
        "ON: samples C0 directional atlas with cosine-weighted hemisphere\n"
        "    integration over surface normal. 8 probes x D^2 bins = 128\n"
        "    texelFetch per shaded pixel at D=4.\n"
        "    Back-facing bins excluded; spatially trilinear-blended.\n\n"
        "Combine with Phase 5h (shadow ray) for correct direct shadow +\n"
        "smoother indirect GI transition across probe boundaries.\n\n"
        "Mode 6 (GI only) respects this toggle -- use it to A/B compare\n"
        "directional vs isotropic in isolation without direct light.\n\n"
        "Toggle does NOT re-bake cascades -- display path only.");
    ImGui::SameLine();
    ImGui::TextDisabled(useDirectionalGI ? "(directional)" : "(isotropic)");

    // ── Phase 5i: soft shadow (display + bake) ───────────────────────────────
    ImGui::Separator();
    ImGui::Text("Soft Shadow (Phase 5i):");
    ImGui::Checkbox("Soft shadow in direct path##5i_display", &useSoftShadow);
    HelpMarker(
        "ON: SDF cone soft shadow (IQ-style) in the final renderer.\n"
        "    shadow = 1 - min(k*h/t) along the shadow ray.\n"
        "    k controls penumbra width (see slider below).\n"
        "    Not physically accurate for a point light -- this is an\n"
        "    appearance approximation that hides the hard binary edge.\n"
        "    Requires Phase 5h (shadow ray) to be ON.\n\n"
        "OFF (default): hard binary shadow from Phase 5h shadowRay().\n\n"
        "Debug: use mode 4 (direct-only) to compare binary vs soft.\n"
        "Toggle does NOT re-bake cascades -- display path only.");
    ImGui::SameLine();
    ImGui::TextDisabled(useSoftShadow ? "(soft)" : "(binary)");

    ImGui::Checkbox("Soft shadow in bake shader##5i_bake", &useSoftShadowBake);
    HelpMarker(
        "ON: replaces binary inShadow() in the radiance bake shader with\n"
        "    the same SDF cone shadow. Reduces probe-baked signal\n"
        "    discontinuity at shadow boundaries (Sources 2+3 in banding doc).\n"
        "    Uses the same k as the display soft shadow.\n\n"
        "OFF (default): binary inShadow() -- hard shadow in probe data.\n\n"
        "Debug: use mode 3 (indirect x5) or mode 6 (GI-only) to see\n"
        "       the reduced banding in the probe-sourced indirect term.\n\n"
        "Toggling DOES re-bake all cascades.");
    ImGui::SameLine();
    ImGui::TextDisabled(useSoftShadowBake ? "(soft bake)" : "(binary bake)");

    if (ImGui::SliderFloat("Soft shadow k##5i_k", &softShadowK, 1.0f, 16.0f, "k=%.1f")) {
        // k change is a display-only change; bake rebuild is handled by the tracking block
    }
    HelpMarker(
        "Penumbra width for SDF cone soft shadow (display and bake).\n"
        "  k=1  very wide soft shadow (over-softened)\n"
        "  k=4  wide penumbra\n"
        "  k=8  tight penumbra (default)\n"
        "  k=16 nearly binary (close to Phase 5h hard shadow)\n\n"
        "k change re-bakes cascades only when 'Soft shadow in bake' is ON.");

    // ── Phase 5e: per-cascade D scaling ─────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Directional Resolution Scaling (Phase 5e):");
    ImGui::Checkbox("Per-cascade D scaling (C0=D4, C1=D8, C2=D16, C3=D16)", &useScaledDirRes);
    HelpMarker(
        "OFF (default): all 4 cascades use D=4 (16 bins per probe).\n\n"
        "ON: upper cascades use more directional bins (D=min(16, 4<<i)).\n"
        "  C0=D4  (16 bins, unchanged)\n"
        "  C1=D8  (64 bins)\n"
        "  C2=D16 (256 bins)\n"
        "  C3=D16 (256 bins, capped)\n\n"
        "Note: C0=D2 was initially planned but is degenerate -- all 4 bin\n"
        "centers land on the octahedral equatorial fold (z=0 plane), causing\n"
        "severe directional mismatch and wall color bleed. Min safe D is 4.\n\n"
        "Toggling destroys and rebuilds all cascade atlases.\n"
        "Memory co-located + scaled ON: ~148 MB VRAM.\n"
        "Memory non-co-located + scaled ON: ~7 MB VRAM.");
    ImGui::SameLine();
    ImGui::TextDisabled(useScaledDirRes ? "(D4/D8/D16/D16)" : "(all D4)");

    // ── Environment fill (4a) ────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Environment Fill (4a):");
    HelpMarker(
        "Controls what happens when a ray exits the SDF volume boundary\n"
        "without hitting any geometry.\n\n"
        "OFF (honest transport): out-of-volume rays return black. Probes\n"
        "near the volume boundary see zero far-field contribution.\n\n"
        "ON (sky fill): out-of-volume rays return the sky color. Sky\n"
        "propagates down the merge chain, raising all levels to ~100%% any%%.\n"
        "Use for open scenes or to simulate an environment map.");
    ImGui::Checkbox("Env fill (out-of-volume)", &useEnvFill);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When ON: rays that exit the SDF volume return a sky color.\n"
            "Sky propagates through the merge chain, raising all levels to ~100%% any.\n"
            "surf%% = probes with >= 1 direct surface hit (stable regardless of fill).\n"
            "sky%%  = probes with >= 1 direct sky exit (true count, not derived).");
    ImGui::SameLine();
    ImGui::TextDisabled(useEnvFill ? "(sky ambient ON)" : "(honest transport)");
    if (useEnvFill)
        ImGui::ColorEdit3("Sky color", &skyColor[0], ImGuiColorEditFlags_Float);

    // ── Ray count scaling (4b / 5a) ──────────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Ray Count Scaling (4b / 5a):");
    {
        char raysHelpText[512];
        snprintf(raysHelpText, sizeof(raysHelpText),
            "Phase 5a: direction scheme changed to D*D octahedral bins.\n"
            "All cascades fire D*D = %d rays per probe (D=%d, fixed).\n\n"
            "The per-cascade scaling slider (Phase 4b) is RETIRED --\n"
            "the base count no longer controls actual dispatch.\n"
            "Per-cascade D scaling is evaluated in Phase 5e A/B after\n"
            "directional merge (5c) is working.\n\n"
            "Phase 4b note (historical): RGBA16F precision limits the\n"
            "ray ceiling to 8; a separate integer buffer is needed to\n"
            "raise it safely -- deferred to a cleanup pass.",
            dirRes * dirRes, dirRes);
        HelpMarker(raysHelpText);
    }
    ImGui::BeginDisabled();
    ImGui::SliderInt("Base rays/probe (retired)", &baseRaysPerProbe, 4, 8);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("actual: D*D=%d rays/probe (all cascades)", dirRes * dirRes);

    // Phase 8: live dirRes slider — rebuilds cascades on change
    ImGui::SliderInt("Dir resolution (D)", &dirRes, 2, 8);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(
            "Octahedral directional bin count per probe: D*D bins total.\n"
            "D=4 (default): 16 bins, coarse angular resolution.\n"
            "D=8: 64 bins, 4x finer — costs 4x in BOTH bake and display per frame.\n"
            "Phase 8 diagnostic: run E1 (toggle Directional GI) before raising D.");

    // ── Temporal accumulation + probe jitter (Phase 9) ──────────────────────
    ImGui::Separator();
    ImGui::Text("Temporal Accumulation (Phase 9):");
    HelpMarker(
        "B+C: temporal probe accumulation + stochastic jitter.\n\n"
        "Without jitter: suppresses stochastic noise only — deterministic probe\n"
        "positions produce the same biased GI every rebuild, so accumulation\n"
        "converges to the same biased result (banding unchanged).\n\n"
        "With jitter ON: each rebuild samples probes at slightly different world\n"
        "positions. The running average integrates over a wider spatial footprint,\n"
        "softening the discrete-grid banding without adding probes.\n"
        "Expected: with alpha=0.1 + jitter ON, rebuild ~30 cycles — bands soften.");
    ImGui::Checkbox("Temporal accumulation##phase9", &useTemporalAccum);
    if (useTemporalAccum) {
        ImGui::SliderFloat("Temporal alpha", &temporalAlpha, 0.01f, 1.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Blend weight for the current bake (0=keep history, 1=replace).\n"
                "1.0 = no accumulation (same as pre-Phase-9).\n"
                "0.1 = ~22 rebuilds for old data to fall below 10%% weight.\n"
                "Recommended: 0.05–0.1 with jitter ON.");
        ImGui::Checkbox("Probe jitter (requires temporal)", &useProbeJitter);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Each rebuild offsets probe world positions by a random [-0.5,0.5]^3\n"
                "sub-cell jitter. Combined with temporal accumulation, the running\n"
                "average integrates over a wider spatial footprint, reducing banding.\n"
                "Has no effect without temporal accumulation.");
    } else if (useProbeJitter) {
        useProbeJitter = false;  // auto-disable jitter when accum is off — it only adds noise alone
    }

    // ── Interval blend (4c) ──────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Interval Blend (4c):");
    HelpMarker(
        "Controls how sharply the cascade hands off at its interval boundary (tMax).\n\n"
        "0.0 = binary (Phase 3 behaviour): surface hit uses local data only;\n"
        "      miss jumps immediately to upper cascade.\n\n"
        "0.5 = default: blend zone covers the outer 50%% of the interval.\n"
        "      A hit at tMax-epsilon lerps smoothly toward the upper cascade.\n\n"
        "C3 (top cascade) is never blended regardless of this value — there\n"
        "is no upper cascade to hand off to, so local data is always used.\n\n"
        "If changing this has no visible effect, the banding is driven by\n"
        "directional mismatch (Phase 5 fix), not the binary switch.");
    ImGui::SliderFloat("Blend fraction", &blendFraction, 0.0f, 1.0f);
    ImGui::SameLine();
    ImGui::TextDisabled(blendFraction < 0.01f ? "(binary — Phase 3)" : "(blended)");

    ImGui::Separator();

    // ── Cascade selector ─────────────────────────────────────────────────────
    ImGui::Text("Render using cascade:");
    HelpMarker(
        "Selects which cascade level feeds the indirect lighting in the\n"
        "final raymarch pass (render modes 3 and 6).\n\n"
        "C0 — near-field only; fast, no far-field bounce\n"
        "C1 — short-range bounce included\n"
        "C2 — mid-range; usually best visual balance\n"
        "C3 — full far-field; includes ceiling/sky contribution\n\n"
        "Also selects the level shown in the Radiance Debug viewer.");
    for (int i = 0; i < cascadeCount; ++i) {
        if (i > 0) ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof(label), "C%d", i);
        ImGui::RadioButton(label, &selectedCascadeForRender, i);
    }

    // ── Radiance debug viewer mode ───────────────────────────────────────────
    if (showRadianceDebug) {
        ImGui::Text("Radiance debug mode ([F] to cycle):");
        HelpMarker(
            "Controls what the top-right 400x400 radiance debug viewer shows.\n\n"
            "Slice    — isotropic probeGridTexture 2D cross-section\n"
            "MaxProj  — isotropic probeGridTexture max intensity projection\n"
            "Avg      — isotropic probeGridTexture average projection\n"
            "Atlas    — raw per-direction atlas; each D*D block = one probe\n"
            "           Shows all 16 directional bins per probe at once.\n"
            "HitType  — surf/sky/miss fractions from atlas alpha per probe\n"
            "           G=surface  B=sky  R=miss (reads atlas, not grid alpha)\n"
            "Bin      — single direction bin (dx,dy), nearest-bin (Phase 5c).\n"
            "           Key validation: near red wall, leftward bin -> red;\n"
            "           opposite bin -> green. Proves directional merge works.\n"
            "Bilinear — same selected bin but bilinear blend at midpoint between\n"
            "           selected bin and its (+1,+1) neighbor (f=0.5,0.5 point).\n"
            "           Toggle Bin<->Bilinear to see Phase 5f smoothing effect.");
        ImGui::RadioButton("Slice##rad",    &radianceVisualizeMode, 0); ImGui::SameLine();
        ImGui::RadioButton("MaxProj##rad",  &radianceVisualizeMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Avg##rad",      &radianceVisualizeMode, 2); ImGui::SameLine();
        ImGui::RadioButton("Atlas##rad",    &radianceVisualizeMode, 3); ImGui::SameLine();
        ImGui::RadioButton("HitType##rad",  &radianceVisualizeMode, 4); ImGui::SameLine();
        ImGui::RadioButton("Bin##rad",      &radianceVisualizeMode, 5); ImGui::SameLine();
        ImGui::RadioButton("Bilinear##rad", &radianceVisualizeMode, 6);

        if (radianceVisualizeMode == 3)
            ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1), "  Atlas raw — each D%cD block is one probe's directional bins", (char)0xD7);
        if (radianceVisualizeMode == 4)
            ImGui::TextColored(ImVec4(0.5f,1,0.5f,1), "  G=surf hit  B=sky exit  R=miss  (reads atlas alpha — fixed for Phase 5b-1)");
        if (radianceVisualizeMode == 5 || radianceVisualizeMode == 6) {
            if (radianceVisualizeMode == 5)
                ImGui::TextColored(ImVec4(1,0.9f,0.5f,1), "  Bin viewer (nearest): one direction across all probes");
            else
                ImGui::TextColored(ImVec4(0.5f,1,0.9f,1), "  Bilinear viewer: blend at midpoint between selected bin and (+1,+1) neighbor");
            ImGui::SliderInt("bin dx##atlas", &atlasBinDx, 0, dirRes - 1);
            ImGui::SliderInt("bin dy##atlas", &atlasBinDy, 0, dirRes - 1);
            if (radianceVisualizeMode == 5)
                ImGui::TextDisabled("  Validation: near red wall, bin pointing left -> red; right -> green");
            else
                ImGui::TextDisabled("  Toggle mode 5 (nearest) vs 6 (bilinear) to see bin-boundary smoothing effect");
        }
    }

    if (probeTotal > 0) {
        // ── Probe fill rate ──────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Probe Fill Rate:");
        HelpMarker(
            "GPU readback taken once per cascade update (not every frame).\n"
            "Denominator is per-cascade probe count (32^3 co-located;\n"
            "32^3/16^3/8^3/4^3 non-co-located).\n\n"
            "any%%   -- probes with any luminance > 1e-4\n"
            "          (includes sky propagated via the merge chain)\n"
            "surf%%  -- probes with >= 1 direct surface hit in their interval\n"
            "          (stable; unaffected by env fill)\n"
            "sky%%   -- probes with >= 1 direct sky exit\n"
            "          (only non-zero when env fill is ON)\n\n"
            "Colour: red < 10%%  yellow 10-50%%  green > 50%%");
        const float d = (cascadeCount > 0) ? cascades[0].cellSize : 0.125f;
        for (int ci = 0; ci < cascadeCount; ++ci) {
            // Phase 5d: per-cascade probe count for fill-rate denominator
            float ciProbeTot = (probeTotalPerCascade[ci] > 0)
                ? float(probeTotalPerCascade[ci]) : float(probeTotal);
            float pct  = 100.0f * probeNonZero[ci]    / ciProbeTot;
            float surf = 100.0f * probeSurfaceHit[ci] / ciProbeTot;
            float sky  = 100.0f * probeSkyHit[ci]     / ciProbeTot;
            ImVec4 col = (pct < 10.0f) ? ImVec4(1,0.3f,0.3f,1)
                       : (pct < 50.0f) ? ImVec4(1,1,0.3f,1)
                                       : ImVec4(0.3f,1,0.3f,1);
            float tMin = (ci == 0) ? 0.02f : d * std::pow(4.0f, float(ci - 1));
            float tMax = d * std::pow(4.0f, float(ci));
            ImGui::TextColored(col,
                "  C%d [%.2f,%.2f] n=%d: any=%5.1f%%  surf=%5.1f%%  sky=%5.1f%%  max=%.3f  mean=%.4f  dist_var=%.5f",
                ci, tMin, tMax, (int)ciProbeTot,
                pct, surf, sky, probeMaxLum[ci], probeMeanLum[ci], probeVariance[ci]);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "dist_var = cascade-wide luminance distribution variance (E[X^2]-E[X]^2)\n"
                    "Measures spread of probe luminances across the full cascade grid.\n"
                    "Includes scene spatial structure (gradients, wall colours) — NOT\n"
                    "a direct per-probe Monte Carlo noise estimate.");

            // Blend zone annotation (4c)
            if (ci == 3) {
                ImGui::TextDisabled("    blend: GUARDED (no upper cascade — C3 always uses full local data)");
            } else if (blendFraction < 0.01f) {
                ImGui::TextDisabled("    blend: OFF (binary mode — set Blend fraction > 0 to enable)");
            } else {
                float bw     = blendFraction * (tMax - tMin);
                float bStart = tMax - bw;
                ImGui::TextDisabled("    blend zone: [%.3f, %.3f]m  (outer %.0f%% of interval)",
                    bStart, tMax, blendFraction * 100.0f);
            }

            // Probe-coverage bars (4e): surf-cov and sky-cov are overlapping any-hit fractions
            {
                float probeF = ciProbeTot;  // Phase 5d: per-cascade denominator
                float surfF  = float(probeSurfaceHit[ci]) / probeF;
                float skyF   = float(probeSkyHit[ci])     / probeF;
                ImGui::Text("    ");
                ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
                ImGui::ProgressBar(surfF, ImVec2(80, 6), "");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 4);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
                ImGui::ProgressBar(skyF, ImVec2(80, 6), "");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextDisabled("surf-cov  sky-cov  (probe any-hit; may overlap)");
            }
        }

        // ── Mean luminance cross-cascade chart (4e) ──────────────────────────
        {
            float lumCeil = 1e-4f;
            for (int i = 0; i < cascadeCount; ++i)
                lumCeil = std::max(lumCeil, probeMeanLum[i]);
            lumCeil *= 1.5f;
            float meanLums[MAX_CASCADES] = {};
            for (int i = 0; i < cascadeCount; ++i)
                meanLums[i] = probeMeanLum[i];
            ImGui::Text("  Mean lum:");
            HelpMarker(
                "Average probe luminance per cascade after the last bake.\n"
                "Bars should be roughly similar — large divergence between\n"
                "levels may indicate a GI discontinuity across cascade bands.\n"
                "Range scales to the brightest cascade x1.5.");
            ImGui::SameLine();
            ImGui::PlotHistogram("##meanlum", meanLums, cascadeCount, 0,
                                 nullptr, 0.0f, lumCeil, ImVec2(120, 30));
            ImGui::SameLine();
            for (int i = 0; i < cascadeCount; ++i)
                ImGui::TextDisabled("C%d=%.4f  ", i, probeMeanLum[i]);
        }

        // ── Probe-luminance distribution ─────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Probe-Luminance Distribution:");
        HelpMarker(
            "16-bin histogram of probe luminance values across the full\n"
            "cascade grid, range [0, mean*4] (adaptive).\n\n"
            "This is a SPATIAL distribution metric, not a per-probe noise\n"
            "estimate. A wide spread can come from real scene structure\n"
            "(bright ceiling vs dark floor, coloured walls) rather than\n"
            "Monte Carlo variance.\n\n"
            "Useful heuristic: comparing the same scene at base=4 vs base=8\n"
            "cancels scene structure — distribution tightening is then\n"
            "consistent with reduced sampling noise, but not proof of it.");
        for (int ci = 0; ci < cascadeCount; ++ci) {
            char label[32];
            std::snprintf(label, sizeof(label), "C%d D^2=%d", ci, dirRes * dirRes);
            ImGui::PlotHistogram(label, probeHistogram[ci], 16, 0, nullptr, 0.0f, 1.0f,
                                 ImVec2(200, 40));
            if (ci + 1 < cascadeCount) ImGui::SameLine();
        }

        // ── Spot samples ─────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("C0 Spot Samples:");
        HelpMarker(
            "RGB radiance read from two fixed probe positions in C0\n"
            "after each cascade update. Used to sanity-check the GI output\n"
            "at known scene locations without opening the debug viewer.\n\n"
            "Center  — probe at (16,16,16), the room centre\n"
            "Backwall— probe at (16,16,1),  near the back wall\n\n"
            "Both should be non-zero when the cascade has converged.\n"
            "Backwall typically shows stronger colour bleed from the walls.");
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
    
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Phase 3 (complete):");
    ImGui::BulletText("Analytic SDF + albedo volume (64^3)");
    ImGui::BulletText("SDF-guided primary raymarching");
    ImGui::BulletText("4-level radiance cascade (C0-C3)");
    ImGui::BulletText("Cascade merge (C3->C2->C1->C0)");
    ImGui::BulletText("Merge toggle + per-level probe stats");
    ImGui::BulletText("7 render modes (0-6) incl. GI-only");

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 4 (complete):");
    // 4a
    {
        bool on = useEnvFill;
        ImVec4 c = on ? ImVec4(0.3f,1,0.3f,1) : ImVec4(0.7f,0.7f,0.7f,1);
        ImGui::TextColored(c, "  [4a] Env fill toggle  %s", on ? "(ON)" : "(OFF)");
    }
    // 4b
    {
        ImVec4 c = ImVec4(0.3f, 1, 0.3f, 1);
        ImGui::TextColored(c, "  [4b] Per-cascade ray scaling  retired (Phase 5a: fixed D^2=%d rays all cascades)",
            dirRes * dirRes);
    }
    // 4c
    {
        ImVec4 c = ImVec4(0.3f, 1, 0.3f, 1);
        ImGui::TextColored(c, "  [4c] Interval blend  blend=%.2f  %s",
            blendFraction,
            blendFraction < 0.01f ? "(binary — Phase 3)" : "(blended)");
    }
    // 4d
    {
        ImVec4 c = ImVec4(0.3f, 1, 0.3f, 1);
        ImGui::TextColored(c, "  [4d] Filter verification  (no-op — WRAP_R/LINEAR confirmed)");
    }
    // 4e
    {
        ImVec4 c = ImVec4(0.3f, 1, 0.3f, 1);
        ImGui::TextColored(c, "  [4e] Packed-decode fix + blend-zone + coverage bars + mean-lum chart");
    }

    ImGui::NewLine();

    // Phase 5 header: green if all major features active, yellow if any are off
    bool p5full = useDirectionalMerge && useDirBilinear;
    ImVec4 p5col = p5full ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
    ImGui::TextColored(p5col, "Phase 5 (5a/5b/5c/5d/5e/5f implemented):");

    // 5a
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [5a] Octahedral encoding  D=%d  (%d bins/probe)",
            dirRes, dirRes * dirRes);
    }
    // 5b / 5b-1
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [5b] Atlas  %dx%dx%d  GL_NEAREST  +  [5b-1] reduction pass",
            cascadeCount > 0 ? cascades[0].resolution * dirRes : 0,
            cascadeCount > 0 ? cascades[0].resolution * dirRes : 0,
            cascadeCount > 0 ? cascades[0].resolution : 0);
    }
    // 5c
    {
        ImVec4 c = useDirectionalMerge ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
        ImGui::TextColored(c, "  [5c] Directional merge  %s",
            useDirectionalMerge ? "ON" : "OFF (isotropic fallback)");
    }
    // 5d
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [5d] Probe layout  %s",
            useColocatedCascades ? "co-located (all 32^3)" : "non-co-located (32/16/8/4)");
    }
    // 5e
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [5e] D scaling  %s",
            useScaledDirRes ? "ON  (C0=D4 C1=D8 C2=D16 C3=D16)" : "OFF (all D4)");
    }
    // 5f
    {
        ImVec4 c = useDirBilinear ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
        ImGui::TextColored(c, "  [5f] Dir bilinear  %s",
            useDirBilinear ? "ON  (4-tap bilinear, isotropic uses GL_LINEAR)"
                           : "OFF (nearest-bin, nearest-probe)");
    }
    // 5h
    {
        ImVec4 c = useShadowRay ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
        ImGui::TextColored(c, "  [5h] Shadow ray  %s",
            useShadowRay ? "ON  (binary direct shadow)" : "OFF (unshadowed)");
    }
    // 5g
    {
        ImVec4 c = useDirectionalGI ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
        ImGui::TextColored(c, "  [5g] Dir GI  %s",
            useDirectionalGI ? "ON  (cosine-weighted atlas)" : "OFF (isotropic avg)");
    }
    // 5i
    {
        if (useSoftShadow || useSoftShadowBake) {
            ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
                "  [5i] Soft shadow  display=%s  bake=%s  k=%.1f",
                useSoftShadow    ? "ON" : "off",
                useSoftShadowBake ? "ON" : "off",
                softShadowK);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "  [5i] Soft shadow  OFF (binary)");
        }
    }

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

void Demo3D::initToolsPaths() {
    namespace fs = std::filesystem;
    // GetApplicationDirectory() returns the directory containing the exe.
    // Walk upward until we find a "tools/" sibling, handling build/ and build/Debug/.
    fs::path root = fs::weakly_canonical(fs::path(GetApplicationDirectory()));
    for (int i = 0; i < 4; i++) {
        if (fs::exists(root / "tools")) break;
        root = root.parent_path();
    }
    fs::path tools = root / "tools";
    screenshotDir = tools.string();
    analysisDir   = tools.string();
    toolsScript   = (tools / "analyze_screenshot.py").string();
    std::cout << "[6a] Tools path: " << tools << std::endl;
}

void Demo3D::takeScreenshot(bool launchAiAnalysis) {
    if (!pendingScreenshot) return;
    pendingScreenshot = false;

    std::filesystem::create_directories(screenshotDir);
    std::filesystem::create_directories(analysisDir);

    int w = GetScreenWidth(), h = GetScreenHeight();
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    stbi_flip_vertically_on_write(1);  // GL origin is bottom-left

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = "frame_" + std::to_string(now) + ".png";
    std::string path     = screenshotDir + "/" + filename;

    if (stbi_write_png(path.c_str(), w, h, 3, pixels.data(), w * 3)) {
        std::cout << "[6a] Screenshot saved: " << path << std::endl;
        if (launchAiAnalysis)
            launchAnalysis(path);
    } else {
        std::cerr << "[6a] Screenshot write failed: " << path << std::endl;
    }
}

void Demo3D::launchAnalysis(const std::string& imagePath) {
    std::thread([imagePath, this]() {
        std::string cmd = "python \"" + toolsScript + "\" \""
                        + imagePath + "\" \""
                        + analysisDir + "\"";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
    }).detach();
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
