/**
 * @file obj_loader.h
 * @brief Simple OBJ file loader for mesh import
 * 
 * Minimal implementation to load triangle meshes from OBJ files
 * and convert them to voxel data for the radiance cascade pipeline.
 */

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>

struct OBJVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
};

struct OBJFace {
    int v[3];  // Vertex indices (0-based)
    int n[3];  // Normal indices (0-based, -1 if not present)
    int t[3];  // Texcoord indices (0-based, -1 if not present)
};

struct OBJMaterial {
    std::string name;
    glm::vec3 diffuse = glm::vec3(0.8f);  // Default gray
    glm::vec3 ambient = glm::vec3(0.1f);
    glm::vec3 specular = glm::vec3(0.5f);
    float shininess = 32.0f;
};

class OBJLoader {
public:
    OBJLoader() {}
    
    /**
     * @brief Load OBJ file and parse geometry
     * @param filename Path to .obj file
     * @return true if successful
     */
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[OBJLoader] Failed to open file: " << filename << std::endl;
            return false;
        }

        // Clear only after a successful open so a failed load preserves existing data.
        vertices.clear();
        normals.clear();
        texcoords.clear();
        faces.clear();
        faceMaterials.clear();

        std::cout << "[OBJLoader] Loading: " << filename << std::endl;
        
        std::string line;
        std::string currentMaterial;
        
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;
            
            if (prefix == "v") {
                // Vertex position
                OBJVertex vertex;
                iss >> vertex.position.x >> vertex.position.y >> vertex.position.z;
                vertices.push_back(vertex);
            }
            else if (prefix == "vn") {
                // Vertex normal
                glm::vec3 normal;
                iss >> normal.x >> normal.y >> normal.z;
                normals.push_back(normal);
            }
            else if (prefix == "vt") {
                // Texture coordinate
                glm::vec2 texcoord;
                iss >> texcoord.x >> texcoord.y;
                texcoords.push_back(texcoord);
            }
            else if (prefix == "f") {
                // Face (triangle)
                OBJFace face;
                std::string v1, v2, v3;
                iss >> v1 >> v2 >> v3;
                
                parseVertexIndex(v1, face.v[0], face.t[0], face.n[0]);
                parseVertexIndex(v2, face.v[1], face.t[1], face.n[1]);
                parseVertexIndex(v3, face.v[2], face.t[2], face.n[2]);
                
                // Convert to 0-based indexing
                face.v[0]--; face.v[1]--; face.v[2]--;
                if (face.n[0] != -1) face.n[0]--;
                if (face.n[1] != -1) face.n[1]--;
                if (face.n[2] != -1) face.n[2]--;
                if (face.t[0] != -1) face.t[0]--;
                if (face.t[1] != -1) face.t[1]--;
                if (face.t[2] != -1) face.t[2]--;
                
                faces.push_back(face);
                faceMaterials.push_back(currentMaterial);
            }
            else if (prefix == "usemtl") {
                // Material reference
                iss >> currentMaterial;
            }
            else if (prefix == "mtllib") {
                // Material library (not implemented yet)
                std::string mtlFile;
                iss >> mtlFile;
                std::cout << "[OBJLoader] Material library referenced: " << mtlFile 
                          << " (not loaded)" << std::endl;
            }
        }
        
        file.close();
        
        std::cout << "[OBJLoader] Loaded: " << vertices.size() << " vertices, "
                  << faces.size() << " faces" << std::endl;
        
        return true;
    }
    
    /**
     * @brief Get bounding box of loaded mesh
     */
    void getBounds(glm::vec3& min, glm::vec3& max) const {
        if (vertices.empty()) {
            min = glm::vec3(0.0f);
            max = glm::vec3(0.0f);
            return;
        }
        
        min = vertices[0].position;
        max = vertices[0].position;
        
        for (const auto& v : vertices) {
            min = glm::min(min, v.position);
            max = glm::max(max, v.position);
        }
    }
    
    /**
     * @brief Center and scale mesh to fit within [-halfExtent, halfExtent] box.
     * Step 4 (codex 08 F2): per-OBJ scale -- Cornell uses 1.0 (legacy default),
     * Sponza uses 1.9 to fill the SDF volume with a 5% boundary margin.
     */
    void normalize(float halfExtent) {
        if (vertices.empty()) return;

        glm::vec3 min, max;
        getBounds(min, max);

        glm::vec3 center = (min + max) * 0.5f;
        glm::vec3 extent = max - min;
        float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));

        if (maxExtent > 0.0f) {
            float scale = (2.0f * halfExtent) / maxExtent;
            for (auto& v : vertices) {
                v.position = (v.position - center) * scale;
            }
        }

        std::cout << "[OBJLoader] Mesh normalized to [-" << halfExtent
                  << ", " << halfExtent << "] bounds (scale=" << ((maxExtent > 0.0f) ? (2.0f * halfExtent) / maxExtent : 0.0f)
                  << ")" << std::endl;
    }

    /** Backwards-compat overload -- old callers expect [-1, 1] normalization. */
    inline void normalize() { normalize(1.0f); }
    
    /**
     * @brief Get color for a material name (hardcoded Cornell Box materials)
     */
    static glm::vec3 getMaterialColor(const std::string& materialName) {
        if (materialName == "BloodyRed" || materialName == "Red") {
            return glm::vec3(0.65f, 0.05f, 0.05f);  // Red wall
        }
        else if (materialName == "Green") {
            return glm::vec3(0.12f, 0.45f, 0.15f);  // Green wall
        }
        else if (materialName == "Light") {
            return glm::vec3(1.0f, 1.0f, 0.9f);     // Light source (warm white)
        }
        else if (materialName == "Khaki" || materialName == "White") {
            return glm::vec3(0.75f, 0.75f, 0.75f);  // White walls/floor/ceiling
        }
        else {
            return glm::vec3(0.8f, 0.8f, 0.8f);     // Default gray
        }
    }
    
    /**
     * @brief Voxelize mesh into a 3D grid
     * @param resolution Grid resolution (e.g., 128)
     * @param grid Output voxel grid (resolution³ x 4 RGBA)
     * @param gridOrigin World space origin of the grid
     * @param gridSize World space size of the grid
     */
    void voxelize(int resolution, std::vector<uint8_t>& grid, 
                  const glm::vec3& gridOrigin, const glm::vec3& gridSize) const {
        if (vertices.empty() || faces.empty()) {
            std::cerr << "[OBJLoader] No geometry to voxelize!" << std::endl;
            return;
        }
        
        std::cout << "[OBJLoader] Voxelizing mesh to " << resolution << "³ grid..." << std::endl;
        
        // Initialize grid to empty (transparent black)
        grid.assign(resolution * resolution * resolution * 4, 0);
        
        float voxelSize = gridSize.x / resolution;  // Assuming cubic grid
        
        // Voxelize each triangle
        int voxelsFilled = 0;
        for (const auto& face : faces) {
            const auto& v0 = vertices[face.v[0]].position;
            const auto& v1 = vertices[face.v[1]].position;
            const auto& v2 = vertices[face.v[2]].position;
            
            // Get material color for this face
            std::string matName = "";
            if (faceMaterials.size() > &face - &faces[0]) {
                matName = faceMaterials[&face - &faces[0]];
            }
            glm::vec3 color = getMaterialColor(matName);
            
            // Rasterize triangle into voxels
            voxelizeTriangle(v0, v1, v2, color, resolution, grid, 
                           gridOrigin, gridSize, voxelSize, voxelsFilled);
        }
        
        std::cout << "[OBJLoader] Voxelize complete: " << voxelsFilled << " voxels filled" << std::endl;
    }
    
