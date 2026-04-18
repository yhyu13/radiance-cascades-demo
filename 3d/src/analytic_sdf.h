/**
 * @file analytic_sdf.h
 * @brief Analytic Signed Distance Field primitives for 3D Radiance Cascades
 * 
 * This header defines simple analytic SDF primitives (boxes, spheres) that can be
 * used for quick validation of the radiance cascade pipeline before implementing
 * full voxel-based SDF generation via Jump Flooding Algorithm.
 * 
 * Benefits:
 * - Immediate visual feedback without complex voxelization
 * - Perfect SDF accuracy (no discretization artifacts)
 * - Easy to debug and validate raymarching
 * - Fast evaluation (no texture lookups)
 * 
 * Limitations:
 * - Only supports primitive shapes (not arbitrary meshes)
 * - Must be replaced with voxel JFA for production use
 */

#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

class AnalyticSDF {
public:
    /**
     * @brief Supported primitive types
     */
    enum class PrimitiveType {
        BOX = 0,      ///< Axis-aligned box
        SPHERE = 1    ///< Sphere
    };

    /**
     * @brief Single primitive definition
     */
    struct Primitive {
        PrimitiveType type;     ///< Type of primitive
        glm::vec3 position;     ///< World space position (center)
        glm::vec3 scale;        ///< Scale dimensions (for box: half-extents, for sphere: radius in x)
        glm::vec3 color;        ///< Albedo color for visualization
        
        Primitive() 
            : type(PrimitiveType::BOX)
            , position(0.0f)
            , scale(1.0f)
            , color(1.0f) {}
    };

    /**
     * @brief Default constructor
     */
    AnalyticSDF();

    /**
     * @brief Add a box primitive to the scene
     * @param center World space center position
     * @param size Full size of the box (width, height, depth)
     * @param color Albedo color (default: white)
     */
    void addBox(const glm::vec3& center, const glm::vec3& size, 
                const glm::vec3& color = glm::vec3(1.0f));

    /**
     * @brief Add a sphere primitive to the scene
     * @param center World space center position
     * @param radius Sphere radius
     * @param color Albedo color (default: white)
     */
    void addSphere(const glm::vec3& center, float radius,
                   const glm::vec3& color = glm::vec3(1.0f));

    /**
     * @brief Remove all primitives from the scene
     */
    void clear();

    /**
     * @brief Get the number of primitives
     * @return Count of primitives
     */
    size_t getPrimitiveCount() const { return primitives.size(); }

    /**
     * @brief Get read-only access to primitives list
     * @return Reference to primitives vector
     */
    const std::vector<Primitive>& getPrimitives() const { return primitives; }

    /**
     * @brief Evaluate SDF at a given world space point
     * @param point World space position to evaluate
     * @return Signed distance (negative inside, positive outside)
     * 
     * This is the CPU version for debugging. GPU version is in shader.
     */
    float evaluate(const glm::vec3& point) const;

    /**
     * @brief Get axis-aligned bounding box of entire scene
     * @param min Output: minimum corner
     * @param max Output: maximum corner
     */
    void getBounds(glm::vec3& min, glm::vec3& max) const;

    /**
     * @brief Create Cornell Box test scene
     * 
     * Classic Cornell Box configuration:
     * - Back wall: white
     * - Left wall: red
     * - Right wall: green  
     * - Floor/ceiling: white
     * - Two boxes in center
     */
    void createCornellBox();

private:
    std::vector<Primitive> primitives;

    /**
     * @brief Compute SDF for a box
     * @param p Point in box's local space (centered at origin)
     * @param b Box half-extents
     * @return Signed distance
     */
    static float sdfBox(const glm::vec3& p, const glm::vec3& b);

    /**
     * @brief Compute SDF for a sphere
     * @param p Point in sphere's local space (centered at origin)
     * @param r Sphere radius
     * @return Signed distance
     */
    static float sdfSphere(const glm::vec3& p, float r);
};
