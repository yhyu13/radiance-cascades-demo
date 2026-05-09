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
// Note: <windows.h> is intentionally NOT included here — it conflicts with raylib.h
// via winuser.h (CloseWindow / ShowCursor overload clash). Windows API calls for
// RenderDoc DLL loading are isolated in rdoc_helper.cpp.
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <random>
#include <cassert>
#include <utility>
#include <limits>

// raylib already compiles STB_IMAGE_WRITE_IMPLEMENTATION in rtextures.c;
// include the header here for declarations only (no redefinition).
#include "external/stb_image_write.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Phase 9b: Halton low-discrepancy sequence for probe jitter.
// Maps index → [0,1) in the given base (van der Corput sequence).
static float halton(uint32_t idx, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (idx > 0) { f /= base; r += f * (idx % base); idx /= base; }
    return r;
}

// =============================================================================
// Step 2: Felzenszwalb separable EDT (1D parabola sweep)
// File-scope helper — pure function, no Demo3D state. Caller passes scratch
// buffers (v, z, d) sized n / n+1 / n so the helper does ZERO heap allocations
// per call (codex F3 — was 49152 calls × 3 allocs at N=128).
// =============================================================================
namespace {
    constexpr float EDT_INF = 1e18f;

    // In-place 1D Euclidean distance transform on `f` (length n).
    // Input  f[i] = squared distance to nearest seed in voxel-index units (0 = seed, INF = empty).
    // Output f[i] = squared distance from i to nearest seed along this axis.
    // Scratch buffers must satisfy: v.size() >= n, z.size() >= n+1, d.size() >= n.
    static void edt1d(std::vector<float>& f, int n,
                      std::vector<int>& v, std::vector<float>& z, std::vector<float>& d) {
        // Skip rows with no seed yet — leaves them as INF for the next axis pass.
        bool anyFinite = false;
        for (int i = 0; i < n; ++i) if (f[i] < EDT_INF) { anyFinite = true; break; }
        if (!anyFinite) return;

        int k = 0;
        v[0] = 0;
        z[0] = -EDT_INF;
        z[1] =  EDT_INF;

        for (int q = 1; q < n; ++q) {
            float s;
            while (true) {
                int   r     = v[k];
                // q != r by construction → denom is 2*(q - v[k]) and never zero.
                float s_num = (f[q] + float(q) * float(q)) - (f[r] + float(r) * float(r));
                s = s_num / (2.f * float(q - r));
                if (s > z[k]) break;
                if (--k < 0) { k = 0; break; }
            }
            ++k;
            v[k]     = q;
            z[k]     = s;
            z[k + 1] = EDT_INF;
        }

        k = 0;
        for (int q = 0; q < n; ++q) {
            while (z[k + 1] < float(q)) ++k;
            float diff = float(q - v[k]);
            d[q] = diff * diff + f[v[k]];
        }
        // Copy scratch back into f (cannot std::move — caller reuses d).
        for (int i = 0; i < n; ++i) f[i] = d[i];
    }
}

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
    , dirRes(8)
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
    , useTemporalAccum(true)
    , temporalAlpha(0.05f)           // Phase 14b: 0.10→0.05 (slower EMA for smoother convergence with 8-tap Halton)
    , useProbeJitter(true)
    , currentProbeJitter(0.0f)
    , probeJitterIndex(0)
    , probeJitterScale(0.06f)         // Phase 14a: 0.25→0.12→0.06
    , jitterPatternSize(8)            // Phase 14b: 4→8 (8-tap Halton(2,3,5) for better low-discrepancy coverage)
    , jitterHoldFrames(2)             // 8 positions × 2 hold = 16-frame cycle
    , jitterHoldCounter(0)
    , temporalRebuildCount(0)
    , useHistoryClamp(true)
    , historyNeedsSeed(false)
    , renderFrameIndex(0)
    , staggerMaxInterval(8)
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
    , lightPosition(0.0f, 0.8f, 0.0f)   // Step 4 (4b ext): default = Cornell-Box-tuned position
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
    , giFBO(0)
    , giDirectTex(0)
    , giGBufferTex(0)
    , giIndirectTex(0)
    , giLastW(0)
    , giLastH(0)
    , useGIBlur(true)
    , giBlurRadius(8)
    , giBlurDepthSigma(0.05f)
    , giBlurNormalSigma(0.2f)
    , giBlurLumSigma(0.4f)           // Phase 13b: stops within-plane tonal blur
    , c0MinRange(1.0f)               // Phase 14b: C0 tMax min (wu); 0=legacy cellSize=0.125
    , c1MinRange(1.0f)               // Phase 14c: C1 tMax min (wu); 0=legacy 0.5wu
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

    // Phase 6b: load RenderDoc in-process API (no-op if RenderDoc not installed)
    initRenderDoc();

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
    // Step 8: sdf_3d.comp re-enabled (was disabled in the Step 7 cleanup).
    // Step 9: voxelize.comp re-enabled, rewritten as a 3-pass GPU triangle
    // voxelizer (init / atomicMin owner-index / resolve owner->color).
    loadShader("sdf_3d.comp");        // Step 8: GPU JFA SDF
    loadShader("voxelize.comp");      // Step 9: GPU triangle voxelizer
    loadShader("sdf_analytic.comp");  // Phase 0: Analytic SDF shader
    loadShader("radiance_3d.comp");
    loadShader("reduction_3d.comp");    // Phase 5b-1: atlas → isotropic reduction
    loadShader("temporal_blend.comp"); // Phase 9: temporal probe accumulation
    loadShader("inject_radiance.comp");
    loadShader("sdf_debug.frag");     // Phase 0: SDF debug visualization (auto-loads .vert)
    loadShader("radiance_debug.frag"); // Phase 1: Radiance cascade debug (auto-loads .vert)
    loadShader("lighting_debug.frag"); // Phase 1: Lighting debug (auto-loads .vert)
    loadShader("raymarch.frag");       // Phase 1: Final raymarched image (auto-loads .vert)
    loadShader("gi_blur.frag");        // Phase 9c: Bilateral GI blur (auto-loads .vert)
    
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
    std::cout << "[Demo3D] SDF Debug View: Press F1 to toggle" << std::endl;
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
     *
     * Step 5 (codex 10 F4): split ImGui capture handling.
     * - Cleanup paths (mouse-look RELEASE / EnableCursor) run unconditionally
     *   so the cursor can't get stuck hidden if the mouse enters ImGui mid-drag.
     * - Mouse-look START gated on !WantCaptureMouse so dragging in UI doesn't
     *   start a camera look.
     * - Keyboard movement (WASD/QE/R) gated on !WantCaptureKeyboard only --
     *   hovering the UI doesn't freeze WASD; only typing in an input box does.
     * - Wheel events gated on !WantCaptureMouse (wheel is a mouse event).
     * - Debug hotkeys (F1/F/P/G + 1/2/3/M slice keys) keep the original
     *   "either capture blocks" behavior (they were never meant for mid-drag).
     */
    ImGuiIO& io = ImGui::GetIO();

    // -- Step 5 (5b cleanup, codex 10 F4): mouse-look RELEASE always runs.
    if (mouseDragging && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        mouseDragging = false;
        EnableCursor();
    }

    // ---------- Debug hotkeys (gated on both captures, original behavior) -----
    if (!io.WantCaptureMouse && !io.WantCaptureKeyboard) {
        // Phase 0: SDF Debug Controls (Step 5 codex 10 F5: rebound D -> F1 so D can strafe)
        if (IsKeyPressed(KEY_F1)) {
            showSDFDebug = !showSDFDebug;
            std::cout << "[Demo3D] SDF Debug View: " << (showSDFDebug ? "ON" : "OFF") << std::endl;
        }

        if (showSDFDebug) {
            if (IsKeyPressed(KEY_ONE))  { sdfSliceAxis = 0; std::cout << "[Demo3D] SDF Slice: X-axis (YZ plane)\n"; }
            if (IsKeyPressed(KEY_TWO))  { sdfSliceAxis = 1; std::cout << "[Demo3D] SDF Slice: Y-axis (XZ plane)\n"; }
            if (IsKeyPressed(KEY_THREE)){ sdfSliceAxis = 2; std::cout << "[Demo3D] SDF Slice: Z-axis (XY plane)\n"; }
            if (IsKeyPressed(KEY_M)) {
                sdfVisualizeMode = (sdfVisualizeMode + 1) % 4;
                const char* modes[] = {"Colorized SDF", "Surface Detection", "Gradient Magnitude", "Surface Normals"};
                std::cout << "[Demo3D] SDF Visualize Mode: " << modes[sdfVisualizeMode] << std::endl;
            }
            // SDF debug owns the wheel for slice scrubbing.
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

        // Phase 6b: G = GPU frame capture via RenderDoc
        if (IsKeyPressed(KEY_G)) {
#ifdef _WIN32
            if (rdoc) {
                pendingRdocCapture = true;
                std::cout << "[6b] RenderDoc capture queued for next frame." << std::endl;
            } else {
                std::cout << "[6b] RenderDoc not loaded -- capture unavailable." << std::endl;
            }
#else
            std::cout << "[6b] RenderDoc capture only supported on Windows." << std::endl;
#endif
        }
    }

    // ---------- Step 5 (5b body, codex 10 F4 + F6): mouse-look START + body ----
    // START gated on !WantCaptureMouse; body runs whenever dragging.
    if (!io.WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        mouseDragging = true;
        DisableCursor();
    }
    if (mouseDragging) {
        Vector2 md = GetMouseDelta();   // codex 10 F6: safer than absolute coords with cursor lock
        cameraYaw   += -md.x * camera.rotationSpeed;
        cameraPitch += -md.y * camera.rotationSpeed;
        cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);   // ~+/-85 deg

        // Reconstruct forward from yaw/pitch -- no cross-product, no singularity.
        glm::vec3 forward(
            std::cos(cameraPitch) * std::sin(cameraYaw),
            std::sin(cameraPitch),
            std::cos(cameraPitch) * std::cos(cameraYaw)
        );
        camera.target = camera.position + forward;
    }

    // ---------- Step 5 (5a + 5d): keyboard movement (codex 10 F4) ----
    // Blocked only by KEYBOARD capture (typing into ImGui), not by mouse hover.
    if (!io.WantCaptureKeyboard) {
        // 5a: WASD/QE strafe + Shift sprint.
        glm::vec3 forward = camera.target - camera.position;
        float fwdLenSq = glm::dot(forward, forward);
        if (fwdLenSq > 1e-12f) {
            forward /= std::sqrt(fwdLenSq);
        } else {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right   = glm::cross(forward, worldUp);
        float rightLenSq  = glm::dot(right, right);
        if (rightLenSq > 1e-6f) {
            right /= std::sqrt(rightLenSq);
        } else {
            right = glm::vec3(1.0f, 0.0f, 0.0f);   // pitch clamp guarantees this branch is unreachable
        }

        // Step 7+ pan: camera-local up = right x forward (perpendicular to view
        // plane, unlike worldUp which doesn't account for pitch). Used by Z/X
        // for screen-space up/down panning.
        glm::vec3 camUp = glm::cross(right, forward);
        float camUpLenSq = glm::dot(camUp, camUp);
        if (camUpLenSq > 1e-6f) camUp /= std::sqrt(camUpLenSq);
        else                    camUp = worldUp;

        float dt = GetFrameTime();
        float speed = camera.moveSpeed * dt;
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) speed *= 4.0f;

        glm::vec3 delta(0.0f);
        if (IsKeyDown(KEY_W)) delta += forward;
        if (IsKeyDown(KEY_S)) delta -= forward;
        if (IsKeyDown(KEY_A)) delta -= right;
        if (IsKeyDown(KEY_D)) delta += right;
        if (IsKeyDown(KEY_E)) delta += worldUp;
        if (IsKeyDown(KEY_Q)) delta -= worldUp;
        // Step 7+ pan up/down along camera-local up vector. Distinct from Q/E
        // (world-axis Y) -- when the camera is pitched, Z/X stay perpendicular
        // to the view direction so panning never drifts the target along the
        // forward axis.
        if (IsKeyDown(KEY_Z)) delta += camUp;
        if (IsKeyDown(KEY_X)) delta -= camUp;
        if (glm::length(delta) > 1e-6f) {
            delta = glm::normalize(delta) * speed;
            camera.position += delta;
            camera.target   += delta;
        }

        // 5d: R resets camera to scene preset (codex 11 F1: shared helper).
        if (IsKeyPressed(KEY_R)) {
            resetCameraToScenePreset();
            std::cout << "[Demo3D] Camera reset to scene preset (R key)\n";
        }
    }

    // ---------- Step 5 (5c): mouse wheel zoom + Ctrl-wheel FOV ----
    // Wheel is a mouse event; gate on !WantCaptureMouse so scrolling ImGui works.
    // Suppressed when SDF debug is active (debug owns wheel for slice scrubbing).
    if (!io.WantCaptureMouse && !showSDFDebug) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                camera.fovy = glm::clamp(camera.fovy - wheel * 2.0f, 20.0f, 110.0f);
            } else {
                glm::vec3 fwd = camera.target - camera.position;
                float lenSq = glm::dot(fwd, fwd);
                if (lenSq > 1e-12f) {
                    fwd /= std::sqrt(lenSq);
                    float zoomStep = wheel * camera.moveSpeed * 0.1f;
                    camera.position += fwd * zoomStep;
                    camera.target   += fwd * zoomStep;
                }
            }
        }
    }
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

    // -------------------------------------------------------------------------
    // Step 8 Phase 2c (codex 01 F1+F2 contract): dynamic-sphere overlay.
    //
    // Triple gate: sphere only fires when GPU SDF is on AND an OBJ is loaded
    // AND the user enabled the demo. Without GPU SDF, per-frame re-bake would
    // cost ~67ms CPU and tank framerate to ~4 fps; without an OBJ we have no
    // base layer to overlay onto.
    //
    // Each frame: copy static OBJ voxels back over voxelGridTexture, animate
    // sphere center, inject sphere voxels (one batched glTexSubImage3D), then
    // invalidate the SAME flags any other trigger uses so the render loop
    // re-bakes the SDF and re-runs ALL cascades with no temporal ghost.
    // -------------------------------------------------------------------------
    if (dynamicSphereEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture) {
        // 1. Restore static OBJ voxels.
        glCopyImageSubData(meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0, 0, 0,
                           voxelGridTexture,    GL_TEXTURE_3D, 0, 0, 0, 0,
                           volumeResolution, volumeResolution, volumeResolution);

        // 2. Animate sphere phase. CLI override (--sphere-time=X) snaps to a
        //    fixed value for deterministic capture (codex 01 F10).
        if (sphereTimeOverride < 0.0f) sphereTime += GetFrameTime() * sphereOrbitSpeed;
        else                            sphereTime  = sphereTimeOverride;

        const glm::vec3 center = (currentObjBmin + currentObjBmax) * 0.5f;
        const glm::vec3 size   = currentObjBmax - currentObjBmin;
        const float orbitR     = std::min(size.x, size.z) * 0.3f;
        const float radius     = size.y * 0.08f;
        dynamicSphereCenter = center + glm::vec3(
            orbitR * std::cos(sphereTime),
            size.y * 0.2f,
            orbitR * std::sin(sphereTime)
        );

        // 3. Overlay sphere (~9^3 voxels for the demo, 1 GL upload).
        addVoxelSphere(dynamicSphereCenter, radius, glm::vec3(1.0f, 0.4f, 0.1f));

        // 4. codex 01 F1+F2 / codex 02 F1: invalidate SAME flags any other
        // trigger uses. forceCascadeRebuild ENTERS the cascade pass; setting
        // renderFrameIndex=0 BYPASSES the per-cascade stagger interval test
        // (interval=1<<i; 0 % anything == 0 -> all cascades dispatch). Without
        // both, C1/C2/C3 stay 1/3/7 frames stale even though the SDF rebuilt.
        // Pattern matches the RenderDoc capture path (demo3d.cpp:4390).
        meshSDFReady        = false;   // sdfGenerationPass re-bakes via GPU JFA
        cascadeReady        = false;   // updateRadianceCascades runs
        forceCascadeRebuild = true;    // enters cascade pass even if cascadeReady
        renderFrameIndex    = 0;       // codex 02 F1: bypass stagger -> ALL cascades
        historyNeedsSeed    = true;    // alpha=1.0, no EMA ghost trail
    } else if (dynamicSphereWasEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture) {
        // codex 02 F2: dynamic sphere just turned OFF. Restore the static
        // base voxels so the previously injected sphere doesn't linger in
        // voxelGridTexture / sdfTexture / cascades. Same invalidation set as
        // the active path so the next frame re-bakes the sphere-free SDF.
        glCopyImageSubData(meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0, 0, 0,
                           voxelGridTexture,    GL_TEXTURE_3D, 0, 0, 0, 0,
                           volumeResolution, volumeResolution, volumeResolution);
        meshSDFReady        = false;
        cascadeReady        = false;
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
        historyNeedsSeed    = true;
        std::cout << "[Demo3D] Dynamic sphere disabled: restored static OBJ base voxels\n";
    }
    // Track previous-frame state for the codex 02 F2 disable-cleanup branch.
    dynamicSphereWasEnabled = (dynamicSphereEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture);

    // Phase 6b: auto-capture after warm-up delay (--auto-rdoc mode)
    // Hides UI one frame before capture so the thumbnail shows the clean 3D scene.
