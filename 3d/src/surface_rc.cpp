#include "surface_rc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

SurfaceRC::SurfaceRC()
    : debugTexture(0)
    , ringDebugTexture(0)
    , radianceDebugTexture(0)
    , ringAtlasTexture(0)
    , directAtlasTexture(0)
    , atlasPing(0)  // Phase 2D: ping-pong atlases for feedback
    , atlasPong(0)
    , writeToPing(true)
    , atlasWidth(0)
    , atlasHeight(0)
    , ringAtlasWidth(0)
    , ringAtlasHeight(0)
    , ringCascadeCount(CASCADE_COUNT)
    , ringBandHeight(256)
    , radianceDebugMode(0)
    , debugMode(0)
    , ringDebugMode(0)
    , debugTarget(0)
    , rayBias(0.01f)
    , enabled(false)
    , showDebug(false)
    , sceneSupported(false)
    , unsupportedWarningPrinted(false)
    , sceneType(0)
    , validChartCount(0)
    , validTexelCount(0)
    , chartActive{}
    , sceneLabel("none")
    , boundsMin(-1.0f)
    , boundsMax(1.0f)
    , shortBoxBmin(0.0f)
    , shortBoxBmax(0.0f)
    , tallBoxBmin(0.0f)
    , tallBoxBmax(0.0f) {
    // Phase 2E: Initialize cascade hierarchy
    cascadeResolutions[0] = 32;
    cascadeResolutions[1] = 16;
    cascadeResolutions[2] = 8;
    cascadeResolutions[3] = 4;
    cascadeResolutions[4] = 2;
    for (int i = 0; i < CASCADE_COUNT; i++) {
        cascadeAtlases[i] = 0;
    }
}

SurfaceRC::~SurfaceRC() {
    destroy();
}

