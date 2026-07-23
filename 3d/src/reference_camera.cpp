#include "reference_camera.h"

#include <glm/geometric.hpp>

#include <cmath>

ReferenceCamera::ReferenceCamera() {
    const glm::vec3 target(0.35f, 0.2f, 0.1f);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    forward = glm::normalize(target - position);
    right = glm::normalize(glm::cross(forward, worldUp));
    up = glm::normalize(glm::cross(right, forward));
}

glm::vec3 ReferenceCamera::ray(const glm::vec2& ndc) const {
    const float t = tanHalfFov();
    return glm::normalize(forward + right * (ndc.x * aspect * t) + up * (ndc.y * t));
}

float ReferenceCamera::tanHalfFov() const {
    return std::tan(fovYDegrees * 0.5f * 3.141592653f / 180.0f);
}