#ifdef _WIN32
    if (autoRdocDelaySeconds > 0.0f && !autoRdocFired && rdoc) {
        if (time >= autoRdocDelaySeconds - 0.1f)
            skipUIRendering = true;  // hide ImGui one frame before capture
        if (time >= autoRdocDelaySeconds) {
            pendingRdocCapture = true;
            autoRdocFired = true;
            std::cout << "[6b] Auto-capture triggered (t=" << time << "s)\n";
        }
    }
#endif
}

void Demo3D::render() {
    /**
     * @brief Execute complete rendering pipeline
     */
    
    // Pass 1: Voxelization (if needed)
    // Step 8 (codex 01 F1): sdfReady + cascadeReady are now Demo3D members
    // (was render-local statics). Promoted so toggles + dynamic-sphere
    // update can invalidate the same flag the render loop checks.
    if (sceneDirty) {
        double t0 = GetTime();
        voxelizationPass();
        voxelizationTimeMs = (GetTime() - t0) * 1000.0;
        sdfReady = false;
    }

    // Pass 2: SDF Generation (codex 01 F1: condition now also re-fires when
    // the OBJ mesh-SDF was invalidated externally -- e.g. GPU/CPU toggle
    // flipped, or dynamic-sphere update cleared meshSDFReady).
    static bool lastMergeFlag = false;
    if (!sdfReady || (useOBJMesh && !meshSDFReady)) {
        double t0 = GetTime();
        // codex 07 F1: only flip sdfReady on success. On mesh-bake failure,
        // sdfReady stays false → next frame retries the bake. cascadeReady is
        // also left untouched so we don't invalidate cascades on a failed bake.
        bool ok = sdfGenerationPass();
        sdfTimeMs = (GetTime() - t0) * 1000.0;
        if (ok) {
            sdfReady     = true;
            cascadeReady = false;  // SDF changed → cascade stale
        }
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
        // Bake output changed: old EMA history encodes the previous blend fraction.
        // Reset it so the new bake overwrites history at alpha=1 on the next rebuild
        // rather than taking 1/alpha frames to converge to the new blend.
        if (useTemporalAccum) {
            historyNeedsSeed = true;
            renderFrameIndex = 0;
        }
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
        if (useTemporalAccum) {
            cascadeReady = false;
            historyNeedsSeed = true;   // Phase 9b: seed before first blend
            probeJitterIndex = 0;      // reset Halton index
            temporalRebuildCount = 0;  // reset general rebuild counter
            renderFrameIndex = 0;      // Phase 10: align so renderFrameIndex=0 on first temporal frame,
                                       // guaranteeing all cascades rebuild (n%2^i==0 for all i when n=0)
                                       // before historyNeedsSeed is cleared.
        }
    }
    // Jitter position advance — runs here (before the cascade rebuild decision) so we
    // can set cascadeReady=false only when the position actually changes, not every frame.
    // updateRadianceCascades() uses currentProbeJitter as already set here.
    static bool lastProbeJitter = false;
    if (useProbeJitter != lastProbeJitter) {
        lastProbeJitter  = useProbeJitter;
        jitterHoldCounter = 0;
        if (!useProbeJitter) { currentProbeJitter = glm::vec3(0.0f); probeJitterIndex = 0; }
        cascadeReady = false;
    }
    if (useProbeJitter) {
        // Sample at current index first, then check whether to advance.
        currentProbeJitter = glm::vec3(
            (halton(probeJitterIndex, 2) - 0.5f) * probeJitterScale,
            (halton(probeJitterIndex, 3) - 0.5f) * probeJitterScale,
            (halton(probeJitterIndex, 5) - 0.5f) * probeJitterScale
        );
        ++jitterHoldCounter;
        if (jitterHoldCounter >= jitterHoldFrames) {
            jitterHoldCounter = 0;
            probeJitterIndex = (probeJitterIndex + 1) % static_cast<uint32_t>(jitterPatternSize);
            // Position changed: bake result differs → need a new EMA blend.
            if (useTemporalAccum)
                cascadeReady = false;
        }
    }

    // Pass 3: Radiance Cascades (only when SDF or merge flag changes, or forced by RenderDoc capture)
    static bool probeDumped = false;
    static int  readbackSkip = 0;
    // Sustain forceCascadeRebuild+renderFrameIndex=0 across TriggerCapture's 1-frame delay:
    // TriggerCapture() captures the NEXT frame, so we must sustain the flags for 2 frames.
    if (rdocForceRebuildCount > 0) {
        forceCascadeRebuild = true;
        renderFrameIndex    = 0;
    }
    if (!cascadeReady || forceCascadeRebuild) {
        forceCascadeRebuild = false;
        if (rdocForceRebuildCount > 0) --rdocForceRebuildCount;
        // Rate-limit atlas readback when jitter is active: the 256^2*32 glGetTexImage
        // at D=8 downloads ~134 MB and causes a full GPU sync every rebuild → 100 ms spike.
        // Without jitter every rebuild is scene-driven (rare), so always readback then.
        if (!useProbeJitter || ++readbackSkip >= 30) {
            probeDumped  = false;
            readbackSkip = 0;
        }
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

    // Auto-capture delay: starts a burst (default) or a sequence (--auto-sequence).
    {
        static bool autoCaptured = false;
        if (!autoCaptured
                && autoCaptureDelaySeconds > 0.0f
                && GetTime() > autoCaptureDelaySeconds
                && cascadeReady
                && burstState == BurstState::Idle
                && seqCapState == SeqCapState::Idle) {
            autoCaptured = true;
            if (autoSequencePending) {
                seqCapState   = SeqCapState::Capturing;
                seqFrameIndex = 0;
                seqPaths.clear();
                lastScreenshotPath.clear();
                std::cout << "[14a] Auto-sequence triggered at t=" << GetTime() << "s\n";
            } else {
                burstState = BurstState::CapM0;
                std::cout << "[12b] Auto-burst triggered at t=" << GetTime() << "s\n";
            }
        }
    }

    // Phase 12b: burst state machine — runs before raymarchPass() so mode is set this frame
    {
        auto abortBurst = [&](const char* reason) {
            std::cerr << "[12b] Burst aborted: " << reason << "\n";
            raymarchRenderMode = savedRenderMode;
            burstState         = BurstState::Idle;
            lastScreenshotPath.clear();
        };

        if (burstState == BurstState::CapM0) {
            savedRenderMode      = raymarchRenderMode;
            lastScreenshotPath.clear();          // clear so failed write leaves it empty
            pendingStatsDump     = true;         // write stats JSON alongside _m0
            pendingScreenshotTag = "_m0";
            pendingScreenshot    = true;
            raymarchRenderMode   = 0;
            burstState           = BurstState::CapM3;

        } else if (burstState == BurstState::CapM3) {
            if (lastScreenshotPath.empty() ||
                    lastScreenshotPath.find("_m0.png") == std::string::npos) {
                abortBurst("_m0 write failed");
            } else {
                burstPaths[0]        = lastScreenshotPath;
                lastScreenshotPath.clear();
                pendingScreenshotTag = "_m3";
                pendingScreenshot    = true;
                raymarchRenderMode   = 3;
                burstState           = BurstState::CapM6;
            }

        } else if (burstState == BurstState::CapM6) {
            if (lastScreenshotPath.empty() ||
                    lastScreenshotPath.find("_m3.png") == std::string::npos) {
                abortBurst("_m3 write failed");
            } else {
                burstPaths[1]        = lastScreenshotPath;
                lastScreenshotPath.clear();
                pendingScreenshotTag = "_m6";
                pendingScreenshot    = true;
                raymarchRenderMode   = 6;
                burstState           = BurstState::Analyze;
            }

        } else if (burstState == BurstState::Analyze) {
            if (lastScreenshotPath.empty() ||
                    lastScreenshotPath.find("_m6.png") == std::string::npos) {
                abortBurst("_m6 write failed");
            } else {
                burstPaths[2]      = lastScreenshotPath;
                raymarchRenderMode = savedRenderMode;
                burstState         = BurstState::Idle;
                launchBurstAnalysis();
            }
        }
    }

    // Phase 14a: multi-frame sequence capture — temporal jitter stability analysis.
    // Captures seqFrameCount consecutive frames of the current render mode,
    // then sends the full sequence to Claude to identify per-frame flickering.
    // Runs only when burst is idle so lastScreenshotPath is unambiguous.
    if (burstState == BurstState::Idle) {
        auto abortSeq = [&](const std::string& reason) {
            std::cerr << "[14a] Sequence aborted: " << reason << "\n";
            seqCapState = SeqCapState::Idle;
            seqPaths.clear();
            seqFrameIndex = 0;
            lastScreenshotPath.clear();
        };

        if (seqCapState == SeqCapState::Capturing) {
            // Step 1: collect the screenshot from the previous tick (if any).
            if (seqFrameIndex > 0) {
                std::string tag = "_f" + std::to_string(seqFrameIndex - 1) + ".png";
                if (lastScreenshotPath.empty() ||
                        lastScreenshotPath.find(tag) == std::string::npos) {
                    abortSeq("_f" + std::to_string(seqFrameIndex - 1) + " write failed");
                } else {
                    seqPaths.push_back(lastScreenshotPath);
                    lastScreenshotPath.clear();
                }
            }

            // Step 2: request the next frame or finish.
            if (seqCapState == SeqCapState::Capturing) {
                if (seqFrameIndex < seqFrameCount) {
                    if (seqFrameIndex == 0)
                        pendingStatsDump = true;  // capture stats with first frame
                    pendingScreenshotTag = "_f" + std::to_string(seqFrameIndex);
                    pendingScreenshot    = true;
                    seqFrameIndex++;
                } else {
                    // All seqFrameCount frames collected — launch analysis.
                    seqCapState   = SeqCapState::Idle;
                    seqFrameIndex = 0;
                    launchSequenceAnalysis();
                }
            }
        }
    }

    // Pass 4: Raymarching (+ optional bilateral GI blur)
    {
        double t0 = GetTime();
        raymarchPass();
        if (useGIBlur && (raymarchRenderMode == 0 || raymarchRenderMode == 3 || raymarchRenderMode == 6)) giBlurPass();
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
    ImGui::Text("  [F1] Toggle debug view");
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

// =============================================================================
// Step 2: Mesh SDF bake — Felzenszwalb separable EDT on CPU.
// Reads meshVoxelData (RGBA8, alpha = surface marker), writes sdfTexture (R32F)
// and propagates albedo into albedoTexture (RGBA8). Produces a CONSERVATIVE UDF:
// after subtracting one half-voxel-diagonal radius and clamping at 0, the result
// reads ~0 across the surface band so the existing shader hit thresholds
// (EPSILON in raymarch.frag, 0.002 in radiance_3d.comp) can land on it, and it
// never overestimates true triangle distance — sphere-trace safe.
// =============================================================================
bool Demo3D::generateMeshSDF() {
    // codex 07 F1 test hook — fail synthetically to exercise the render-loop retry path.
    if (injectBakeFailures > 0) {
        --injectBakeFailures;
        std::cerr << "[INJECT] generateMeshSDF: synthetic failure ("
                  << injectBakeFailures << " remaining)\n";
        return false;
    }
    // codex 04 F5: drain pre-existing GL errors so the post-upload
    // glGetError check below attributes errors to THIS bake only.
    // (Without this, a stale error from voxelize.comp's compile or a
    // prior pipeline stage falsely fails the first-frame CPU EDT bake
    // -- visible as `[ERROR] generateMeshSDF: sdfTexture upload failed
    // (GL 0x501)` then a successful retry on frame 2.)
    while (glGetError() != GL_NO_ERROR) { /* drain */ }

    const int   N        = volumeResolution;
    const int   N2       = N * N;
    const int   N3       = N * N * N;
    const float voxelSz  = volumeSize.x / float(N);

    // Validate input
    if (meshVoxelData.size() != size_t(N3) * 4) {
        std::cerr << "[ERROR] generateMeshSDF: meshVoxelData size " << meshVoxelData.size()
                  << " != expected " << (size_t(N3) * 4) << " (N=" << N << ")\n";
        return false;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Seed grid: 0 at occupied (alpha > 0), INF elsewhere.
    std::vector<float> sq(N3, EDT_INF);
    int seedCount = 0;
    for (int i = 0; i < N3; ++i) {
        if (meshVoxelData[i * 4 + 3] > 0) {
            sq[i] = 0.f;
            ++seedCount;
        }
    }
    if (seedCount == 0) {
        std::cerr << "[ERROR] generateMeshSDF: zero seeds — voxelization produced no surface voxels\n";
        return false;
    }

    // codex 08 F7 — confirm the halfExtent margin keeps the boundary slice empty.
    // If any of the 6 boundary slices contains seeds, surface voxels touch the
    // volume edge and trilinear sampling near the boundary will read into a
    // 1-voxel-thick "wall" of zero distance. Bump halfExtent margin if non-zero.
    {
        int boundarySeeds = 0;
        const int N1 = N - 1;
        for (int z = 0; z < N; ++z)
        for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            if (x == 0 || x == N1 || y == 0 || y == N1 || z == 0 || z == N1) {
                if (meshVoxelData[(z*N2 + y*N + x) * 4 + 3] > 0) ++boundarySeeds;
            }
        }
        std::cout << "[Demo3D] Boundary-slice surface seeds: " << boundarySeeds
                  << " (target=0; >0 means surface voxels touch volume edge)\n";
        if (boundarySeeds > 0) {
            std::cerr << "[WARN] " << boundarySeeds
                      << " surface seeds on volume boundary; consider larger halfExtent margin\n";
        }
    }

    // 2-4. Three separable axis sweeps. All scratch (rowBuf + edt1d v/z/d)
    //      preallocated once — zero heap allocations across 49,152 row sweeps at N=128.
    std::vector<float> rowBuf(N);
    std::vector<int>   scratchV(N);
    std::vector<float> scratchZ(N + 1);
    std::vector<float> scratchD(N);
    for (int z = 0; z < N; ++z)
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) rowBuf[x] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, N, scratchV, scratchZ, scratchD);
        for (int x = 0; x < N; ++x) sq[z*N2 + y*N + x] = rowBuf[x];
    }
    for (int z = 0; z < N; ++z)
    for (int x = 0; x < N; ++x) {
        for (int y = 0; y < N; ++y) rowBuf[y] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, N, scratchV, scratchZ, scratchD);
        for (int y = 0; y < N; ++y) sq[z*N2 + y*N + x] = rowBuf[y];
    }
    for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
        for (int z = 0; z < N; ++z) rowBuf[z] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, N, scratchV, scratchZ, scratchD);
        for (int z = 0; z < N; ++z) sq[z*N2 + y*N + x] = rowBuf[z];
    }

    auto t1 = std::chrono::high_resolution_clock::now();

    // 5. Convert squared-voxel distances → conservative world-space UDF.
    //    Subtract half-diagonal so the band is hittable by existing thresholds
    //    and never overestimates after the subtraction.
    const float surfaceRadius = voxelSz * std::sqrt(3.0f) * 0.5f;
    std::vector<float> sdfData(N3);
    for (int i = 0; i < N3; ++i) {
        float d = std::sqrt(sq[i]) * voxelSz - surfaceRadius;
        sdfData[i] = d > 0.0f ? d : 0.0f;
    }
    if (!std::isfinite(sdfData[0]) || !std::isfinite(sdfData[N3 / 2]) || !std::isfinite(sdfData[N3 - 1])) {
        std::cerr << "[ERROR] generateMeshSDF: non-finite SDF values\n";
        return false;
    }

    // 6. Albedo flood-fill: 3-iter 6-neighbor dilation. Fills the conservative band
    //    + one ring beyond, so band-region trilinear samples don't blend into black.
    //    Far-interior voxels stay black — never sampled because rays terminate first.
    std::vector<uint8_t> albedoData = meshVoxelData;
    {
        std::vector<uint8_t> next(albedoData.size());
        const int off[6] = { -4, +4, -N*4, +N*4, -N2*4, +N2*4 };
        for (int iter = 0; iter < 3; ++iter) {
            next = albedoData;
            for (int z = 1; z < N - 1; ++z)
            for (int y = 1; y < N - 1; ++y)
            for (int x = 1; x < N - 1; ++x) {
                int i = (z*N2 + y*N + x) * 4;
                if (albedoData[i + 3] != 0) continue;     // already has color
                for (int n = 0; n < 6; ++n) {
                    int j = i + off[n];
                    if (albedoData[j + 3] != 0) {
                        next[i + 0] = albedoData[j + 0];
                        next[i + 1] = albedoData[j + 1];
                        next[i + 2] = albedoData[j + 2];
                        next[i + 3] = 1;                  // mark as filled (not seed)
                        break;
                    }
                }
            }
            albedoData.swap(next);
        }
    }

    auto t2 = std::chrono::high_resolution_clock::now();

    // 7. Upload sdfTexture (R32F).
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RED, GL_FLOAT, sdfData.data());
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDF: sdfTexture upload failed (GL 0x"
                  << std::hex << err << std::dec << ")\n";
        glBindTexture(GL_TEXTURE_3D, 0);
        return false;
    }

    // 8. Upload propagated albedoTexture (RGBA8).
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RGBA, GL_UNSIGNED_BYTE, albedoData.data());
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDF: albedoTexture upload failed (GL 0x"
                  << std::hex << err << std::dec << ")\n";
        glBindTexture(GL_TEXTURE_3D, 0);
        return false;
    }
    glBindTexture(GL_TEXTURE_3D, 0);

    double edtMs    = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double albedoMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "[Demo3D] Mesh SDF: EDT complete N=" << N
              << " voxelSz=" << voxelSz << "m"
              << " surfaceRadius=" << surfaceRadius << "m"
              << " seeds=" << seedCount
              << " edt=" << edtMs << "ms albedo=" << albedoMs << "ms\n";
    return true;
}

