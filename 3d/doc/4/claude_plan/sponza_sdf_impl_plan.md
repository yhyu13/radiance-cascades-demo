# Sponza OBJ → SDF: Implementation Plan

**Date:** 2026-05-06  
**Goal:** Replace the toy analytic-SDF Sponza approximation with an SDF baked from the real `res/scene/sponza.obj` mesh.

---

## Current State

### What exists

| Component | File | State |
|---|---|---|
| OBJ parse + normalize | `src/obj_loader.h` | Works |
| CPU voxelizer | `src/obj_loader.h:voxelizeTriangle` | **Buggy** — wrong surface test |
| loadOBJMesh() → upload voxels | `src/demo3d.cpp:4054` | Works, but stops here |
| GPU JFA SDF (`sdf_3d.comp`) | `res/shaders/sdf_3d.comp` | Loaded, **never dispatched** (TODO at demo3d.cpp:1302) |
| GPU voxelizer (`voxelize.comp`) | `res/shaders/voxelize.comp` | Loaded, **never dispatched** |

### Pipeline gap

After `loadOBJMesh()` uploads the CPU-voxelized data to `voxelGridTexture`, nothing
converts that into `sdfTexture`. The analytic SDF pass writes `sdfTexture` when enabled;
the mesh path leaves it zeroed.

### Sponza facts

- **262,267 faces**, 22.8 MB
- Raw coordinates ~[-430, +280] world units
- `OBJLoader::normalize()` scales to [-1, 1] on longest axis ✓
- Volume bounds: `origin=(-2,-2,-2)`, `size=(4,4,4)` → normalized Sponza fits in center half ✓
- Sponza materials (`arch`, `bricks`, `column_a`, …) all fall through to default gray in
  `getMaterialColor()` — acceptable: gray albedo works for GI; SDF needs only occupancy

---

## Root Causes

### Bug 1 — Wrong surface test in CPU voxelizer

`obj_loader.h:297` calls `pointInTriangle()`, which projects the voxel center onto the
triangle's dominant plane and tests whether the 2D projection lands inside the triangle.

Problem: for thin walls and floors (the majority of Sponza geometry), many voxel centers
are physically adjacent to the surface but their 2D projections fall *outside* the triangle.
Result: large gaps in the voxel grid → SDF has missing walls.

