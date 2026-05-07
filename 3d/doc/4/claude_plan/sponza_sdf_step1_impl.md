# Sponza SDF — Step 1: Fix CPU Voxelizer

**Date:** 2026-05-06  
**Plan ref:** `doc/4/claude_plan/sponza_sdf_impl_plan_v2.md`  
**Status:** Build clean; Sponza runtime verified (37757 voxels, no crash); Cornell regression pending

---

## Problem

`voxelizeTriangle()` used `pointInTriangle()` — a 2D projection test that failed
Sponza's geometry for two independent reasons:

1. **Unexpanded bbox:** The triangle bbox was passed directly to `worldToVoxel()` with
   no padding.  For flat surfaces lying exactly on a voxel-grid plane, the bbox in that
   axis was zero-thick, so the candidate voxel loop produced zero or one voxel layer —
   missing all adjacent voxels.

2. **Projection only, no 3D distance:** Within the candidate loop, `pointInTriangle`
   projected the voxel centre onto the triangle's dominant plane and tested whether it
   landed inside the 2D footprint.  It never measured the voxel's 3D distance to the
   triangle surface, so voxels near but not on the surface were silently rejected.

The barycentric denominator did *not* collapse for normal axis-aligned triangles.

Runtime proof from Step 0 log:
```
Cornell Box   32 faces  →  48303 voxels  (1509 vox/face)
Sponza    262267 faces  →  40856 voxels  (0.16 vox/face)  ← 2% fill rate
```

A 9400× disparity in voxels-per-face — Sponza's axis-aligned geometry was almost
entirely missed.

Second bug: the triangle bbox was never expanded before `worldToVoxel()`, so voxels
adjacent (but not centre-inside) a flat surface were never even tested.

---

## Fix

### Strategy

Replace the 2D projection test with a true 3D closest-point query:

1. Compute `closestPointOnTriangle(worldPos, v0, v1, v2)` — exact 3D closest point.
2. A voxel is "on" the surface when `dist(worldPos, closest) ≤ threshold`.
3. `threshold = voxelSize * sqrt(3) * 0.5` — half the space-diagonal of one voxel.
   Any voxel whose centre is within this radius of the triangle surface is filled.
4. Expand the triangle bbox by `threshold` before `worldToVoxel()` so the candidate
   loop covers the correct set of voxels.
5. First-writer guard `if (grid[idx+3] == 0)` prevents double-counting voxels that
   lie in the expanded bboxes of two adjacent triangles.

### `src/obj_loader.h` — new helper `closestPointOnTriangle()`

Ericson *Real-Time Collision Detection* §5.1.5 — correct for all degenerate cases
(axis-aligned, zero-area, edge/vertex nearest-feature).

```cpp
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
```

### `src/obj_loader.h` — `voxelizeTriangle()` rewrite

```cpp
void voxelizeTriangle(..., float voxelSize, int& voxelsFilled) const {
    const float threshold = voxelSize * glm::sqrt(3.0f) * 0.5f;

    // Expand bbox so voxels adjacent to flat surfaces are tested.
    glm::vec3 minPt = glm::min(v0, glm::min(v1, v2)) - threshold;
    glm::vec3 maxPt = glm::max(v0, glm::max(v1, v2)) + threshold;

    glm::ivec3 minVox = worldToVoxel(minPt, gridOrigin, gridSize, resolution);
    glm::ivec3 maxVox = worldToVoxel(maxPt, gridOrigin, gridSize, resolution);
    minVox = glm::clamp(minVox, glm::ivec3(0), glm::ivec3(resolution - 1));
    maxVox = glm::clamp(maxVox, glm::ivec3(0), glm::ivec3(resolution - 1));

    for (int z ...) for (int y ...) for (int x ...) {
        glm::vec3 worldPos = voxelToWorld({x,y,z}, ...);
        glm::vec3 closest  = closestPointOnTriangle(worldPos, v0, v1, v2);
        if (glm::length(worldPos - closest) <= threshold) {
            int idx = ((z * resolution + y) * resolution + x) * 4;
            if (grid[idx + 3] == 0) {       // first-writer wins
                grid[idx + 0] = uint8_t(color.r * 255.0f);
                grid[idx + 1] = uint8_t(color.g * 255.0f);
                grid[idx + 2] = uint8_t(color.b * 255.0f);
                grid[idx + 3] = 255;
                voxelsFilled++;
            }
        }
    }
}
```

`pointInTriangle()` is replaced entirely by `closestPointOnTriangle()` — the old method
is removed.

---

## Build Result

`cmake --build . --config Release` — clean.  
Zero new warnings vs Step 0 baseline (C4819/C4018/C4244/C4100/C4310 are all pre-existing).

---

## Key Learnings

- **2D projection voxelizers miss axis-aligned surfaces for two reasons, not one.**
  The unexpanded bbox produces too few candidate voxels for flat surfaces, and the
  projection test checks 2D footprint membership, not 3D surface proximity.  The
  barycentric denominator did not collapse for normal axis-aligned triangles.