// =============================================================================
// Step 8 (codex 01 F1/F6/F7/F8): GPU JFA equivalent of generateMeshSDF.
// =============================================================================
bool Demo3D::generateMeshSDFGPU() {
    // codex 02 F4: validate inputs before issuing any GL work so a missing
    // shader / null texture / lost context returns false (lets the render
    // loop's existing meshSDFReady-stays-false retry path take over).
    auto sit = shaders.find("sdf_3d.comp");
    if (sit == shaders.end() || sit->second == 0) {
        std::cerr << "[ERROR] generateMeshSDFGPU: sdf_3d.comp not loaded\n";
        return false;
    }
    GLuint prog = sit->second;
    if (!voxelGridTexture || !voronoiTextureA || !voronoiTextureB ||
        !sdfTexture || !albedoTexture) {
        std::cerr << "[ERROR] generateMeshSDFGPU: required texture handle is 0 ("
                  << "voxel=" << voxelGridTexture
                  << " voronoiA=" << voronoiTextureA
                  << " voronoiB=" << voronoiTextureB
                  << " sdf=" << sdfTexture
                  << " albedo=" << albedoTexture << ")\n";
        return false;
    }

    // Drain any pre-existing GL error so the post-dispatch check below
    // attributes errors to THIS call only.
    while (glGetError() != GL_NO_ERROR) { /* drain */ }

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "GPU JFA SDF");

    // codex 01 F8: real GPU-side timing via GL_TIME_ELAPSED query.
    GLuint timer = 0;
    glGenQueries(1, &timer);
    glBeginQuery(GL_TIME_ELAPSED, timer);

    auto cpuT0 = std::chrono::high_resolution_clock::now();
    const int N  = volumeResolution;
    const int wg = (N + 7) / 8;

    glUseProgram(prog);
    GLint uPassLoc = glGetUniformLocation(prog, "uPass");
    GLint uStepLoc = glGetUniformLocation(prog, "uStepSize");
    GLint uVoxLoc  = glGetUniformLocation(prog, "uVoxelSizeWorld");

    // Pass 0: init Voronoi from voxelGridTexture (binding=0 R, binding=1 W).
    glUniform1i(uPassLoc, 0);
    glBindImageTexture(0, voxelGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(1, voronoiTextureA,  0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(wg, wg, wg);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 1: log2(N) JFA steps with ping-pong (binding=2 R, binding=1 W; swap each pass).
    GLuint readTex = voronoiTextureA, writeTex = voronoiTextureB;
    glUniform1i(uPassLoc, 1);
    int passCount = 0;
    for (int step = N / 2; step >= 1; step /= 2) {
        glUniform1i(uStepLoc, step);
        glBindImageTexture(2, readTex,  0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA32F);
        glBindImageTexture(1, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(wg, wg, wg);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        std::swap(readTex, writeTex);
        ++passCount;
    }

    // Pass 2: finalize -> sdfTexture + albedoTexture (with conservative band).
    const float voxelSize = volumeSize.x / float(N);
    glUniform1i(uPassLoc, 2);
    glUniform1f(uVoxLoc, voxelSize);
    glBindImageTexture(2, readTex,         0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA32F);
    glBindImageTexture(0, voxelGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(3, sdfTexture,      0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
    glBindImageTexture(4, albedoTexture,   0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(wg, wg, wg);

    // codex 01 F6: texture-fetch + image-access barriers BOTH needed before
    // raymarch.frag / radiance_3d.comp sample sdf/albedo as sampler3D.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    glEndQuery(GL_TIME_ELAPSED);
    glPopDebugGroup();

    // codex 02 F4: check for GL errors accumulated during the dispatch
    // sequence (bad bindings, lost context, etc). Report and return false so
    // the render loop honors the failure (sdfReady stays false next frame).
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDFGPU: GL error 0x" << std::hex << err
                  << std::dec << " during JFA dispatch sequence\n";
        glDeleteQueries(1, &timer);
        return false;
    }

    float cpuMs = std::chrono::duration<float, std::milli>(
                    std::chrono::high_resolution_clock::now() - cpuT0).count();
    GLuint64 gpuNs = 0;
    glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &gpuNs);
    glDeleteQueries(1, &timer);
    float gpuMs = gpuNs * 1.0e-6f;

    std::cout << "[Demo3D] GPU JFA SDF: GPU=" << gpuMs << "ms"
              << "  CPU-submit=" << cpuMs << "ms"
              << "  (N=" << N << ", 1 init + " << passCount << " steps + 1 finalize)\n";
    return true;
}

// =============================================================================
// Step 9 Phase 3 (codex 03 F4-F7): GPU triangle voxelizer.
// =============================================================================
bool Demo3D::voxelizeOBJ_GPU() {
    auto sit = shaders.find("voxelize.comp");
    if (sit == shaders.end() || sit->second == 0) {
        std::cerr << "[ERROR] voxelizeOBJ_GPU: voxelize.comp not loaded\n";
        return false;
    }
    GLuint prog = sit->second;
    if (!voxelGridTexture || !voxelOwnerTexture || !triangleSSBO) {
        std::cerr << "[ERROR] voxelizeOBJ_GPU: required handle is 0 ("
                  << "grid=" << voxelGridTexture
                  << " owner=" << voxelOwnerTexture
                  << " ssbo=" << triangleSSBO << ")\n";
        return false;
    }

    // Build flat triangle list using the same per-face material lookup as
    // CPU voxelize() (codex 03 F4 reuse).
    auto buildT0 = std::chrono::high_resolution_clock::now();
    std::vector<GPUTriangle> tris;
    objLoader.buildTriangles(tris);
    const size_t numTris = tris.size();
    if (numTris == 0) {
        std::cerr << "[ERROR] voxelizeOBJ_GPU: OBJ has zero triangles\n";
        return false;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 GLsizeiptr(numTris * sizeof(GPUTriangle)),
                 tris.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    float ssboMs = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - buildT0).count();

    // Drain pre-existing GL errors (Step 8 codex 02 F4 pattern).
    while (glGetError() != GL_NO_ERROR) { /* drain */ }

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "GPU triangle voxelize");

    GLuint timer = 0;
    glGenQueries(1, &timer);
    glBeginQuery(GL_TIME_ELAPSED, timer);

    auto cpuT0 = std::chrono::high_resolution_clock::now();
    const int N = volumeResolution;
    const int wgVox = (N + 7) / 8;                            // per-voxel passes (8x8x8 = 512 threads per WG)
    const int wgTri = static_cast<int>((numTris + 511) / 512); // per-triangle pass uses flat index 512/WG
    const float voxelHalfDiag = 0.5f * std::sqrt(3.0f) * (volumeSize.x / float(N));

    glUseProgram(prog);
    GLint uPassLoc       = glGetUniformLocation(prog, "uPass");
    GLint uNumTrisLoc    = glGetUniformLocation(prog, "uNumTriangles");
    GLint uVolOriginLoc  = glGetUniformLocation(prog, "uVolumeOrigin");
    GLint uVolSizeLoc    = glGetUniformLocation(prog, "uVolumeSize");
    GLint uVolDimLoc     = glGetUniformLocation(prog, "uVolumeDim");
    GLint uHalfDiagLoc   = glGetUniformLocation(prog, "uVoxelHalfDiag");
    // codex 04 F6: validate uniform locations -- glUniform*(-1, ...) is
    // silently ignored by GL spec, which would mask a renamed/removed
    // uniform with empty/incorrect output AND glGetError success.
    if (uPassLoc == -1 || uNumTrisLoc == -1 || uVolOriginLoc == -1 ||
        uVolSizeLoc == -1 || uVolDimLoc == -1 || uHalfDiagLoc == -1) {
        std::cerr << "[ERROR] voxelizeOBJ_GPU: missing uniform location ("
                  << "uPass=" << uPassLoc
                  << " uNumTriangles=" << uNumTrisLoc
                  << " uVolumeOrigin=" << uVolOriginLoc
                  << " uVolumeSize=" << uVolSizeLoc
                  << " uVolumeDim=" << uVolDimLoc
                  << " uVoxelHalfDiag=" << uHalfDiagLoc
                  << ") -- shader contract changed?\n";
        glDeleteQueries(1, &timer);
        glPopDebugGroup();
        return false;
    }

    glUniform1i(uNumTrisLoc, int(numTris));
    glUniform3f(uVolOriginLoc, volumeOrigin.x, volumeOrigin.y, volumeOrigin.z);
    glUniform3f(uVolSizeLoc,   volumeSize.x,   volumeSize.y,   volumeSize.z);
    glUniform3i(uVolDimLoc,    N, N, N);
    glUniform1f(uHalfDiagLoc,  voxelHalfDiag);

    // Pass 0: init owner + voxel grid.
    glUniform1i(uPassLoc, 0);
    glBindImageTexture(1, voxelOwnerTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
    glBindImageTexture(2, voxelGridTexture,  0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(wgVox, wgVox, wgVox);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 1: per-triangle atomicMin into owner.
    glUniform1i(uPassLoc, 1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
    glBindImageTexture(1, voxelOwnerTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
    glDispatchCompute(wgTri, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 2: resolve owner -> RGBA8 voxel grid.
    glUniform1i(uPassLoc, 2);
    glBindImageTexture(1, voxelOwnerTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_R32UI);
    glBindImageTexture(2, voxelGridTexture,  0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(wgVox, wgVox, wgVox);

    // codex 03 F7: full barrier set covering downstream
    //   - glCopyImageSubData voxelGridTexture -> meshVoxelBaseTexture (TEXTURE_UPDATE)
    //   - glGetTexImage readback for cache populate (TEXTURE_UPDATE)
    //   - sampler3D fetches from raymarch.frag / radiance_3d.comp (TEXTURE_FETCH)
    //   - imageLoad in generateMeshSDFGPU init pass (SHADER_IMAGE_ACCESS)
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                  | GL_TEXTURE_UPDATE_BARRIER_BIT
                  | GL_TEXTURE_FETCH_BARRIER_BIT);

    glEndQuery(GL_TIME_ELAPSED);
    glPopDebugGroup();

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[ERROR] voxelizeOBJ_GPU: GL error 0x" << std::hex << err
                  << std::dec << "\n";
        glDeleteQueries(1, &timer);
        return false;
    }

    float cpuMs = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - cpuT0).count();
    GLuint64 gpuNs = 0;
    glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &gpuNs);
    glDeleteQueries(1, &timer);
    float gpuMs = gpuNs * 1.0e-6f;

    std::cout << "[Demo3D] GPU voxelize: GPU=" << gpuMs << "ms"
              << "  CPU-submit=" << cpuMs << "ms"
              << "  ssbo-build=" << ssboMs << "ms"
              << "  (" << numTris << " tris -> N=" << N << ")\n";

    // Mirror to base texture so dynamic-sphere overlay still has its base layer.
    if (meshVoxelBaseTexture) {
        glCopyImageSubData(voxelGridTexture,    GL_TEXTURE_3D, 0, 0,0,0,
                           meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0,0,0,
                           N, N, N);
    }

    // codex 03 F1: signal sdfGenerationPass that the OBJ branch can run
    // without requiring meshVoxelData (CPU mirror).
    gpuVoxelGridReady = true;
    return true;
}

bool Demo3D::sdfGenerationPass() {
    /**
     * @brief Generate 3D signed distance field
     *
     * Returns true on success, false on mesh-bake failure (codex 07 F1).
     * Caller (render loop) only flips its `sdfReady` flag on true, so a
     * failed bake retries on the next frame instead of locking the renderer
     * into stale/partial texture state.
     *
     * Phase 0 implementation: Use analytic SDF for quick validation.
     * Future: Replace with voxel-based JFA when mesh loading is ready.
     * Step 3 (3b): added OBJ mesh branch at the top — bakes via generateMeshSDF()
     * and bypasses the analytic compute dispatch.
     */

    // --- Step 3 (3b, F1): OBJ mesh branch ---
    // Step 8: branch on useGPUSDF -- CPU EDT (default) or GPU JFA.
    // Step 9 (codex 03 F1): predicate now also accepts gpuVoxelGridReady
    // so the GPU/GPU path (no CPU meshVoxelData mirror) doesn't fall through
    // to the analytic SDF branch.
    if (useOBJMesh && (!meshVoxelData.empty() || gpuVoxelGridReady)) {
        if (!meshSDFReady) {
            // CPU EDT requires meshVoxelData; if the user picked CPU EDT
            // but only the GPU mirror exists, refuse rather than crash.
            if (!useGPUSDF && meshVoxelData.empty()) {
                std::cerr << "[ERROR] sdfGenerationPass: CPU EDT requires "
                             "meshVoxelData but it's empty (GPU/CPU combo "
                             "without readback). Toggle GPU SDF or restart "
                             "with CPU voxelizer.\n";
                return false;
            }
            bool ok = useGPUSDF ? generateMeshSDFGPU() : generateMeshSDF();
            if (!ok) {
                std::cerr << "[ERROR] sdfGenerationPass: mesh SDF bake failed (path="
                          << (useGPUSDF ? "GPU" : "CPU")
                          << "); render loop keeps sdfReady=false and retries next frame\n";
                return false;   // codex 07 F1: tell render loop NOT to flip sdfReady
            }
            meshSDFReady = true;
        }
        return true;   // analytic path never runs while OBJ is active
    }

    if (analyticSDFEnabled) {
        std::cout << "[Demo3D] Generating analytic SDF..." << std::endl;
        
        // Check if we have a valid shader
        auto it = shaders.find("sdf_analytic.comp");
        if (it == shaders.end()) {
            std::cerr << "[ERROR] Analytic SDF shader not loaded!" << std::endl;
            return false;
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
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "sdf_analytic");
        glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glPopDebugGroup();

        std::cout << "[Demo3D] Analytic SDF generation complete." << std::endl;
    } else {
        std::cout << "[Demo3D] SDF generation skipped (analytic SDF disabled, JFA not implemented)" << std::endl;

        // TODO: Implement full 3D JFA when ready
        // For now, just clear the SDF texture
        glBindTexture(GL_TEXTURE_3D, sdfTexture);
        glClearTexImage(sdfTexture, 0, GL_RED, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    return true;
}

void Demo3D::updateRadianceCascades() {
    // Phase 9b: Halton(2,3,5) low-discrepancy jitter.
    // Phase 11: wrap at jitterPatternSize (repeating N-tap coverage) + dwell for jitterHoldFrames
    // before advancing. probeJitterScale controls amplitude (default ±0.25 cell).
    // currentProbeJitter, probeJitterIndex, and jitterHoldCounter are managed in
    // update() before this call, so that the rebuild decision can be tied to position changes.

    // Coarse→fine: each level reads the already-written level above it for misses.
    // Phase 10: staggered updates — cascade i rebuilds every min(2^i, staggerMaxInterval) frames.
    // Coarser cascades change slowly; staleness over a few frames is visually negligible.
    for (int i = cascadeCount - 1; i >= 0; --i) {
        if (!cascades[i].active) continue;
        int interval = std::min(1 << i, staggerMaxInterval);
        if ((renderFrameIndex % interval) != 0) continue;
        updateSingleCascade(i);
    }
    ++renderFrameIndex;
    historyNeedsSeed = false;  // Phase 9b: cleared after all cascades seeded this rebuild
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
    // Phase 14c: per-cascade minimum ray reach (wu); 0 = legacy formula.
    float cnMinRange = (cascadeIndex == 0) ? c0MinRange
                     : (cascadeIndex == 1) ? c1MinRange
                     : 0.0f;
    glUniform1f(glGetUniformLocation(prog, "uCnMinRange"), cnMinRange);

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
    glUniform3fv(glGetUniformLocation(prog, "uLightPos"),  1, glm::value_ptr(lightPosition));
    glUniform3f(glGetUniformLocation(prog, "uLightColor"), 1.0f, 0.95f, 0.85f);
    glUniform1i(glGetUniformLocation(prog, "uUseEnvFill"), useEnvFill ? 1 : 0);
    glUniform3fv(glGetUniformLocation(prog, "uSkyColor"),  1, glm::value_ptr(skyColor));
    glUniform1f(glGetUniformLocation(prog, "uBlendFraction"), blendFraction);
    // 5i: soft shadow in bake shader
    glUniform1i(glGetUniformLocation(prog, "uUseSoftShadowBake"), useSoftShadowBake ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "uSoftShadowK"),        softShadowK);
    // Phase 9: probe jitter — offsets probe world positions by ±0.5 cell for temporal supersampling
    glUniform3fv(glGetUniformLocation(prog, "uProbeJitter"), 1, glm::value_ptr(currentProbeJitter));

    // Phase 10: fused atlas EMA — determine early so upper cascade binding can use same flag.
    auto tb = shaders.find("temporal_blend.comp");
    const bool doFusedEMA = useTemporalAccum && tb != shaders.end() &&
                            c.probeAtlasHistory != 0 && c.probeGridHistory != 0;
    const float fusedAlpha = historyNeedsSeed ? 1.0f : temporalAlpha;
    glUniform1i(glGetUniformLocation(prog, "uTemporalActive"), doFusedEMA ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "uTemporalAlpha"),  fusedAlpha);
    glUniform1i(glGetUniformLocation(prog, "uClampHistory"),   useHistoryClamp ? 1 : 0);
    if (doFusedEMA)
        glBindImageTexture(1, c.probeAtlasHistory, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);

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
        // Phase 10: when fused EMA is active, probeAtlasHistory holds the accumulated (fresh) atlas
        // after the handle swap from the upper cascade's last update. Read it as the canonical atlas.
        GLuint upperAtlas = (doFusedEMA && cascades[upperIdx].probeAtlasHistory != 0)
                            ? cascades[upperIdx].probeAtlasHistory
                            : cascades[upperIdx].probeAtlasTexture;
        GLuint upperGrid  = (doFusedEMA && cascades[upperIdx].probeGridHistory != 0)
                            ? cascades[upperIdx].probeGridHistory
                            : cascades[upperIdx].probeGridTexture;
        // Unit 2: directional atlas (Phase 5c texelFetch path)
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, upperAtlas);
        glUniform1i(glGetUniformLocation(prog, "uUpperCascadeAtlas"), 2);
        // Unit 3: isotropic probeGridTexture (Phase 4 fallback when toggle is OFF)
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, upperGrid);
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
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "radiance_3d");
    glDispatchCompute(wg.x, wg.y, wg.z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glPopDebugGroup();

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

        // Phase 10: local_size changed to 8x8x4=256 threads; use matching workgroup counts.
        glm::ivec3 wgRed((c.resolution + 7) / 8, (c.resolution + 7) / 8, (c.resolution + 3) / 4);
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "reduction_3d");
        glDispatchCompute(wgRed.x, wgRed.y, wgRed.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glPopDebugGroup();
    }

    // Phase 10: temporal history update — two paths depending on fused EMA flag.
    if (doFusedEMA) {
        // Fused path: bake already wrote EMA-blended result into probeAtlasTexture.
        // Reduction wrote isotropic average of that into probeGridTexture.
        // Swap handles so display (and next-frame upper cascade reads) use the fresh data
        // via probeAtlasHistory / probeGridHistory — no temporal_blend.comp dispatches needed.
        // historyNeedsSeed is handled automatically: fusedAlpha=1.0 → mix(stale,bake,1)=bake.
        std::swap(c.probeAtlasTexture, c.probeAtlasHistory);
        std::swap(c.probeGridTexture,  c.probeGridHistory);
        if (cascadeIndex == 0) ++temporalRebuildCount;
    } else if (useTemporalAccum && tb != shaders.end() &&
               c.probeAtlasHistory != 0 && c.probeGridHistory != 0) {
        // Non-fused fallback path (temporal ON but fused unavailable): original temporal_blend.comp.
        // Phase 9b: seed history = current on warm-up to eliminate dark startup.
        int D    = cascadeDirRes[cascadeIndex];
        int axyz = c.resolution * D;
        if (historyNeedsSeed) {
            glCopyImageSubData(c.probeAtlasTexture, GL_TEXTURE_3D, 0, 0, 0, 0,
                               c.probeAtlasHistory,  GL_TEXTURE_3D, 0, 0, 0, 0,
                               axyz, axyz, c.resolution);
            glCopyImageSubData(c.probeGridTexture, GL_TEXTURE_3D, 0, 0, 0, 0,
                               c.probeGridHistory,  GL_TEXTURE_3D, 0, 0, 0, 0,
                               c.resolution, c.resolution, c.resolution);
        }

        GLuint tbProg = tb->second;
        glUseProgram(tbProg);
        glUniform1f(glGetUniformLocation(tbProg, "uAlpha"),        temporalAlpha);
        glUniform1i(glGetUniformLocation(tbProg, "uClampHistory"), useHistoryClamp ? 1 : 0);

        // Blend directional atlas — uDirRes = D (probe tile stride in atlas texels)
        glUniform3i(glGetUniformLocation(tbProg, "uSize"),   axyz, axyz, c.resolution);
        glUniform1i(glGetUniformLocation(tbProg, "uDirRes"), D);
        glBindImageTexture(0, c.probeAtlasHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(1, c.probeAtlasTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
        glm::ivec3 wgA = calculateWorkGroups(axyz, axyz, c.resolution, 4);
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "temporal_blend");
        glDispatchCompute(wgA.x, wgA.y, wgA.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glPopDebugGroup();

        // Blend isotropic grid — uDirRes = 0 (cardinal 3D neighborhood)
        glUniform3i(glGetUniformLocation(tbProg, "uSize"),   c.resolution, c.resolution, c.resolution);
        glUniform1i(glGetUniformLocation(tbProg, "uDirRes"), 0);
        glBindImageTexture(0, c.probeGridHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(1, c.probeGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "temporal_blend");
        glDispatchCompute(wg.x, wg.y, wg.z);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glPopDebugGroup();

        if (cascadeIndex == 0) ++temporalRebuildCount;
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
    glm::vec3 lightPos = lightPosition;   // Step 4 (4b ext): per-scene light from member
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
    //
    // Phase 9c: always set probe grid bounds so mode 8 (probe cell boundary visualization)
    // works regardless of whether the directional atlas is active.
    {
        glm::ivec3 probeGridRes(cascadeC0Res);
        glUniform3iv(glGetUniformLocation(prog, "uAtlasVolumeSize"), 1, glm::value_ptr(probeGridRes));
        glUniform3fv(glGetUniformLocation(prog, "uAtlasGridOrigin"), 1, glm::value_ptr(volumeOrigin));
        glUniform3fv(glGetUniformLocation(prog, "uAtlasGridSize"),   1, glm::value_ptr(volumeSize));
    }
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
        glUniform1i(glGetUniformLocation(prog, "uAtlasDirRes"),      cascadeDirRes[selC]);
        atlasAvailable = true;
    }
    glUniform1i(glGetUniformLocation(prog, "uUseDirectionalGI"), (useDirectionalGI && atlasAvailable) ? 1 : 0);

    // GI blur: redirect mode-0/3/6 render to 3-attachment FBO (direct / gbuffer / indirect).
    // Modes 3 and 6 are pure-indirect views so direct=black and blur applies to full output.
    // Other debug modes go directly to the default framebuffer and are unaffected.
    const bool giBlurActive = useGIBlur && (raymarchRenderMode == 0 || raymarchRenderMode == 3 || raymarchRenderMode == 6);
    glUniform1i(glGetUniformLocation(prog, "uSeparateGI"), giBlurActive ? 1 : 0);

    if (giBlurActive) {
        int w = GetScreenWidth(), h = GetScreenHeight();
        if (w != giLastW || h != giLastH) initGIBlur(w, h);
        if (giFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, giFBO);
            GLenum drawBufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
            glDrawBuffers(3, drawBufs);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    // Reuse the existing fullscreen quad VAO
    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "raymarch");
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glPopDebugGroup();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);

    // Restore default framebuffer; giBlurPass() will composite to screen in render()
    if (giBlurActive && giFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
    }
}

void Demo3D::initGIBlur(int w, int h) {
    destroyGIBlur();

    glGenFramebuffers(1, &giFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, giFBO);

    auto makeColorTex = [](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    // [0] = linear direct lighting (from raymarch.frag location=0 when uSeparateGI=1)
    makeColorTex(giDirectTex,   w, h);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, giDirectTex,   0);

    // [1] = GBuffer: normal*0.5+0.5 (rgb), linearDepth (a). Sky = a=0.
    makeColorTex(giGBufferTex,  w, h);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, giGBufferTex,  0);

    // [2] = linear indirect/GI (from raymarch.frag location=2 when uSeparateGI=1)
    makeColorTex(giIndirectTex, w, h);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, giIndirectTex, 0);

    GLenum drawBufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, drawBufs);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[GIBlur] FBO incomplete at " << w << "x" << h << "\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    giLastW = w;
    giLastH = h;
}