private:
    std::vector<OBJVertex> vertices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<OBJFace> faces;
    std::vector<std::string> faceMaterials;
    
    /**
     * @brief Parse vertex index string (v/vt/vn or v//vn format)
     */
    void parseVertexIndex(const std::string& str, int& v, int& t, int& n) {
        std::istringstream iss(str);
        std::string token;
        
        // Vertex index (required)
        std::getline(iss, token, '/');
        v = std::stoi(token);
        
        // Texture coordinate index (optional)
        if (iss.peek() == '/') {
            iss.get();  // Skip '/'
            t = -1;  // No texcoord
        } else {
            std::getline(iss, token, '/');
            t = token.empty() ? -1 : std::stoi(token);
        }
        
        // Normal index (optional)
        if (iss.peek() == '/') {
            iss.get();  // Skip '/'
            std::getline(iss, token);
            n = token.empty() ? -1 : std::stoi(token);
        } else {
            n = -1;
        }
    }
    
    /**
     * @brief Voxelize a single triangle using closest-point distance (Ericson §5.1.5).
     *        Bbox is expanded by half-diagonal of a voxel so axis-aligned surfaces
     *        (Sponza walls/floors/ceilings) are captured correctly.
     */
    void voxelizeTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                         const glm::vec3& color, int resolution, std::vector<uint8_t>& grid,
                         const glm::vec3& gridOrigin, const glm::vec3& gridSize,
                         float voxelSize, int& voxelsFilled) const {
        // Half-diagonal of a voxel: a point within this distance of the triangle
        // surface is considered "on" the surface.
        const float threshold = voxelSize * glm::sqrt(3.0f) * 0.5f;

        // Expand bbox by threshold so voxels adjacent to flat surfaces are tested.
        glm::vec3 minPt = glm::min(v0, glm::min(v1, v2)) - threshold;
        glm::vec3 maxPt = glm::max(v0, glm::max(v1, v2)) + threshold;

        glm::ivec3 minVox = worldToVoxel(minPt, gridOrigin, gridSize, resolution);
        glm::ivec3 maxVox = worldToVoxel(maxPt, gridOrigin, gridSize, resolution);
        minVox = glm::clamp(minVox, glm::ivec3(0), glm::ivec3(resolution - 1));
        maxVox = glm::clamp(maxVox, glm::ivec3(0), glm::ivec3(resolution - 1));

        for (int z = minVox.z; z <= maxVox.z; ++z) {
            for (int y = minVox.y; y <= maxVox.y; ++y) {
                for (int x = minVox.x; x <= maxVox.x; ++x) {
                    glm::vec3 worldPos = voxelToWorld(glm::ivec3(x, y, z),
                                                     gridOrigin, gridSize, resolution);
                    glm::vec3 closest = closestPointOnTriangle(worldPos, v0, v1, v2);
                    if (glm::length(worldPos - closest) <= threshold) {
                        int idx = ((z * resolution + y) * resolution + x) * 4;
                        if (grid[idx + 3] == 0) {  // first writer wins; no double-count
                            grid[idx + 0] = static_cast<uint8_t>(color.r * 255.0f);
                            grid[idx + 1] = static_cast<uint8_t>(color.g * 255.0f);
                            grid[idx + 2] = static_cast<uint8_t>(color.b * 255.0f);
                            grid[idx + 3] = 255;
                            voxelsFilled++;
                        }
                    }
                }
            }
        }
    }
    
    /**
     * @brief Convert world position to voxel coordinates
     */
    glm::ivec3 worldToVoxel(const glm::vec3& worldPos, const glm::vec3& gridOrigin,
                           const glm::vec3& gridSize, int resolution) const {
        glm::vec3 normalized = (worldPos - gridOrigin) / gridSize;
        return glm::ivec3(normalized * static_cast<float>(resolution));
    }
    
    /**
     * @brief Convert voxel coordinates to world position (center of voxel)
     */
    glm::vec3 voxelToWorld(const glm::ivec3& voxel, const glm::vec3& gridOrigin,
                          const glm::vec3& gridSize, int resolution) const {
        glm::vec3 normalized = (glm::vec3(voxel) + 0.5f) / static_cast<float>(resolution);
        return gridOrigin + normalized * gridSize;
    }
    
    /**
     * @brief Closest point on triangle abc to point p (Ericson Real-Time Collision Detection §5.1.5).
     *        Works correctly for degenerate (axis-aligned) triangles unlike a 2D projection test.
     */
    glm::vec3 closestPointOnTriangle(const glm::vec3& p,
                                     const glm::vec3& a,
                                     const glm::vec3& b,
                                     const glm::vec3& c) const {
        glm::vec3 ab = b - a, ac = c - a, ap = p - a;
        float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        glm::vec3 bp = p - b;
        float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;

        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            return a + (d1 / (d1 - d3)) * ab;

        glm::vec3 cp = p - c;
        float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            return a + (d2 / (d2 - d6)) * ac;

        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);

        float sum = va + vb + vc;
        if (std::abs(sum) < 1e-12f) return a;  // degenerate/zero-area triangle
        float denom = 1.0f / sum;
        return a + ab * (vb * denom) + ac * (vc * denom);
    }
};
