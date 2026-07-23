#ifndef REFERENCE_PIPELINE_H
#define REFERENCE_PIPELINE_H

#include "reference_camera.h"
#include "reference_cornell_scene.h"
#include "reference_rc_atlases.h"

#include <GL/glew.h>

#include <cstdint>
#include <vector>

// Phase 7: owns the validated reference frame pipeline (scene upload, atlas
// allocation, per-frame C5->C0 hierarchy with feedback/merge, atomic swap)
// and the final consumer dispatch. Used by the G9 validation harness and the
// interactive reference render mode. Requires a current GL context.

class ReferenceRcPipeline final {
public:
    ReferenceRcPipeline() = default;
    ~ReferenceRcPipeline();
    ReferenceRcPipeline(const ReferenceRcPipeline&) = delete;
    ReferenceRcPipeline& operator=(const ReferenceRcPipeline&) = delete;

    bool initialize();
    void shutdown() noexcept;

    // One complete frame: C5 -> C0 with upper merge and previous-generation
    // C0 feedback, then a single atomic swap. Returns false (and does not
    // swap) when a required dispatch cannot be issued.
    bool runFrame();

    // Final consumer into an RGBA32F texture of width x height using the
    // completed read[C0] view. referenceEnabled=false renders the declared
    // baseline (sky + direct only).
    bool renderFinalView(GLuint target, int width, int height, bool referenceEnabled);

    const ReferenceCornellScene& scene() const { return scene_; }
    ReferenceRcAtlases& atlases() { return atlases_; }
    const ReferenceRcAtlases& atlases() const { return atlases_; }
    const ReferenceCamera& camera() const { return camera_; }
    uint64_t generation() const { return atlases_.historyGeneration(); }
    void invalidateHistory() { atlases_.invalidateHistory(); }

    // Display-only mapping for human viewing (does not affect transport or
    // validated pixels; validation keeps the 1/1 defaults).
    void setDisplayMapping(float exposure, float invGamma) {
        exposure_ = exposure;
        invGamma_ = invGamma;
    }

private:
    ReferenceCornellScene scene_;
    ReferenceCamera camera_;
    ReferenceRcAtlases atlases_;
    GLuint sceneBuffer_ = 0;
    GLuint transportShader_ = 0;
    float exposure_ = 1.0f;
    float invGamma_ = 1.0f;
    bool initialized_ = false;
};

#endif