bool SurfaceRC::initialize(int width, int height) {
    destroy();

    atlasWidth = width;
    atlasHeight = height;

    glGenTextures(1, &debugTexture);
    glBindTexture(GL_TEXTURE_2D, debugTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 atlasWidth, atlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (debugTexture == 0) {
        std::cerr << "[SurfaceRC] ERROR: failed to allocate debug atlas\n";
        atlasWidth = 0;
        atlasHeight = 0;
        return false;
    }

    // Phase 2C: Increased to 2560 to accommodate 18 charts (5 room + 12 box + 1 reserved)
    // Each chart is 128px wide, total = 18 * 128 = 2304px minimum, using 2560 for alignment
    ringAtlasWidth = 2560;  // Was 1024 in Phase 2B
    ringAtlasHeight = ringBandHeight * ringCascadeCount;
    glGenTextures(1, &ringDebugTexture);
    glBindTexture(GL_TEXTURE_2D, ringDebugTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 ringAtlasWidth, ringAtlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (ringDebugTexture == 0) {
        std::cerr << "[SurfaceRC] ERROR: failed to allocate ring-packed debug atlas\n";
        destroy();
        return false;
    }

    glGenTextures(1, &radianceDebugTexture);
    glBindTexture(GL_TEXTURE_2D, radianceDebugTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 ringAtlasWidth, ringAtlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (radianceDebugTexture == 0) {
        std::cerr << "[SurfaceRC] ERROR: failed to allocate radiance skeleton debug atlas\n";
        destroy();
        return false;
    }

    // Phase 2B-6: Create direct radiance atlas texture (single-frame)
    glGenTextures(1, &directAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, directAtlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ringAtlasWidth, ringAtlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Phase 2D: Create ping-pong atlases for persistent feedback
    glGenTextures(1, &atlasPing);
    glBindTexture(GL_TEXTURE_2D, atlasPing);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ringAtlasWidth, ringAtlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glGenTextures(1, &atlasPong);
    glBindTexture(GL_TEXTURE_2D, atlasPong);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ringAtlasWidth, ringAtlasHeight, 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Phase 2E: Create cascade hierarchy textures (C0-C4)
    for (int i = 0; i < CASCADE_COUNT; i++) {
        int res = cascadeResolutions[i];
        // Each cascade uses same chart layout as ring atlas but at different resolution
        int width = ringAtlasWidth * res / 32;   // Scale from C0 (32³) base
        int height = ringAtlasHeight * res / 32;
        
        // Ensure minimum size
        width = std::max(width, 64);
        height = std::max(height, 64);
        
        glGenTextures(1, &cascadeAtlases[i]);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        std::cout << "[SurfaceRC] Cascade C" << i << ": " << res << "³ probes, atlas " 
                  << width << "x" << height << std::endl;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (directAtlasTexture == 0) {
        std::cerr << "[SurfaceRC] ERROR: failed to allocate direct radiance atlas\n";
        destroy();
        return false;
    }

    // Phase 1 hardcoded Cornell chart packing:
    // row 0: floor 256x256, ceiling 256x256, four walls 128x256 each = 1024x256
    // row 1 reserved for later interior/object charts.
    validChartCount = 6;
    validTexelCount = 256 * 256 * 2 + 128 * 256 * 4;

    std::cout << "[SurfaceRC] debug atlas allocated " << atlasWidth << "x" << atlasHeight
              << " (valid charts=" << validChartCount
              << ", valid texels=" << validTexelCount << ")\n";
    std::cout << "[SurfaceRC] ring-packed debug atlas allocated "
              << ringAtlasWidth << "x" << ringAtlasHeight
              << " (bandHeight=" << ringBandHeight
              << ", cascades=" << ringCascadeCount << ")\n";
    std::cout << "[SurfaceRC] radiance skeleton debug atlas allocated "
              << ringAtlasWidth << "x" << ringAtlasHeight << "\n";
    return true;
}

void SurfaceRC::destroy() {
    if (debugTexture) {
        glDeleteTextures(1, &debugTexture);
        debugTexture = 0;
    }
    if (ringDebugTexture) {
        glDeleteTextures(1, &ringDebugTexture);
        ringDebugTexture = 0;
    }
    if (radianceDebugTexture) {
        glDeleteTextures(1, &radianceDebugTexture);
        radianceDebugTexture = 0;
    }
    if (directAtlasTexture) {
        glDeleteTextures(1, &directAtlasTexture);
        directAtlasTexture = 0;
    }
    
    // Phase 2D: Cleanup ping-pong atlases
    if (atlasPing) {
        glDeleteTextures(1, &atlasPing);
        atlasPing = 0;
    }
    if (atlasPong) {
        glDeleteTextures(1, &atlasPong);
        atlasPong = 0;
    }
    
    // Phase 2E: Cleanup cascade hierarchy textures
    for (int i = 0; i < CASCADE_COUNT; i++) {
        if (cascadeAtlases[i]) {
            glDeleteTextures(1, &cascadeAtlases[i]);
            cascadeAtlases[i] = 0;
        }
    }
    atlasWidth = 0;
    atlasHeight = 0;
    ringAtlasWidth = 0;
    ringAtlasHeight = 0;
}

void SurfaceRC::setEnabled(bool v) {
    if (enabled == v) return;
    enabled = v;
    if (enabled) {
        showDebug = true;
        std::cout << "[SurfaceRC] enabled: scene-keyed surface charts\n";
        // CLI parsing can enable SurfaceRC before --load-obj runs. Avoid a false
        // unsupported-scene warning during that transient startup state; updateScene()
        // will log the real scene support state once OBJ loading commits.
        if (!sceneSupported && sceneLabel != "none" && sceneLabel != "analytic" && !unsupportedWarningPrinted) {
            std::cout << "[SurfaceRC] WARN: current scene is not supported by the surface chart set\n";
            unsupportedWarningPrinted = true;
        }
    } else {
        std::cout << "[SurfaceRC] disabled; volumetric cascade remains active\n";
    }
}

void SurfaceRC::setDebugMode(int mode) {
    debugMode = std::clamp(mode, 0, 4);
}

void SurfaceRC::setDebugTarget(int target) {
    debugTarget = std::clamp(target, 0, 2);
}

void SurfaceRC::setRingDebugMode(int mode) {
    ringDebugMode = std::clamp(mode, 0, 6);
}

void SurfaceRC::setRadianceDebugMode(int mode) {
    // Phase 2D: Extended to support modes 17 (feedback write) and 18 (feedback readback)
    radianceDebugMode = std::clamp(mode, 0, 18);
}

void SurfaceRC::setRayBias(float bias) {
    rayBias = std::clamp(bias, 0.0005f, 0.10f);
}

int SurfaceRC::getActiveChartCount() const {
    int count = 0;
    for (int active : chartActive) count += active ? 1 : 0;
    return count;
}

int SurfaceRC::getMaxChartID() const {
    int maxID = 0;
    for (const ChartDef& chart : charts)
        maxID = std::max(maxID, chart.id);
    return std::max(maxID, static_cast<int>(chartActive.size()));
}

bool SurfaceRC::isChartActive(int chartID) const {
    if (chartID < 1 || chartID > static_cast<int>(chartActive.size()))
        return false;
    return chartActive[static_cast<size_t>(chartID - 1)] != 0;
}

glm::vec3 SurfaceRC::chartToWorld(int chartID, float u, float v) const {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    for (const ChartDef& chart : charts) {
        if (chart.id == chartID)
            return chart.origin + chart.uAxis * u + chart.vAxis * v;
    }

    return glm::vec3(0.0f);
}

bool SurfaceRC::worldToChart(const glm::vec3& p, int& chartID, float& u, float& v) const {
    auto inUnit = [](float value) {
        constexpr float eps = 0.001f;
        return value >= -eps && value <= 1.0f + eps;
    };

    float bestDistance = 1e30f;
    int bestChart = 0;
    float bestU = 0.0f;
    float bestV = 0.0f;

    auto consider = [&](const ChartDef& chart, float distance, float candidateU, float candidateV) {
        if (!chart.active || !isChartActive(chart.id) || !inUnit(candidateU) || !inUnit(candidateV))
            return;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestChart = chart.id;
            bestU = std::clamp(candidateU, 0.0f, 1.0f);
            bestV = std::clamp(candidateV, 0.0f, 1.0f);
        }
    };

    for (const ChartDef& chart : charts) {
        const float uLen2 = glm::dot(chart.uAxis, chart.uAxis);
        const float vLen2 = glm::dot(chart.vAxis, chart.vAxis);
        if (uLen2 < 1e-8f || vLen2 < 1e-8f)
            continue;
        const glm::vec3 rel = p - chart.origin;
        const float distance = std::abs(glm::dot(rel, chart.normal));
        const float candidateU = glm::dot(rel, chart.uAxis) / uLen2;
        const float candidateV = glm::dot(rel, chart.vAxis) / vLen2;
        consider(chart, distance, candidateU, candidateV);
    }

    if (bestChart == 0)
        return false;

    chartID = bestChart;
    u = bestU;
    v = bestV;
    return true;
}

int SurfaceRC::getAtlasWidth() const { return atlasWidth; }
int SurfaceRC::getAtlasHeight() const { return atlasHeight; }
int SurfaceRC::getRingAtlasWidth() const { return ringAtlasWidth; }
int SurfaceRC::getRingAtlasHeight() const { return ringAtlasHeight; }
int SurfaceRC::getRingCascadeCount() const { return ringCascadeCount; }
int SurfaceRC::getRingBandHeight() const { return ringBandHeight; }

const char* SurfaceRC::debugModeName(int mode) {
    switch (mode) {
        case 0: return "chart id";
        case 1: return "normal";
        case 2: return "world position";
        case 3: return "albedo";
        case 4: return "valid mask";
        default: return "unknown";
    }
}

const char* SurfaceRC::ringDebugModeName(int mode) {
    switch (mode) {
        case 0: return "chart+cascade";
        case 1: return "probe coordinate";
        case 2: return "direction coordinate";
        case 3: return "ring/theta";
        case 4: return "probe world position";
        case 5: return "ray origin";
        case 6: return "hemisphere direction";
        default: return "unknown";
    }
}

const char* SurfaceRC::debugTargetName(int target) {
    switch (target) {
        case 0: return "chart atlas";
        case 1: return "ring-packed atlas";
        case 2: return "radiance skeleton atlas";
        default: return "unknown";
    }
}

const char* SurfaceRC::radianceDebugModeName(int mode) {
    switch (mode) {
        case 0: return "ray origin";
        case 1: return "hemisphere direction";
        case 2: return "normal";
        case 3: return "active/chart mask";
        case 4: return "trace classification";
        case 5: return "trace distance";
        case 6: return "hit chart id";
        case 7: return "hit chart uv";
        case 8: return "UV round-trip test";
        case 9: return "hit normal";
        case 10: return "unshadowed direct";
        case 11: return "NdotL";
        case 12: return "trace state (with escapes)";
        case 13: return "shadow visibility";
        case 14: return "shadowed direct";
        case 15: return "direct atlas write";
        case 16: return "atlas readback";
        // Phase 2D: Feedback modes
        case 17: return "feedback write (accumulated)";
        case 18: return "feedback readback (accumulated GI)";
        default: return "unknown";
    }
}

// Phase 2D: Ping-pong atlas management methods
void SurfaceRC::flipAtlases() {
    writeToPing = !writeToPing;
}

GLuint SurfaceRC::getCurrentReadAtlas() const {
    return writeToPing ? atlasPong : atlasPing;
}

GLuint SurfaceRC::getCurrentWriteAtlas() const {
    return writeToPing ? atlasPing : atlasPong;
}

void SurfaceRC::clearAtlases() {
    // Fill both atlases with zeros to reset accumulation
    int pixelCount = ringAtlasWidth * ringAtlasHeight;
    std::vector<unsigned short> zeros(pixelCount * 4, 0);  // RGBA16F
    
    glActiveTexture(GL_TEXTURE0);
    
    glBindTexture(GL_TEXTURE_2D, atlasPing);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ringAtlasWidth, ringAtlasHeight,
                    GL_RGBA, GL_HALF_FLOAT, zeros.data());
    
    glBindTexture(GL_TEXTURE_2D, atlasPong);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ringAtlasWidth, ringAtlasHeight,
                    GL_RGBA, GL_HALF_FLOAT, zeros.data());
}

// Phase 2E: Cascade hierarchy management methods
void SurfaceRC::clearCascadeAtlases() {
    // Reset all cascade levels to black
    for (int i = 0; i < CASCADE_COUNT; i++) {
        int res = cascadeResolutions[i];
        int width = ringAtlasWidth * res / 32;
        int height = ringAtlasHeight * res / 32;
        width = std::max(width, 64);
        height = std::max(height, 64);
        
        int pixelCount = width * height;
        std::vector<uint16_t> zeros(pixelCount * 4, 0);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[i]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_HALF_FLOAT, zeros.data());
    }
}

void SurfaceRC::seedCascadeBaseFromDirect() {
    if (!enabled || directAtlasTexture == 0 || cascadeAtlases[0] == 0)
        return;

    const int baseWidth = std::max(ringAtlasWidth * cascadeResolutions[0] / 32, 64);
    const int baseHeight = std::max(ringAtlasHeight * cascadeResolutions[0] / 32, 64);

    glCopyImageSubData(directAtlasTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                       cascadeAtlases[0], GL_TEXTURE_2D, 0, 0, 0, 0,
                       baseWidth, baseHeight, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void SurfaceRC::dispatchCascadeHierarchy(GLuint downsampleProgram, 
                                         GLuint upsampleProgram) {
    if (!enabled || downsampleProgram == 0 || upsampleProgram == 0) return;
    
    // Phase 1: Downsample from C0 → C1 → C2 → C3 → C4
    for (int level = 0; level < CASCADE_COUNT - 1; level++) {
        glUseProgram(downsampleProgram);
        
        // Bind fine atlas as input
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[level]);
        glUniform1i(glGetUniformLocation(downsampleProgram, "uFineAtlas"), 0);
        
        // Bind coarse atlas as output
        glBindImageTexture(0, cascadeAtlases[level + 1], 0, GL_FALSE, 0, 
                          GL_WRITE_ONLY, GL_RGBA16F);
        
        // Set uniforms
        int fineRes = cascadeResolutions[level];
        int coarseRes = cascadeResolutions[level + 1];
        int fineWidth = std::max(ringAtlasWidth * fineRes / 32, 64);
        int fineHeight = std::max(ringAtlasHeight * fineRes / 32, 64);
        int coarseWidth = std::max(ringAtlasWidth * coarseRes / 32, 64);
        int coarseHeight = std::max(ringAtlasHeight * coarseRes / 32, 64);
        
        glUniform2i(glGetUniformLocation(downsampleProgram, "uFineAtlasSize"),
                   fineWidth, fineHeight);
        glUniform2i(glGetUniformLocation(downsampleProgram, "uCoarseAtlasSize"),
                   coarseWidth, coarseHeight);
        glUniform1i(glGetUniformLocation(downsampleProgram, "uFineLevel"), level);
        
        // Dispatch
        GLuint groupsX = (coarseWidth + 7) / 8;
        GLuint groupsY = (coarseHeight + 7) / 8;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    
    // Phase 2: Upsample/inject from C4 → C3 → C2 → C1 → C0
    for (int level = CASCADE_COUNT - 2; level >= 0; level--) {
        glUseProgram(upsampleProgram);
        
        // Bind coarse atlas as input
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cascadeAtlases[level + 1]);
        glUniform1i(glGetUniformLocation(upsampleProgram, "uCoarseAtlas"), 0);
        
        // Bind fine atlas as input/output (read-modify-write)
        glBindImageTexture(0, cascadeAtlases[level], 0, GL_FALSE, 0, 
                          GL_READ_WRITE, GL_RGBA16F);
        
        // Set uniforms
        int fineRes = cascadeResolutions[level];
        int coarseRes = cascadeResolutions[level + 1];
        int fineWidth = std::max(ringAtlasWidth * fineRes / 32, 64);
        int fineHeight = std::max(ringAtlasHeight * fineRes / 32, 64);
        int coarseWidth = std::max(ringAtlasWidth * coarseRes / 32, 64);
        int coarseHeight = std::max(ringAtlasHeight * coarseRes / 32, 64);
        
        glUniform2i(glGetUniformLocation(upsampleProgram, "uFineAtlasSize"),
                   fineWidth, fineHeight);
        glUniform2i(glGetUniformLocation(upsampleProgram, "uCoarseAtlasSize"),
                   coarseWidth, coarseHeight);
        glUniform1i(glGetUniformLocation(upsampleProgram, "uCoarseLevel"), level + 1);
        glUniform1f(glGetUniformLocation(upsampleProgram, "uInjectionWeight"), 0.5f);
        
        // Dispatch
        GLuint groupsX = (fineWidth + 7) / 8;
        GLuint groupsY = (fineHeight + 7) / 8;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}

void SurfaceRC::updateScene(const std::string& sceneKey,
                            const glm::vec3& sceneBMin,
                            const glm::vec3& sceneBMax,
                            bool objMeshActive) {
    const std::string previousLabel = sceneLabel;
    const bool previousSupported = sceneSupported;
    const int previousSceneType = sceneType;

    sceneLabel = sceneKey.empty() ? "analytic" : sceneKey;
    boundsMin = sceneBMin;
    boundsMax = sceneBMax;
    charts.clear();
    chartActive.fill(0);
    sceneType = 0;
    sceneSupported = false;
    shortBoxBmin = glm::vec3(0.0f);
    shortBoxBmax = glm::vec3(0.0f);
    tallBoxBmin = glm::vec3(0.0f);
    tallBoxBmax = glm::vec3(0.0f);

    auto activateChart = [&](ChartDef chart) {
        if (chart.id < 1 || chart.id > static_cast<int>(chartActive.size()))
            return;
        chartActive[static_cast<size_t>(chart.id - 1)] = chart.active ? 1 : 0;
        charts.push_back(std::move(chart));
    };

    auto addPlane = [&](int id,
                        const std::string& name,
                        const glm::vec3& origin,
                        const glm::vec3& normal,
                        const glm::vec3& uAxis,
                        const glm::vec3& vAxis,
                        bool active,
                        bool approximate = false) {
        ChartDef chart;
        chart.id = id;
        chart.name = name;
        chart.normal = glm::normalize(normal);
        chart.origin = origin;
        chart.uAxis = uAxis;
        chart.vAxis = vAxis;
        chart.active = active;
        chart.approximate = approximate;
        const glm::vec3 p1 = origin + uAxis;
        const glm::vec3 p2 = origin + vAxis;
        const glm::vec3 p3 = origin + uAxis + vAxis;
        chart.bmin = glm::min(glm::min(origin, p1), glm::min(p2, p3));
        chart.bmax = glm::max(glm::max(origin, p1), glm::max(p2, p3));
        activateChart(chart);
    };

    auto addRoomCharts = [&](bool frontActive, bool approximate) {
        const glm::vec3 bmin = boundsMin;
        const glm::vec3 bmax = boundsMax;
        const float dx = std::max(bmax.x - bmin.x, 1e-4f);
        const float dy = std::max(bmax.y - bmin.y, 1e-4f);
        const float dz = std::max(bmax.z - bmin.z, 1e-4f);

        addPlane(1, "floor",   glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3( 0,  1,  0), glm::vec3(dx, 0, 0), glm::vec3(0, 0, dz), true, approximate);
        addPlane(2, "ceiling", glm::vec3(bmin.x, bmax.y, bmin.z), glm::vec3( 0, -1,  0), glm::vec3(dx, 0, 0), glm::vec3(0, 0, dz), true, approximate);
        addPlane(3, "left_wall",  glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3( 1, 0, 0), glm::vec3(0, dy, 0), glm::vec3(0, 0, dz), true, approximate);
        addPlane(4, "right_wall", glm::vec3(bmax.x, bmin.y, bmin.z), glm::vec3(-1, 0, 0), glm::vec3(0, dy, 0), glm::vec3(0, 0, dz), true, approximate);
        addPlane(5, "back_wall",  glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3(0, 0,  1), glm::vec3(0, dy, 0), glm::vec3(dx, 0, 0), true, approximate);
        addPlane(6, "front_wall", glm::vec3(bmin.x, bmin.y, bmax.z), glm::vec3(0, 0, -1), glm::vec3(0, dy, 0), glm::vec3(dx, 0, 0), frontActive, approximate);
    };

    auto addBoxCharts = [&](int baseID,
                            const glm::vec3& bmin,
                            const glm::vec3& bmax,
                            const std::string& prefix,
                            bool active,
                            bool approximate = false) {
        const float dx = std::max(bmax.x - bmin.x, 1e-4f);
        const float dy = std::max(bmax.y - bmin.y, 1e-4f);
        const float dz = std::max(bmax.z - bmin.z, 1e-4f);

        addPlane(baseID + 0, prefix + "_bottom", glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3( 0, -1,  0), glm::vec3(dx, 0, 0), glm::vec3(0, 0, dz), active, approximate);
        addPlane(baseID + 1, prefix + "_top",    glm::vec3(bmin.x, bmax.y, bmin.z), glm::vec3( 0,  1,  0), glm::vec3(dx, 0, 0), glm::vec3(0, 0, dz), active, approximate);
        addPlane(baseID + 2, prefix + "_left",   glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3(-1,  0,  0), glm::vec3(0, dy, 0), glm::vec3(0, 0, dz), active, approximate);
        addPlane(baseID + 3, prefix + "_right",  glm::vec3(bmax.x, bmin.y, bmin.z), glm::vec3( 1,  0,  0), glm::vec3(0, dy, 0), glm::vec3(0, 0, dz), active, approximate);
        addPlane(baseID + 4, prefix + "_front",  glm::vec3(bmin.x, bmin.y, bmin.z), glm::vec3( 0,  0, -1), glm::vec3(dx, 0, 0), glm::vec3(0, dy, 0), active, approximate);
        addPlane(baseID + 5, prefix + "_back",   glm::vec3(bmin.x, bmin.y, bmax.z), glm::vec3( 0,  0,  1), glm::vec3(dx, 0, 0), glm::vec3(0, dy, 0), active, approximate);
    };

    auto texelsForChart = [](int id) {
        if (id == 1 || id == 2)
            return 256 * 256;
        if (id >= 3 && id <= 18)
            return 128 * 256;
        return 0;
    };

    const bool cornellScene = objMeshActive &&
        (sceneKey == "cornell" || sceneKey == "cornell_orig" || sceneKey == "cornell_orig_alcove");
    const bool sponzaScene = objMeshActive &&
        (sceneKey == "sponza" || sceneKey == "sponza_master");

    // Phase 2C: Set box bounds for Cornell scene (extracted from OBJ vertices)
    if (cornellScene) {
        sceneType = 1;
        sceneSupported = true;
        addRoomCharts(false, false);

        // short_box bounds (from OBJ vertices 25-44)
        shortBoxBmin = glm::vec3(-0.354011f, -0.160399f, -2.964912f);
        shortBoxBmax = glm::vec3( 1.725989f,  1.491249f, -0.893599f);
        
        // tall_box bounds (from OBJ vertices 45-64)
        tallBoxBmin = glm::vec3(-2.174011f, -0.161864f, -4.806226f);
        tallBoxBmax = glm::vec3(-0.104011f,  3.139799f, -2.713598f);
        
        if (previousLabel != sceneLabel || previousSceneType != sceneType) {
            std::cout << "[SurfaceRC] Cornell box bounds set:\n";
            std::cout << "  short_box: (" << shortBoxBmin.x << "," << shortBoxBmin.y << "," << shortBoxBmin.z
                      << ") to (" << shortBoxBmax.x << "," << shortBoxBmax.y << "," << shortBoxBmax.z << ")\n";
            std::cout << "  tall_box:  (" << tallBoxBmin.x << "," << tallBoxBmin.y << "," << tallBoxBmin.z
                      << ") to (" << tallBoxBmax.x << "," << tallBoxBmax.y << "," << tallBoxBmax.z << ")\n";
        }

        addBoxCharts(7, shortBoxBmin, shortBoxBmax, "short_box", true, false);
        addBoxCharts(13, tallBoxBmin, tallBoxBmax, "tall_box", true, false);
    } else if (sponzaScene) {
        sceneType = 2;
        sceneSupported = true;
        addRoomCharts(true, false);

        const glm::vec3 extent = glm::max(boundsMax - boundsMin, glm::vec3(1e-4f));
        const float yMin = boundsMin.y + extent.y * 0.25f;
        const float yMax = boundsMin.y + extent.y * 0.72f;
        const float zMin = boundsMin.z + extent.z * 0.15f;
        const float zMax = boundsMin.z + extent.z * 0.45f;
        shortBoxBmin = glm::vec3(boundsMin.x + extent.x * 0.04f, yMin, zMin);
        shortBoxBmax = glm::vec3(boundsMin.x + extent.x * 0.55f, yMax, zMax);
        tallBoxBmin = glm::vec3(boundsMin.x + extent.x * 0.55f, yMin, zMin);
        tallBoxBmax = glm::vec3(boundsMin.x + extent.x * 0.80f, yMax, zMax);

        addBoxCharts(7, shortBoxBmin, shortBoxBmax, "sponza_left_arch_proxy", true, true);
        addBoxCharts(13, tallBoxBmin, tallBoxBmax, "sponza_right_column_proxy", true, true);
    } else {
        addRoomCharts(false, true);
    }

    validChartCount = getActiveChartCount();
    validTexelCount = 0;
    for (int chartID = 1; chartID <= static_cast<int>(chartActive.size()); ++chartID) {
        if (isChartActive(chartID))
            validTexelCount += texelsForChart(chartID);
    }

    if (previousLabel != sceneLabel || previousSupported != sceneSupported || previousSceneType != sceneType) {
        unsupportedWarningPrinted = false;
        std::cout << "[SurfaceRC] scene=" << sceneLabel
                  << " supported=" << (sceneSupported ? "yes" : "no")
                  << " type=" << sceneType
                  << " activeCharts=" << validChartCount
                  << " bounds=(" << boundsMin.x << "," << boundsMin.y << "," << boundsMin.z
                  << ")..(" << boundsMax.x << "," << boundsMax.y << "," << boundsMax.z << ")\n";
        if (enabled && !sceneSupported && !unsupportedWarningPrinted) {
            std::cout << "[SurfaceRC] WARN: unsupported scene for surface RC charts; final path may remain black\n";
            unsupportedWarningPrinted = true;
        }
    }
}

void SurfaceRC::dispatchDebug(GLuint computeProgram) {
    if (!enabled || debugTexture == 0 || computeProgram == 0) return;

    glUseProgram(computeProgram);
    glUniform2i(glGetUniformLocation(computeProgram, "uAtlasSize"), atlasWidth, atlasHeight);
    glUniform1i(glGetUniformLocation(computeProgram, "uDebugMode"), debugMode);
    glUniform1i(glGetUniformLocation(computeProgram, "uSceneSupported"), sceneSupported ? 1 : 0);
    glUniform1i(glGetUniformLocation(computeProgram, "uSurfaceSceneType"), sceneType);
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMin"), 1, &boundsMin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMax"), 1, &boundsMax[0]);
    
    // Phase 2C: Pass box bounds uniforms (for consistency across all dispatch functions)
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmin"), 1, &shortBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmax"), 1, &shortBoxBmax[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmin"), 1, &tallBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmax"), 1, &tallBoxBmax[0]);

    glBindImageTexture(0, debugTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    const GLuint groupsX = static_cast<GLuint>((atlasWidth + 7) / 8);
    const GLuint groupsY = static_cast<GLuint>((atlasHeight + 7) / 8);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "surface_cornell_debug");
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glPopDebugGroup();
}

