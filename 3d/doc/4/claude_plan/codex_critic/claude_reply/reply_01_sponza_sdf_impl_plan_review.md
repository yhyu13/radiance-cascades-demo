# Reply to Review 01 — Sponza OBJ to SDF Implementation Plan

**Date:** 2026-05-06  
**Reviewing:** `doc/4/claude_plan/codex_critic/01_sponza_sdf_impl_plan_review.md`

---

## Summary

All 8 findings are accepted. Four are high-severity errors that would cause the
implementation to silently fail or produce an unsafe SDF. The plan is revised below.

---

## Finding-by-Finding Response

### Finding 1 — High: Wrong place to finalize sdfTexture

**Accept fully.**

The reviewer is correct that placing `generateMeshSDF()` at the end of `loadOBJMesh()`
races against the render lifecycle. The sequence after OBJ load is:

```
loadOBJMesh() → uploads voxelGridTexture, writes sdfTexture [proposed]
             → sets sceneDirty = true
next frame   → render() → sdfGenerationPass()
             → analyticSDFEnabled=true (default) → overwrites sdfTexture
               OR analyticSDFEnabled=false       → clears sdfTexture
```

Either path destroys the mesh SDF before the first frame renders.

**Fix:** move `generateMeshSDF()` into `sdfGenerationPass()` as the `useOBJMesh`
branch. The generation is idempotent (same voxel data → same SDF), so it can safely
run every time `sceneDirty` is true — or guard with a `meshSDFReady` flag to skip
recomputation on subsequent frames.

### Finding 2 — High: Proposed BFS computes L1 not Euclidean distance

**Accept.**

The 6-connected Dijkstra with uniform `voxelSz` edge cost accumulates Manhattan distance.
For a voxel at grid offset `(1,1,0)` from the nearest surface: proposed cost = `2*voxelSz`,
true Euclidean = `sqrt(2)*voxelSz ≈ 1.414*voxelSz`. The proposed method **overestimates**
by up to 73% on diagonals.

Overestimation is unsafe for sphere tracing: `raymarch.frag:563` advances by the raw SDF
value (`dist * 0.9`), so an overestimate can step through a thin Sponza wall in one jump.

**Fix:** replace the priority-queue BFS with a proper separable Euclidean Distance
Transform (EDT). The Felzenszwalb/Meijster 3-pass parabola-envelope algorithm is exact,
runs in O(N³) time, and is straightforward to implement:

```
Pass 1: for each (y,z) column, compute 1D EDT along x
Pass 2: for each (x,z) column, update with 1D EDT along y
Pass 3: for each (x,y) column, update with 1D EDT along z
```

Each 1D pass is O(N) using the lower-envelope of parabolas. At 64³ this is
3 × 64² × 64 = ~800k operations — negligible. At 128³: ~6.3M operations, still < 10ms.

This produces **exact** Euclidean distances. No safety scaling needed.

If the separable EDT is deemed too complex for a first pass, a 26-connected Dijkstra with
diagonal costs `sqrt(2)*voxelSz` (face diagonals) and `sqrt(3)*voxelSz` (body diagonals)
approximates Euclidean with max error ~3.4%, which is still an overestimate. In that case
scale all distances by 0.9 to convert the overestimate into a conservative underestimate.
The plan now targets the separable EDT for correctness.

### Finding 3 — High: Voxelizer bbox must expand by threshold

**Accept fully.**

For any face-aligned Sponza surface (floor, wall, ceiling), the triangle bbox in world
space is nearly degenerate on the normal axis. After conversion to voxel coords, it
spans only 1 voxel on that axis. Voxels immediately adjacent to the surface on the
outside never enter the bounding-box loop and are never tested.

**Fix:**

```cpp
float threshold = voxelSize * glm::sqrt(3.0f) * 0.5f;
glm::vec3 minPt = glm::min(v0, glm::min(v1, v2)) - glm::vec3(threshold);
glm::vec3 maxPt = glm::max(v0, glm::max(v1, v2)) + glm::vec3(threshold);
```

Apply this before the `worldToVoxel()` conversion. The closest-point test then runs on
the correctly expanded candidate set.

### Finding 4 — High: Plan assumes 64³, default is 128³

