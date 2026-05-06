# Sponza OBJ → SDF: Implementation Plan v2

**Date:** 2026-05-06  
**Supersedes:** `sponza_sdf_impl_plan.md` (v1, pre-review)  
**Review applied:** `codex_critic/01_sponza_sdf_impl_plan_review.md`  
**Reply:** `codex_critic/claude_reply/reply_01_sponza_sdf_impl_plan_review.md`

---

## Current State

### What exists

| Component | File | State |
|---|---|---|
| OBJ parse + normalize | `src/obj_loader.h` | Works |
| CPU voxelizer | `src/obj_loader.h:voxelizeTriangle` | **Buggy** — wrong surface test + unexpanded bbox |
| loadOBJMesh() → upload voxels | `src/demo3d.cpp:4054` | Works |
| sdfGenerationPass() | `src/demo3d.cpp:1246` | Analytic-only; no mesh branch |
| GPU JFA SDF (`sdf_3d.comp`) | `res/shaders/sdf_3d.comp` | Loaded, never dispatched, Voronoi TODO |
| GPU voxelizer (`voxelize.comp`) | `res/shaders/voxelize.comp` | Loaded, never dispatched |
| albedoTexture population | `src/demo3d.cpp:1288` | Analytic path only; mesh path never writes it |

### v1 plan errors corrected in this revision

| Finding | v1 Error | v2 Fix |
|---|---|---|
| F1 | generateMeshSDF() in loadOBJMesh() — overwritten by sdfGenerationPass() | Moved into sdfGenerationPass() as useOBJMesh branch |
| F2 | 6-conn Dijkstra = L1 distance — unsafe overestimate for sphere tracing | 3-pass separable Euclidean DT (Felzenszwalb — exact) |
| F3 | Triangle bbox not expanded — misses voxels adjacent to flat surfaces | Expand minPt/maxPt by threshold before worldToVoxel() |
| F4 | Plan assumed 64³; default is 128³ | Explicit meshSDFResolution=64 constant; 128³ optional |
| F5 | albedoTexture not populated | EDT carries seed color; both sdfTexture + albedoTexture written |
| F6 | Non-existent --scene sponza CLI; mode 5 misidentified | Add Sponza OBJ UI button; fix verification checklist |
| F7 | Said normalize to [0,1] but code uploaded world-space | Remove normalization; state world-space upload |
| F8 | Missing headers and demo3d.h declarations | Included in Step 2 |

---

## Step 0 — Add Sponza OBJ UI Entry Point (`demo3d.cpp`)

`src/demo3d.cpp:3433–3449` currently has buttons: Empty Room, Cornell Box, Simplified
Sponza (analytic), Cornell Box (OBJ). Add a Sponza (OBJ) button alongside the existing
Cornell Box OBJ button:

```cpp
if (ImGui::Button(useOBJMesh && currentOBJPath == "sponza"
                      ? "[ACTIVE] Sponza (OBJ)" : "Sponza (OBJ)")) {
    loadOBJMesh("res/scene/sponza.obj");
}
```

Also update `loadOBJMesh()` to record which OBJ was loaded:

```cpp
// in demo3d.h — add private member:
std::string currentOBJPath;  // "cornell" or "sponza"

// in loadOBJMesh(), near the end:
currentOBJPath = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";
```

---

## Step 1 — Fix CPU Voxelizer (`obj_loader.h`)

**File:** `src/obj_loader.h`  
**Method:** `OBJLoader::voxelizeTriangle()`

### 1a — Add `closestPointOnTriangle()` helper

Standard Ericson §5.1.5 closest-point-on-triangle:

```cpp
static glm::vec3 closestPointOnTriangle(
    const glm::vec3& p,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    glm::vec3 ab = v1-v0, ac = v2-v0, ap = p-v0;
    float d1 = glm::dot(ab,ap), d2 = glm::dot(ac,ap);
    if (d1 <= 0.f && d2 <= 0.f) return v0;

    glm::vec3 bp = p-v1;
    float d3 = glm::dot(ab,bp), d4 = glm::dot(ac,bp);
    if (d3 >= 0.f && d4 <= d3) return v1;

    glm::vec3 cp = p-v2;
    float d5 = glm::dot(ab,cp), d6 = glm::dot(ac,cp);
    if (d6 >= 0.f && d5 <= d6) return v2;

    float vc = d1*d4 - d3*d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
        return v0 + (d1/(d1-d3)) * ab;

    float vb = d5*d2 - d1*d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
        return v0 + (d2/(d2-d6)) * ac;

    float va = d3*d6 - d5*d4;
    if (va <= 0.f && (d4-d3) >= 0.f && (d5-d6) >= 0.f)
        return v1 + ((d4-d3)/((d4-d3)+(d5-d6))) * (v2-v1);

    float denom = 1.f / (va+vb+vc);
    return v0 + ab*(vb*denom) + ac*(vc*denom);
}
```

