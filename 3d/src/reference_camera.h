#ifndef REFERENCE_CAMERA_H
#define REFERENCE_CAMERA_H

#include <glm/glm.hpp>

// Deterministic native display camera for the Phase 7 final consumer.
// The captured ShaderToy snapshot is a cubemap renderer with no camera; this
// camera is declared native display policy, recorded in the LightingView
// schema, and does not affect G1-G8 reference atlas parity.

struct ReferenceCamera {
    // Matches the legacy Demo3D Cornell view: camera looks into the box from
    // the +z side with fovy 60 (legacy pose (0,0,4)->(0,0,0) fovy 60 on the
    // legacy scene). The parity room is enclosed, so the camera sits just
    // inside the z=1 wall and looks toward z=0 with the same framing and FOV.
    glm::vec3 position{0.5f, 0.25f, 0.97f};
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
    float fovYDegrees = 60.0f;
    float aspect = 4.0f / 3.0f;

    ReferenceCamera();
    ReferenceCamera(const glm::vec3& position, const glm::vec3& target,
                    float fovYDegrees, float aspect);

    // NDC in [-1,1] with +y up; returns a normalized world ray.
    glm::vec3 ray(const glm::vec2& ndc) const;
    float tanHalfFov() const;
};

#endif