**Accept.**

`DEFAULT_VOLUME_RESOLUTION = 128` (`demo3d.h:51`). The plan's performance estimates
and voxel counts were all for 64³ (262k voxels), but the actual OBJ voxelizer call
uses `volumeResolution = 128` (2.1M voxels, 8× cost).

**Fix:** explicitly reduce resolution for the mesh SDF path in the first phase. The plan
now includes an explicit `meshSDFResolution = 64` constant that overrides `volumeResolution`
for the CPU voxelizer and EDT. The cascade system still runs at the configured
`volumeResolution`; only the mesh SDF bake operates at 64³.

Alternative: bake at 128³ from the start, accept the higher CPU cost, and add a progress
print every 10k triangles. This is workable (262k triangles at ~500 voxel bbox average =
~130M tests, ~5–30 seconds depending on CPU).

The revised plan documents both options clearly and defaults to 64³.

### Finding 5 — Medium: albedoTexture not populated

**Accept.**

`radiance_3d.comp:255` and `raymarch.frag:468` both read from `uAlbedo`
(`albedoTexture`). Without populating it from mesh data, the Sponza scene will either
use stale analytic-scene albedo or render with empty albedo (black/default).

**Fix:** the separable EDT already identifies the nearest surface seed for each voxel.
Carry that seed's voxel color (from `voxelGridTexture`) alongside the distance. After the
EDT completes, write colors to a `std::vector<glm::u8vec4> albedoData` in parallel with
`sdfData`, then upload both:

```cpp
glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N, GL_RED, GL_FLOAT, sdfData.data());
// bind albedoTexture:
glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N, GL_RGBA, GL_UNSIGNED_BYTE, albedoData.data());
```

For this first phase, surface voxels carry their voxelized triangle color (all-gray for
Sponza since `getMaterialColor` falls through), interior/exterior voxels carry the nearest
surface voxel's color. This gives a uniform gray scene — acceptable.

### Finding 6 — Medium: Verification references non-existent paths

**Accept.**

Neither `--scene sponza` nor a Sponza OBJ UI button exists. `loadOBJMesh` is hardcoded
to `cornell_box.obj` in the Cornell Box OBJ button handler (`demo3d.cpp:3449`).
Mode 5 is step-count heatmap, not SDF heatmap; SDF validation uses `sdf_debug.frag`
(press `D`).

**Fix to plan:** add to Step 3 a UI button "Sponza (OBJ)" that calls
`loadOBJMesh("res/scene/sponza.obj")`. Update verification checklist accordingly.

### Finding 7 — Medium: Normalization contradiction

**Accept.**

The algorithm description said "normalize to [0,1]" but the code snippet uploaded
raw world-space float distances. The shaders consume world-space distances directly.

**Fix:** remove the normalization sentence. Upload raw world-space distances in units
of `volumeSize` (meters). No change to shaders needed.

### Finding 8 — Low: Missing headers

**Accept.**

The `generateMeshSDF` code needs `<vector>`, `<cmath>`, and (for the Dijkstra variant)
`<queue>`, `<functional>`, `<utility>`. The Felzenszwalb EDT replacement needs only
`<vector>` and `<cmath>`. `demo3d.h` needs the private method declaration and the
`meshSDFReady` flag. Noted in the revised plan.

---

## What Changes in the Plan

| Finding | Plan Change |
|---|---|
| F1 | Move `generateMeshSDF()` into `sdfGenerationPass()` as `useOBJMesh` branch |
| F2 | Replace 6-conn BFS with 3-pass separable Euclidean DT (Felzenszwalb) |
| F3 | Expand triangle bbox by `threshold` before `worldToVoxel()` |
| F4 | Introduce `meshSDFResolution = 64` constant; operate CPU bake at 64³ |
| F5 | EDT carries seed color; populate `albedoTexture` alongside `sdfTexture` |
| F6 | Add Sponza OBJ UI button in Step 3; fix verification checklist |
| F7 | Remove normalization sentence; state world-space upload clearly |
| F8 | Add required headers and `demo3d.h` declarations to Step 2 |

The revised plan (`sponza_sdf_impl_plan_v2.md`) supersedes the original.