void Demo3D::destroyGIBlur() {
    if (giFBO)         { glDeleteFramebuffers(1, &giFBO);        giFBO = 0; }
    if (giDirectTex)   { glDeleteTextures(1, &giDirectTex);      giDirectTex = 0; }
    if (giGBufferTex)  { glDeleteTextures(1, &giGBufferTex);     giGBufferTex = 0; }
    if (giIndirectTex) { glDeleteTextures(1, &giIndirectTex);    giIndirectTex = 0; }
    giLastW = giLastH = 0;
}

void Demo3D::giBlurPass() {
    auto it = shaders.find("gi_blur.frag");
    if (it == shaders.end() || giFBO == 0 || giDirectTex == 0) return;

    GLuint prog = it->second;
    glUseProgram(prog);

    // unit 0: linear direct lighting
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, giDirectTex);
    glUniform1i(glGetUniformLocation(prog, "uDirectTex"), 0);

    // unit 1: GBuffer (normal+depth)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, giGBufferTex);
    glUniform1i(glGetUniformLocation(prog, "uGBufferTex"), 1);

    // unit 2: linear indirect/GI (only this buffer is blurred)
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, giIndirectTex);
    glUniform1i(glGetUniformLocation(prog, "uIndirectTex"), 2);

    glUniform1i(glGetUniformLocation(prog, "uBlurRadius"),  giBlurRadius);
    glUniform1f(glGetUniformLocation(prog, "uDepthSigma"),  giBlurDepthSigma);
    glUniform1f(glGetUniformLocation(prog, "uNormalSigma"), giBlurNormalSigma);
    glUniform1f(glGetUniformLocation(prog, "uLumSigma"),    giBlurLumSigma);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, GetScreenWidth(), GetScreenHeight());

    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "gi_blur");
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glPopDebugGroup();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    
    // Phase 6b: label program for RenderDoc dispatch identification
    glObjectLabel(GL_PROGRAM, program, -1, shaderName.c_str());

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

    // Step 8/9: see startup-load comment.
    loadShader("sdf_3d.comp");
    loadShader("voxelize.comp");
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

    // Step 8 (codex 01 F9): GPU JFA Voronoi ping-pong + static-OBJ voxel cache.
    voronoiTextureA = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr
    );
    voronoiTextureB = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr
    );
    meshVoxelBaseTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );
    if (!voronoiTextureA || !voronoiTextureB || !meshVoxelBaseTexture) {
        std::cerr << "[ERROR] createVolumeBuffers: failed to allocate Step 8 GPU JFA textures\n";
    }

    // Step 9 Phase 3 (codex 03 F5): R32UI owner-index texture + triangle SSBO.
    voxelOwnerTexture = gl::createTexture3D(
        volumeResolution, volumeResolution, volumeResolution,
        GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr
    );
    if (!voxelOwnerTexture) {
        std::cerr << "[ERROR] createVolumeBuffers: failed to allocate Step 9 voxelOwnerTexture\n";
    }
    glGenBuffers(1, &triangleSSBO);   // empty until first GPU voxelize

    // Phase 6b: label volume textures for RenderDoc resource identification
    glObjectLabel(GL_TEXTURE, voxelGridTexture,        -1, "voxelGridTexture");
    glObjectLabel(GL_TEXTURE, sdfTexture,              -1, "sdfTexture");
    glObjectLabel(GL_TEXTURE, albedoTexture,           -1, "albedoTexture");
    glObjectLabel(GL_TEXTURE, directLightingTexture,   -1, "directLightingTexture");
    glObjectLabel(GL_TEXTURE, prevFrameTexture,        -1, "prevFrameTexture");
    glObjectLabel(GL_TEXTURE, currentRadianceTexture,  -1, "currentRadianceTexture");
    glObjectLabel(GL_TEXTURE, voronoiTextureA,         -1, "voronoiTextureA");
    glObjectLabel(GL_TEXTURE, voronoiTextureB,         -1, "voronoiTextureB");
    glObjectLabel(GL_TEXTURE, meshVoxelBaseTexture,    -1, "meshVoxelBaseTexture");
    if (voxelOwnerTexture) glObjectLabel(GL_TEXTURE, voxelOwnerTexture, -1, "voxelOwnerTexture");
    if (triangleSSBO)      glObjectLabel(GL_BUFFER,  triangleSSBO,      -1, "triangleSSBO");

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
    // Step 8 (codex 01 F9): cleanup Step 8 textures.
    if (voronoiTextureA)      glDeleteTextures(1, &voronoiTextureA);
    if (voronoiTextureB)      glDeleteTextures(1, &voronoiTextureB);
    if (meshVoxelBaseTexture) glDeleteTextures(1, &meshVoxelBaseTexture);
    // Step 9 Phase 3 (codex 03 F9): cleanup Step 9 voxelizer resources.
    if (voxelOwnerTexture)    glDeleteTextures(1, &voxelOwnerTexture);
    if (triangleSSBO)         glDeleteBuffers(1, &triangleSSBO);
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

        // Phase 6b: label cascade textures for RenderDoc identification
        std::string pfx = "cascade" + std::to_string(i);
        glObjectLabel(GL_TEXTURE, cascades[i].probeAtlasTexture,  -1, (pfx + "_probeAtlas").c_str());
        glObjectLabel(GL_TEXTURE, cascades[i].probeAtlasHistory,  -1, (pfx + "_probeAtlasHistory").c_str());
        glObjectLabel(GL_TEXTURE, cascades[i].probeGridTexture,   -1, (pfx + "_probeGrid").c_str());
        glObjectLabel(GL_TEXTURE, cascades[i].probeGridHistory,   -1, (pfx + "_probeGridHistory").c_str());

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
    useOBJMesh = false;
    currentOBJPath.clear();

    // Step 3 (3c): clear mesh state — no implied cache, the only caller is loadOBJMesh
    // which always re-reads the file. shrink_to_fit reclaims the ~8 MB voxel buffer.
    meshVoxelData.clear();
    meshVoxelData.shrink_to_fit();
    meshSDFReady = false;

    // Step 3 (3c, F2): scene-switch invariant -- reseed temporal cascade history so the
    // previous scene's history doesn't EMA-blend into the new one. Without this, mode 0
    // can ghost the old scene's lighting into the new scene for many frames.
    historyNeedsSeed     = true;
    renderFrameIndex     = 0;
    temporalRebuildCount = 0;

    // Step 4 (codex 09 F1): reset lightPosition to the analytic-scene default.
    // Without this, switching from Sponza OBJ (light at (0, 0.5, 0)) into an
    // analytic Cornell scene leaks the Sponza light position into Cornell.
    lightPosition = glm::vec3(0.0f, 0.8f, 0.0f);
    std::cout << "[Demo3D] setScene(" << sceneType << "): lightPosition reset to ("
              << lightPosition.x << "," << lightPosition.y << "," << lightPosition.z << ")\n";

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
     * @brief Helper function to add a box of voxels.
     *
     * Step 9 follow-up (post codex 04): rewritten to mirror addVoxelSphere.
     * Two long-standing bugs are fixed:
     *   - Coord math: was `voxelSize = 1/N` and `gridOrigin = (0,0,0)`,
     *     which silently clipped negative-coord boxes (Cornell walls at
     *     x=-1.5 mapped to voxel -192 -> clamped to 0). The actual SDF
     *     volume is at volumeOrigin=(-2,-2,-2) volumeSize=(4,4,4); now
     *     uses `(world - volumeOrigin) / volumeSize * N` (matches the
     *     OBJ + sphere voxelizers).
     *   - Upload: was per-voxel glTexSubImage3D in a tight loop. For
     *     analytic Cornell that's ~525K GL calls = ~10 s scene-switch
     *     stall. Now builds a single sub-volume buffer and uploads with
     *     ONE glTexSubImage3D (~1 ms total).
     */
    const int N = volumeResolution;
    auto worldToVoxel = [&](const glm::vec3& w) {
        glm::vec3 norm = (w - volumeOrigin) / volumeSize;
        return glm::ivec3(norm * float(N));
    };
    glm::vec3 halfSize = size * 0.5f;
    glm::vec3 minPos = center - halfSize;
    glm::vec3 maxPos = center + halfSize;

    glm::ivec3 minV = worldToVoxel(minPos);
    glm::ivec3 maxV = worldToVoxel(maxPos);
    minV = glm::clamp(minV, glm::ivec3(0), glm::ivec3(N - 1));
    maxV = glm::clamp(maxV, glm::ivec3(0), glm::ivec3(N - 1));
    glm::ivec3 dim = maxV - minV + glm::ivec3(1);
    if (dim.x <= 0 || dim.y <= 0 || dim.z <= 0) return;

    // Build sub-volume on CPU. Boxes are solid volumes (every voxel inside
    // the bbox gets the box's color); the alpha=128 vs 255 distinction
    // (emissive) is preserved from the prior behavior.
    const uint8_t r = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    const uint8_t g = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    const uint8_t b = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    const uint8_t a = emissive ? 255 : 128;

    std::vector<uint8_t> sub(size_t(dim.x) * dim.y * dim.z * 4);
    for (size_t i = 0, n = sub.size() / 4; i < n; ++i) {
        sub[i * 4 + 0] = r;
        sub[i * 4 + 1] = g;
        sub[i * 4 + 2] = b;
        sub[i * 4 + 3] = a;
    }

    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, minV.x, minV.y, minV.z,
                    dim.x, dim.y, dim.z, GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}

