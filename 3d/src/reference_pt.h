#ifndef REFERENCE_PT_H
#define REFERENCE_PT_H

#include "reference_camera.h"
#include "reference_cornell_scene.h"
#include "reference_legacy_scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Deterministic CPU path tracer for the locked parity scene, used as the
// same-scene/same-camera quality reference for the surface-RC kernel.
//
// Convention (deliberately matched to the RC final consumer, see plan 4.7):
//   Lo = reflectance * (E_cosine[Li] + direct_sun)
// where E_cosine is the expectation under the normalized cosine pdf (the RC
// weighting sums to ~1 over the hemisphere), direct sun is added without a
// 1/pi factor, and reflective surfaces mirror perfectly. This estimates the
// same fixed point the RC kernel approximates; remaining differences are
// angular discretization, noise, and the locked "reflective = zero" kernel
// policy (documented as a deliberate difference).

struct ReferencePtOptions {
    int width = 640;
    int height = 480;
    int samplesPerPixel = 64;
    int maxBounces = 5;
    uint32_t seed = 0x9e3779b9u;
    int threads = 0;              // 0 = hardware concurrency
    bool reflectiveZero = false;  // true: apply the RC kernel's locked
                                  // reflective=zero policy (mirror surfaces
                                  // absorb) to isolate the policy difference
};

struct ReferencePtResult {
    std::vector<glm::vec3> pixels;  // linear, width*height
    int width = 0;
    int height = 0;
    int samplesPerPixel = 0;
    double seconds = 0.0;
    uint64_t raysTraced = 0;
};

ReferencePtResult renderReferencePT(const ReferenceCornellScene& scene,
                                    const ReferenceCamera& camera,
                                    const ReferencePtOptions& options);

ReferencePtResult renderReferencePT(const ReferenceLegacyCornellScene& scene,
                                    const ReferenceCamera& camera,
                                    const ReferencePtOptions& options);

#endif