void SurfaceRC::dispatchRingDebug(GLuint computeProgram) {
    if (!enabled || ringDebugTexture == 0 || computeProgram == 0) return;

    glUseProgram(computeProgram);
    glUniform2i(glGetUniformLocation(computeProgram, "uAtlasSize"), ringAtlasWidth, ringAtlasHeight);
    glUniform1i(glGetUniformLocation(computeProgram, "uDebugMode"), ringDebugMode);
    glUniform1i(glGetUniformLocation(computeProgram, "uSceneSupported"), sceneSupported ? 1 : 0);
    glUniform1i(glGetUniformLocation(computeProgram, "uSurfaceSceneType"), sceneType);
    // Phase 2C: Updated chartActive array size to 18 (was 6)
    glUniform1iv(glGetUniformLocation(computeProgram, "uChartActive"), 18, chartActive.data());
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMin"), 1, &boundsMin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMax"), 1, &boundsMax[0]);

    // Phase 2C: Pass box bounds uniforms (for consistency across all dispatch functions)
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmin"), 1, &shortBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmax"), 1, &shortBoxBmax[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmin"), 1, &tallBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmax"), 1, &tallBoxBmax[0]);

    glBindImageTexture(0, ringDebugTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    const GLuint groupsX = static_cast<GLuint>((ringAtlasWidth + 7) / 8);
    const GLuint groupsY = static_cast<GLuint>((ringAtlasHeight + 7) / 8);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "surface_ring_debug");
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glPopDebugGroup();
}

