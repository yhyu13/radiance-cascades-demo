#include "reference_camera.h"

#include <glm/geometric.hpp>

#include <cmath>

ReferenceCamera::ReferenceCamera()
    : ReferenceCamera(glm::vec3(0.5f, 0.25f, 0.97f), glm::vec3(0.5f, 0.25f, 0.0f),
                      60.0f, 4.0f / 3.0f) {}

ReferenceCamera::ReferenceCamera(const glm::vec3& pos, const glm::vec3& target,
                                 float fovY, float aspectRatio) {
    position = pos;
    fovYDegrees = fovY;
    aspect = aspectRatio;
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