// =============================================================================
// Step 8 Phase 2b (codex 01 F3 + F4): solid-sphere voxel rasterizer.
//   F3 -- correct world->voxel math via volumeOrigin/volumeSize/resolution
//         (NOT addVoxelBox's broken (0,0,0) origin assumption).
//   F4 -- ONE batched glTexSubImage3D call per invocation, not thousands.
// Used by the dynamic-sphere overlay path; safe to call every frame.
// =============================================================================
void Demo3D::addVoxelSphere(const glm::vec3& center, float radius, const glm::vec3& color) {
    const int N = volumeResolution;
    auto worldToVoxel = [&](const glm::vec3& w) {
        glm::vec3 norm = (w - volumeOrigin) / volumeSize;
        return glm::ivec3(norm * float(N));
    };
    glm::ivec3 minV = worldToVoxel(center - glm::vec3(radius));
    glm::ivec3 maxV = worldToVoxel(center + glm::vec3(radius));
    minV = glm::clamp(minV, glm::ivec3(0), glm::ivec3(N - 1));
    maxV = glm::clamp(maxV, glm::ivec3(0), glm::ivec3(N - 1));
    glm::ivec3 dim = maxV - minV + glm::ivec3(1);
    if (dim.x <= 0 || dim.y <= 0 || dim.z <= 0) return;

    std::vector<uint8_t> sub(size_t(dim.x) * dim.y * dim.z * 4, 0);
    // codex 02 F3: rasterize SURFACE BAND (one voxel-diagonal wide), not
    // solid fill. Solid-fill turned every interior voxel into a JFA seed,
    // which produced zero-gradient interiors and chunky silhouettes (the
    // GPU JFA finalizes distance-to-nearest-seed, so interior voxels read
    // as distance=0 -> no surface gradient inside the sphere). A surface
    // band matches what OBJLoader::voxelize produces for triangles.
    const glm::vec3 voxStep = volumeSize / float(N);
    const float halfDiag = 0.5f * std::sqrt(voxStep.x * voxStep.x +
                                            voxStep.y * voxStep.y +
                                            voxStep.z * voxStep.z);
    for (int z = 0; z < dim.z; ++z)
    for (int y = 0; y < dim.y; ++y)
    for (int x = 0; x < dim.x; ++x) {
        glm::ivec3 v(minV.x + x, minV.y + y, minV.z + z);
        glm::vec3 wp = volumeOrigin + (glm::vec3(v) + 0.5f) * voxStep;
        glm::vec3 d  = wp - center;
        // surface band: |length(d) - radius| <= halfDiag => voxel center
        // is within half a voxel-diagonal of the sphere's surface.
        float dist = std::sqrt(glm::dot(d, d));
        if (std::abs(dist - radius) <= halfDiag) {
            int i = ((z * dim.y + y) * dim.x + x) * 4;
            sub[i + 0] = uint8_t(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            sub[i + 1] = uint8_t(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            sub[i + 2] = uint8_t(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            sub[i + 3] = 255;
        }
    }

    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, minV.x, minV.y, minV.z,
                    dim.x, dim.y, dim.z, GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
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
        // codex 11 F1: scene-aware so Sponza button -> Sponza preset, not Cornell default.
        resetCameraToScenePreset();
        std::cout << "[Demo3D] Camera reset to scene preset (button)\n";
    }
    ImGui::SameLine();
    if (ImGui::Button("Screenshot [P]"))
        pendingScreenshot = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Save a PNG screenshot to the tools/ directory (same as pressing P).\n"
                          "Phase 6a: AI analysis script is also triggered if available.");
    ImGui::SameLine();
    if (ImGui::Button("Burst Capture##burst")) {
        if (burstState == BurstState::Idle)
            burstState = BurstState::CapM0;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Phase 12b: capture modes 0/3/6 over 3 frames, then\n"
                          "send all three images + probe stats to Claude in one call.\n"
                          "Produces frame_T.md in tools/.");
    ImGui::SameLine();
    if (ImGui::Button("Seq Capture##seq")) {
        if (burstState == BurstState::Idle && seqCapState == SeqCapState::Idle) {
            seqCapState   = SeqCapState::Capturing;
            seqFrameIndex = 0;
            seqPaths.clear();
            lastScreenshotPath.clear();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Phase 14a: capture N consecutive frames of the current render mode\n"
                          "to analyze temporal jitter stability. Sends all frames to Claude.\n"
                          "Set N with the 'Seq Frames' slider below.\n"
                          "Default N=8 = one full jitter cycle (4 positions x 2 hold frames).\n"
                          "Produces frame_T_seq.md in tools/.");
    ImGui::SliderInt("Seq Frames##seq", &seqFrameCount, 2, 32);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Number of consecutive frames to capture for sequence analysis.\n"
                          "8 = one full jitter cycle at jitterPatternSize=4, holdFrames=2.");
    ImGui::SliderFloat("Auto-capture delay (s)##ac", &autoCaptureDelaySeconds, 0.0f, 30.0f, "%.1f s");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Phase 12b: 0 = disabled.\n"
                          "After N seconds AND cascade is ready, triggers a burst capture\n"
                          "(modes 0, 3, 6 over 4 frames) + probe_stats JSON, then sends\n"
                          "all three images to Claude for multi-mode analysis.");
    if (!lastAnalysisPath.empty())
        ImGui::TextDisabled("Last analysis: %s", lastAnalysisPath.c_str());

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
    ImGui::RadioButton("ProbeCell (8)",   &raymarchRenderMode, 8);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Mode 8: probe cell boundary (fract of probe-grid coord as RGB).\nColor transitions occur at probe center positions; halfway = cell boundary.\nCompare with Mode 6 (GI-only): aligned banding = Type A (cell-size limited);\nmisaligned banding = Type B (directional D quantization).");

    // Step 3 (3d, F3): gate the analytic SDF toggle in OBJ mode. The analytic shader
    // path has nothing to evaluate against an OBJ mesh, so allowing it would render
    // empty/wrong. Disabled grays out the checkbox while useOBJMesh is true.
    ImGui::BeginDisabled(useOBJMesh);
    ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
    if (useOBJMesh) {
        ImGui::SameLine();
        ImGui::TextDisabled("(OBJ mode — uses grid SDF)");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("OFF (default): SDF read from 128^3 texture (trilinear, grid-quantized).\nON: SDF evaluated analytically per-sample — truly continuous, no voxel grid.\nDiagnostic: toggle in Mode 5 or 7. If banding disappears -> grid is the cause.\nIf banding stays -> it is the natural rectangular iso-contours of the Cornell Box.\n(Disabled in OBJ mode — analytic primitives don't apply to mesh geometry.)");

    ImGui::Separator();
    ImGui::Text("GI Bilateral Blur:");
    ImGui::Checkbox("Enable GI Blur##giblur", &useGIBlur);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Depth+normal aware bilateral filter applied to the GI indirect term only\n"
                          "(modes 0, 3, 6 — direct light is unaffected).\n"
                          "Reduces probe-grid spatial banding without blurring depth/normal edges.\n"
                          "Adjust Radius, DepthSigma, and NormalSigma to taste.");
    if (useGIBlur) {
        ImGui::SliderInt("Radius##giblur",         &giBlurRadius,       1, 8);
        ImGui::SliderFloat("DepthSigma##giblur",   &giBlurDepthSigma,   0.005f, 0.5f, "%.3f");
        ImGui::SliderFloat("NormalSigma##giblur",  &giBlurNormalSigma,  0.05f,  1.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("NormalSigma: cosine-distance threshold for normal edge preservation.\nLower = sharper edges preserved; higher = more blurring across normals.");
        ImGui::SliderFloat("LumSigma##giblur",     &giBlurLumSigma,     0.0f,   2.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Phase 13b: luminance edge-stop for within-plane tonal transitions.\n"
                              "Lower = stops blur at GI brightness boundaries on flat surfaces.\n"
                              "0.0 = disabled (same as pre-Phase-13).");
    }

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
        static const int kC0Options[] = { 8, 16, 24, 32, 48, 64 };
        static const char* kC0Labels[] = { "8^3  (fast, coarse)", "16^3", "24^3", "32^3 (default)", "48^3", "64^3  (slow)" };
        static const int kC0Count = 6;
        int curIdx = 3;
        for (int k = 0; k < kC0Count; ++k) if (kC0Options[k] == cascadeC0Res) { curIdx = k; break; }
        ImGui::Text("C0 probe resolution:");
        HelpMarker(
            "Sets the C0 probe grid resolution. All other cascades derive from this:\n"
            "  co-located:     all cascades use the same N^3 grid.\n"
            "  non-co-located: Ci uses (N>>i)^3, halving per level.\n\n"
            "Changing this sets baseInterval = volumeSize / N (C0 cell size).\n"
            "The full cascade hierarchy is rebuilt on change.\n\n"
            "Non-co-located minimum: N=32 (gives C3=4^3). N=8 gives C3=1^3 (degenerate).\n"
            "N=64 co-located uses ~340 MB VRAM with D scaling ON.");
        if (ImGui::Combo("##C0Res", &curIdx, kC0Labels, kC0Count))
            cascadeC0Res = kC0Options[curIdx];
        ImGui::SameLine();
        ImGui::TextDisabled("baseInterval=%.4fm", volumeSize.x / float(cascadeC0Res));
    }
    // Phase 14b/14c: per-cascade minimum ray reach
    ImGui::SliderFloat("C0 min range##c0mr", &c0MinRange, 0.0f, 2.0f, "%.2f wu");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Minimum C0 ray reach (wu). 0=legacy cellSize=0.125wu.\n"
                          "1.0 (default) raises C0 surfPct to ~98%% and eliminates\n"
                          "near-field hit/miss oscillation on colored walls.");
    ImGui::SliderFloat("C1 min range##c1mr", &c1MinRange, 0.0f, 4.0f, "%.2f wu");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Minimum C1 ray reach (wu). 0=legacy 0.5wu.\n"
                          "1.0 (default) raises C1 surfPct from 75%% to ~100%%\n"
                          "and eliminates outer-wall convergence drift.");

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

    // Phase 8: live dirRes — even values only (odd D is degenerate in octahedral encoding)
    {
        ImGui::Text("Dir resolution (D):");
        HelpMarker(
            "Octahedral directional bin count per probe: D*D bins total.\n"
            "Must be even — odd D produces degenerate bin centers on the octahedral fold.\n"
            "D=4 (default): 16 bins, coarse angular resolution.\n"
            "D=8: 64 bins, 4x finer — costs 4x in BOTH bake and display per frame.\n"
            "Phase 8 diagnostic: run E1 (toggle Directional GI) before raising D.");
        static const int kDirResOpts[] = { 2, 4, 6, 8 };
        for (int k = 0; k < 4; ++k) {
            if (k > 0) ImGui::SameLine();
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%d##dr%d", kDirResOpts[k], k);
            if (ImGui::RadioButton(lbl, dirRes == kDirResOpts[k]))
                dirRes = kDirResOpts[k];
        }
        ImGui::SameLine();
        ImGui::TextDisabled("D^2=%d bins/probe", dirRes * dirRes);
    }

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
        ImGui::Checkbox("History clamp (TAA-style)", &useHistoryClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Clamp history to the AABB of the current-frame probe neighborhood\n"
                "before blending. Rejects stale samples that fall outside the current\n"
                "lighting range — eliminates color bleeding from jitter ghost samples.\n"
                "Grid: 6-tap cardinal AABB. Atlas: same direction bin in adjacent probes.\n"
                "With clamping ON: use alpha 0.3-0.8 for fast, clean convergence.\n"
                "With clamping OFF: use alpha 0.05-0.1 to suppress ghosting manually.");
        ImGui::SliderFloat("Temporal alpha", &temporalAlpha, 0.01f, 1.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Blend weight for the current bake (0=keep history, 1=replace).\n"
                "With history clamp ON: 0.3-0.8 recommended (fast, ghost-free).\n"
                "With history clamp OFF: 0.05-0.1 recommended (suppresses ghosting).\n"
                "1.0 = no accumulation (same as pre-Phase-9).");
        ImGui::Checkbox("Probe jitter (requires temporal)", &useProbeJitter);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Offsets probe world positions by a Halton(2,3,5) low-discrepancy\n"
                "sub-cell jitter each dwell period. Combined with temporal accumulation,\n"
                "the running EMA integrates over a wider spatial footprint, reducing banding.\n"
                "Has no effect without temporal accumulation.\n\n"
                "Scale: amplitude in probe-cell units (0.25 = ±0.25 cell, default).\n"
                "Pattern N: wrap Halton at N. After N distinct positions the cycle repeats.\n"
                "Dwell: frames to hold each position before advancing (gives EMA time to settle).");
        if (useProbeJitter) {
            ImGui::Indent();
            ImGui::SliderFloat("Scale##jitter", &probeJitterScale, 0.05f, 0.5f, "%.2f cell");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(
                    "Jitter amplitude in probe-cell units.\n"
                    "0.25 (default) = ±0.25 cell — enough to cover banding without\n"
                    "large frame-to-frame position jumps that require very low alpha.\n"
                    "0.5 = ±0.5 cell (original, maximum cell coverage but more ghosting).");
            ImGui::SliderInt("Pattern N##jitter", &jitterPatternSize, 2, 32);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(
                    "Number of distinct Halton positions before the cycle repeats.\n"
                    "Default 8: indices 0-7 give good 3-D coverage of the probe cell.\n"
                    "Larger N = more spatial footprint but slower EMA convergence per cycle.\n"
                    "Recommended: set alpha ≈ 1/N for an unbiased N-tap running average.");
            ImGui::SliderInt("Dwell (frames/pos)##jitter", &jitterHoldFrames, 1, 8);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(
                    "Hold each jitter position for this many frames before advancing.\n"
                    "1 (default): advance every frame — fastest cycling, most temporal noise.\n"
                    "4-8: EMA has several frames to integrate each position before moving on,\n"
                    "reducing per-cycle flickering at the cost of slower banding suppression.");
            ImGui::Unindent();
        }
        // Phase 10: stagger control
        {
            ImGui::Text("Stagger max interval:");
            ImGui::SameLine();
            static const int kStaggerOpts[] = { 1, 2, 4, 8 };
            for (int s = 0; s < 4; ++s) {
                if (s > 0) ImGui::SameLine();
                char lbl[8]; snprintf(lbl, sizeof(lbl), "%d##st%d", kStaggerOpts[s], s);
                if (ImGui::RadioButton(lbl, staggerMaxInterval == kStaggerOpts[s]))
                    staggerMaxInterval = kStaggerOpts[s];
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(
                    "Max cascade update interval. Cascade i updates every min(2^i, max) frames.\n"
                    "1 = all cascades every frame (no stagger).\n"
                    "8 = C0 every frame, C1 every 2nd, C2 every 4th, C3 every 8th.\n"
                    "Reduces cascade cost ~2x. Use 1 for dynamic lighting scenes.");
        }
        // Phase 9b: debug readout — rebuild count, EMA fill heuristic, jitter vector
        {
            float fill = 1.0f - std::pow(1.0f - temporalAlpha, (float)temporalRebuildCount);
            ImGui::Text("Rebuilds: %u  EMA fill: %.0f%%", temporalRebuildCount, fill * 100.0f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(
                    "EMA fill = 1-(1-alpha)^N: settling heuristic assuming constant alpha.\n"
                    "Not a pixel-accurate history readout.");
            if (useProbeJitter)
                ImGui::TextDisabled("Jitter: (%.3f, %.3f, %.3f)",
                    currentProbeJitter.x, currentProbeJitter.y, currentProbeJitter.z);
        }
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
    
    // Step 5: full camera-control bullet list (codex 10 F5)
    ImGui::Text("Camera (when not in UI):");
    ImGui::BulletText("WASD: strafe");
    ImGui::BulletText("Q/E: down/up (world Y axis)");
    ImGui::BulletText("Z/X: pan up/down (camera-local up)");
    ImGui::BulletText("Shift: sprint x4");
    ImGui::BulletText("Right-click drag: look");
    ImGui::BulletText("Wheel: zoom; Ctrl+Wheel: FOV");
    ImGui::BulletText("R: reset camera to scene preset");
    ImGui::Text("Debug:");
    ImGui::BulletText("F1: Toggle SDF debug");
    ImGui::BulletText("F: Cycle radiance debug mode");
    ImGui::BulletText("P: Screenshot + AI analysis");
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

    if (ImGui::Button(useOBJMesh && currentOBJPath == "cornell" ? "[ACTIVE] Cornell Box (OBJ)" : "Cornell Box (OBJ)")) {
        if (loadOBJMesh("res/scene/cornell_box.obj")) {
            std::cout << "[Demo3D] Loaded real Cornell Box mesh from OBJ!" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load Cornell Box OBJ!" << std::endl;
        }
    }

    // Step 6: Cornell-Original variant -- ships its own .mtl with distinct
    // red/green/white wall colors and an emissive `light` material.
    if (ImGui::Button(useOBJMesh && currentOBJPath == "cornell_orig" ? "[ACTIVE] Cornell-Original (OBJ+MTL)" : "Cornell-Original (OBJ+MTL)")) {
        if (loadOBJMesh("res/scene/CornellBox-Original/CornellBox-Original.obj")) {
            std::cout << "[Demo3D] Loaded Cornell-Original (OBJ+MTL)!" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load Cornell-Original OBJ!" << std::endl;
        }
    }

    if (ImGui::Button(useOBJMesh && currentOBJPath == "sponza" ? "[ACTIVE] Sponza (OBJ)" : "Sponza (OBJ)")) {
        if (loadOBJMesh("res/scene/sponza.obj")) {
            std::cout << "[Demo3D] Loaded Sponza OBJ mesh!" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load Sponza OBJ!" << std::endl;
        }
    }

    // Step 6: Sponza-master variant -- denser mesh (262K faces), .mtl present
    // but textures not loaded -> uniform mid-gray (Sponza's Kd is 0.4704).
    if (ImGui::Button(useOBJMesh && currentOBJPath == "sponza_master" ? "[ACTIVE] Sponza-master (OBJ+MTL)" : "Sponza-master (OBJ+MTL, slow)")) {
        if (loadOBJMesh("res/scene/Sponza-master/sponza.obj")) {
            std::cout << "[Demo3D] Loaded Sponza-master (OBJ+MTL)!" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load Sponza-master OBJ!" << std::endl;
        }
    }

    ImGui::NewLine();
    {
        const char* names[] = { "Empty Room", "Cornell Box", "Simplified Sponza" };
        if (useOBJMesh) {
            // Step 6: 4-way label so the new variants show up correctly.
            const char* objName = "Cornell Box (OBJ)";
            if      (currentOBJPath == "sponza")        objName = "Sponza (OBJ)";
            else if (currentOBJPath == "sponza_master") objName = "Sponza-master (OBJ+MTL)";
            else if (currentOBJPath == "cornell_orig")  objName = "Cornell-Original (OBJ+MTL)";
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active: %s", objName);
        }
        else if (currentScene >= 0 && currentScene < 3)
            ImGui::Text("Active: %s", names[currentScene]);
    }

    // Step 8 (codex 01 F1): GPU/CPU SDF toggle. Flipping invalidates
    // meshSDFReady + cascadeReady so the next frame re-bakes through the
    // newly-selected path.
    ImGui::Separator();
    if (ImGui::Checkbox("GPU SDF (dynamic-friendly)", &useGPUSDF)) {
        meshSDFReady = false;
        cascadeReady = false;
        std::cout << "[Demo3D] Mesh SDF path: " << (useGPUSDF ? "GPU JFA" : "CPU EDT") << "\n";
    }

    // Step 9 Phase 3 (codex 03 F10): GPU voxelize toggle. Re-runs the
    // current OBJ through loadOBJMesh on flip so the user sees the effect
    // immediately (cache key is per-voxelizer-kind so both bakes coexist).
    if (ImGui::Checkbox("GPU voxelize (re-runs current OBJ)", &useGPUVoxelize)) {
        std::cout << "[Demo3D] Voxelizer: " << (useGPUVoxelize ? "GPU" : "CPU") << "\n";
        if (useOBJMesh && !currentOBJPath.empty()) {
            // The current OBJ was loaded under the OTHER voxelizer; reload
            // through the new path. Cache key per-kind means CPU and GPU
            // bakes coexist, so this is fast on the second click each way.
            std::string pathToReload =
                (currentOBJPath == "cornell")        ? "res/scene/cornell_box.obj" :
                (currentOBJPath == "cornell_orig")   ? "res/scene/CornellBox-Original/CornellBox-Original.obj" :
                (currentOBJPath == "sponza")         ? "res/scene/sponza.obj" :
                (currentOBJPath == "sponza_master")  ? "res/scene/Sponza-master/sponza.obj" : "";
            if (!pathToReload.empty()) {
                loadOBJMesh(pathToReload);
            }
        }
    }

    // Step 8 Phase 2d: dynamic sphere overlay (greyed out unless GPU+OBJ).
    ImGui::BeginDisabled(!useGPUSDF || !useOBJMesh);
    if (ImGui::Checkbox("Dynamic sphere overlay", &dynamicSphereEnabled)) {
        std::cout << "[Demo3D] Dynamic sphere overlay: "
                  << (dynamicSphereEnabled ? "ON" : "OFF") << "\n";
    }
    if (dynamicSphereEnabled) {
        ImGui::SliderFloat("Sphere orbit speed", &sphereOrbitSpeed, 0.1f, 5.0f);
    }
    ImGui::EndDisabled();
    if (!useGPUSDF || !useOBJMesh) {
        ImGui::TextDisabled("(requires GPU SDF + OBJ scene)");
    }

    ImGui::NewLine();
    
    // Debug Visualization Controls
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Debug Visualizations:");
    ImGui::Separator();
    
    if (ImGui::Button(showSDFDebug ? "[ON] SDF Debug (F1)" : "[OFF] SDF Debug (F1)")) {
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

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 6 (complete):");
    // 6a: screenshot + AI analysis
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [6a] Screenshot  [P] key or Settings button -> tools/  (AI analysis optional)");
    }
    // 6b: RenderDoc capture (doc only, no runtime toggle)
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [6b] RenderDoc GPU capture workflow (see doc; no runtime UI)");
    }

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 7 (complete):");
    // 7: analytic SDF, new render modes, banding analysis
    {
        ImVec4 c = useAnalyticRaymarch ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
        ImGui::TextColored(c, "  [7]  Analytic SDF toggle  %s  (Settings panel)",
            useAnalyticRaymarch ? "(ON — continuous)" : "(OFF — grid texture)");
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [7]  Mode 7: ray distance heatmap  Mode 8: probe-cell boundary");
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [7]  SDF quantization & banding analysis complete (see phase7 docs)");
    }

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 8 (complete):");
    // 8: live dirRes slider
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [8]  Live D slider (D=%d -> D^2=%d rays/probe)  (Cascades panel)",
            dirRes, dirRes * dirRes);
    }

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 9 (complete):");
    // 9a: temporal accumulation
    {
        ImVec4 c = useTemporalAccum ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
        ImGui::TextColored(c, "  [9a] Temporal accumulation  %s  alpha=%.2f  (Cascades panel)",
            useTemporalAccum ? "ON" : "OFF", temporalAlpha);
    }
    // 9b: history clamp + probe jitter
    {
        ImVec4 cClamp  = useHistoryClamp ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
        ImVec4 cJitter = useProbeJitter  ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
        ImGui::TextColored(cClamp,  "  [9b] History clamp (TAA)  %s", useHistoryClamp ? "ON" : "OFF");
        ImGui::SameLine();
        if (useProbeJitter)
            ImGui::TextColored(cJitter, "  Probe jitter  ON  scale=%.2f  N=%d  dwell=%d",
                probeJitterScale, jitterPatternSize, jitterHoldFrames);
        else
            ImGui::TextColored(cJitter, "  Probe jitter  OFF");
    }
    // 9c/9d: GI blur
    {
        ImVec4 c = useGIBlur ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
        ImGui::TextColored(c, "  [9c/9d] Bilateral GI blur  %s  r=%d  (Settings panel, modes 0/3/6)",
            useGIBlur ? "ON" : "OFF", giBlurRadius);
    }

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Phase 10 (complete):");
    // 10: staggered cascade updates + fused atlas EMA
    {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [10] Staggered cascade updates  interval=%d  (Cascades panel -> Temporal)",
            staggerMaxInterval);
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [10] Fused atlas EMA in bake shader  (eliminates atlas temporal_blend dispatch)");
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
            "  [10] Reduction workgroup 4x4x4 -> 8x8x4 (better GPU occupancy)");
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