void SurfaceRC::dispatchRadianceDebug(GLuint computeProgram,
                                      GLuint sdfTexture,
                                      const glm::vec3& gridOrigin,
                                      const glm::vec3& gridSize,
                                      const glm::vec3& lightPos,
                                      const glm::vec3& lightColor) {
    if (!enabled || radianceDebugTexture == 0 || computeProgram == 0) return;

    glUseProgram(computeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glUniform1i(glGetUniformLocation(computeProgram, "uSDF"), 0);

    glUniform2i(glGetUniformLocation(computeProgram, "uAtlasSize"), ringAtlasWidth, ringAtlasHeight);
    glUniform1i(glGetUniformLocation(computeProgram, "uDebugMode"), radianceDebugMode);
    glUniform1i(glGetUniformLocation(computeProgram, "uSceneSupported"), sceneSupported ? 1 : 0);
    glUniform1i(glGetUniformLocation(computeProgram, "uSurfaceSceneType"), sceneType);
    // Phase 2C: Updated chartActive array size to 18 (was 6)
    glUniform1iv(glGetUniformLocation(computeProgram, "uChartActive"), 18, chartActive.data());
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMin"), 1, &boundsMin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uSceneBoundsMax"), 1, &boundsMax[0]);
    
    // Phase 2C: Pass box bounds uniforms
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmin"), 1, &shortBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uShortBoxBmax"), 1, &shortBoxBmax[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmin"), 1, &tallBoxBmin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uTallBoxBmax"), 1, &tallBoxBmax[0]);
    
    glUniform1f(glGetUniformLocation(computeProgram, "uRayBias"), rayBias);
    glUniform3fv(glGetUniformLocation(computeProgram, "uGridOrigin"), 1, &gridOrigin[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uGridSize"), 1, &gridSize[0]);
    glUniform1i(glGetUniformLocation(computeProgram, "uTraceSteps"), 96);
    glUniform1f(glGetUniformLocation(computeProgram, "uTraceMaxDist"), glm::length(gridSize));
    glUniform1f(glGetUniformLocation(computeProgram, "uHitEpsilon"), 0.002f);
    glUniform3fv(glGetUniformLocation(computeProgram, "uLightPos"), 1, &lightPos[0]);
    glUniform3fv(glGetUniformLocation(computeProgram, "uLightColor"), 1, &lightColor[0]);

    // Phase 2B-6: Bind atlas texture for mode 16 readback
    if (radianceDebugMode == 16 && directAtlasTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, directAtlasTexture);
        glUniform1i(glGetUniformLocation(computeProgram, "uRadianceAtlas"), 1);
    }
    
    // Phase 2D: Bind ping-pong atlases for modes 17/18
    if ((radianceDebugMode == 17 || radianceDebugMode == 18) && atlasPing != 0 && atlasPong != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, getCurrentReadAtlas());
        glUniform1i(glGetUniformLocation(computeProgram, "uAtlasRead"), 1);
        
        // Mode 18 reads from atlas, doesn't write
        if (radianceDebugMode == 17) {
            glBindImageTexture(0, getCurrentWriteAtlas(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            const GLuint groupsX = static_cast<GLuint>((ringAtlasWidth + 7) / 8);
            const GLuint groupsY = static_cast<GLuint>((ringAtlasHeight + 7) / 8);
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "surface_feedback_write");
            glDispatchCompute(groupsX, groupsY, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            glPopDebugGroup();
            return;  // Early exit - already dispatched
        }
        // Mode 18: will use radianceDebugTexture for visualization below
    }

    // Phase 2B-6: For mode 15, write directly to atlas texture instead of debug texture
    GLuint targetTexture = radianceDebugTexture;
    if (radianceDebugMode == 15 && directAtlasTexture != 0) {
        targetTexture = directAtlasTexture;
    }

    glBindImageTexture(0, targetTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    const GLuint groupsX = static_cast<GLuint>((ringAtlasWidth + 7) / 8);
    const GLuint groupsY = static_cast<GLuint>((ringAtlasHeight + 7) / 8);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "surface_radiance_debug");
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glPopDebugGroup();
}

void SurfaceRC::renderDebug(GLuint fragmentProgram, GLuint quadVAO) const {
    GLuint texture = debugTexture;
    if (debugTarget == 1) texture = ringDebugTexture;
    if (debugTarget == 2) texture = radianceDebugTexture;
    if (!enabled || !showDebug || texture == 0 || fragmentProgram == 0 || quadVAO == 0) return;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    int debugW = std::min(512, viewport[2]);
    int debugH = std::min(256, viewport[3]);
    if (debugTarget == 1 || debugTarget == 2) {
        const float aspect = (ringAtlasHeight > 0)
            ? float(ringAtlasWidth) / float(ringAtlasHeight)
            : (1024.0f / 1536.0f);
        debugH = std::min(576, viewport[3]);
        debugW = std::min(384, std::min(viewport[2], int(float(debugH) * aspect)));
        debugH = std::max(1, int(float(debugW) / aspect));
    }
    glViewport(0, 0, debugW, debugH);

    glUseProgram(fragmentProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(fragmentProgram, "uSurfaceAtlas"), 0);
    glUniform1i(glGetUniformLocation(fragmentProgram, "uDebugMode"),
                (debugTarget == 1) ? ringDebugMode : ((debugTarget == 2) ? radianceDebugMode : debugMode));

    glBindVertexArray(quadVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}