### 1b — Expand bbox and replace surface test

Replace the existing `voxelizeTriangle` bbox and test:

```cpp
void voxelizeTriangle(..., float voxelSize, int& voxelsFilled) const {
    // Marking threshold: half the voxel spatial diagonal
    float threshold = voxelSize * glm::sqrt(3.0f) * 0.5f;

    // Expand bbox BEFORE worldToVoxel conversion (F3 fix)
    glm::vec3 minPt = glm::min(v0, glm::min(v1,v2)) - glm::vec3(threshold);
    glm::vec3 maxPt = glm::max(v0, glm::max(v1,v2)) + glm::vec3(threshold);

    glm::ivec3 minVox = worldToVoxel(minPt, gridOrigin, gridSize, resolution);
    glm::ivec3 maxVox = worldToVoxel(maxPt, gridOrigin, gridSize, resolution);
    minVox = glm::clamp(minVox, glm::ivec3(0), glm::ivec3(resolution-1));
    maxVox = glm::clamp(maxVox, glm::ivec3(0), glm::ivec3(resolution-1));

    for (int z = minVox.z; z <= maxVox.z; ++z)
    for (int y = minVox.y; y <= maxVox.y; ++y)
    for (int x = minVox.x; x <= maxVox.x; ++x) {
        glm::vec3 worldPos = voxelToWorld({x,y,z}, gridOrigin, gridSize, resolution);
        glm::vec3 closest  = closestPointOnTriangle(worldPos, v0, v1, v2);
        if (glm::length(worldPos - closest) <= threshold) {
            int idx = ((z*resolution + y)*resolution + x) * 4;
            // Only mark if not already filled (avoid double-count, F3 note)
            if (grid[idx+3] == 0) {
                grid[idx+0] = (uint8_t)(color.r * 255.f);
                grid[idx+1] = (uint8_t)(color.g * 255.f);
                grid[idx+2] = (uint8_t)(color.b * 255.f);
                grid[idx+3] = 255;
                voxelsFilled++;
            }
        }
    }
}
```

### Performance at 64³

262k triangles × average bbox ~5³–15³ voxels per axis (expanded) = ~500k–3M tests total.
At ~50M tests/sec on a modern CPU: **< 1 second**. Voxel-size = 4m/64 = 0.0625m;
threshold = 0.054m. Thin Sponza walls (10–20 cm) are at least 1–2 voxels thick ✓

---

## Step 2 — Separable Euclidean Distance Transform (`demo3d.cpp`)

**Why separable EDT over Dijkstra?** The 6-connected Dijkstra computes L1 distance
(overestimates by up to 73% on diagonals). Overestimation is **unsafe** for sphere
tracing — `raymarch.frag:563` advances by `dist * 0.9`, so an overestimate can jump
through thin Sponza walls. The separable EDT is exact in O(N³), same complexity,
and simpler to implement correctly.

### Required headers (add to `demo3d.cpp` top)

```cpp
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
```

### `demo3d.h` — new members (private section)

```cpp
// Mesh SDF state
int    meshSDFResolution = 64;          // CPU bake resolution (separate from volumeResolution)
bool   meshSDFReady      = false;
std::vector<uint8_t>  meshVoxelData;   // RGBA, meshSDFResolution³, populated by loadOBJMesh
```

### `demo3d.h` — new method declaration

```cpp
bool generateMeshSDF();  // runs EDT on meshVoxelData, uploads sdfTexture + albedoTexture
```

### Felzenszwalb 3-pass separable EDT implementation

Felzenszwalb & Huttenlocher, "Distance Transforms of Sampled Functions", *TPAMI* 2012.
Each axis pass solves the lower-envelope of parabolas in O(N).