// =============================================================================
// Phase 6b — RenderDoc in-process capture
// =============================================================================

void Demo3D::initRenderDoc() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    // Walk up from exe to find project root (same logic as initToolsPaths)
    fs::path root = fs::weakly_canonical(fs::path(GetApplicationDirectory()));
    for (int i = 0; i < 4; i++) {
        if (fs::exists(root / "doc")) break;
        root = root.parent_path();
    }
    rdocCaptureDir  = (root / "tools" / "captures").string();
    rdocAnalysisDir = (root / "tools" / "analysis").string();

    // rdoc_load_api() is in rdoc_helper.cpp (isolated from raylib.h / winuser.h)
    if (!rdoc_load_api(&rdoc)) {
        std::cout << "[6b] RenderDoc DLL not found or API init failed — GPU capture disabled.\n";
        rdoc = nullptr;
        return;
    }
    fs::create_directories(rdocCaptureDir);
    fs::create_directories(rdocAnalysisDir);
    std::string capTemplate = rdocCaptureDir + "/rdoc_frame";
    rdoc->SetCaptureFilePathTemplate(capTemplate.c_str());
    rdoc->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    std::cout << "[6b] RenderDoc in-process API loaded OK. Press G to capture.\n";
#endif
}

void Demo3D::beginRdocFrameIfPending() {
#ifdef _WIN32
    // TriggerCapture() captures the next presented frame (next SwapBuffers),
    // which includes both update() cascade dispatches AND render() passes.
    // More robust than Start/EndFrameCapture(null,null): that API fails when
    // called before BeginDrawing() because RenderDoc needs a window association.
    if (pendingRdocCapture && rdoc && !rdocCaptureWaiting) {
        rdocCaptureCountBefore  = rdoc->GetNumCaptures();
        forceCascadeRebuild     = true;  // ensure cascades dispatch in this frame
        rdocForceRebuildCount   = 2;     // sustain for 2 frames: this one + the captured frame
        renderFrameIndex        = 0;     // reset stagger so ALL cascades run (0 % any == 0)
        rdoc->TriggerCapture();
        pendingRdocCapture  = false;
        rdocCaptureWaiting  = true;
        std::cout << "[6b] TriggerCapture() called (captures next frame).\n";
    }
#endif
}

