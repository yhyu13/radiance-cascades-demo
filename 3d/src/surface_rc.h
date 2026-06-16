#ifndef SURFACE_RC_H
#define SURFACE_RC_H

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>

/**
 * @brief Experimental ShaderToy2 surface-attached radiance-cascade path.
 *
 * Phase 0/1 scope only:
 * - owns a Cornell hardcoded surface debug atlas
 * - dispatches surface_cornell_debug.comp
 * - renders the atlas through surface_debug.frag
 *
 * The actual ring-packed persistent radiance atlas starts in Phase 2; this
 * class deliberately keeps the current volumetric cascade path untouched.
 */
class SurfaceRC {
public:
    struct ChartDef {
        int id = 0;
        std::string name;
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 origin = glm::vec3(0.0f);
        glm::vec3 uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 vAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 bmin = glm::vec3(0.0f);
        glm::vec3 bmax = glm::vec3(0.0f);
        bool active = true;
        bool approximate = false;
    };

    SurfaceRC();
    ~SurfaceRC();

    bool initialize(int atlasWidth = 1024, int atlasHeight = 512);
    void destroy();

    void setEnabled(bool v);
    bool isEnabled() const { return enabled; }

    void setShowDebug(bool v) { showDebug = v; }
    bool getShowDebug() const { return showDebug; }

    void setDebugMode(int mode);
    int getDebugMode() const { return debugMode; }

    void setDebugTarget(int target);
    int getDebugTarget() const { return debugTarget; }

    void setRingDebugMode(int mode);
    int getRingDebugMode() const { return ringDebugMode; }

    void setRadianceDebugMode(int mode);
    int getRadianceDebugMode() const { return radianceDebugMode; }
    void setRayBias(float bias);
    float getRayBias() const { return rayBias; }

    void updateScene(const std::string& sceneKey,
                     const glm::vec3& sceneBMin,
                     const glm::vec3& sceneBMax,
                     bool useOBJMesh);

    void dispatchDebug(GLuint computeProgram);
    void dispatchRingDebug(GLuint computeProgram);
    void dispatchRadianceDebug(GLuint computeProgram,
                               GLuint sdfTexture,
                               const glm::vec3& gridOrigin,
                               const glm::vec3& gridSize,
                               const glm::vec3& lightPos,
                               const glm::vec3& lightColor);
    void renderDebug(GLuint fragmentProgram, GLuint quadVAO) const;

    GLuint getDebugTexture() const { return debugTexture; }
    GLuint getRingDebugTexture() const { return ringDebugTexture; }
    GLuint getRadianceDebugTexture() const { return radianceDebugTexture; }
    GLuint getDirectAtlasTexture() const { return directAtlasTexture; }  // Phase 2B-6
    
    // Phase 2D: Ping-pong atlas management
    void flipAtlases();
    GLuint getCurrentReadAtlas() const;
    GLuint getCurrentWriteAtlas() const;
    void clearAtlases();  // Reset both atlases to black (for camera movement)
    
    // Phase 2E: Cascade hierarchy management
    void dispatchCascadeHierarchy(GLuint downsampleProgram, GLuint upsampleProgram);
    void seedCascadeBaseFromDirect();
    void clearCascadeAtlases();  // Reset all cascade levels to black
    int getCascadeCount() const { return CASCADE_COUNT; }
    int getCascadeResolution(int level) const { return cascadeResolutions[level]; }
    GLuint getCascadeAtlas(int level) const { return cascadeAtlases[level]; }
    
    int getAtlasWidth() const;
    int getAtlasHeight() const;
    int getRingAtlasWidth() const;
    int getRingAtlasHeight() const;
    int getRingCascadeCount() const;
    int getRingBandHeight() const;
    // Phase 2C: Updated chartActive array size from 6 to 18
    const std::array<int, 18>& getChartActive() const { return chartActive; }
    const std::vector<ChartDef>& getChartDefs() const { return charts; }
    bool isChartActive(int chartID) const;
    glm::vec3 chartToWorld(int chartID, float u, float v) const;
    bool worldToChart(const glm::vec3& p, int& chartID, float& u, float& v) const;
    int getActiveChartCount() const;
    int getMaxChartID() const;
    int getValidChartCount() const { return validChartCount; }
    int getValidTexelCount() const { return validTexelCount; }
    bool isSceneSupported() const { return sceneSupported; }
    int getSceneType() const { return sceneType; }
    const std::string& getSceneLabel() const { return sceneLabel; }
    
    // Phase 2F: Get scene bounds for raymarch integration
    void getSceneBounds(glm::vec3& outMin, glm::vec3& outMax) const {
        outMin = boundsMin;
        outMax = boundsMax;
    }
    
    // Phase 3A: Get box bounds for chart classification
    void getBoxBounds(glm::vec3& outShortMin, glm::vec3& outShortMax,
                      glm::vec3& outTallMin, glm::vec3& outTallMax) const {
        outShortMin = shortBoxBmin;
        outShortMax = shortBoxBmax;
        outTallMin  = tallBoxBmin;
        outTallMax  = tallBoxBmax;
    }

    static const char* debugModeName(int mode);
    static const char* ringDebugModeName(int mode);
    static const char* radianceDebugModeName(int mode);
    static const char* debugTargetName(int target);

private:
    GLuint debugTexture;
    GLuint ringDebugTexture;
    GLuint radianceDebugTexture;
    GLuint ringAtlasTexture;
    GLuint directAtlasTexture;  // Phase 2B-6: single-frame direct radiance atlas
    
    // Phase 2D: Ping-pong atlases for persistent feedback
    GLuint atlasPing;
    GLuint atlasPong;
    bool writeToPing;  // true = write to ping, read from pong
    
    // Phase 2E: Cascade hierarchy textures
    static const int CASCADE_COUNT = 5;  // C0-C4
    GLuint cascadeAtlases[CASCADE_COUNT];  // One atlas per level
    int cascadeResolutions[CASCADE_COUNT]; // {32, 16, 8, 4, 2}
    
    // Atlas dimensions (Phase 2B-6)
    int atlasWidth;
    int atlasHeight;
    int ringAtlasWidth;
    int ringAtlasHeight;
    int ringCascadeCount;
    int ringBandHeight;
    
    int radianceDebugMode;
    int debugMode;
    int ringDebugMode;
    int debugTarget; // 0=chart atlas, 1=ring-packed atlas, 2=radiance skeleton atlas
    float rayBias;
    bool enabled;
    bool showDebug;
    bool sceneSupported;
    bool unsupportedWarningPrinted;
    int sceneType; // 0=unsupported/analytic, 1=Cornell-family, 2=Sponza-family
    int validChartCount;
    int validTexelCount;
    std::array<int, 18> chartActive;  // Phase 2C: expanded to 18 charts (5 room + 12 box + 1 reserved)
    std::vector<ChartDef> charts;
    std::string sceneLabel;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Phase 2C: Box geometry bounds for Cornell scene
    glm::vec3 shortBoxBmin;
    glm::vec3 shortBoxBmax;
    glm::vec3 tallBoxBmin;
    glm::vec3 tallBoxBmax;
};

#endif // SURFACE_RC_H
