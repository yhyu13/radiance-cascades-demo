/**
 * @file analytic_sdf.cpp
 * @brief Implementation of analytic SDF primitives
 */

#include "analytic_sdf.h"
#include <algorithm>
#include <cmath>
#include <limits>

AnalyticSDF::AnalyticSDF() {
    // Start with empty scene
}

void AnalyticSDF::addBox(const glm::vec3& center, const glm::vec3& size,
                         const glm::vec3& color) {
    Primitive prim;
    prim.type = PrimitiveType::BOX;
    prim.position = center;
    prim.scale = size * 0.5f;  // Store half-extents for SDF calculation
    prim.color = color;
    primitives.push_back(prim);
}

void AnalyticSDF::addSphere(const glm::vec3& center, float radius,
                            const glm::vec3& color) {
    Primitive prim;
    prim.type = PrimitiveType::SPHERE;
    prim.position = center;
    prim.scale = glm::vec3(radius);
    prim.color = color;
    primitives.push_back(prim);
}

void AnalyticSDF::clear() {
    primitives.clear();
}

float AnalyticSDF::evaluate(const glm::vec3& point) const {
    float minDistance = std::numeric_limits<float>::max();

    for (const auto& prim : primitives) {
        // Transform point to primitive's local space
        glm::vec3 localPoint = point - prim.position;

        float distance;
        switch (prim.type) {
            case PrimitiveType::BOX:
                distance = sdfBox(localPoint, prim.scale);
                break;
            case PrimitiveType::SPHERE:
                distance = sdfSphere(localPoint, prim.scale.x);
                break;
            default:
                distance = std::numeric_limits<float>::max();
                break;
        }

        minDistance = std::min(minDistance, distance);
    }

    return minDistance;
}

void AnalyticSDF::getBounds(glm::vec3& min, glm::vec3& max) const {
    if (primitives.empty()) {
        min = glm::vec3(0.0f);
        max = glm::vec3(0.0f);
        return;
    }

    min = glm::vec3(std::numeric_limits<float>::max());
    max = glm::vec3(-std::numeric_limits<float>::max());

    for (const auto& prim : primitives) {
        switch (prim.type) {
            case PrimitiveType::BOX: {
                glm::vec3 halfExtents = prim.scale;  // Already stored as half-extents
                glm::vec3 primMin = prim.position - halfExtents;
                glm::vec3 primMax = prim.position + halfExtents;
                min = glm::min(min, primMin);
                max = glm::max(max, primMax);
                break;
            }
            case PrimitiveType::SPHERE: {
                float radius = prim.scale.x;
                glm::vec3 primMin = prim.position - radius;
                glm::vec3 primMax = prim.position + radius;
                min = glm::min(min, primMin);
                max = glm::max(max, primMax);
                break;
            }
        }
    }
}

void AnalyticSDF::createCornellBox() {
    clear();

    // Cornell Box dimensions (unit cube from 0 to 1)
    const float boxSize = 1.0f;
    const float wallThickness = 0.05f;

    // Back wall (white) - at z = 0
    addBox(
        glm::vec3(boxSize * 0.5f, boxSize * 0.5f, -wallThickness * 0.5f),
        glm::vec3(boxSize, boxSize, wallThickness),
        glm::vec3(0.8f, 0.8f, 0.8f)  // White
    );

    // Left wall (red) - at x = 0
    addBox(
        glm::vec3(-wallThickness * 0.5f, boxSize * 0.5f, boxSize * 0.5f),
        glm::vec3(wallThickness, boxSize, boxSize),
        glm::vec3(0.8f, 0.2f, 0.2f)  // Red
    );

    // Right wall (green) - at x = 1
    addBox(
        glm::vec3(boxSize + wallThickness * 0.5f, boxSize * 0.5f, boxSize * 0.5f),
        glm::vec3(wallThickness, boxSize, boxSize),
        glm::vec3(0.2f, 0.8f, 0.2f)  // Green
    );

    // Floor (white) - at y = 0
    addBox(
        glm::vec3(boxSize * 0.5f, -wallThickness * 0.5f, boxSize * 0.5f),
        glm::vec3(boxSize, wallThickness, boxSize),
        glm::vec3(0.8f, 0.8f, 0.8f)  // White
    );

    // Ceiling (white) - at y = 1
    addBox(
        glm::vec3(boxSize * 0.5f, boxSize + wallThickness * 0.5f, boxSize * 0.5f),
        glm::vec3(boxSize, wallThickness, boxSize),
        glm::vec3(0.8f, 0.8f, 0.8f)  // White
    );

    // Front wall removed (camera looks through it)

    // Tall box in center-left
    addBox(
        glm::vec3(0.35f, 0.2f, 0.5f),
        glm::vec3(0.3f, 0.4f, 0.3f),
        glm::vec3(0.8f, 0.8f, 0.8f)  // White
    );

    // Short box in center-right
    addBox(
        glm::vec3(0.65f, 0.1f, 0.4f),
        glm::vec3(0.3f, 0.2f, 0.3f),
        glm::vec3(0.8f, 0.8f, 0.8f)  // White
    );
}

float AnalyticSDF::sdfBox(const glm::vec3& p, const glm::vec3& b) {
    // Signed distance to axis-aligned box
    // p: point in box's local space (box centered at origin)
    // b: box half-extents
    
    glm::vec3 d = glm::abs(p) - b;
    float outsideDist = glm::length(glm::max(d, 0.0f));
    float insideDist = std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
    
    return outsideDist + insideDist;
}

float AnalyticSDF::sdfSphere(const glm::vec3& p, float r) {
    // Signed distance to sphere
    // p: point in sphere's local space (sphere centered at origin)
    // r: sphere radius
    
    return glm::length(p) - r;
}