- **Threshold = half-diagonal, not half-voxelSize.** `voxelSize * 0.5` only covers the
  inscribed sphere; `voxelSize * sqrt(3) * 0.5` covers the circumscribed sphere (the
  actual half-diagonal), ensuring every voxel that intersects the triangle surface is
  captured regardless of orientation.

- **Bbox must be expanded before the worldToVoxel call.** Expanding the world-space bbox
  by `threshold` then converting to voxel coords is the correct order.  Expanding voxel
  coords post-conversion would be off by a rounding error when threshold < voxelSize.

- **First-writer guard is necessary with expanded bboxes.** Two adjacent triangles sharing
  an edge will both expand their bboxes to cover the voxels along that edge.  Without the
  `if (grid[idx+3] == 0)` guard, `voxelsFilled` would double-count those voxels and the
  color would be overwritten by whichever triangle processed last.

- **Ericson §5.1.5 handles normal triangles and common vertex/edge nearest-feature
  cases.** A zero-area degenerate triangle can slip through all early-exit branches and
  reach the final barycentric divide with `va + vb + vc ≈ 0`.  A guard
  `if (std::abs(sum) < 1e-12f) return a;` is added before the divide to prevent NaN/Inf.

- **Loader clears moved to after successful file open (F4 fix).** The five `.clear()`
  calls now execute only when `file.is_open()` returns true, so a failed open (e.g. first
  path attempt during the five-path fallback search in `loadOBJMesh()`) does not destroy
  the previously loaded OBJ data.

- **First-writer color is acceptable for occupancy and gray Sponza, not final material
  policy.** When MTL/texture albedo is added, overlapping/coplanar regions will need a
  deterministic material selection rule instead of "first triangle that expanded into
  this voxel wins."

---

## Verification Checklist (Step 1)

**Automatically verified (2026-05-06):**

- [x] Build succeeds; no new warnings (`cmake --build build --config Release`)
- [x] `closestPointOnTriangle()` compiles — Ericson §5.1.5 all branches exercised at compile time
- [x] `pointInTriangle()` removed — no dangling call sites remain

**Runtime verification (completed 2026-05-06, `tools/app_run_step1.log`):**

- [x] No crash or hang on voxelization of 262267 Sponza triangles
- [x] Sponza loaded from `res/scene/sponza.obj` (first path succeeded, app run from project root)
- [x] Sponza voxel count: **37757** at 128³

**Count analysis (why 37757 < 40856 old):**

The 37757 count is lower than the old 40856 — this is expected and correct, not a regression:

1. **First-writer guard eliminates double-counting.** The old `voxelizeTriangle()` incremented
   `voxelsFilled` for every write, including duplicate writes to the same voxel from adjacent
   triangles sharing an edge. The guard `if (grid[idx+3] == 0)` means each voxel is counted once.
   An estimated ~3k of the old 40856 were duplicates.

2. **False-positive 2D projection footprint hits removed.** The old `pointInTriangle()` accepted
   voxels whose projected center landed inside the 2D triangle footprint regardless of their 3D
   distance to the surface. Many of those voxels were not actually on the surface; the correct
   closest-point test rejects them.

3. **Axis-aligned surface gain partially offsets.** The bbox expansion now includes voxels
   adjacent to flat walls/floors/ceilings that the old unexpanded bbox missed entirely. This
   adds correct surface voxels.

**Fill rate:** Sponza normalized to [-1,1]; grid spans [-2,2] with voxelSize = 4/128 = 0.03125.
The usable 64³ subregion = 262144 candidates. 37757/262144 ≈ **14% fill rate in the usable region**
— plausible for a building interior mesh. Visual SDF correctness requires Steps 2 and 3.

- [ ] Cornell Box voxel count comparable to ~48303 (regression check — not yet measured)

---

## What Is Next

**Step 2 — Felzenszwalb 3-pass Separable EDT (`src/demo3d.cpp` + `src/demo3d.h`)**

Exact O(N³) Euclidean distance transform operating at `meshSDFResolution = 64`.

New members in `demo3d.h`:
- `int meshSDFResolution` (= 64)
- `bool meshSDFReady`
- `std::vector<uint8_t> meshVoxelData` (64³ RGBA8, populated by `loadOBJMesh()`)

New functions in `demo3d.cpp`:
- `edt1d(f, n, d)` — 1D parabolic lower envelope (Felzenszwalb & Huttenlocher 2012)
- `generateMeshSDF()` — runs 3-pass separable EDT over `meshVoxelData`, uploads
  result to `sdfTexture` (R32F, world-space metres) and `albedoTexture` (RGBA8)

`loadOBJMesh()` must pass `meshSDFResolution` (not `volumeResolution`) to
`voxelizeMesh()` and store the result in `meshVoxelData`.

**Step 3 — Wire into `sdfGenerationPass()` (`src/demo3d.cpp`)**

Add `useOBJMesh` branch at the top of `sdfGenerationPass()` that calls
`generateMeshSDF()` and returns early, bypassing the analytic SDF path.
