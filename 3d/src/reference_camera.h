#ifndef REFERENCE_CAMERA_H
#define REFERENCE_CAMERA_H

#include <glm/glm.hpp>

// Deterministic native display camera for the Phase 7 final consumer.
// The captured ShaderToy snapshot is a cubemap renderer with no camera; this
// camera is declared native display policy, recorded in the LightingView
// schema, and does not affect G1-G8 reference atlas parity.

struct ReferenceCamera {
    glm::vec3 position{0.5f, 0.22f, 0.55f};
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
    float fovYDegrees = 80.0f;
    float aspect = 4.0f / 3.0f;

    ReferenceCamera();

    // NDC in [-1,1] with +y up; returns a normalized world ray.
    glm::vec3 ray(const glm::vec2& ndc) const;
    float tanHalfFov() const;
};

#endif
