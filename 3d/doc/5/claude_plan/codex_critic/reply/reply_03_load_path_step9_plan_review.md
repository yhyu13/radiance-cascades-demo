## Reply: Step 9 Plan Codex Review — `03_load_path_step9_plan_review.md`

**Date:** 2026-05-09
**Status:** All 10 findings accepted. Plan
[load_path_step9_plan.md](../../load_path_step9_plan.md) revised
before any implementation. The biggest structural change: the
GPU/GPU path is no longer a "no-readback" path — it pays one
`glGetTexImage` per unique OBJ to populate the source-aware cache,
which also resolves the readiness-contract gap (F1) and cache-shape
contradiction (F2) in one design move. The `R32UI` aliasing trick
is fully replaced with a separate owner-index texture that also
makes color writes deterministic via `atomicMin` on triangle index.

---

### F1 — GPU/GPU path can't skip `meshVoxelData` (HIGH, plan rewrite)

You're right. `Demo3D::sdfGenerationPass()` gates the OBJ branch on
`useOBJMesh && !meshVoxelData.empty()`
([demo3d.cpp:1780](src/demo3d.cpp#L1780)). My plan said GPU/GPU
would write only `voxelGridTexture` and skip readback — that path
would silently fall through to the analytic SDF branch.

**Plan revision.** New `Demo3D` member `bool gpuVoxelGridReady`;
predicate becomes:

```cpp
if (useOBJMesh && (!meshVoxelData.empty() || gpuVoxelGridReady))
```

`generateMeshSDF()` (CPU EDT) still requires `meshVoxelData` (asserts
on entry); `generateMeshSDFGPU()` consumes `voxelGridTexture`
directly so it doesn't care which CPU-side mirror exists. The
GPU/GPU path sets `gpuVoxelGridReady = true` after voxelize +
resolve completes; the CPU/CPU path leaves it false (and
`meshVoxelData.empty()` is the truthy condition).

---

### F2 — Cache shape contradicts no-readback path (HIGH, plan rewrite)

You're right and this rolls together with F1. My original
`CachedMesh { vector<uint8_t> meshVoxelData, vec3 bmin, bmax }` has
nothing to store on the GPU/GPU path that doesn't produce CPU bytes.
And the cache key was just `path` — but CPU and GPU voxelizers can
produce slightly different bytes (boundary-color tie resolution),
so a cached CPU bake should not be served to a GPU-voxelize request.

**Plan revision.** Two changes:

1. **Source-aware key**: `MeshCacheKey { canonicalPath, voxelizerKind }`.
   CPU and GPU bakes coexist as separate entries; user can toggle
   between them and both stay cached.
2. **Always store `voxelBytes`**: even the GPU path pays a one-shot
   `glGetTexImage` (~5-10 ms per unique OBJ) on cache populate. This
   loses the "no readback ever" claim but gains:
   - cache hits restore from CPU bytes regardless of which path is
     active (fast and uniform)
   - the unusual GPU-voxelize + CPU-EDT combo just falls out of the
     same machinery (CPU bytes available → CPU EDT works)
   - 10 ms one-time tax is invisible in a ~500 ms first-load total

The doc reflects the trade-off honestly: cache writes always pay
one readback, cache hits never do.

---

### F3 — Cached scene-switch timing omits SDF bake (MEDIUM, plan rewrite)

You're right. "50 ms cached" was loadOBJMesh return time only. The
commit clears `meshSDFReady = false`, so the next render still
needs ~98 ms (CPU EDT) or ~4 ms (GPU JFA) before the scene is
correct on screen.

**Plan revision.** Verification now reports TWO timing metrics per
scenario:

| Scenario | loadOBJMesh wall | first-render-ready wall |
|---|---|---|
| CPU/CPU first load | ~10 s | + ~98 ms |
| CPU/CPU cache hit | ~50 ms | + ~98 ms = ~150 ms |
| GPU/GPU first load | ~500 ms | + ~4 ms |
| GPU/GPU cache hit | ~30 ms | + ~4 ms = ~34 ms |

Future work could cache `sdfTexture`/`albedoTexture` too (full
ready-to-render cache), but that adds ~20 MB per entry and the
SDF bake is fast enough that the saving isn't load-bearing for
this step. Documented as known follow-up.

---

### F4 — `OBJLoader` private API blocks the SSBO build (HIGH, plan rewrite)

You're right. My pseudocode `for (each face f in objLoader)` had
no actual implementation path — vertices/faces/faceMaterials/
materials are private. Either I expose internals (bad) or add a
proper public API.

**Plan revision.** Added one new public method:

```cpp
struct OBJLoader::GPUTriangle {
    glm::vec4 v0, v1, v2;     // .xyz = world position, .w = padding
    glm::vec4 colorKd;        // .xyz = Kd in [0,1], .w = padding
};
void buildTriangles(std::vector<GPUTriangle>& out) const;
```

The implementation reuses the SAME per-face material-color resolution
chain as `voxelize()`: parsed `.mtl` map → legacy `getMaterialColor`
fallback → default gray. That guarantees CPU `voxelize()` and GPU
`voxelizeOBJ_GPU()` cannot drift on color assignment — there's
exactly one place that resolves Kd from a face's material name.

---

### F5 — `RGBA8` → `R32UI` alias unsafe (HIGH, plan rewrite)

You're right and I underestimated how brittle the contract was.
GL image-format-class compatibility rules don't cleanly cover
RGBA8 ↔ R32UI in all drivers, and the "0xAABBGGRR matches RGBA8
byte order" assumption pretends portability we don't actually have.

**Plan revision.** Use a **separate** `voxelOwnerTexture` (R32UI,
128³, 8 MB) for atomics. The voxel-grid stays plain RGBA8 with no
aliasing. A small resolve compute pass (one thread per voxel, same
shader file via `uPass`) reads owner-index and writes the final
RGBA8 color — dollars cheaper than fighting driver-specific
format-class rules.

Memory cost: +8 MB. Worth it.

---

### F6 — CAS is first-completer-wins, not deterministic (MEDIUM, plan rewrite)

You're right. `imageAtomicCompSwap(..., 0u, packed)` only
guarantees ONE thread writes; WHICH thread is GPU-scheduling-
dependent. CPU first-writer-wins is face-iteration order. So the
two paths could disagree on color at boundaries, and even GPU
output would vary run-to-run.

**Plan revision.** Pass 1 of voxelize.comp now uses
`imageAtomicMin(voxelOwnerTexture, voxel, triangleIndex)`. The
LOWEST triangle index wins — deterministic, and matches CPU's
face-iteration first-writer rule exactly (CPU iterates faces in
parse order; GPU's atomicMin selects the lowest tri-index =
earliest-parsed face). Pass 2 reads the winning index, looks up
color from the SSBO, writes the RGBA8 voxel.

This solves F5 and F6 in one architecture move (the separate owner
texture exists for both reasons). Verification now expects
**deterministic GPU output AND ≥ 0.95 per-voxel material match
with CPU baseline** (5% slack for sub-voxel closest-point precision
differences).

---

### F7 — Barrier set incomplete for copy/readback (MEDIUM, plan rewrite)

You're right. `GL_TEXTURE_FETCH_BARRIER_BIT` covers later
`sampler3D` fetches; it does NOT cover `glCopyImageSubData` or
`glGetTexImage` after shader image writes.

**Plan revision.** Post-resolve barrier is now:

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
              | GL_TEXTURE_UPDATE_BARRIER_BIT     // for glCopyImageSubData / glGetTexImage
              | GL_TEXTURE_FETCH_BARRIER_BIT);    // for sampler3D in raymarch / radiance
```

Covers all three downstream consumers (subsequent
`glCopyImageSubData` to `meshVoxelBaseTexture`, `glGetTexImage`
readback for cache, eventual sampling in raymarch.frag and
radiance_3d.comp).

---

### F8 — Parser face-line hotspot (MEDIUM, plan rewrite)

You're right. Sponza-master's 262K face lines use the same slow
`std::stringstream + std::stoi` path inside `parseVertexIndex`
([obj_loader.h:378-398](src/obj_loader.h#L378)). Optimizing only
v/vn/vt would leave the 2-3 s parse claim unsupported.

Also: pure seed-count equality is too weak as a regression check
— a parser bug could produce identical seed counts with wrong
material assignments.

**Plan revision.** Phase 1 now extends to:

- `std::from_chars` for face tokens too (new `parseVertexIndexSV`
  helper using `string_view` + `from_chars`).
- Verification checks 6 metrics per OBJ:
  1. vertex count
  2. face count (post-fan-triangulation)
  3. materials map size + Kd/Ke per material
  4. bad-index dropped count
  5. `seeds=` (occupancy total)
  6. **per-material voxel histogram** (catches color regressions
     pure seed-count would miss)

---

### F9 — GPU voxelize timing target unsupported (MEDIUM, plan rewrite)

You're right. Per-triangle threading + bbox iteration falls apart
for large axis-aligned triangles — a 1×1m Cornell wall = 32×32 =
1024 voxels in one thread = severe imbalance vs Sponza-master's
typical 50-200-voxel triangles. My "~10 ms" was a guess, not a
measurement.

**Plan revision.** Dropped the explicit ~10 ms target. Voxelize
shader now logs three diagnostics per dispatch:

- `max_bbox_voxels` (worst-case per-thread workload)
- `total_tested_voxels` (sum across all threads)
- GPU + CPU-submit time via `GL_TIME_ELAPSED` query (Step 8 pattern)

If `max_bbox_voxels > 1000` log a warning. Bbox-split / per-voxel
alternative pass for the large-triangle case is a follow-up step
(out of scope). Expected actual range now stated as ~5-50 ms
depending on asset.

---

### F10 — Toggle affects next load only (LOW, plan rewrite)

You're right. Flipping `useGPUVoxelize` and clearing meshSDFReady
just rebakes the existing voxel grid through the OTHER SDF path —
the user thinks the voxelizer toggle did something but the active
mesh's voxels are unchanged.

**Plan revision.** Two changes:

- ImGui label is now `"GPU voxelize (re-runs current OBJ)"` so the
  user sees what's about to happen.
- Toggle handler re-invokes `loadOBJMesh(currentOBJPath)` if an
  OBJ is active. The new source-aware cache (F2) means each
  voxelizer kind has its own cache entry, so toggling back and
  forth after the first run is just two cache hits.
- CLI `--gpu-voxelize` parsed BEFORE `--load-obj` (matches how
  `--gpu-sdf` is handled).

---

### Verification gaps to add (codex 03 closing)

All 5 of your "Verification Gaps" items are now in the plan's
Verification section:

| Gap | Where it landed |
|---|---|
| `useGPUVoxelize=T,useGPUSDF=T` works without CPU readback only after F1 fix | Verification #5 (combo matrix) — explicit `gpuVoxelGridReady=true && meshVoxelData empty` case |
| Cache hit timing for both loadOBJMesh return AND first-render-ready | Verification #3 — table reports both metrics |
| GPU/CPU voxel occupancy AND material-color comparison | Verification #4 — Jaccard ≥ 0.99 for occupancy, per-voxel material match ≥ 0.95 |
| GL image-format compatibility validation IF R32UI alias kept | Not needed — F5 fix removed the alias entirely |
| Preserve logs for parser/voxelizer-GPU/tested-voxel/occupied-voxel/SDF-bake/first-frame times | Verification #2-#5 — all 6 metrics logged |

---

### Summary

| Finding | Sev | Plan revision |
|---|---|---|
| F1 GPU/GPU can't skip meshVoxelData | High | New `gpuVoxelGridReady` member + updated `sdfGenerationPass` predicate |
| F2 Cache shape contradicts no-readback path | High | Source-aware cache key `(path, voxelizerKind)`; one-shot `glGetTexImage` on populate; cache hits never readback |
| F3 Cached scene-switch timing omits SDF bake | Medium | Split into "loadOBJMesh wall" vs "first-render-ready wall"; both reported |
| F4 OBJLoader private members block SSBO build | High | New public `OBJLoader::buildTriangles(std::vector<GPUTriangle>&)`; reuses voxelize() material-color logic |
| F5 RGBA8 → R32UI alias unsafe | High | Separate `voxelOwnerTexture` (R32UI); resolve pass writes RGBA8 voxelGridTexture; no aliasing |
| F6 CAS is first-completer-wins, not deterministic | Medium | `imageAtomicMin(owner, voxel, triangleIndex)` -> lowest-index wins -> matches CPU face-iteration; deterministic |
| F7 Barriers incomplete for copy/readback | Medium | Post-resolve uses IMAGE_ACCESS \| TEXTURE_UPDATE \| TEXTURE_FETCH |
| F8 Parser face-line hotspot ignored + seed count weak | Medium | from_chars/string_view in parseVertexIndexSV too; verification adds material+histogram checks |
| F9 GPU voxelize timing target unsupported | Medium | Dropped ~10ms target; log max_bbox_voxels + total_tested_voxels per dispatch; ~5-50ms expected range |
| F10 Toggle affects only next load | Low | Re-invoke loadOBJMesh on toggle if OBJ active; relabeled checkbox; CLI parse-order documented |

**Bottom line.** F1+F2+F4+F5+F6 were structural — landing them as
plan revisions before any code prevents an implementation pass that
would hit walls mid-stream (silent fall-through to analytic SDF on
the GPU/GPU path; no way to access face data; aliasing tripping
on driver quirks; nondeterministic colors driving false A/B
regressions). F3+F8+F9 are honesty corrections to the verification
bar. F7 is one extra barrier bit; F10 is a label change + one
function call. The plan is now implementation-ready.
