#ifndef REFERENCE_LEGACY_PIPELINE_H
#define REFERENCE_LEGACY_PIPELINE_H

#include "reference_camera.h"
#include "reference_legacy_scene.h"
#include "reference_rc_atlases.h"

#include <GL/glew.h>

#include <cstdint>

// Legacy Cornell pipeline: same frame structure as the parity pipeline
// (C5->C0 hierarchy, previous-generation C0 feedback, atomic swap, final
// consumer) over the legacy Cornell chart layout (1344x256 physical).
class ReferenceLegacyPipeline final {
public:
    ReferenceLegacyPipeline() : atlases_(reflegacy::kPhysicalWidth,
                                         reflegacy::kPhysicalHeight) {}
    ~ReferenceLegacyPipeline();
    ReferenceLegacyPipeline(const ReferenceLegacyPipeline&) = delete;
    ReferenceLegacyPipeline& operator=(const ReferenceLegacyPipeline&) = delete;

    bool initialize();
    void shutdown() noexcept;
    bool runFrame();
    bool renderFinalView(GLuint target, int width, int height, bool referenceEnabled);

    const ReferenceLegacyCornellScene& scene() const { return scene_; }
    ReferenceRcAtlases& atlases() { return atlases_; }
    const ReferenceRcAtlases& atlases() const { return atlases_; }
    const ReferenceCamera& camera() const { return camera_; }
    uint64_t generation() const { return atlases_.historyGeneration(); }
    void invalidateHistory() { atlases_.invalidateHistory(); }
    void setDisplayMapping(float exposure, float invGamma) {
        exposure_ = exposure;
        invGamma_ = invGamma;
    }

private:
    ReferenceLegacyCornellScene scene_;
    ReferenceCamera camera_{glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f),
                          60.0f, 4.0f / 3.0f};
    ReferenceRcAtlases atlases_;
    GLuint sceneBuffer_ = 0;
    GLuint transportShader_ = 0;
    float exposure_ = 1.0f;
    float invGamma_ = 1.0f;
    bool initialized_ = false;
};

#endif
