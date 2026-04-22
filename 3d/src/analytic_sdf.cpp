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

    // Room interior: [-1, 1] in all dims. Walls 0.4 thick = ~6 voxels at 64^3 in a 4-unit volume.
    const float hs = 1.0f;   // interior half-size
    const float wt = 0.2f;   // wall half-thickness (full thickness = 0.4 = 6+ voxels at 64^3)
    const float ext = hs + wt; // outer half-size including wall

    // Back wall (white) - inner face at z = -hs, no front wall (camera looks through)
    addBox(glm::vec3(0.0f, 0.0f, -(hs + wt)),
           glm::vec3(2.0f * ext, 2.0f * ext, 2.0f * wt),
           glm::vec3(0.8f, 0.8f, 0.8f));

    // Floor (white) - inner face at y = -hs
    addBox(glm::vec3(0.0f, -(hs + wt), 0.0f),
           glm::vec3(2.0f * ext, 2.0f * wt, 2.0f * ext),
           glm::vec3(0.8f, 0.8f, 0.8f));

    // Ceiling (white) - inner face at y = +hs
    addBox(glm::vec3(0.0f, hs + wt, 0.0f),
           glm::vec3(2.0f * ext, 2.0f * wt, 2.0f * ext),
           glm::vec3(0.8f, 0.8f, 0.8f));

    // Left wall (red) - inner face at x = -hs
    addBox(glm::vec3(-(hs + wt), 0.0f, 0.0f),
           glm::vec3(2.0f * wt, 2.0f * ext, 2.0f * ext),
           glm::vec3(0.8f, 0.2f, 0.2f));

    // Right wall (green) - inner face at x = +hs
    addBox(glm::vec3(hs + wt, 0.0f, 0.0f),
           glm::vec3(2.0f * wt, 2.0f * ext, 2.0f * ext),
           glm::vec3(0.2f, 0.8f, 0.2f));

    // Tall box (left-center of room)
    addBox(glm::vec3(-0.35f, -0.5f, -0.1f),
           glm::vec3(0.5f, 1.0f, 0.5f),
           glm::vec3(0.8f, 0.8f, 0.8f));

    // Short box (right-center of room)
    addBox(glm::vec3(0.4f, -0.7f, 0.2f),
           glm::vec3(0.5f, 0.6f, 0.5f),
           glm::vec3(0.8f, 0.8f, 0.8f));
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