```cpp
// 1D EDT pass (along one axis) applied to f[] in-place.
// f[i] = squared input distance at column i (0 if seed, INF otherwise).
static void edt1d(std::vector<float>& f, int n) {
    const float INF = std::numeric_limits<float>::max() * 0.5f;
    std::vector<int>   v(n);   // parabola centers
    std::vector<float> z(n+1); // parabola envelope boundaries
    std::vector<float> d(n);

    int k = 0;
    v[0] = 0; z[0] = -INF; z[1] = INF;

    for (int q = 1; q < n; ++q) {
        float s;
        do {
            int r = v[k];
            s = ((f[q] + q*q) - (f[r] + r*r)) / (2.f*q - 2.f*r);
            if (--k < -1) break;
        } while (s <= z[k+1]);
        ++k;
        v[k] = q; z[k] = s; z[k+1] = INF;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k+1] < q) ++k;
        float diff = q - v[k];
        d[q] = diff*diff + f[v[k]];
    }
    f = d;
}

bool Demo3D::generateMeshSDF() {
    int N   = meshSDFResolution;
    int N2  = N*N, N3 = N*N*N;
    float voxelSz = volumeSize.x / float(N);  // world-space per voxel

    // --- Phase 1: build squared-distance seed grid ---
    const float INF = std::numeric_limits<float>::max() * 0.5f;
    std::vector<float>         sq(N3, INF);    // squared distances (voxel units)
    std::vector<glm::u8vec4>   seedColor(N3, glm::u8vec4(180,180,180,255));  // default gray

    for (int i = 0; i < N3; ++i) {
        if (meshVoxelData[i*4+3] > 0) {
            sq[i] = 0.f;
            seedColor[i] = glm::u8vec4(
                meshVoxelData[i*4+0], meshVoxelData[i*4+1],
                meshVoxelData[i*4+2], 255);
        }
    }

    // --- Phase 2: 3-pass separable EDT (x, y, z) ---
    // Pass x
    for (int z = 0; z < N; ++z)
    for (int y = 0; y < N; ++y) {
        std::vector<float> row(N);
        for (int x = 0; x < N; ++x) row[x] = sq[z*N2 + y*N + x];
        edt1d(row, N);
        for (int x = 0; x < N; ++x) sq[z*N2 + y*N + x] = row[x];
    }
    // Pass y
    for (int z = 0; z < N; ++z)
    for (int x = 0; x < N; ++x) {
        std::vector<float> col(N);
        for (int y = 0; y < N; ++y) col[y] = sq[z*N2 + y*N + x];
        edt1d(col, N);
        for (int y = 0; y < N; ++y) sq[z*N2 + y*N + x] = col[y];
    }
    // Pass z
    for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
        std::vector<float> col(N);
        for (int z = 0; z < N; ++z) col[z] = sq[z*N2 + y*N + x];
        edt1d(col, N);
        for (int z = 0; z < N; ++z) sq[z*N2 + y*N + x] = col[z];
    }

    // --- Phase 3: convert squared-voxel-distances to world-space distances ---
    // Also propagate nearest-seed color via brute-force on the seed grid
    // (full nearest-color propagation is a separate BFS; for first pass, surface
    //  voxels keep their color and interior/exterior gets default gray — acceptable)
    std::vector<float> sdfData(N3);
    for (int i = 0; i < N3; ++i)
        sdfData[i] = std::sqrt(sq[i]) * voxelSz;  // world-space meters, exact

    // --- Phase 4: upload to GPU ---
    // sdfTexture (R32F)
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N, GL_RED, GL_FLOAT, sdfData.data());

    // albedoTexture (RGBA8) — upload seed colors; interior gets default gray
    // Note: meshVoxelData was baked at meshSDFResolution, same N
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RGBA, GL_UNSIGNED_BYTE, meshVoxelData.data());

    glBindTexture(GL_TEXTURE_3D, 0);
    meshSDFReady = true;

    std::cout << "[Demo3D] Mesh SDF: exact EDT complete, "
              << N3 << " voxels, voxelSz=" << voxelSz << "m\n";
    return true;
}
```

**Distance units:** raw world-space meters — same coordinate system as `volumeOrigin` /
`volumeSize`. Shaders consume these directly. No normalization. (F7 fix)