void Demo3D::endRdocFrameIfPending() {
#ifdef _WIN32
    if (!rdocCaptureWaiting || !rdoc) return;
    // Poll — capture appears after the next SwapBuffers; may take 1-2 frames.
    uint32_t n = rdoc->GetNumCaptures();
    if (n <= rdocCaptureCountBefore) return;  // not ready yet
    rdocCaptureWaiting = false;

    char capPath[512]; uint32_t pathLen = sizeof(capPath); uint64_t ts;
    if (rdoc->GetCapture(n - 1, capPath, &pathLen, &ts)) {
        std::string path(capPath, pathLen > 0 ? pathLen - 1 : 0);
        std::cout << "[6b] Capture saved: " << path << std::endl;
        launchRdocAnalysis(path);
    }
#endif
}

void Demo3D::launchRdocAnalysis(const std::string& capturePath) {
    namespace fs = std::filesystem;
    // Use absolute path — qrenderdoc.exe runs from its install dir so relative paths resolve there.
    std::string outDir = fs::absolute(fs::path(rdocAnalysisDir)).string();
    fs::create_directories(fs::path(rdocAnalysisDir));
    std::string toolsDir = fs::path(toolsScript).parent_path().string();
    std::string extractPath = (fs::path(toolsDir) / "rdoc_extract.py").string();
    std::string analyzePath = (fs::path(toolsDir) / "analyze_renderdoc.py").string();

    // Two-step pipeline (both inherit RDOC_CAPTURE / RDOC_OUTDIR env vars):
    //   Step 1: qrenderdoc.exe --py rdoc_extract.py
    //           Opens the .rdc via the renderdoc Python API, extracts texture slices
    //           and GPU timing to <outDir>/<stem>_manifest.json + <stem>_<tex>.png.
    //   Step 2: python analyze_renderdoc.py
    //           Reads the manifest, calls Claude for visual analysis, writes _pipeline.md.
    //
    // qrenderdoc.exe embeds Python 3.6 (has renderdoc module, no anthropic).
    // Regular Python has anthropic (no renderdoc module).
    // sys.exit(0) in rdoc_extract.py prevents qrenderdoc's Qt GUI from opening.
    std::thread([capturePath, outDir, extractPath, analyzePath]() {
        std::string envPrefix =
            "set \"RDOC_CAPTURE=" + capturePath + "\""
            " && set \"RDOC_OUTDIR=" + outDir + "\"";

        // Step 1: extract via qrenderdoc (blocks until sys.exit(0) in rdoc_extract.py)
        std::string extractCmd =
            envPrefix
            + " && \"C:/Program Files/RenderDoc/qrenderdoc.exe\" --py \"" + extractPath + "\"";
        std::cout << "[6b] Running rdoc_extract.py via qrenderdoc...\n";
        int r1 = system(extractCmd.c_str());
        if (r1 != 0)
            std::cerr << "[6b] rdoc_extract.py failed (exit " << r1 << ") — timing/textures skipped\n";

        // Step 2: analyze with Claude (reads manifest written by step 1)
        std::string analyzeCmd =
            envPrefix
            + " && python \"" + analyzePath + "\"";
        std::cout << "[6b] Running analyze_renderdoc.py...\n";
        int r2 = system(analyzeCmd.c_str());
        if (r2 != 0)
            std::cerr << "[6b] analyze_renderdoc.py failed (exit " << r2 << ")\n";
    }).detach();
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

    // Phase 12b: consume tag and clear lastScreenshotPath before attempting write.
    // Clearing here ensures a failed write leaves lastScreenshotPath empty so the
    // burst state machine can detect the failure on the next frame.
    std::string tag = pendingScreenshotTag;
    pendingScreenshotTag.clear();
    lastScreenshotPath.clear();

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string stem     = "frame_" + std::to_string(now) + tag;  // e.g. frame_T_m0
    std::string filename = stem + ".png";
    std::string path     = screenshotDir + "/" + filename;

    if (!stbi_write_png(path.c_str(), w, h, 3, pixels.data(), w * 3)) {
        std::cerr << "[6a] Screenshot write failed: " << path << std::endl;
        pendingStatsDump = false;
        return;
    }
    std::cout << "[6a] Screenshot saved: " << path << std::endl;
    lastScreenshotPath = path;  // Phase 12b: burst state machine reads this next frame

    // Phase 12a: write probe stats JSON alongside the screenshot.
    // statsToPass is local — it is empty for plain screenshots and only populated here,
    // preventing statsPathForAnalysis (a member) from leaking into future P-key captures.
    std::string statsToPass;
    if (pendingStatsDump) {
        pendingStatsDump = false;
        statsPathForAnalysis.clear();  // clear before write; failed write leaves it empty
        std::ostringstream j;
        j << "{\n";
        j << "  \"dirRes\": "           << dirRes          << ",\n";
        j << "  \"cascadeCount\": "     << cascadeCount    << ",\n";
        j << "  \"temporalAlpha\": "    << temporalAlpha   << ",\n";
        j << "  \"probeJitterScale\": " << probeJitterScale << ",\n";
        j << "  \"jitterPatternSize\": "<< jitterPatternSize<< ",\n";
        j << "  \"jitterHoldFrames\": " << jitterHoldFrames << ",\n";
        j << "  \"c0MinRange\": "       << c0MinRange       << ",\n";
        j << "  \"c1MinRange\": "       << c1MinRange       << ",\n";
        j << "  \"baseRes\": "          << cascadeC0Res     << ",\n";
        j << "  \"volumeSize\": "       << volumeSize.x     << ",\n";
        j << "  \"cascadeTimeMs\": "    << cascadeTimeMs    << ",\n";
        j << "  \"raymarchTimeMs\": "   << raymarchTimeMs   << ",\n";
        j << "  \"cascades\": [\n";
        for (int ci = 0; ci < cascadeCount; ++ci) {
            int tot = probeTotalPerCascade[ci]; if (tot < 1) tot = 1;
            j << "    {\n";
            j << "      \"anyPct\": "  << (100.f * probeNonZero[ci]    / tot) << ",\n";
            j << "      \"surfPct\": " << (100.f * probeSurfaceHit[ci] / tot) << ",\n";
            j << "      \"skyPct\": "  << (100.f * probeSkyHit[ci]     / tot) << ",\n";
            j << "      \"meanLum\": " << probeMeanLum[ci]  << ",\n";
            j << "      \"maxLum\": "  << probeMaxLum[ci]   << ",\n";
            j << "      \"variance\": "<< probeVariance[ci] << "\n";
            j << "    }";
            if (ci < cascadeCount - 1) j << ",";
            j << "\n";
        }
        j << "  ]\n}\n";

        std::string statsPath = screenshotDir + "/probe_stats_" + std::to_string(now) + ".json";
        std::ofstream sf(statsPath);
        if (sf) {
            sf << j.str();
            statsToPass          = statsPath;
            statsPathForAnalysis = statsPath;  // member kept for UI display only
            std::cout << "[12a] Probe stats written: " << statsPath << std::endl;
        } else {
            std::cerr << "[12a] Failed to write stats: " << statsPath << std::endl;
        }
    }

    // During burst or sequence capture, suppress the single-image P-key analysis.
    // Burst: burstState advanced beyond Idle before render() returns (safe guard).
    // Sequence: seqCapState == Capturing while frames are being collected.
    if (launchAiAnalysis && burstState == BurstState::Idle && seqCapState == SeqCapState::Idle)
        launchAnalysis(path, statsToPass);  // statsToPass="" for plain P-key screenshots
}

void Demo3D::launchAnalysis(const std::string& imagePath, const std::string& statsPath) {
    namespace fs = std::filesystem;
    std::string stem = fs::path(imagePath).stem().string();
    lastAnalysisPath = analysisDir + "/" + stem + ".md";

    std::string cmd = "python \"" + toolsScript + "\" \""
                    + imagePath + "\" \""
                    + analysisDir + "\"";
    if (!statsPath.empty())
        cmd += " \"" + statsPath + "\"";

    if (autoCloseAfterCapture) {
        // --auto-analyze mode: run synchronously so the process stays alive for the full
        // API call, then signal the main loop to exit cleanly.
        std::cout << "[12a] Running analysis synchronously (auto-close mode)...\n";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;
    } else {
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
        }).detach();
    }
}

void Demo3D::launchBurstAnalysis() {
    namespace fs = std::filesystem;
    // Strip "_m0" suffix to get shared stem "frame_T"
    std::string stem = fs::path(burstPaths[0]).stem().string();
    if (stem.size() >= 3 && stem.substr(stem.size() - 3) == "_m0")
        stem = stem.substr(0, stem.size() - 3);
    lastAnalysisPath = analysisDir + "/" + stem + ".md";

    std::string cmd = "python \"" + toolsScript + "\""
                    + " --burst"
                    + " \"" + burstPaths[0] + "\""
                    + " \"" + burstPaths[1] + "\""
                    + " \"" + burstPaths[2] + "\""
                    + " \"" + analysisDir   + "\"";
    if (!statsPathForAnalysis.empty())
        cmd += " \"" + statsPathForAnalysis + "\"";

    std::cout << "[12b] Burst analysis: " << burstPaths[0]
              << " + m3 + m6 -> " << lastAnalysisPath << "\n";

    if (autoCloseAfterCapture) {
        std::cout << "[12b] Running burst analysis synchronously (auto-close mode)...\n";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;
    } else {
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        }).detach();
    }
}

void Demo3D::launchSequenceAnalysis() {
    namespace fs = std::filesystem;
    // Strip "_f0" suffix to get shared stem "frame_T", then append "_seq"
    std::string stem = fs::path(seqPaths[0]).stem().string();
    if (stem.size() >= 3 && stem.substr(stem.size() - 3) == "_f0")
        stem = stem.substr(0, stem.size() - 3);
    lastAnalysisPath = analysisDir + "/" + stem + "_seq.md";

    // --sequence <output_dir> <f0> <f1> ... [stats.json]
    std::string cmd = "python \"" + toolsScript + "\""
                    + " --sequence"
                    + " \"" + analysisDir + "\"";
    for (const auto& p : seqPaths)
        cmd += " \"" + p + "\"";
    if (!statsPathForAnalysis.empty())
        cmd += " \"" + statsPathForAnalysis + "\"";

    std::cout << "[14a] Sequence analysis: " << seqPaths.size()
              << " frames -> " << lastAnalysisPath << "\n";

    if (autoCloseAfterCapture) {
        std::cout << "[14a] Running sequence analysis synchronously (auto-close mode)...\n";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[14a] Sequence analysis failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;
    } else {
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[14a] Sequence analysis failed (exit " << ret << ")\n";
        }).detach();
    }
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
    syncCameraYawPitchFromTarget();   // Step 5: keep mouse-look scalars in sync

    std::cout << "[Demo3D] Camera reset to position: "
              << camera.position.x << ", " << camera.position.y << ", " << camera.position.z << std::endl;
}

// =============================================================================
// Step 5: Camera control helpers (codex 10 F3, F6)
// =============================================================================

void Demo3D::syncCameraYawPitchFromTarget() {
    // codex 10 F6: maintain yaw/pitch as scalars to avoid the
    // cross-product singularity at world-up/down. Initialize from the
    // current forward vector so mouse-look starts coherent.
    glm::vec3 fwd = camera.target - camera.position;
    float lenSq = glm::dot(fwd, fwd);
    if (lenSq < 1e-12f) {
        cameraYaw = 0.0f;
        cameraPitch = 0.0f;
        return;
    }
    fwd /= std::sqrt(lenSq);
    cameraYaw   = std::atan2(fwd.x, fwd.z);                   // 0 = +Z forward
    cameraPitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
}

void Demo3D::resetCameraToScenePreset() {
    // codex 11 F1: scene-aware camera reset. Single source of truth for both
    // the R key (in processInput) and the ImGui "Reset Camera" button.
    // OBJ scenes go through the per-OBJ preset (which also restores the
    // matching light); analytic scenes use the legacy resetCamera() default.
    //
    // Step 7: applyOBJViewPreset is now bounds-driven and parameterless;
    // the codex 12 F1 4-way -> 2-way translation is no longer needed.
    if (useOBJMesh && !currentOBJPath.empty()) {
        applyOBJViewPreset();
    } else {
        resetCamera();
    }
}

