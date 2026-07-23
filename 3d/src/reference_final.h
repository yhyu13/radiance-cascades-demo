#ifndef REFERENCE_FINAL_H
#define REFERENCE_FINAL_H

#include "reference_camera.h"
#include "reference_cornell_scene.h"
#include "reference_transport.h"

#include <glm/glm.hpp>

// Phase 7 final consumer (G9): the native LightingView over the completed
// reference C0 read view. Declared native display policy (recorded in every
// report, cannot alter G1-G8 atlas parity):
//   reconstruction: exact four C0 direction bins (same addressing as
//                   hit-chart feedback), read only from the completed C0 view;
//   albedo:         visible-surface reflectance is applied;
//   one_over_pi:    not applied (stored bins are already directionally
//                   integrated by the locked weights);
//   direct_light:   composited separately per pixel (C0 bins carry only
//                   indirect radiance; the point's own direct is never in
//                   the bins, so no double counting);
//   reflective:     zero (parity kernel contribution), black uncharted black;
//   sky:            GetSkyLight(ray direction);
//   display_map:    linear, no tone curve in the validated path.

namespace reffinal {

struct LightingViewPolicy {
    bool reconstructFourBins = true;
    bool applyVisibleSurfaceAlbedo = true;
    bool applyOneOverPi = false;
    bool compositeDirectSeparately = true;
};

struct FinalSample {
    glm::vec3 rgb{0.0f};
    ReferenceChartId chartId = ReferenceChartId::Invalid;
    glm::vec2 chartUv{-1.0f};
    bool hit = false;
    bool sky = true;
};

// CPU oracle for the final consumer. fetchC0 reads the completed read view;
// passing referenceEnabled=false yields the declared baseline (sky + direct
// only), which the G9 disabled-reference check compares against.
// chartUvOffset (in normalized chart UV units) perturbs only the four-bin
// lookup, used to classify bin-boundary conformance pixels where CPU and GPU
// float traces legitimately land on adjacent C0 bins.
FinalSample shadeFinalView(const ReferenceCornellScene& scene,
                           const glm::vec3& origin, const glm::vec3& direction,
                           const reftransport::C0Fetch& fetchC0,
                           bool referenceEnabled,
                           const LightingViewPolicy& policy = {},
                           const glm::vec2& chartUvOffset = glm::vec2(0.0f));

}  // namespace reffinal

#endif
