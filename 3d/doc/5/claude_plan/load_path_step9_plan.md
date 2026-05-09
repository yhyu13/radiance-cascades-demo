# Plan: Phase 5 Step 9 — OBJ Load-Path Acceleration (revised after codex 03)

## Changelog (post codex `03_load_path_step9_plan_review.md`)

All 10 findings accepted. Plan revised before any code lands:

- **F1 (high) — readiness contract.** `Demo3D::sdfGenerationPass()`
  currently gates the OBJ branch on `meshVoxelData.empty()`
  ([demo3d.cpp:1780](src/demo3d.cpp#L1780)); the GPU/GPU path that
  skips CPU readback would silently fall through to the analytic SDF
  branch. Plan now adds `bool gpuVoxelGridReady` member; predicate
  becomes `useOBJMesh && (!meshVoxelData.empty() || gpuVoxelGridReady)`.
  CPU SDF path still requires `meshVoxelData`; GPU SDF path consumes
  `voxelGridTexture` directly.
- **F2 (high) — cache contradiction.** Cache stored only CPU vector;
  GPU path that doesn't produce one would have nothing to cache.
  Plan now: cache is **source-aware**, keyed by
  `(canonical_path, voxelizer_kind)`. Stores CPU `meshVoxelData`
  when that path produced it, OR a "GPU-bake-bytes" copy via one-shot
  `glGetTexImage` readback (~5-10 ms tax paid once). Subsequent
  cache hits restore from the stored bytes regardless of which path
  is currently active.
- **F3 (medium) — cached scene-switch timing.** "50 ms cached" was
  loadOBJMesh return time only, not first-render-ready. Plan now
  splits two timing metrics: **loadOBJMesh wall time** (parse +
  voxelize + upload + commit) and **first-correct-rendered-frame
  wall time** (adds SDF bake + cascade rebuild). Both reported per
  scenario.
- **F4 (high) — `OBJLoader` private members.** Plan referenced
  `for (each face f in objLoader)` but vertices/faces/faceMaterials/
  materials are private ([obj_loader.h:365-370](src/obj_loader.h#L365)).
  Plan now adds **public `OBJLoader::buildTriangles(std::vector<GPUTriangle>& out)`**
  that reuses the same per-face material-color logic as `voxelize()`
  so CPU and GPU voxelizers cannot drift.
- **F5 (high) — RGBA8 → R32UI alias unsafe.** Image-format
  compatibility rules + bit-pattern packing assumptions made the
  alias fragile. Plan now uses a **separate `voxelOwnerTexture`
  (R32UI, 128³, 8 MB)** for atomics. Voxel-grid `RGBA8` consumers
  stay on their normal format. A small resolve compute pass
  converts owner-index → RGBA8 color.
- **F6 (medium) — CAS first-completer-wins, not deterministic.**
  `imageAtomicCompSwap(..., 0u, packed)` lets ANY thread win first;
  GPU scheduling decides — output varies run-to-run AND differs
  from CPU face-order first-writer. Plan now uses
  **`imageAtomicMin(voxelOwnerTexture, voxel, triangleIndex)`** in
  pass 1 — lowest triangle index wins, deterministic, matches CPU
  iteration order exactly. Pass 2 reads owner index, looks up color
  from triangle SSBO, writes RGBA8.
- **F7 (medium) — barrier set incomplete for copy/readback.**
  `GL_TEXTURE_FETCH_BARRIER_BIT` is for sampler fetches, not copy
  or readback. Plan now uses
  `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT
   | GL_TEXTURE_FETCH_BARRIER_BIT` after the resolve pass —
  covers subsequent `glCopyImageSubData` / `glGetTexImage` /
  `sampler3D` consumers.
- **F8 (medium) — parser face-line hotspot.** Phase 1 originally
  only accelerated v/vn/vt lines. Sponza-master has 262K face lines
  using slow `std::stringstream + std::stoi` in `parseVertexIndex`
  ([obj_loader.h:378-398](src/obj_loader.h#L378)). Plan now extends
  Phase 1 to **face-line tokenization with `std::from_chars` +
  `string_view`**. Verification adds vertex/face/material/seed/dropped-
  index counts AND a per-material occupancy histogram (catches color
  regressions that pure seed count would miss).
- **F9 (medium) — GPU voxelize timing optimistic.** Per-triangle
  threading creates severe imbalance for large axis-aligned triangles
  (Cornell wall = 64×64 voxels = 4096 voxels in one thread). Plan
  reframes the ~10 ms target as a **hypothesis pending measurement**;
  shader logs `max_bbox_voxels` and `total_tested_voxels` per dispatch.
  If max bbox > 1000 voxels documented as known limitation; split-
  large-triangle pass deferred.
- **F10 (low) — toggle semantics.** Flipping `useGPUVoxelize`
  affects only the next OBJ load, not the active mesh. Plan now:
  checkbox label is `"GPU voxelize (re-runs current OBJ)"`. The
  toggle handler **re-invokes `loadOBJMesh(currentOBJPath)`** if an
  OBJ is active, so the user sees the effect immediately. CLI
  `--gpu-voxelize` parsed before `--load-obj` (mirrors `--gpu-sdf`).

## Context

Step 8 dropped GPU SDF bake to ~4 ms but `loadOBJMesh()` still
takes ~10 s for `sponza-master` because the slow steps are upstream:

| Stage (Sponza-master, 145K verts / 262K tris) | Time |
|---|---|
| `OBJLoader::load` (text parse) | ~2-3 s |
| `OBJLoader::voxelize` (CPU triangle voxelizer) | ~5-7 s |
| GPU JFA SDF bake (Step 8) | ~4 ms |

The user wants all three speedups landed. None of them touches the
GI pipeline; they're orthogonal load-path optimizations:

- **Parser** — universal win; helps any OBJ load through any path.
- **Cache** — turns repeat clicks of the same OBJ into instant
  scene-switches; benefits the CPU EDT baseline most (saves 5-7 s).
- **GPU triangle voxelizer** — biggest single delta (5-7 s → ~10 ms);
  enables instant first-time loads under the GPU SDF path.

**User decisions captured during planning:**
- **Separate ImGui checkbox for GPU voxelize** (not paired with GPU
  SDF). Lets you A/B all four combinations: CPU/CPU, CPU/GPU,
  GPU/CPU, GPU/GPU. The "weird combo" GPU-voxelize + CPU-EDT eats a
  one-shot GPU→CPU readback (~5-10 ms) so `meshVoxelData` is
  available for the CPU EDT input.
- **Atomic CAS via R32UI image alias** for color writes (not race-
  tolerated `imageStore`). Output is run-to-run deterministic;
  shader gets a bounded CAS-retry loop.

Outcome: scene-switch cost target

| Path | Today | Step 9 |
|---|---|---|
| CPU EDT baseline (default) | ~10 s | ~10 s first time, ~50 ms cached |
| GPU SDF + GPU voxelize | ~10 s (voxelize dominates) | ~500 ms first time, ~50 ms cached |

---

## Approach

### Phase 1 — Faster OBJ parser ([src/obj_loader.h](src/obj_loader.h))

Rewrite `OBJLoader::load` AND `parseVertexIndex`:

- **`std::from_chars` for all numeric parsing** — replaces both
  `std::istringstream >> float` (v/vn/vt lines) AND
  `std::stoi`-via-`parseVertexIndex` (codex 03 F8 — face lines are
  also a major hotspot at 262K Sponza-master face lines).
- **Read whole file once** into a `std::string` via `ifstream::seekg
  + read`, then walk lines in-memory with `string_view`. Eliminates
  per-line getline + heap allocations.
- **Skip `vn` / `vt` value parsing** — we don't use the values.
  Track only the COUNT (`vnCount`, `vtCount`) so negative-index
  resolution in face lines still works.
- **Face-line tokenization with `string_view`** (codex 03 F8) —
  manual scan for spaces and `/` separators; no per-token heap
  allocation. New helper `parseVertexIndexSV(string_view, int& v,
  int& t, int& n)`. Fan-triangulation logic and negative-index
  resolution preserved exactly.
- Preserve all existing public API (`load`, `normalize`, `getBounds`,
  `voxelize`, `materials` lookup, `loadMTL`, badIndex counters).
- Log parse time so the speedup is visible:
  `[OBJLoader] Parse: <Xms> for <N>v / <N>f`.

**Risk:** silent regression if `from_chars` handles some corner
formats differently (e.g. trailing comments, locale). Mitigate with
codex 03 F8 expanded A/B verification (NOT just seed count):

| Metric (per OBJ) | Acceptance |
|---|---|
| Vertex count | identical to baseline |
| Face count (post-fan-triangulation) | identical |
| Materials map size + Kd/Ke values | identical |
| `bad_index_dropped` count | identical |
| `seeds=` (voxelize occupancy) | identical |
| Per-material voxel histogram (NEW) | within ±1 voxel per material |

The per-material histogram catches "right number of voxels but
assigned to wrong material" — what pure seed count would miss.

**Expected:** ~2-3 s → ~300-500 ms for Sponza-master.

---

### Phase 2 — Source-aware per-OBJ cache (codex 03 F2+F3)

#### 2a. Cache structure

```cpp
struct CachedMesh {
    // The raw RGBA8 voxel grid bytes, regardless of which voxelizer
    // produced them. CPU path stores its computed vector here directly;
    // GPU path pays a one-shot glGetTexImage readback (~5-10 ms) on
    // cache populate, then subsequent hits restore from CPU bytes.
    std::vector<uint8_t> voxelBytes;
    glm::vec3 bmin, bmax;
};

// Source-aware key: voxelizer kind matters (CPU's first-writer-wins
// ordering vs GPU's atomicMin-by-triangle-index can produce different
// boundary colors). Resolution + volumeOrigin/Size are session-constant
// so they're not part of the key in practice; if that ever changes,
// flush meshCache.
struct MeshCacheKey {
    std::string canonicalPath;   // resolved successful path (not the user-typed string)
    int         voxelizerKind;   // 0 = CPU, 1 = GPU
    bool operator==(const MeshCacheKey&) const = default;
};
struct MeshCacheKeyHash { size_t operator()(const MeshCacheKey&) const; };

std::unordered_map<MeshCacheKey, CachedMesh, MeshCacheKeyHash> meshCache;
```

#### 2b. Cache hit / miss paths

In `loadOBJMesh(path)`:

```cpp
MeshCacheKey key{ canonicalize(path), useGPUVoxelize ? 1 : 0 };
auto cit = meshCache.find(key);
if (cit != meshCache.end()) {
    // CACHE HIT
    auto t0 = now();
    currentObjBmin = cit->second.bmin;
    currentObjBmax = cit->second.bmax;
    if (useGPUVoxelize && useGPUSDF) {
        // GPU/GPU restore: upload bytes to voxelGridTexture + base.
        // gpuVoxelGridReady set so sdfGenerationPass enters the OBJ branch.
        uploadVoxelBytes(cit->second.voxelBytes, voxelGridTexture);
        uploadVoxelBytes(cit->second.voxelBytes, meshVoxelBaseTexture);
        meshVoxelData.clear();      // GPU/GPU does NOT need CPU mirror
        gpuVoxelGridReady = true;
    } else {
        // CPU path (or GPU/CPU combo): also populate CPU mirror.
        meshVoxelData = cit->second.voxelBytes;   // copy
        uploadVoxelBytes(meshVoxelData, voxelGridTexture);
        uploadVoxelBytes(meshVoxelData, meshVoxelBaseTexture);
        gpuVoxelGridReady = false;
    }
    // commit block (meshSDFReady = false, sceneDirty = true, ...)
    applyOBJViewPreset();
    std::cout << "[Demo3D] OBJ cache hit: " << path
              << " voxelizer=" << (key.voxelizerKind ? "GPU" : "CPU")
              << " loadOBJMesh wall=" << ms_since(t0) << "ms\n";
    return true;
}
// CACHE MISS: existing parse + normalize + voxelize path,
// then meshCache[key] = { voxelBytes, bmin, bmax };
```

Where `voxelBytes` is sourced as:
- CPU path: just `std::move(newVoxelData)` (no extra cost).
- GPU path: one-shot `glGetTexImage(voxelGridTexture, ...)` readback
  after the GPU voxelize pass completes. ~5-10 ms tax paid ONCE per
  unique OBJ-under-GPU; subsequent same-OBJ clicks skip the entire
  voxelize work.

#### 2c. Lifetime + cleanup

- Cache populates on every successful first load (cache miss → fill).
- Cache is process-scoped; no in-session invalidation. Editing an
  OBJ file on disk and re-clicking won't pick up changes (documented
  limitation; acceptable for this step).
- `useGPUVoxelize` toggle handler does NOT clear the cache —
  per-voxelizer-kind keys mean both versions can coexist.
- Cleanup in `Demo3D` destructor: `meshCache.clear()`.

#### 2d. Two timing metrics (codex 03 F3)

The plan's "50 ms cached" was loadOBJMesh return time, NOT
first-render-ready. Verification reports both per scenario:

| Scenario | loadOBJMesh wall time | first-render-ready wall time |
|---|---|---|
| CPU/CPU first load | ~10 s | + ~98 ms = ~10.1 s |
| CPU/CPU cache hit | ~50 ms | + ~98 ms = ~150 ms |
| GPU/GPU first load | ~500 ms (post Phase 1+3) | + ~4 ms = ~504 ms |
| GPU/GPU cache hit | ~30 ms | + ~4 ms = ~34 ms |

(`first-render-ready` adds the SDF bake. Cascade rebuild runs the
following frame; that's not blocking the load itself.)

---

### Phase 3 — GPU triangle voxelizer (codex 03 F4-F7+F9+F10)

#### 3a. New public `OBJLoader::buildTriangles` API (codex 03 F4)

`OBJLoader` keeps geometry private; the GPU path needs flat
triangle data with per-face Kd looked up via the same logic
`voxelize()` uses. Add ONE public method:

```cpp
// In src/obj_loader.h, OBJLoader public section:
struct GPUTriangle {
    glm::vec4 v0, v1, v2;       // .xyz = world position, .w = padding
    glm::vec4 colorKd;          // .xyz = Kd in [0,1], .w = padding
};
void buildTriangles(std::vector<GPUTriangle>& out) const;
```

Implementation reuses the SAME per-face material-color resolution
chain as `voxelize()`:
1. parsed materials map (.mtl Kd)
2. legacy `getMaterialColor` fallback
3. default gray

Result: CPU and GPU voxelizers cannot drift on color assignment.
No private-member exposure; `voxelize()` and `buildTriangles()` are
parallel public consumers of the same private state.

#### 3b. SSBO build + upload (CPU side, once per load)

In `Demo3D::voxelizeOBJ_GPU()`:

```cpp
std::vector<OBJLoader::GPUTriangle> tris;
objLoader.buildTriangles(tris);    // codex 03 F4 reuse
const size_t numTris = tris.size();

glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
glBufferData(GL_SHADER_STORAGE_BUFFER,
             numTris * sizeof(OBJLoader::GPUTriangle),
             tris.data(), GL_STATIC_DRAW);
```

For Sponza-master: 262K tris × 64 bytes = 16.8 MB. Acceptable.

#### 3c. Owner-index texture (codex 03 F5+F6 — replaces R32UI alias)

R32UI aliasing of an RGBA8 texture is fragile (image-format-class
compatibility + bit-pattern packing assumptions). Use a **separate**
`voxelOwnerTexture` (R32UI, 128³, 8 MB) for atomics. The voxel-grid
RGBA8 consumers stay on their normal format.

Allocated in `createVolumeBuffers` next to `voxelGridTexture`, with
RenderDoc label and cleanup. Sentinel value `0xFFFFFFFFu` =
"no owner" (because triangle indices start at 0 — using 0 as
sentinel would clash).

```cpp
voxelOwnerTexture = gl::createTexture3D(N,N,N, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
glObjectLabel(GL_TEXTURE, voxelOwnerTexture, -1, "voxelOwnerTexture");
```

#### 3d. Voxelize.comp pass 1 — atomicMin on triangle index (codex 03 F6)

Existing `voxelize.comp` is a stub (Möller-Trumbore ray test +
half-baked `sdfTriangle`). Replace with a per-triangle thread that
writes its own index via `imageAtomicMin`:

```glsl
#version 430 core
layout(local_size_x = 64) in;

struct Triangle { vec4 v0; vec4 v1; vec4 v2; vec4 colorKd; };
layout(std430, binding = 0) buffer TriangleBuffer { Triangle tris[]; };

// codex 03 F5+F6: separate R32UI owner texture, atomicMin on tri index.
layout(r32ui, binding = 1) uniform uimage3D uVoxelOwner;

uniform int   uNumTriangles;
uniform vec3  uVolumeOrigin;
uniform vec3  uVolumeSize;
uniform ivec3 uVolumeDim;
uniform float uVoxelHalfDiag;

vec3 closestPointOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c) { ... }

void main() {
    uint tid = gl_GlobalInvocationID.x;
    if (tid >= uint(uNumTriangles)) return;

    Triangle t = tris[tid];
    vec3 v0 = t.v0.xyz, v1 = t.v1.xyz, v2 = t.v2.xyz;

    // Triangle bbox in voxel coords + half-diag expansion.
    vec3 trMin = min(v0, min(v1, v2)) - vec3(uVoxelHalfDiag);
    vec3 trMax = max(v0, max(v1, v2)) + vec3(uVoxelHalfDiag);
    ivec3 vMin = ivec3(floor((trMin - uVolumeOrigin) / uVolumeSize * vec3(uVolumeDim)));
    ivec3 vMax = ivec3(floor((trMax - uVolumeOrigin) / uVolumeSize * vec3(uVolumeDim)));
    vMin = clamp(vMin, ivec3(0), uVolumeDim - 1);
    vMax = clamp(vMax, ivec3(0), uVolumeDim - 1);

    vec3 voxStep = uVolumeSize / vec3(uVolumeDim);

    for (int z = vMin.z; z <= vMax.z; ++z)
    for (int y = vMin.y; y <= vMax.y; ++y)
    for (int x = vMin.x; x <= vMax.x; ++x) {
        vec3 wp = uVolumeOrigin + (vec3(x, y, z) + 0.5) * voxStep;
        vec3 cp = closestPointOnTriangle(wp, v0, v1, v2);
        if (length(wp - cp) <= uVoxelHalfDiag) {
            // codex 03 F6: atomicMin -> lowest triangle index wins ->
            // deterministic, matches CPU face-iteration first-writer rule.
            imageAtomicMin(uVoxelOwner, ivec3(x, y, z), tid);
        }
    }
}
```

Pre-dispatch: clear `voxelOwnerTexture` to `0xFFFFFFFFu` (no owner).

#### 3e. Voxelize.comp pass 2 — owner → color resolve

A second compute kernel (one thread per voxel, 8×8×8 workgroups)
reads `voxelOwnerTexture`, looks up the winning triangle's color
from the SSBO, writes to `voxelGridTexture` (RGBA8). Same shader
file, switched via `uPass` uniform (matches Step 8 sdf_3d.comp
pattern).

```glsl
// uPass == 1 path (resolve)
ivec3 pos = ivec3(gl_GlobalInvocationID);
if (any(greaterThanEqual(pos, uVolumeDim))) return;
uint owner = imageLoad(uVoxelOwner, pos).r;
if (owner == 0xFFFFFFFFu) {
    imageStore(uVoxelGrid, pos, vec4(0.0));   // empty
    return;
}
vec3 kd = tris[owner].colorKd.xyz;
imageStore(uVoxelGrid, pos, vec4(kd, 1.0));
```

`uVoxelGrid` is the regular RGBA8 voxel grid (image bound at
`binding=2`, declared `layout(rgba8)`). No format aliasing.

#### 3f. Dispatch + barriers (codex 03 F7)

```cpp
const int wg1 = (numTris + 63) / 64;
const int wgV = (N + 7) / 8;

// Pre-clear owner texture to no-owner sentinel.
GLuint clearVal = 0xFFFFFFFFu;
glClearTexImage(voxelOwnerTexture, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &clearVal);

GLuint timer = 0; glGenQueries(1, &timer); glBeginQuery(GL_TIME_ELAPSED, timer);

// Pass 1: per-triangle atomicMin into owner texture.
glUseProgram(voxelizeProg);
glUniform1i(uPassLoc, 0);
glUniform1i(uNumTrisLoc, int(numTris));
// ... other uniforms ...
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
glBindImageTexture(1, voxelOwnerTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
glDispatchCompute(wg1, 1, 1);
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);   // owner writes -> resolve reads

// Pass 2: per-voxel resolve owner -> color.
glUniform1i(uPassLoc, 1);
glBindImageTexture(1, voxelOwnerTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_R32UI);
glBindImageTexture(2, voxelGridTexture,  0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
glDispatchCompute(wgV, wgV, wgV);

// codex 03 F7: full barrier set covering subsequent
//   - glCopyImageSubData (TEXTURE_UPDATE)
//   - glGetTexImage readback for cache populate (TEXTURE_UPDATE)
//   - sampler3D fetches in raymarch.frag / radiance_3d.comp (TEXTURE_FETCH)
//   - image reads in generateMeshSDFGPU init pass (SHADER_IMAGE_ACCESS)
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
              | GL_TEXTURE_UPDATE_BARRIER_BIT
              | GL_TEXTURE_FETCH_BARRIER_BIT);

glEndQuery(GL_TIME_ELAPSED);
```

Followed by:
```cpp
// Mirror to base for dynamic sphere.
glCopyImageSubData(voxelGridTexture, GL_TEXTURE_3D, 0, 0,0,0,
                   meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0,0,0, N, N, N);
gpuVoxelGridReady = true;   // codex 03 F1: enable OBJ branch in sdfGenerationPass
```

#### 3g. Cache populate via readback (codex 03 F2)

After voxelize completes, populate the mesh cache with the GPU
result so subsequent same-OBJ clicks skip the entire voxelize work:

```cpp
std::vector<uint8_t> bytes(N * N * N * 4);
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
glBindTexture(GL_TEXTURE_3D, 0);
meshCache[{ canonicalPath, /*kind=*/1 }] = { std::move(bytes), bmin, bmax };
```

The `glGetTexImage` is properly synchronized by the
`GL_TEXTURE_UPDATE_BARRIER_BIT` issued above (codex 03 F7). One-shot
~5-10 ms readback per unique GPU-path OBJ; subsequent same-OBJ cache
hits restore from CPU bytes (no readback path runs).

#### 3h. CPU EDT compatibility (optional readback)

Only when `useGPUVoxelize && !useGPUSDF` (the unusual combo): the
cache-populate readback above already produced the bytes; just
also populate `meshVoxelData = bytes` so CPU EDT has its input.
Common combos (GPU/GPU and CPU/CPU) skip nothing extra — readback
runs ONCE per unique OBJ regardless, for cache reasons.

#### 3i. Per-triangle bbox profiling (codex 03 F9 — not a target)

The plan does NOT promise "GPU voxelize takes ~10 ms". Per-triangle
threading creates per-invocation imbalance for large axis-aligned
triangles (a 1×1m Cornell wall = 32×32 = 1024 voxels per triangle
at N=128). Add **diagnostic logs** to characterize the actual cost
per asset:

- A separate compute pass before the main voxelize (or a CPU pre-scan
  of the triangle SSBO) computes `max_bbox_voxels` and
  `total_tested_voxels` and uploads them as atomic counters; logged
  per dispatch.
- If `max_bbox_voxels > 1000` log a warning (large-triangle case);
  bbox-split / per-voxel pass is a follow-up step (out of scope).

Treat the ~10 ms target as a hypothesis; expected range ~5-50 ms
depending on triangle size distribution. Cornell-Original has a few
~1000-voxel walls; Sponza-master is mostly small (50-200 voxel)
triangles.

#### 3j. Toggle wiring (codex 03 F10)

- New `Demo3D` member `bool useGPUVoxelize = false`.
- New ImGui checkbox **`"GPU voxelize (re-runs current OBJ)"`** next
  to the GPU SDF checkbox. The label hints that the current mesh
  re-voxelizes on toggle (rather than only affecting the next click).
- Toggle handler:

```cpp
if (ImGui::Checkbox("GPU voxelize (re-runs current OBJ)", &useGPUVoxelize)) {
    if (useOBJMesh && !currentOBJPath.empty()) {
        loadOBJMesh(currentOBJPath);   // re-runs through cache; new key
    }
    // No need to clear the cache: per-voxelizer-kind key means the
    // CPU and GPU bakes coexist as separate entries.
}
```

- New CLI flag `--gpu-voxelize` mirroring `--gpu-sdf`. Parsed
  BEFORE `--load-obj` so the first load uses the chosen path.
- `loadOBJMesh()` branches on `useGPUVoxelize` for the voxelize step.

---

## Files to modify

- [src/obj_loader.h](src/obj_loader.h) — Phase 1 parser rewrite
  (whole-file read + `std::from_chars` for floats AND face tokens
  + skip vn/vt values + new `parseVertexIndexSV` helper). New
  public `OBJLoader::GPUTriangle` struct + `buildTriangles()` method
  (codex 03 F4).
- [res/shaders/voxelize.comp](res/shaders/voxelize.comp) — Phase 3
  full rewrite. TWO passes via `uPass` (matches sdf_3d.comp pattern):
  pass 0 = per-triangle atomicMin into owner texture; pass 1 =
  per-voxel resolve owner → color. No format aliasing.
- [src/demo3d.h](src/demo3d.h) — new members:
  `meshCache` (source-aware, keyed by `(path, voxelizerKind)`),
  `useGPUVoxelize`, `triangleSSBO`, `voxelOwnerTexture`,
  `gpuVoxelGridReady` (codex 03 F1). New method declarations
  `voxelizeOBJ_GPU()`, `setUseGPUVoxelize(bool)`,
  `uploadVoxelBytes(...)` helper.
- [src/demo3d.cpp](src/demo3d.cpp):
  - Re-add `loadShader("voxelize.comp")` in init + reload paths
    (was removed in the Step 7 cleanup; mirrors the Step 8 sdf_3d.comp
    re-add).
  - `triangleSSBO` allocation + `voxelOwnerTexture` allocation in
    `createVolumeBuffers` + cleanup in `destroyVolumeBuffers` +
    RenderDoc labels (codex 03 F5).
  - New `Demo3D::voxelizeOBJ_GPU()` per Phase 3b-3g.
  - `sdfGenerationPass()` predicate updated to
    `useOBJMesh && (!meshVoxelData.empty() || gpuVoxelGridReady)`
    (codex 03 F1).
  - Cache lookup at the top of `loadOBJMesh()`; cache populate at
    the end of the commit block.
  - Branch on `useGPUVoxelize` to call `voxelizeOBJ_GPU()` instead
    of `objLoader.voxelize()`.
  - Optional readback (3d) when `useGPUVoxelize && !useGPUSDF`.
  - ImGui checkbox + toggle handler.
- [src/main3d.cpp](src/main3d.cpp) — `--gpu-voxelize` CLI flag.

No changes to GI pipeline (cascade compute, raymarch, temporal blend,
blur). `raymarch.frag`/`radiance_3d.comp` thresholds untouched.

---

## Reuse from existing code

- `OBJLoader::voxelize`'s `closestPointOnTriangle`
  ([obj_loader.h:417](src/obj_loader.h#L417)) — direct port of math
  to GLSL for the new compute shader.
- `Demo3D::uploadPrimitivesToGPU`
  ([demo3d.cpp:1155](src/demo3d.cpp#L1155)) — template for the
  triangle SSBO upload (size + buffer + std430 padding).
- `Demo3D::generateMeshSDFGPU` (Step 8) — template for GL_TIME_ELAPSED
  query, debug-group wrapping, validation pattern, error checks.
- `Demo3D::addVoxelSphere` (Step 8) — already uses correct
  world→voxel math; same formula in the compute shader.
- `meshVoxelBaseTexture` mirror via `glCopyImageSubData` (Step 8) —
  same pattern, just from voxelGridTexture this time.
- `imageAtomicCompSwap` is core GL 4.3+ — no extension needed.

---

## Verification

1. **Build clean** after each phase. Target: 0 errors, ≤ 40 warnings
   (39 baseline post-Step-8 + maybe 1 from line shifts).

2. **Phase 1 — parser timing + extended A/B (codex 03 F8).** Load
   each of the 4 OBJs, log parse time, compare against pre-Phase-1
   baseline ALL of:
   - vertex count, face count (post-fan-triangulation),
   - materials map size + Kd/Ke per material,
   - bad-index dropped count,
   - `seeds=` (voxelize occupancy),
   - per-material voxel histogram (catches "right total seeds, wrong
     material assignment").
   Acceptance: identical to baseline on all metrics; parse time
   <1 s for sponza-master.

3. **Phase 2 — cache hit timing (codex 03 F3 split).** Load
   cornell-orig, switch to sponza-master (cache miss, slow), switch
   back to cornell-orig. Report TWO timing metrics per case:
   - **loadOBJMesh wall time** (parse + voxelize + upload + commit)
   - **first-correct-rendered-frame wall time** (adds SDF bake)
   Acceptance: second cornell-orig click logs `OBJ cache hit`;
   loadOBJMesh < 100 ms; first-render-ready < 200 ms (CPU EDT) or
   < 110 ms (GPU JFA).

4. **Phase 3 — GPU voxelize correctness + speed (codex 03 F6+F9).**
   - Per-load timing log: `[Demo3D] GPU voxelize: GPU=Xms
     CPU-submit=Yms max_bbox_voxels=Z total_tested_voxels=W
     (N tris, M occupied)`.
   - Numeric A/B vs CPU voxelize for cornell-orig (deterministic
     output expected with atomicMin):
     - **Voxel occupancy Jaccard** ≥ 0.99 (atomicMin-by-tri-index
       matches CPU face-iteration order, so set should be very close
       to identical).
     - **Per-voxel material match** ≥ 0.95 (atomicMin orders by
       triangle index → CPU's first-writer = lowest-index → should
       give same color winner. Allow 5% slack for closest-point
       precision differences).
   - Visual A/B captures for all 4 OBJs:
     `tools/step9_<name>_<voxpath>_mode0.png` for `voxpath` in
     {cpu, gpu}. Should be visually equivalent.
   - Codex 03 F9: GPU-voxelize time is a measurement, not a target.
     Report actual values; if max_bbox_voxels > 1000 for any
     dispatch, log warning + document as known limitation.

5. **Phase 3 — combo matrix (codex 03 F1 readiness).** Verify all
   4 toggle combinations actually work end-to-end (no silent
   fall-through to analytic SDF):
   - `(useGPUVoxelize=F, useGPUSDF=F)` baseline (CPU/CPU).
   - `(F, T)` GPU JFA on CPU-voxelized grid (Step 8 baseline).
   - `(T, T)` fastest path; `gpuVoxelGridReady=true`,
     `meshVoxelData` empty, OBJ branch still taken.
   - `(T, F)` GPU voxelize + CPU EDT; cache-readback populates
     `meshVoxelData` so CPU EDT has its input.
   Capture per-load timing for each.

6. **Phase 3 — dynamic sphere still works** with GPU voxelize on
   (since meshVoxelBaseTexture is now sourced from GPU via
   `glCopyImageSubData`). Run `--gpu-sdf --gpu-voxelize
   --dynamic-sphere --sphere-time=1.5 --exit-frames=120`; capture
   should match Step 8 v2.

7. **Frame-budget for Sponza-master scene-switch:**
   - Today (Step 8): ~10 s
   - Step 9 (GPU voxelize, GPU SDF): expect ~500 ms first time,
     ~50 ms cached. Log it.

8. **Logs preserved** to `tools/app_run_step9_*.log`. Captures to
   `tools/step9_*.png`.

---

## Out of scope (deferred to later)

- Multi-threaded CPU voxelize (would help only the CPU path; GPU
  path makes this unnecessary).
- BVH for triangle voxelization (per-triangle threading is fast
  enough for our triangle counts).
- OBJ asset hot-reload (cache invalidation on file mtime); the
  cache is intentionally process-scoped.
- Sparse voxel storage / octree.
- 256³ voxel resolution.