void Demo3D::applyOBJViewPreset() {
    // Step 7 (auto-fit): bounds-driven camera + light placement. Replaces
    // the Step 4-6 hardcoded per-objKind branches so any new OBJ "just works"
    // without editing this function.
    //
    // Heuristic: position the camera one diagonal away from the bounds
    // center, looking back at center. Pick lookDir = +Z by default; switch
    // to +X when the X-extent dominates Z (Sponza-style halls keep their
    // down-the-axis framing). Light sits 30% of the height above center so
    // walls/floors are illuminated regardless of scene size.
    const glm::vec3 bmin = currentObjBmin;
    const glm::vec3 bmax = currentObjBmax;
    const glm::vec3 size = bmax - bmin;
    const glm::vec3 center = (bmin + bmax) * 0.5f;
    const float diag = glm::length(size);
    if (diag <= 1e-6f) {
        std::cerr << "[WARN] applyOBJViewPreset: degenerate bounds, using fallback\n";
        camera.position = glm::vec3(0.0f, 0.0f, 4.0f);
        camera.target   = glm::vec3(0.0f);
        camera.up       = glm::vec3(0.0f, 1.0f, 0.0f);
        camera.fovy     = 60.0f;
        lightPosition   = glm::vec3(0.0f, 0.8f, 0.0f);
        syncCameraYawPitchFromTarget();
        return;
    }

    glm::vec3 lookDir(0.0f, 0.0f, 1.0f);
    if (size.x > size.z * 1.3f) lookDir = glm::vec3(1.0f, 0.0f, 0.0f);

    // codex 13 F3: FOV-aware backoff. 1.0x diag (Step 7 v1) put Sponza too
    // far -- the 3.8x1.59x2.34 mesh filled only ~30% of vertical screen.
    // Solve for the distance that just-fits the visible perpendicular
    // extent within fovy and the screen aspect, then add a small margin.
    const float fovy = 60.0f;
    const int   sw   = GetScreenWidth();
    const int   sh   = GetScreenHeight();
    const float aspect = (sh > 0) ? static_cast<float>(sw) / static_cast<float>(sh)
                                  : (16.0f / 9.0f);
    const float halfFovyRad = glm::radians(fovy) * 0.5f;
    const float halfFovxRad = std::atan(std::tan(halfFovyRad) * aspect);

    // Visible extent perpendicular to lookDir: subtract the lookDir-aligned
    // component from `size`. For lookDir=+Z this leaves XY; for lookDir=+X
    // this leaves YZ. One axis ends up zero in either branch.
    glm::vec3 perp = size - lookDir * glm::dot(size, lookDir);
    const float visH = std::abs(perp.y);                                       // vertical
    const float visW = std::sqrt(perp.x * perp.x + perp.z * perp.z);           // horizontal

    const float distFromY = (visH * 0.5f) / std::tan(halfFovyRad);
    const float distFromX = (visW * 0.5f) / std::tan(halfFovxRad);
    float fitDist = std::max(distFromY, distFromX) * 1.4f;                     // 40% headroom

    // codex 13 F3 follow-up: clamp to "at least outside the bounding box
    // along lookDir + 30% diag margin". Without this, FOV-fit can park
    // the camera right against (or inside) a wall when the bounding box
    // fills the SDF volume -- e.g. Sponza's bmax.x=1.9 with FOV-fit
    // distance 1.93 lands almost exactly on the wall, producing a
    // solid-gray render. The clamp pushes Sponza to ~3.3 (matching the
    // old hand-tuned 3.5) while leaving Cornell at the tight FOV value.
    const float boxHalfAlongLook = std::abs(glm::dot(size, lookDir)) * 0.5f;
    const float minBackoff       = boxHalfAlongLook + 0.3f * diag;
    fitDist = std::max(fitDist, minBackoff);

    glm::vec3 camPos    = center + lookDir * fitDist + glm::vec3(0.0f, size.y * 0.05f, 0.0f);
    glm::vec3 camTarget = center;
    glm::vec3 lightPos  = center + glm::vec3(0.0f, size.y * 0.3f, 0.0f);

    // F3 alpha-sample validation against meshVoxelData (only meaningful for
    // INSIDE-volume cameras; codex 09 F2 fix preserved).
    if (!meshVoxelData.empty()) {
        glm::vec3 uvw = (camPos - volumeOrigin) / volumeSize;
        bool insideVolume =
            uvw.x >= 0.0f && uvw.x <= 1.0f &&
            uvw.y >= 0.0f && uvw.y <= 1.0f &&
            uvw.z >= 0.0f && uvw.z <= 1.0f;
        if (insideVolume) {
            glm::ivec3 voxel = glm::ivec3(uvw * float(volumeResolution));
            voxel = glm::clamp(voxel, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
            int idx = (voxel.z * volumeResolution + voxel.y) * volumeResolution + voxel.x;
            uint8_t alphaAtCam = meshVoxelData[idx * 4 + 3];
            std::cout << "[Demo3D] Camera preset validation (inside volume): pos=("
                      << camPos.x << "," << camPos.y << "," << camPos.z
                      << ") voxel=(" << voxel.x << "," << voxel.y << "," << voxel.z
                      << ") alpha=" << int(alphaAtCam) << "\n";
            if (alphaAtCam > 0) {
                std::cerr << "[WARN] Proposed camera position lies inside a marked surface voxel; "
                             "view will start inside geometry. Adjust the preset.\n";
            }
        } else {
            std::cout << "[Demo3D] Camera preset validation: pos=("
                      << camPos.x << "," << camPos.y << "," << camPos.z
                      << ") OUTSIDE SDF volume (uvw=(" << uvw.x << "," << uvw.y << "," << uvw.z
                      << ")); alpha check skipped, relying on ray-box intersection at march time\n";
        }
    }

    camera.position = camPos;
    camera.target   = camTarget;
    camera.up       = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovy     = fovy;
    syncCameraYawPitchFromTarget();
    lightPosition   = lightPos;
    std::cout << "[Demo3D] Applied auto-fit view preset (" << currentOBJPath
              << "): bounds=(" << bmin.x << "," << bmin.y << "," << bmin.z
              << ")..(" << bmax.x << "," << bmax.y << "," << bmax.z
              << ") diag=" << diag
              << " camPos=(" << camPos.x << "," << camPos.y << "," << camPos.z
              << ") fovy=" << camera.fovy
              << " light=(" << lightPosition.x << ","
              << lightPosition.y << "," << lightPosition.z << ")\n";
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
    const auto loadT0 = std::chrono::high_resolution_clock::now();

    // Step 9 Phase 2 (codex 03 F2): source-aware cache lookup.
    // Hits skip parse + normalize + voxelize entirely; just upload cached
    // bytes to GPU textures + commit + applyOBJViewPreset.
    {
        MeshCacheKey key{ filename, useGPUVoxelize ? 1 : 0 };
        auto cit = meshCache.find(key);
        if (cit != meshCache.end()) {
            const auto& cm = cit->second;
            currentObjBmin = cm.bmin;
            currentObjBmax = cm.bmax;
            // Upload cached bytes to voxelGridTexture + meshVoxelBaseTexture.
            glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                            volumeResolution, volumeResolution, volumeResolution,
                            GL_RGBA, GL_UNSIGNED_BYTE, cm.voxelBytes.data());
            if (meshVoxelBaseTexture) {
                glBindTexture(GL_TEXTURE_3D, meshVoxelBaseTexture);
                glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                                volumeResolution, volumeResolution, volumeResolution,
                                GL_RGBA, GL_UNSIGNED_BYTE, cm.voxelBytes.data());
            }
            glBindTexture(GL_TEXTURE_3D, 0);
            // codex 04 F2: ALWAYS keep the CPU mirror (8 MB at 128^3) so the
            // user can flip "GPU SDF" off mid-session without stranding CPU
            // EDT (which requires meshVoxelData as input). The earlier
            // "clear on GPU/GPU" optimization saved 8 MB but broke the
            // toggle-off transition. gpuVoxelGridReady stays true so
            // sdfGenerationPass uses the GPU path while it's enabled.
            meshVoxelData     = cm.voxelBytes;   // copy
            gpuVoxelGridReady = (useGPUVoxelize ? true : false);
            // Commit-block invariants (mirror the cache-miss commit below).
            meshSDFReady         = false;
            useOBJMesh           = true;
            useAnalyticRaymarch  = false;
            historyNeedsSeed     = true;
            renderFrameIndex     = 0;
            temporalRebuildCount = 0;
            sceneDirty           = true;
            // Compute objKey same way as cache-miss path so currentOBJPath stays consistent.
            std::string objKey;
            if      (filename.find("Sponza-master") != std::string::npos)       objKey = "sponza_master";
            else if (filename.find("CornellBox-Original") != std::string::npos) objKey = "cornell_orig";
            else if (filename.find("sponza") != std::string::npos
                  || filename.find("Sponza") != std::string::npos)              objKey = "sponza";
            else                                                                objKey = "cornell";
            currentOBJPath = objKey;
            applyOBJViewPreset();
            const double loadMs = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - loadT0).count();
            std::cout << "[Demo3D] OBJ cache hit (path=" << filename
                      << " kind=" << (key.voxelizerKind ? "GPU" : "CPU")
                      << "): loadOBJMesh wall=" << loadMs << "ms\n";
            return true;
        }
    }

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
    
    // Step 4 (4a, codex 08 F2): per-OBJ normalization scale.
    //   Cornell: 1.0 (legacy [-1,1] -- unchanged baseline for clean regression).
    //   Sponza : 1.9 (fills the [-2,2] SDF volume with 5% boundary margin -> ~3.6x
    //                 surface-area increase, expected ~136K seeds vs Step 3's 38K).
    //   Step 6: objKey is 4-way (cornell|cornell_orig|sponza|sponza_master) so
    //   the ImGui ACTIVE indicator distinguishes variants; objKind stays 2-way
    //   because preset/halfExtent only depend on Cornell-vs-Sponza.
    const bool isSponza = (filename.find("sponza") != std::string::npos)
                       || (filename.find("Sponza") != std::string::npos);
    const std::string objKind = isSponza ? "sponza" : "cornell";
    std::string objKey;
    if (filename.find("Sponza-master") != std::string::npos)            objKey = "sponza_master";
    else if (filename.find("CornellBox-Original") != std::string::npos) objKey = "cornell_orig";
    else if (isSponza)                                                  objKey = "sponza";
    else                                                                objKey = "cornell";
    float halfExtent = 1.0f;
    if (objKind == "sponza") {
        halfExtent = 1.9f;
    }
    objLoader.normalize(halfExtent);
    std::cout << "[Demo3D] OBJ normalized to halfExtent=" << halfExtent
              << " (volume halfSize=" << (volumeSize.x * 0.5f) << ", objKind=" << objKind << ")\n";

    // Step 7 (auto-fit): capture post-normalize bounds for the auto-fit
    // preset. Held local until the commit block below (codex 13 F1: must
    // not be assigned to currentObjBmin/Bmax before voxelization succeeds,
    // otherwise a failed-load case leaves the previous mesh visible but
    // R-key reset would use the failed mesh's bounds).
    glm::vec3 nbmin, nbmax;
    objLoader.getBounds(nbmin, nbmax);
    std::cout << "[Demo3D] Post-normalize bounds: ("
              << nbmin.x << "," << nbmin.y << "," << nbmin.z << ")..("
              << nbmax.x << "," << nbmax.y << "," << nbmax.z << ")\n";

    // Step 3 (3a, F5): stage voxelization in a local vector. Previous mesh state
    // (meshVoxelData, useOBJMesh, etc.) is untouched until commit at the end --
    // so a failed voxelization preserves whatever was on screen.
    // Step 9 Phase 3 (codex 03 F1): branch on useGPUVoxelize. CPU path
    // produces newVoxelData; GPU path runs voxelizeOBJ_GPU AFTER commit
    // (it writes voxelGridTexture + meshVoxelBaseTexture directly and sets
    // gpuVoxelGridReady = true).
    std::vector<uint8_t> newVoxelData;
    if (!useGPUVoxelize) {
        objLoader.voxelize(volumeResolution, newVoxelData, volumeOrigin, volumeSize);
        if (newVoxelData.empty()) {
            std::cerr << "[ERROR] Empty voxelization for " << successfulPath
                      << "; keeping previous mesh state\n";
            return false;
        }
        // Debug-display upload (voxelGridTexture). This is the only side effect before
        // commit; if a later step somehow fails the worst case is that the debug texture
        // briefly shows the new mesh while meshVoxelData still holds the old data.
        glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                        volumeResolution, volumeResolution, volumeResolution,
                        GL_RGBA, GL_UNSIGNED_BYTE, newVoxelData.data());
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // Commit. All scene-switch invariants set together:
    //  - meshSDFReady=false -> Step 3b's branch in sdfGenerationPass() will rebake.
    //  - useAnalyticRaymarch=false (3d, F3): final raymarch shader has a separate
    //    analytic toggle (uUseAnalyticSDF) that, if true, ignores uSDF and draws the
    //    Cornell Box analytic primitives. Force off here so the mesh actually shows.
    //  - historyNeedsSeed/renderFrameIndex/temporalRebuildCount (3a, F2): temporal
    //    cascade reseed so previous-scene history doesn't EMA-blend into OBJ frames.
    // codex 04 F1: snapshot prior scene state BEFORE the commit so GPU
    // voxelize failure can roll back. Captures only the CPU-side fields
    // the commit mutates -- texture contents are best-effort restored
    // by re-clicking the prior scene's button (cache hit makes that fast).
    struct PriorScene {
        std::vector<uint8_t> meshVoxelData;
        bool        useOBJMesh;
        bool        useAnalyticRaymarch;
        std::string currentOBJPath;
        glm::vec3   currentObjBmin;
        glm::vec3   currentObjBmax;
        bool        gpuVoxelGridReady;
    } prior {
        std::move(meshVoxelData),
        useOBJMesh, useAnalyticRaymarch, currentOBJPath,
        currentObjBmin, currentObjBmax, gpuVoxelGridReady
    };
    // codex 04 F2: always store CPU mirror so GPU SDF toggle-off doesn't
    // strand CPU EDT. GPU path reads it from the cache-populate readback
    // below (after voxelizeOBJ_GPU completes); CPU path gets it directly
    // from objLoader.voxelize() above.
    if (!useGPUVoxelize) meshVoxelData = std::move(newVoxelData);
    else                 meshVoxelData.clear();   // GPU path repopulates from readback
    meshSDFReady         = false;
    useOBJMesh           = true;
    useAnalyticRaymarch  = false;
    historyNeedsSeed     = true;
    renderFrameIndex     = 0;
    temporalRebuildCount = 0;
    sceneDirty           = true;
    currentOBJPath       = objKey;  // Step 6: 4-way key
    currentObjBmin       = nbmin;   // codex 13 F1: assign bounds atomically with the rest
    currentObjBmax       = nbmax;
    gpuVoxelGridReady    = false;   // reset; GPU path sets true below on success

    // Step 8 Phase 2a: cache static OBJ voxels in meshVoxelBaseTexture so the
    // dynamic-sphere overlay path can fast-restore them via glCopyImageSubData
    // each frame without re-running voxelization. CPU path uploads from
    // meshVoxelData here; GPU path mirrors voxelGridTexture inside
    // voxelizeOBJ_GPU below.
    if (!useGPUVoxelize && meshVoxelBaseTexture) {
        glBindTexture(GL_TEXTURE_3D, meshVoxelBaseTexture);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                        volumeResolution, volumeResolution, volumeResolution,
                        GL_RGBA, GL_UNSIGNED_BYTE, meshVoxelData.data());
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // Step 9 Phase 3 (codex 03 F1): GPU voxelize fires AFTER commit so
    // gpuVoxelGridReady takes effect on the next sdfGenerationPass.
    if (useGPUVoxelize) {
        if (!voxelizeOBJ_GPU()) {
            // codex 04 F1: full rollback -- restore every CPU-side field
            // the commit mutated. Texture contents are best-effort: the
            // user can re-click the prior scene's button (cache hit fast)
            // to fully restore the visible output. Without rollback the
            // user would see an empty volume + lose any prior OBJ.
            std::cerr << "[Demo3D] loadOBJMesh GPU voxelize failed -- "
                         "rolling back to prior scene state\n";
            meshVoxelData       = std::move(prior.meshVoxelData);
            useOBJMesh          = prior.useOBJMesh;
            useAnalyticRaymarch = prior.useAnalyticRaymarch;
            currentOBJPath      = std::move(prior.currentOBJPath);
            currentObjBmin      = prior.currentObjBmin;
            currentObjBmax      = prior.currentObjBmax;
            gpuVoxelGridReady   = prior.gpuVoxelGridReady;
            meshSDFReady        = false;
            sceneDirty          = true;
            return false;
        }
    }

    std::cout << "[Demo3D] OBJ committed (" << currentOBJPath
              << "); SDF will be baked next frame\n";

    // Step 9 Phase 2 (codex 03 F2): populate cache so subsequent same-OBJ
    // clicks under the same voxelizer skip parse + voxelize entirely.
    // CPU path stores meshVoxelData directly. GPU path pays a one-shot
    // glGetTexImage readback (~5-10 ms) -- the OBJ-load total still
    // wins big over re-doing CPU voxelize.
    {
        MeshCacheKey key{ filename, useGPUVoxelize ? 1 : 0 };
        CachedMesh cm;
        cm.bmin = nbmin;
        cm.bmax = nbmax;
        if (useGPUVoxelize) {
            cm.voxelBytes.resize(size_t(volumeResolution) * volumeResolution * volumeResolution * 4);
            glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
            glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, cm.voxelBytes.data());
            glBindTexture(GL_TEXTURE_3D, 0);
            // codex 04 F2: always populate CPU mirror so GPU SDF toggle-off
            // (interactive: useGPUVoxelize stays true, useGPUSDF flips to
            // false) doesn't strand CPU EDT without input. The 8 MB cost is
            // already paid for the cache.
            meshVoxelData = cm.voxelBytes;   // copy
        } else {
            cm.voxelBytes = meshVoxelData;   // copy
        }
        meshCache[key] = std::move(cm);
    }
    const double loadMs = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - loadT0).count();
    std::cout << "[Demo3D] loadOBJMesh wall=" << loadMs << "ms (cache miss, voxelizer="
              << (useGPUVoxelize ? "GPU" : "CPU") << ")\n";

    // Step 5 (5-helper, codex 10 F3): per-OBJ camera + light preset extracted
    // into a helper so R-key reset can apply it without reloading the OBJ.
    // Step 7: now bounds-driven and parameterless — uses currentObjBmin/Bmax
    // stored above.
    applyOBJViewPreset();

    return true;
}
