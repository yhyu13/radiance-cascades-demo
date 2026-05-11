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
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>
#include <charconv>      // Step 9: from_chars for fast numeric parsing
#include <chrono>        // Step 9: parse-time logging
#include <cstring>       // strchr
#include <cctype>        // tolower (Step 9 follow-up: name-based color hints)

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
    glm::vec3 diffuse  = glm::vec3(0.8f);  // Default gray (Kd)
    glm::vec3 ambient  = glm::vec3(0.1f);
    glm::vec3 specular = glm::vec3(0.5f);
    glm::vec3 emissive = glm::vec3(0.0f);  // Step 6: Ke (boosted into albedo)
    float shininess = 32.0f;
};

// Step 9 Phase 3 (codex 03 F4): flat triangle layout for the GPU voxelizer
// SSBO. std430-padded so each triangle is 64 bytes (4 vec4s).
struct GPUTriangle {
    glm::vec4 v0;        // .xyz = world position, .w = padding
    glm::vec4 v1;
    glm::vec4 v2;
    glm::vec4 colorKd;   // .xyz = Kd (with Ke albedo-boost from Step 6), .w = padding
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
        // Step 9 Phase 1 (codex 03 F8): rewrite uses whole-file read +
        // string_view + std::from_chars for both float lines (v/vn/vt) AND
        // face tokens (parseVertexIndexSV). Sponza-master 23.8MB / ~2-3 s
        // -> ~300-500 ms expected.
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[OBJLoader] Failed to open file: " << filename << std::endl;
            return false;
        }
        const std::streamsize fsize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string buf(fsize, '\0');
        if (!file.read(buf.data(), fsize)) {
            std::cerr << "[OBJLoader] Failed to read file: " << filename << std::endl;
            return false;
        }
        file.close();

        // Clear only after a successful read so a failed load preserves existing data.
        vertices.clear();
        normals.clear();
        texcoords.clear();
        faces.clear();
        faceMaterials.clear();
        materials.clear();
        unknownMaterialsLogged.clear();
        badIndexWarnings = 0;   // codex 12 F5
        badIndexDropped  = 0;

        // Step 6: directory of the .obj file, used to resolve mtllib paths.
        std::string objDir;
        {
            size_t slash = filename.find_last_of("/\\");
            if (slash != std::string::npos) objDir = filename.substr(0, slash + 1);
        }

        std::cout << "[OBJLoader] Loading: " << filename
                  << " (" << fsize << " bytes)" << std::endl;

        const auto t0 = std::chrono::high_resolution_clock::now();

        // Track vn/vt counts for negative-index resolution even though we
        // don't store the actual values (codex 03 F8 -- skip parse cost).
        size_t vnCount = 0;
        size_t vtCount = 0;
        std::string currentMaterial;

        // Walk the buffer line by line via string_view.
        const char* p   = buf.data();
        const char* end = p + buf.size();
        while (p < end) {
            const char* lineStart = p;
            while (p < end && *p != '\n' && *p != '\r') ++p;
            std::string_view line(lineStart, size_t(p - lineStart));
            // Skip CR, LF, CRLF.
            while (p < end && (*p == '\r' || *p == '\n')) ++p;
            if (line.empty()) continue;

            // Trim leading whitespace.
            size_t s0 = 0;
            while (s0 < line.size() && (line[s0] == ' ' || line[s0] == '\t')) ++s0;
            line = line.substr(s0);
            if (line.empty() || line[0] == '#') continue;

            // Identify prefix. Single chars 'v'/'f' are common; multi-char
            // prefixes 'vn', 'vt', 'usemtl', 'mtllib' need lookahead.
            const char c0 = line[0];
            const char c1 = line.size() > 1 ? line[1] : 0;

            if (c0 == 'v' && (c1 == ' ' || c1 == '\t')) {
                // Vertex position
                OBJVertex vertex;
                if (parse3Floats(line.substr(2),
                                 vertex.position.x, vertex.position.y, vertex.position.z)) {
                    vertices.push_back(vertex);
                }
            }
            else if (c0 == 'v' && c1 == 'n' && line.size() > 2 && (line[2] == ' ' || line[2] == '\t')) {
                // Vertex normal -- codex 03 F8: skip value parse, count only.
                ++vnCount;
            }
            else if (c0 == 'v' && c1 == 't' && line.size() > 2 && (line[2] == ' ' || line[2] == '\t')) {
                // Texture coord -- codex 03 F8: skip value parse, count only.
                ++vtCount;
            }
            else if (c0 == 'f' && (c1 == ' ' || c1 == '\t')) {
                // Face line: tokenize on whitespace, parse each token as v/vt/vn
                // via parseVertexIndexSV. Fan-triangulate. Codex 03 F8 fast path.
                parseFaceLine(line.substr(2), vnCount, vtCount, currentMaterial);
            }
            else if (line.substr(0, 7) == "usemtl ") {
                size_t name0 = 7;
                while (name0 < line.size() && (line[name0] == ' ' || line[name0] == '\t')) ++name0;
                size_t name1 = name0;
                while (name1 < line.size() && line[name1] != ' ' && line[name1] != '\t' && line[name1] != '\r') ++name1;
                currentMaterial = std::string(line.substr(name0, name1 - name0));
            }
            else if (line.substr(0, 7) == "mtllib ") {
                size_t name0 = 7;
                while (name0 < line.size() && (line[name0] == ' ' || line[name0] == '\t')) ++name0;
                size_t name1 = name0;
                while (name1 < line.size() && line[name1] != ' ' && line[name1] != '\t' && line[name1] != '\r') ++name1;
                std::string mtlFile(line.substr(name0, name1 - name0));
                if (!mtlFile.empty()) loadMTL(objDir + mtlFile);
            }
        }

        const auto t1 = std::chrono::high_resolution_clock::now();
        const double parseMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "[OBJLoader] Parse: " << parseMs << "ms"
                  << " (" << vertices.size() << " v, "
                  << vnCount << " vn-skipped, "
                  << vtCount << " vt-skipped, "
                  << faces.size() << " f, "
                  << materials.size() << " materials, "
                  << badIndexDropped << " dropped-tris)" << std::endl;
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
     * @brief Step 6: parse a .mtl file. Reads only `newmtl`, `Kd`, `Ke`.
     *        All other fields (`Ns`, `Ka`, `Ks`, `illum`, `Ni`, `d`, `map_*`) ignored.
     *        On failure, leaves `materials` empty so the default-gray fallback in
     *        voxelize() preserves prior behavior.
     */
    bool loadMTL(const std::string& mtlPath) {
        std::ifstream f(mtlPath);
        if (!f.is_open()) {
            std::cout << "[OBJLoader] Material library not found: " << mtlPath
                      << " (will use default gray)" << std::endl;
            return false;
        }
        std::cout << "[OBJLoader] Loading materials: " << mtlPath << std::endl;

        OBJMaterial cur;
        bool haveCur = false;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string p;
            iss >> p;
            if (p == "newmtl") {
                if (haveCur) materials[cur.name] = cur;
                cur = OBJMaterial();
                iss >> cur.name;
                haveCur = true;
            } else if (p == "Kd" && haveCur) {
                iss >> cur.diffuse.x >> cur.diffuse.y >> cur.diffuse.z;
            } else if (p == "Ke" && haveCur) {
                iss >> cur.emissive.x >> cur.emissive.y >> cur.emissive.z;
            }
        }
        if (haveCur) materials[cur.name] = cur;

        // Post-process: name-based color hints for Sponza-style placeholder
        // Kd values (0.4704 0.4704 0.4704 across all 25 materials means the
        // real color lives in textures we don't load yet). Cornell-Original
        // and other meshes with distinct .mtl Kd values are untouched -- the
        // override only fires when (a) the name matches a known pattern AND
        // (b) the existing Kd looks like a placeholder gray.
        applyNameBasedColorHints();

        std::cout << "[OBJLoader] Loaded " << materials.size() << " materials" << std::endl;
        return true;
    }

    /**
     * @brief Step 9 follow-up: name-based color hints for placeholder-Kd
     *        materials (Sponza-master + similar). Only overrides materials
     *        whose existing Kd is a near-gray in [0.3, 0.7] -- distinct
     *        colors (Cornell-Original red/green walls) are left intact.
     *        Quick win until proper texture loading lands.
     */
    void applyNameBasedColorHints() {
        const float TOL = 0.05f;
        int hits = 0;
        for (auto& kv : materials) {
            OBJMaterial& mat = kv.second;
            const bool looksGray =
                std::abs(mat.diffuse.r - mat.diffuse.g) < TOL &&
                std::abs(mat.diffuse.r - mat.diffuse.b) < TOL &&
                mat.diffuse.r > 0.3f && mat.diffuse.r < 0.7f;
            if (!looksGray) continue;
            glm::vec3 hint = sponzaMaterialHint(kv.first);
            if (hint.r < 0.0f) continue;   // sentinel: no override
            mat.diffuse = hint;
            ++hits;
        }
        if (hits > 0) {
            std::cout << "[OBJLoader] Applied " << hits
                      << " name-based color hints to placeholder materials" << std::endl;
        }
    }

    /** Substring-match a material name to a "make-sense" diffuse color.
     *  Returns vec3(-1) sentinel when no pattern matches.
     *  Codex 05 F4: per-letter suffix patterns (`_g`, `_c`, `_e`) use
     *  `endsWith` rather than naked `has` to avoid matching incidental
     *  letters anywhere in the name (e.g. `has("g")` would have flagged
     *  `background` or `flag` as gold-fabric). */
    static glm::vec3 sponzaMaterialHint(const std::string& name) {
        // Lowercase the name so we match regardless of asset capitalization.
        std::string n; n.reserve(name.size());
        for (char c : name) n.push_back(char(std::tolower(unsigned(c))));
        auto has = [&](const char* sub) { return n.find(sub) != std::string::npos; };
        auto endsWith = [&](const char* sub) {
            size_t L = std::strlen(sub);
            return n.size() >= L && n.compare(n.size() - L, L, sub) == 0;
        };

        if (has("brick"))                              return glm::vec3(0.65f, 0.45f, 0.35f); // reddish brown
        if (has("ceiling"))                            return glm::vec3(0.92f, 0.86f, 0.72f); // warm cream
        if (has("floor"))                              return glm::vec3(0.72f, 0.62f, 0.48f); // sandstone
        if (has("arch"))                               return glm::vec3(0.80f, 0.74f, 0.62f); // light stone
        if (has("column"))                             return glm::vec3(0.88f, 0.85f, 0.78f); // marble
        if (has("chain"))                              return glm::vec3(0.35f, 0.32f, 0.30f); // dark metal
        // Curtain colors (Crytek Sponza variants); has("dif") removed (codex 05 F3).
        if (has("curtain") && has("blue"))             return glm::vec3(0.20f, 0.30f, 0.65f); // blue velvet
        if (has("curtain") && has("green"))            return glm::vec3(0.20f, 0.55f, 0.30f); // green velvet
        if (has("curtain"))                            return glm::vec3(0.65f, 0.12f, 0.12f); // red velvet (default)
        // Fabric variants -- Sponza-master uses fabric_a/c/d/e/f/g suffixes.
        // codex 05 F4: use endsWith to keep the suffix bound; bare has("g")
        // would also match background, flag, etc.
        if (has("fabric") && endsWith("_g"))           return glm::vec3(0.70f, 0.50f, 0.20f); // gold-ish
        if (has("fabric") && (endsWith("_c") || endsWith("_e"))) return glm::vec3(0.55f, 0.35f, 0.55f); // purple
        if (has("fabric"))                             return glm::vec3(0.62f, 0.45f, 0.30f); // brown
        if (has("vase") && has("plant"))               return glm::vec3(0.30f, 0.50f, 0.22f); // foliage
        if (has("vase") && has("hang"))                return glm::vec3(0.78f, 0.55f, 0.20f); // copper
        if (has("vase"))                               return glm::vec3(0.70f, 0.42f, 0.28f); // terracotta
        if (has("lion"))                               return glm::vec3(0.72f, 0.50f, 0.22f); // bronze
        if (has("flag") || has("pole"))                return glm::vec3(0.78f, 0.65f, 0.30f); // gold
        if (has("roof"))                               return glm::vec3(0.45f, 0.28f, 0.22f); // dark tile
        if (has("thorn") || has("plant") || has("leaf")) return glm::vec3(0.30f, 0.50f, 0.22f); // green
        if (has("detail"))                             return glm::vec3(0.82f, 0.78f, 0.68f); // off-white trim
        if (has("background"))                         return glm::vec3(0.60f, 0.65f, 0.75f); // sky-ish
        return glm::vec3(-1.0f);  // sentinel
    }

    /**
     * @brief Get color for a material name (hardcoded Cornell Box materials).
     *        Step 6: kept as legacy fallback only -- voxelize() now consults
     *        the parsed `materials` map first.
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
     * @brief Step 9 Phase 3 (codex 03 F4): build flat triangle list for the
     *        GPU voxelizer SSBO. Reuses the SAME per-face material-color
     *        resolution chain as voxelize() (parsed .mtl Kd -> legacy
     *        getMaterialColor -> default gray, plus Ke albedo-boost from
     *        Step 6) so CPU and GPU voxelizers cannot drift on color.
     */
    void buildTriangles(std::vector<GPUTriangle>& out) const {
        out.clear();
        out.reserve(faces.size());
        for (size_t fi = 0; fi < faces.size(); ++fi) {
            const auto& face = faces[fi];
            const auto& v0 = vertices[face.v[0]].position;
            const auto& v1 = vertices[face.v[1]].position;
            const auto& v2 = vertices[face.v[2]].position;

            // Same material lookup as voxelize() -- parsed .mtl wins, then
            // legacy table, then default gray; Ke baked as albedo boost.
            std::string matName;
            if (faceMaterials.size() > fi) matName = faceMaterials[fi];
            glm::vec3 kd, ke(0.0f);
            auto mit = materials.find(matName);
            if (mit != materials.end()) {
                kd = mit->second.diffuse;
                ke = mit->second.emissive;
            } else {
                kd = getMaterialColor(matName);
            }
            glm::vec3 color = kd;
            float maxKe = std::max({ ke.x, ke.y, ke.z, 1.0f });
            if (maxKe > 1.0f) {
                color = glm::clamp(kd + ke / maxKe, glm::vec3(0.0f), glm::vec3(1.0f));
            }

            GPUTriangle t;
            t.v0      = glm::vec4(v0, 0.0f);
            t.v1      = glm::vec4(v1, 0.0f);
            t.v2      = glm::vec4(v2, 0.0f);
            t.colorKd = glm::vec4(color, 0.0f);
            out.push_back(t);
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
            
            // Step 6: per-face material lookup -- prefer parsed .mtl, fall back to
            // legacy hardcoded names, finally to default gray.
            std::string matName;
            if (faceMaterials.size() > &face - &faces[0]) {
                matName = faceMaterials[&face - &faces[0]];
            }
            glm::vec3 kd, ke;
            auto mit = materials.find(matName);
            if (mit != materials.end()) {
                kd = mit->second.diffuse;
                ke = mit->second.emissive;
            } else {
                // codex 12 F6: distinguish (a) legacy hardcoded match -- a real
                // recognized name, color is correct -- from (b) true default-gray
                // miss. Both used to log "Unknown material ..." which made the
                // legacy fallback path look like a failure.
                kd = getMaterialColor(matName);  // legacy hardcoded names
                ke = glm::vec3(0.0f);
                const glm::vec3 defaultGray(0.8f, 0.8f, 0.8f);
                const bool isLegacyHit = (kd != defaultGray);
                if (!matName.empty() && unknownMaterialsLogged.insert(matName).second) {
                    if (isLegacyHit) {
                        std::cout << "[OBJLoader] Material '" << matName
                                  << "' -> legacy fallback color "
                                  << kd.x << "," << kd.y << "," << kd.z
                                  << " (no .mtl entry)" << std::endl;
                    } else {
                        std::cout << "[OBJLoader] Material '" << matName
                                  << "' -> default gray (no .mtl entry, no legacy match)"
                                  << std::endl;
                    }
                }
            }

            // Step 6: bake Ke into the albedo voxel ("Kd + Ke as albedo boost").
            // For Cornell's `light` (Ke 17 12 4): max=17 -> ke/17 ~ (1, 0.7, 0.24);
            // saturate(Kd + ke/maxKe) makes that face glow warm-white. For Ke=0
            // this collapses to the existing Kd-only path (no change to walls/floors).
            glm::vec3 color = kd;
            float maxKe = std::max({ ke.x, ke.y, ke.z, 1.0f });
            if (maxKe > 1.0f) {
                color = glm::clamp(kd + ke / maxKe, glm::vec3(0.0f), glm::vec3(1.0f));
            }

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
    std::map<std::string, OBJMaterial> materials;       // Step 6: parsed from .mtl
    mutable std::set<std::string> unknownMaterialsLogged;  // Step 6: dedupe warnings
    int badIndexWarnings = 0;   // codex 12 F5: bounded out-of-range face counter
    int badIndexDropped  = 0;
    
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

    // =========================================================================
    // Step 9 Phase 1 fast-path helpers (codex 03 F8): from_chars + string_view.
    // =========================================================================

    /** Parse 3 floats from a string_view (whitespace-separated). Returns true
     *  iff all 3 parsed successfully. */
    static bool parse3Floats(std::string_view sv, float& x, float& y, float& z) {
        auto skipWS = [&]() {
            while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
        };
        auto parseOne = [&](float& out) -> bool {
            skipWS();
            if (sv.empty()) return false;
            const char* b = sv.data();
            const char* e = b + sv.size();
            auto r = std::from_chars(b, e, out);
            if (r.ec != std::errc()) return false;
            sv.remove_prefix(size_t(r.ptr - b));
            return true;
        };
        return parseOne(x) && parseOne(y) && parseOne(z);
    }

    /** Parse a single OBJ face token of the form "v", "v/t", "v//n", or "v/t/n".
     *  v required; t and n default to -1 if absent. Indices may be negative. */
    static bool parseVertexIndexSV(std::string_view tok, int& v, int& t, int& n) {
        v = 0; t = -1; n = -1;
        if (tok.empty()) return false;
        const char* b = tok.data();
        const char* e = b + tok.size();

        auto r = std::from_chars(b, e, v);
        if (r.ec != std::errc()) return false;
        b = r.ptr;
        if (b >= e || *b != '/') return true;            // "v"
        ++b;                                             // skip '/'
        if (b < e && *b != '/') {
            r = std::from_chars(b, e, t);
            if (r.ec != std::errc()) return false;
            b = r.ptr;
        }
        if (b >= e || *b != '/') return true;            // "v/t"
        ++b;                                             // skip second '/'
        if (b < e) {
            r = std::from_chars(b, e, n);
            if (r.ec != std::errc()) return false;
        }
        return true;
    }

    /** Parse the body of an `f` line (already past the "f " prefix), tokenize
     *  on whitespace, fan-triangulate, push triangles into `faces` +
     *  `faceMaterials` while honoring negative indices and bounds-checking
     *  resolved indices. Mirror of the Step 6 stringstream-based path. */
    void parseFaceLine(std::string_view body, size_t /*vnCount*/, size_t /*vtCount*/,
                       const std::string& currentMaterial)
    {
        // Tokenize on whitespace.
        std::vector<int> fv, ft, fn;
        size_t i = 0, N = body.size();
        while (i < N) {
            while (i < N && (body[i] == ' ' || body[i] == '\t')) ++i;
            if (i >= N) break;
            size_t j = i;
            while (j < N && body[j] != ' ' && body[j] != '\t' && body[j] != '\r') ++j;
            std::string_view tok = body.substr(i, j - i);
            i = j;

            int v = 0, t = -1, n = -1;
            if (!parseVertexIndexSV(tok, v, t, n)) continue;
            if (v < 0)              v = static_cast<int>(vertices.size())  + v + 1;
            if (t < 0 && t != -1)   t = static_cast<int>(texcoords.size()) + t + 1;
            if (n < 0 && n != -1)   n = static_cast<int>(normals.size())   + n + 1;
            fv.push_back(v - 1);
            ft.push_back(t == -1 ? -1 : t - 1);
            fn.push_back(n == -1 ? -1 : n - 1);
        }
        // Fan-triangulate (v0, vi, vi+1) with bounds-check (codex 12 F5).
        const int vcount = static_cast<int>(vertices.size());
        for (size_t k = 1; k + 1 < fv.size(); ++k) {
            int a = fv[0], b = fv[k], c = fv[k+1];
            if (a < 0 || a >= vcount || b < 0 || b >= vcount || c < 0 || c >= vcount) {
                if (badIndexWarnings < 8) {
                    std::cerr << "[OBJLoader] WARN: face vertex index out of range "
                              << "(a=" << a << " b=" << b << " c=" << c
                              << " vcount=" << vcount << "), dropping triangle\n";
                    ++badIndexWarnings;
                    if (badIndexWarnings == 8)
                        std::cerr << "[OBJLoader] (further out-of-range warnings suppressed)\n";
                }
                ++badIndexDropped;
                continue;
            }
            OBJFace face;
            face.v[0] = a; face.t[0] = ft[0];   face.n[0] = fn[0];
            face.v[1] = b; face.t[1] = ft[k];   face.n[1] = fn[k];
            face.v[2] = c; face.t[2] = ft[k+1]; face.n[2] = fn[k+1];
            faces.push_back(face);
            faceMaterials.push_back(currentMaterial);
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