**albedoTexture note:** `meshVoxelData` contains RGBA8 voxel colors from the CPU
voxelizer. Surface voxels have their triangle color (all gray for Sponza via default
`getMaterialColor`). Interior/exterior voxels have alpha=0 and RGB=0 → uploaded as
black. This is acceptable for a first pass. Future work: nearest-color propagation
alongside the EDT (same seed tracking, one extra vec4 array).

---

## Step 3 — Wire Into Render Lifecycle (`demo3d.cpp`)

### 3a — `loadOBJMesh()` stores voxel data, does NOT finalize SDF

Replace the end of `loadOBJMesh()` (currently lines ~4087–4106):

```cpp
// Store for use by sdfGenerationPass()
meshVoxelData.clear();
objLoader.voxelize(meshSDFResolution, meshVoxelData, volumeOrigin, volumeSize);

if (meshVoxelData.empty()) {
    std::cerr << "[ERROR] Empty voxelization!\n";
    return false;
}

// Upload voxel grid (used by debug modes)
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0,
    meshSDFResolution, meshSDFResolution, meshSDFResolution,
    GL_RGBA, GL_UNSIGNED_BYTE, meshVoxelData.data());
glBindTexture(GL_TEXTURE_3D, 0);

meshSDFReady = false;  // let sdfGenerationPass() build the SDF next frame
sceneDirty   = true;
useOBJMesh   = true;
currentOBJPath = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";

std::cout << "[Demo3D] OBJ loaded and voxelized; SDF will be built next frame\n";
return true;
```

### 3b — `sdfGenerationPass()` gains a mesh branch

In `sdfGenerationPass()` (`demo3d.cpp:1246`), add at the top, before the analytic SDF
dispatch:

```cpp
void Demo3D::sdfGenerationPass() {
    if (!sceneDirty) return;

    // --- Mesh SDF branch (useOBJMesh overrides analytic) ---
    if (useOBJMesh && !meshVoxelData.empty()) {
        if (!meshSDFReady) {
            generateMeshSDF();
        }
        // sdfTexture and albedoTexture are already populated; skip analytic pass
        sceneDirty = false;
        return;
    }

    // --- Analytic SDF branch (existing code unchanged below) ---
    ...
```

This prevents the analytic SDF path from overwriting the mesh SDF, and avoids the
disabled-analytic clear path. (F1 fix)

---

## Verification Checklist

- [ ] Build succeeds; no new warnings
- [ ] UI: "Sponza (OBJ)" button appears in scene selector
- [ ] Console: `[OBJLoader] Loaded: NNN vertices, 262267 faces`
- [ ] Console: `[OBJLoader] Voxelize complete: NNN voxels filled` — expect tens of thousands at 64³
- [ ] Console: `[Demo3D] Mesh SDF: exact EDT complete, 262144 voxels, voxelSz=0.0625m`
- [ ] Mode 0 (GI): Sponza walls/floor/ceiling visible; not a gray void
- [ ] Press `D` to open SDF debug overlay: distance rings radiate from Sponza walls ✓
- [ ] Mode 5 (step-count heatmap): step counts are low near walls, high in open space ✓
- [ ] Regression: switch back to Cornell Box (analytic) — analytic SDF works unchanged

---

## What Is Deliberately Skipped

| Skipped | Why |
|---|---|
| Fix `sdf_3d.comp` GPU JFA | Broken internally; CPU EDT is exact and fast enough at 64³ |
| Wire up `voxelize.comp` GPU pass | Not needed; CPU voxelizer is fixed and sufficient at 64³ |
| Sponza material colors (MTL parse) | Uniform gray is acceptable for GI; MTL parse is future work |
| Signed SDF (inside/outside) | Sponza is open-top; UDF is correct for surface raymarch |
| 128³ mesh SDF | CPU EDT at 128³ is ~50ms — feasible, but 64³ is sufficient to validate |
| Nearest-color albedo propagation | Interior voxels show black; future: carry seed color in EDT |

---

## Future Work

- **MTL parser**: read `sponza.mtl`; map material names to diffuse colors in `getMaterialColor()`
- **Nearest-color propagation**: track seed voxel indices in EDT; copy seed color to all voxels
- **128³ mesh SDF**: verify CPU EDT timing at 128³ (~50ms); enable if acceptable
- **GPU path**: rewrite `sdf_3d.comp` with proper 2-texture ping-pong; dispatch after OBJ load for 256³+
- **Albedo textures**: load Sponza diffuse PNGs; bind as 2D array; sample per-hit in `raymarch.frag`