Fix: replace with **3D point-to-closest-point-on-triangle distance**. Mark a voxel as
surface if `dist3D < voxelDiagonal / 2` (half the voxel's spatial diagonal).

### Bug 2 — GPU JFA SDF never fires

`demo3d.cpp:1302`:
```cpp
// TODO: Implement full 3D JFA when ready
```
After the OBJ voxel upload, `sdfTexture` is cleared to zero and nothing more happens.

### Bug 3 — `sdf_3d.comp` JFA is internally broken (secondary)

The shader's seed-position propagation (Voronoi buffer) is entirely commented out as TODO.
Without seed positions, the JFA can only propagate distances within each step's 3×3×3
window — distances further than 1 voxel will be wrong. This shader needs a rewrite before
it can be used; we avoid it in this plan.

---

## Implementation Plan

### Overview

Three isolated changes, each buildable and testable independently:

```
Step 1: Fix obj_loader.h voxelizeTriangle()   (~30 lines changed)
Step 2: Add generateMeshSDF() to demo3d.cpp   (~60 lines new)
Step 3: Call generateMeshSDF() in loadOBJMesh() (~5 lines)
```

We deliberately **skip** fixing `sdf_3d.comp` and `voxelize.comp`. Both need major
rewrites and aren't needed when the CPU path is fast enough for a one-time bake.

---

## Step 1 — Fix CPU Voxelizer (`obj_loader.h`)

**File:** `src/obj_loader.h`  
**Method:** `OBJLoader::voxelizeTriangle()`  
**Lines affected:** ~270–360

### Replace `pointInTriangle` with point-to-triangle distance

Implement `closestPointOnTriangle(p, v0, v1, v2)` using the standard barycentric
clamp approach (Christer Ericson, *Real-Time Collision Detection*, §5.1.5):

```cpp
// Returns the closest point on triangle (v0,v1,v2) to point p.
static glm::vec3 closestPointOnTriangle(
    const glm::vec3& p,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    glm::vec3 ab = v1 - v0, ac = v2 - v0, ap = p - v0;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return v0;         // vertex region v0

    glm::vec3 bp = p - v1;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return v1;           // vertex region v1

    glm::vec3 cp = p - v2;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return v2;           // vertex region v2

    float vc = d1*d4 - d3*d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        float v = d1 / (d1 - d3);
        return v0 + v * ab;                          // edge v0-v1
    }
    float vb = d5*d2 - d1*d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        float w = d2 / (d2 - d6);
        return v0 + w * ac;                          // edge v0-v2
    }
    float va = d3*d6 - d5*d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return v1 + w * (v2 - v1);                  // edge v1-v2
    }
    float denom = 1.f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    return v0 + ab*v + ac*w;                         // inside triangle
}
```

Replace the `if (pointInTriangle(...))` mark block with:

```cpp
// Threshold: mark voxel if within half the voxel's spatial diagonal
float threshold = voxelSize * glm::sqrt(3.f) * 0.5f;

glm::vec3 closest = closestPointOnTriangle(worldPos, v0, v1, v2);
if (glm::length(worldPos - closest) <= threshold) {
    // mark voxel as surface
    ...
}
```

**Why `sqrt(3)/2 * voxelSize`:** the spatial diagonal of a cube-voxel has length
`sqrt(3) * voxelSize`. A voxel whose center is within half that distance from the surface
is guaranteed to intersect the surface.

### Performance note

262k triangles × CPU bounding-box loop at 64³:

- Average triangle projected BBox in voxel space: ~5–15 voxels per axis
- Per-triangle cost: O(bbox_x × bbox_y × bbox_z) = O(~1000 voxels max)
- Total: ~200M distance checks → **5–20 seconds one-time bake** at 64³

This is acceptable. The bake happens once per scene load. At 128³ it is 8× slower (~2 min),
which may require moving to GPU voxelization in a future phase.

---

## Step 2 — CPU BFS Distance Transform (`demo3d.cpp`)

**Why not fix `sdf_3d.comp`?** The GPU JFA needs a proper two-texture ping-pong for seed
positions (the Voronoi buffer), which requires shader rewrites and new GL texture allocations.
The CPU BFS at 64³ = 262k voxels runs in < 100 ms and produces correct distances — cheaper
to implement correctly right now.

### Algorithm: multi-source BFS (Euclidean approximation)

```cpp
// New private method on Demo3D
bool Demo3D::generateMeshSDF()
```

```
Input:  voxelData (uint8 RGBA, resolution³) — produced by OBJLoader::voxelize()
Output: sdfFloats (float32, resolution³)    — uploaded to sdfTexture

1. Mark surface voxels: voxel is surface if alpha > 0 (occupied).
2. BFS from all surface voxels simultaneously:
   - Queue all surface voxels with distance = 0.
   - For each neighbor (6-connected), if not visited:
       dist = euclid_distance(current_voxel_center, neighbor_voxel_center)
              propagated as running minimum
   - Use a min-heap (priority queue) for exact Euclidean BFS.
3. Normalize distances to [0, 1] by dividing by (voxelSize * sqrt(3) * resolution).
4. Upload sdfFloats to sdfTexture via glTexSubImage3D.
```

Exact Euclidean BFS (Meijster / Felzenszwalb variant) runs in O(N³) with small constant.
For 64³ = 262k voxels it completes in < 50 ms.

### Signature

```cpp
// demo3d.h — add to private section:
bool generateMeshSDF(const std::vector<uint8_t>& voxelData);

// demo3d.cpp — implementation:
bool Demo3D::generateMeshSDF(const std::vector<uint8_t>& voxelData) {
    int N = volumeResolution;
    int total = N * N * N;

    const float INF = 1e30f;
    std::vector<float> dist(total, INF);

    // Priority queue: (distance, linear_index)
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    float voxelSz = volumeSize.x / float(N);

    // Seed surface voxels
    for (int i = 0; i < total; ++i) {
        if (voxelData[i * 4 + 3] > 0) {  // alpha > 0 → occupied
            dist[i] = 0.f;
            pq.push({0.f, i});
        }
    }

    // 6-connected neighbors
    const int dx[] = {1,-1,0,0,0,0};
    const int dy[] = {0,0,1,-1,0,0};
    const int dz[] = {0,0,0,0,1,-1};

    while (!pq.empty()) {
        auto [d, idx] = pq.top(); pq.pop();
        if (d > dist[idx]) continue;  // stale entry

        int z = idx / (N*N), rem = idx % (N*N);
        int y = rem / N,     x = rem % N;

        for (int k = 0; k < 6; ++k) {
            int nx = x+dx[k], ny = y+dy[k], nz = z+dz[k];
            if (nx<0||ny<0||nz<0||nx>=N||ny>=N||nz>=N) continue;
            int ni = nz*N*N + ny*N + nx;
            float nd = d + voxelSz;  // unit step cost
            if (nd < dist[ni]) {
                dist[ni] = nd;
                pq.push({nd, ni});
            }
        }
    }

    // Upload to sdfTexture
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RED, GL_FLOAT, dist.data());
    glBindTexture(GL_TEXTURE_3D, 0);

    float maxDist = voxelSz * N * glm::sqrt(3.f);
    std::cout << "[Demo3D] Mesh SDF generated: " << total
              << " voxels, max dist ~" << maxDist << "m\n";
    return true;
}
```

**Note on distance units:** distances are in world-space meters (same units as `volumeSize`).
The raymarch shader (`raymarch.frag`) already reads `sdfTexture` in world-space units — no
change needed there.

---

## Step 3 — Wire Up in `loadOBJMesh()` (`demo3d.cpp`)

At the end of `Demo3D::loadOBJMesh()`, after the voxel upload (line ~4100), add:

```cpp
// Generate SDF from the freshly voxelized mesh
if (!generateMeshSDF(voxelData)) {
    std::cerr << "[ERROR] Mesh SDF generation failed\n";
    return false;
}
```

**Also:** ensure `loadOBJMesh` passes `voxelData` to `generateMeshSDF` before it goes
out of scope. Currently `voxelData` is a local `std::vector<uint8_t>` — pass by const ref
into `generateMeshSDF`, which reads it without copying.

---

## Verification Checklist

After implementation:

- [ ] Build succeeds (no compile errors)
- [ ] Run with `--scene sponza` or toggle via ImGui OBJ button
- [ ] Console shows: `[OBJLoader] Loaded: NNN vertices, 262267 faces`
- [ ] Console shows: `[OBJLoader] Voxelize complete: NNN voxels filled` (NNN > 0, expect tens of thousands)
- [ ] Console shows: `[Demo3D] Mesh SDF generated: 262144 voxels...`
- [ ] Mode 0 (GI): Sponza walls/pillars/floor visible, not a gray void
- [ ] Mode 5 (SDF heatmap): concentric-ring pattern emanating from walls — confirms SDF is populated
- [ ] No regression: analytic SDF mode still works after toggling back

---

## What Is Deliberately Skipped

| Skipped | Why |
|---|---|
| Fix `sdf_3d.comp` GPU JFA | Needs Voronoi 2-texture rewrite; CPU BFS is sufficient for now |
| Wire up `voxelize.comp` GPU pass | CPU voxelizer is faster to fix; GPU needed only at 128³+ |
| Sponza material colors | All gray is fine; real material lookup needs MTL parser (future) |
| Signed SDF (inside/outside) | Sponza is open-top; UDF is correct for surface raymarch |
| 128³ voxel grid | CPU voxelizer too slow at 128³; revisit with GPU pass |

---

## Future Work (not this phase)

- **GPU voxelizer**: rewrite `voxelize.comp` to use SSBO triangle buffer; dispatch after OBJ load for 128³+ support
- **GPU JFA SDF**: fix `sdf_3d.comp` with proper 2-texture ping-pong (rgba32f seed positions + r32f distances); dispatch log₂(N) passes  
- **MTL parser**: extend `OBJLoader` to read `sponza.mtl` and map Sponza material names to diffuse colors
- **Albedo texture**: bind Sponza's diffuse textures as 2D arrays; sample per-hit in `raymarch.frag`
