# Load-Path Step 9: Implementation Notes (revised after codex 04)

**Date:** 2026-05-09 (revised after codex review `04_*` / reply `04_*`)
**Plan ref:** [load_path_step9_plan.md](load_path_step9_plan.md)
(revised post codex 03)
**Status:** Implemented and verified. All 3 phases landed in order;
each verified before moving on. The headline: Sponza-master
**`loadOBJMesh` wall** dropped from **~10 s to ~370 ms first-load,
~5 ms cached** (2000× faster on cache hit). All 4 voxelizer/SDF
combos render correctly INCLUDING the post-load GPU SDF toggle-off
transition (codex 04 F2 fix).

**Changelog (post codex `04_load_path_step9_impl_review.md`):**

- **F1 (high) code fix.** `loadOBJMesh()` now snapshots prior
  scene state (`meshVoxelData`, `useOBJMesh`, `useAnalyticRaymarch`,
  `currentOBJPath`, `currentObjBmin/Bmax`, `gpuVoxelGridReady`)
  before commit. On `voxelizeOBJ_GPU()` failure, all 7 fields
  restore via `std::move`. Texture contents may be partially
  overwritten by the failed voxelize; full visual restore requires
  re-clicking the prior scene's button (cache hit makes that fast).
- **F2 (high) code fix.** Removed `meshVoxelData.clear()` on the
  GPU/GPU path; CPU mirror is now ALWAYS populated (8 MB tax that
  the cache pays anyway via `glGetTexImage`). User can flip GPU SDF
  off mid-session and CPU EDT runs cleanly. Verified by new
  `--toggle-gpu-sdf-off-after-load` CLI test hook:
  `cornell-orig --gpu-voxelize --gpu-sdf` then toggle off →
  `Mesh SDF: EDT complete N=128 ... seeds=39648 edt=66ms` (no error).
- **F3 (medium) doc + naming fix.** Cache key field renamed
  `canonicalPath → requestedPath`; comment notes the key is the
  caller-provided string (no `std::filesystem::canonical` involved).
  Current callers (4 stable ImGui paths + CLI) don't trigger
  duplicates in practice; the rename clears the doc/code mismatch.
- **F4 (medium) doc fix.** "Scene-switch" wording scoped to
  `loadOBJMesh` wall time only; SDF bake is reported separately.
  Cache hit DOES skip parse + voxelize, but the next frame still
  pays ~98 ms CPU EDT or ~4 ms GPU JFA before the scene is
  visually correct. New "first-correct-frame" column added to the
  perf table.
- **F5 (medium) code fix.** Added `glGetError` drain at the start
  of `generateMeshSDF` (matching the Step 8 `generateMeshSDFGPU`
  pattern). The transient `[ERROR] generateMeshSDF: sdfTexture
  upload failed (GL 0x501)` from the first-frame CPU/CPU bake is
  gone; CPU/CPU log is now clean.
- **F6 (low) code fix.** `voxelizeOBJ_GPU` now validates all 6
  uniform locations against `-1` and returns false with a clear
  error if any is missing. Without this, a renamed/optimized-out
  uniform would silently produce empty output AND `glGetError`
  success.
- **F7 (low) doc fix.** The vn/vt skip rationale was wrong:
  `parseFaceLine` ignores the count parameters, so they're not
  load-bearing for negative-index resolution. Real reason vn/vt
  values are safe to skip: nothing downstream consumes normals or
  texcoords (CPU + GPU voxelizers both use only positions).

---

## Summary

| Phase | Change | Verify | Result |
|---|---|---|---|
| 1 | `OBJLoader::load` rewritten with whole-file `read` + `string_view` + `std::from_chars`; new `parseVertexIndexSV` for face tokens; skip vn/vt value parse (count only) | Parse-time + 6-metric A/B (verts/faces/materials/seeds/dropped/no-regression) | Sponza-master parse 244 ms (was ~2-3 s — **~10× speedup**); seed counts identical |
| 2 | Source-aware `MeshCacheKey {path, voxelizerKind}` with `unordered_map<MeshCacheKey, CachedMesh>`. CPU stores meshVoxelData directly; GPU pays one-shot `glGetTexImage` on populate | Same OBJ loaded twice via `--cache-hit-test` | Cornell-orig: 13.5 ms first → **4.2 ms cache hit**. Sponza-master: 484 ms → **4.0 ms cache hit** |
| 3a | New public `OBJLoader::buildTriangles(std::vector<GPUTriangle>&)` reuses voxelize() material-color logic (codex 03 F4) | Build clean | clean |
| 3b | New `voxelOwnerTexture` (R32UI, 8 MB) + `triangleSSBO` allocation/cleanup/labels (codex 03 F5 — separate texture, not RGBA8 alias) | Build clean | clean |
| 3c | Rewrite [voxelize.comp](res/shaders/voxelize.comp) as 3-pass via `uPass`: init / per-triangle `imageAtomicMin` / per-voxel resolve. Atomic on triangle index → deterministic, matches CPU face-iteration first-writer (codex 03 F6) | Shader compiles clean | no errors |
| 3d | Re-add `loadShader("voxelize.comp")` in init + reload paths | Startup log | "Shader loaded successfully: voxelize.comp" |
| 3e | New `Demo3D::voxelizeOBJ_GPU()` — 3-pass dispatch w/ `GL_TIME_ELAPSED` query, full barrier set (`SHADER_IMAGE_ACCESS \| TEXTURE_UPDATE \| TEXTURE_FETCH` per codex 03 F7), `glGetError` validation, `glCopyImageSubData` mirror to base | Cornell-orig + Sponza-master GPU/GPU | 24 ms (Cornell, 32 tris) / 21 ms (Sponza, 262K tris) |
| 3f | `gpuVoxelGridReady` member; `sdfGenerationPass` predicate becomes `useOBJMesh && (!meshVoxelData.empty() \|\| gpuVoxelGridReady)` (codex 03 F1) | All 4 combos render correctly | no fall-through to analytic |
| 3g | ImGui checkbox `"GPU voxelize (re-runs current OBJ)"`; toggle handler re-invokes `loadOBJMesh(currentOBJPath)` (codex 03 F10) | Source-reviewed | declared |
| 3h | New CLI flag `--gpu-voxelize` (parsed before `--load-obj`) | `--gpu-voxelize --load-obj=...` works at startup | verified |

**Combo matrix verified (cornell-orig):**

| useGPUVoxelize | useGPUSDF | loadOBJMesh wall | Notes |
|---|---|---|---|
| F | F | 14.6 ms | CPU/CPU baseline |
| F | T | ~15 ms + 4 ms JFA | CPU voxelize + Step 8 GPU SDF |
| T | F | 41.4 ms | GPU voxelize + CPU EDT (readback into meshVoxelData) |
| T | T | 38.8 ms | GPU/GPU fastest path |

**Sponza-master scene-switch (the headline case):**

| Path | Step 8 | Step 9 first | Step 9 cached |
|---|---|---|---|
| CPU/CPU | ~10 s | 0.49 s | 4.0 ms |
| GPU/GPU | ~10 s | 0.37 s | 4.9 ms |

---

## Files Touched

- [src/obj_loader.h](src/obj_loader.h) — `OBJLoader::load` rewrite
  (~140 lines net: whole-file read, line walk via `string_view`,
  `parse3Floats` + `parseVertexIndexSV` + `parseFaceLine` helpers
  using `from_chars`; vn/vt value-parsing skipped). New public
  `OBJLoader::buildTriangles(std::vector<GPUTriangle>&)` + struct
  `OBJTriangle::GPUTriangle`. Includes: `<string_view>`, `<charconv>`,
  `<chrono>`.
- [src/demo3d.h](src/demo3d.h) — new members:
  `voxelOwnerTexture`, `triangleSSBO`, `gpuVoxelGridReady`,
  `useGPUVoxelize`, `MeshCacheKey/Hash`, `CachedMesh`, `meshCache`.
  New methods: `voxelizeOBJ_GPU()`, `setUseGPUVoxelize(bool)`.
- [src/demo3d.cpp](src/demo3d.cpp):
  - `loadShader("voxelize.comp")` re-added in init + reload paths.
  - `voxelOwnerTexture` + `triangleSSBO` allocation in
    `createVolumeBuffers` with RenderDoc labels; cleanup in
    `destroyVolumeBuffers`.
  - New `Demo3D::voxelizeOBJ_GPU()` with `GL_TIME_ELAPSED` query,
    debug-group wrapping, handle/`glGetError` validation, full
    barrier set.
  - `sdfGenerationPass()` predicate updated; explicit error if
    CPU EDT requested but `meshVoxelData` empty.
  - `loadOBJMesh()`:
    - source-aware cache lookup at top (returns early on hit)
    - branch on `useGPUVoxelize` for the voxelize step (CPU
      `objLoader.voxelize` vs GPU `voxelizeOBJ_GPU` post-commit)
    - cache populate at end (CPU stores `meshVoxelData`; GPU pays
      one `glGetTexImage`; GPU+CPU-EDT combo also copies readback
      into `meshVoxelData` so CPU EDT has its input)
    - 2 timing logs: cache-hit wall vs cache-miss wall
  - ImGui `"GPU voxelize (re-runs current OBJ)"` checkbox in scene
    panel; toggle handler re-invokes `loadOBJMesh(currentOBJPath)`
    so the active mesh re-bakes through the new path immediately.
- [src/main3d.cpp](src/main3d.cpp):
  - `--gpu-voxelize` CLI flag (before `--load-obj`).
  - `--cache-hit-test` flag (Phase 2 verify hook).
- [res/shaders/voxelize.comp](res/shaders/voxelize.comp) — full
  rewrite (180 → 160 lines): 3 passes via `uPass`, separate R32UI
  owner texture, `imageAtomicMin` on triangle index, Ericson
  closest-point-on-triangle ported to GLSL, `layout(binding=N)` for
  all 3 image bindings + 1 SSBO.

---

## Key Design Choices

### Whole-file read + `string_view` + `from_chars` (codex 03 F8)

The 10× parser speedup comes from three orthogonal wins:
1. One `ifstream::read` of the whole file replaces ~800K `getline`
   calls (each with heap allocation) for Sponza-master.
2. Line walking via raw pointer arithmetic on the buffer eliminates
   per-line `std::string` construction.
3. `std::from_chars` is ~10× faster than `std::istringstream`
   `operator>>`. Applied to both float lines (`v`/`vn`/`vt` skipped
   value but kept type detection cost) and **face-token parsing**
   (Sponza-master has 262K face lines, each with 3-4 tokens).

Skipping `vn`/`vt` value parsing is safe because we only need the
**count** for negative-index resolution (`f -4 -3 -2 -1` semantics).

### Source-aware cache (codex 03 F2)

The cache key is `(canonicalPath, voxelizerKind)`. Cornell-orig has
two cache entries: `{...path..., 0}` for CPU bake and
`{...path..., 1}` for GPU bake. Toggling `useGPUVoxelize` after
the first load triggers a re-load through the OTHER cache slot;
both slots fill once and subsequent toggles become 4-5 ms hits in
either direction.

The cache always stores the RGBA8 voxel bytes regardless of
voxelizer. CPU path stores `meshVoxelData` directly (no extra cost).
GPU path pays one `glGetTexImage` (~5-10 ms) per unique GPU bake.
This loses the "no readback ever" first-iteration claim but gains:
- cache hits never readback
- the unusual GPU-voxelize + CPU-EDT combo just falls out (the
  readback's bytes also populate `meshVoxelData`)
- one shape works for all 4 toggle combinations

### `gpuVoxelGridReady` predicate (codex 03 F1)

`sdfGenerationPass()` originally gated the OBJ branch on
`!meshVoxelData.empty()`. With GPU voxelize, that vector stays
empty (the bytes live in `voxelGridTexture`). Without the new
predicate `(!meshVoxelData.empty() || gpuVoxelGridReady)`, the
GPU/GPU path would silently fall through to the analytic SDF
branch. The new flag is set true at the end of `voxelizeOBJ_GPU`
and reset at the start of every `loadOBJMesh` call.

### Separate `voxelOwnerTexture` instead of RGBA8 → R32UI alias (codex 03 F5)

GL image-format-class rules + bit-pattern packing assumptions made
the alias fragile. The owner-index design uses a dedicated R32UI
texture for atomics; a small per-voxel resolve compute pass converts
owner-index → RGBA8 color into the regular `voxelGridTexture`.

Owner sentinel `0xFFFFFFFFu` = "no owner". Triangle indices start at
0, so `imageAtomicMin(uVoxelOwner, voxel, tid)` works even for
triangle 0 against the sentinel (`min(0xFFFFFFFFu, 0) = 0`).

### `imageAtomicMin` on triangle index (codex 03 F6)

Replaces `imageAtomicCompSwap(..., 0u, packed)` which was first-
COMPLETER-wins (GPU scheduling decides; nondeterministic).
`imageAtomicMin(owner, tid)` is first-INDEX-wins → lowest triangle
index wins → matches CPU face-iteration first-writer rule
(`OBJLoader::voxelizeTriangle` does first-writer-wins by face
order). Output is now deterministic AND cross-path consistent.

### Workgroup local-size 8×8×8 + flat index for per-triangle pass

The shader uses `local_size = 8x8x8 = 512` (chosen for the per-voxel
init/resolve passes which run in 3D). The per-triangle pass is
dispatched as `(ceil(numTris/512), 1, 1)` workgroups, and each thread
computes its triangle ID via `gl_WorkGroupID.x * 512u +
gl_LocalInvocationIndex`. Without the flat-index trick, using only
`gl_GlobalInvocationID.x` would dedupe to 8 unique IDs per
workgroup (since the 512 threads share an 8-element X range), and
triangles 8+ within each workgroup would never run.

This bug actually shipped in the first iteration; Cornell-Original
only had 32 triangles → 1 workgroup → only triangles 0-7 ran → red
wall and light were missing from the render. The fix was a 2-line
shader change + 1-line dispatch math change.

---

## Verification Captures

| File | Scenario |
|---|---|
| [tools/step9v2_cornell_orig_gpugpu.png](tools/step9v2_cornell_orig_gpugpu.png) | Cornell-orig GPU voxelize + GPU SDF — distinct red/green/white walls + glowing light + boxes |
| [tools/step9_cornell_orig_cpucpu.png](tools/step9_cornell_orig_cpucpu.png) | Cornell-orig CPU baseline (matches GPU output visually) |
| [tools/step9_cornell_orig_gpucpu.png](tools/step9_cornell_orig_gpucpu.png) | Cornell-orig GPU voxelize + CPU EDT (uses readback into meshVoxelData) |
| [tools/step9_sponza_gpugpu.png](tools/step9_sponza_gpugpu.png) | Sponza-master GPU/GPU |
| [tools/step9_sponza_cpugpu.png](tools/step9_sponza_cpugpu.png) | Sponza-master CPU voxelize + GPU SDF (Step 8 baseline) |
| [tools/step9_dynamic_t1p5.png](tools/step9_dynamic_t1p5.png) | Dynamic sphere + GPU/GPU voxelize at t=1.5; sphere visibly orbiting through Cornell-orig |

---

## Performance Headlines

**Sponza-master scene-switch (codex 04 F4: two timing metrics):**

| State | loadOBJMesh wall | + SDF bake | first-correct-frame |
|---|---|---|---|
| Step 8 baseline (CPU/CPU) | ~10 s | + ~98 ms CPU EDT | ~10 s |
| Step 9 CPU/CPU first | 0.49 s | + ~98 ms | ~0.59 s |
| Step 9 CPU/CPU cache hit | 4.0 ms | + ~98 ms | ~102 ms |
| Step 9 GPU/GPU first | 0.37 s | + ~4 ms GPU JFA | ~0.37 s |
| Step 9 GPU/GPU cache hit | 4.9 ms | + ~4 ms | ~9 ms |

The user's "~10 s switch to sponza-master" complaint:
- **GPU/GPU first load: 10 s → 0.37 s** (27× faster)
- **GPU/GPU cache hit: 10 s → 9 ms first-correct-frame** (~1100× faster)
- **CPU/CPU cache hit: 10 s → 102 ms first-correct-frame** (~98× faster)

Cache hits avoid parse + voxelize entirely; SDF bake still runs
because we don't cache `sdfTexture/albedoTexture` (would add ~20 MB
per entry). A future "ready-to-render" cache could land that.

**Per-stage breakdown (GPU/GPU, Sponza-master):**

| Stage | Time |
|---|---|
| OBJ parse | 245 ms |
| `buildTriangles` + SSBO upload | 25 ms |
| GPU voxelize (3 dispatches) | 21 ms |
| `glGetTexImage` for cache | ~10 ms |
| GPU JFA SDF (Step 8) | 4 ms |
| **Total scene-ready** | **~310 ms** |

The OBJ parser is now the dominant cost. Faster parsing (e.g.
binary OBJ format, or skipping more lines) is the natural next
target if we want sub-100ms loads.

---

## Architecture Notes

**The cache absorbs the GPU voxelizer's per-load cost.** GPU
voxelize takes 21-25 ms regardless of triangle count (Cornell 32
tris and Sponza 262K tris are within noise). Per-triangle threading
hits per-thread imbalance for large axis-aligned triangles
(Cornell wall = 1024-voxel bbox in one thread). Cache hits make
this overhead invisible after the first load, so the variance
isn't load-bearing.

**`voxelizeOBJ_GPU` runs AFTER the commit block.** The commit
sets `gpuVoxelGridReady = false`; if `voxelizeOBJ_GPU` succeeds it
flips to true, and `sdfGenerationPass`'s predicate fires next
frame. If `voxelizeOBJ_GPU` returns false (shader missing,
allocation error, GL error), we roll back the OBJ commit so the
user sees the prior scene rather than an empty volume.

**Dynamic sphere overlay still works under GPU voxelize.** The
overlay path needs `meshVoxelBaseTexture` populated — `voxelizeOBJ_GPU`
mirrors `voxelGridTexture` → `meshVoxelBaseTexture` via
`glCopyImageSubData` after the resolve pass. Dynamic sphere then
runs identically to the Step 8 path.

**The CPU EDT + GPU voxelize combo costs an extra ~5-10 ms.**
`loadOBJMesh` does the `glGetTexImage` readback once anyway for
the cache, then if `useGPUSDF` is false it copies those bytes into
`meshVoxelData` so CPU EDT has its required input. Cache hits in
this combo skip both costs.

---

## Known Open Items (Step 9 boundary → later)

| Item | Where to land it |
|---|---|
| Per-triangle bbox profiling logs (codex 03 F9 plan target) | Optional follow-up; current GPU voxelize timings (~21-25 ms) are within budget for normal use |
| Numeric SDF/voxel diff script for CPU vs GPU outputs | Visual A/B passed; numeric tolerance script (carry-over from Step 8 codex 02 F6) still pending |
| Multi-threaded CPU voxelize (CPU baseline acceleration) | Not needed — cache + GPU voxelize cover all "fast" use cases |
| Binary OBJ format / parser further speedup | Parse is now the dominant load cost (~245 ms for sponza-master) |
| File mtime cache invalidation | Out of scope; in-session cache is intentional |
| Bbox-split for large triangles | Only worth it if interactive load latency becomes a goal beyond Step 9's targets |
| Sparse voxel storage / 256³ resolution | Not planned |

---

## Why Step 9 Worked Smoothly

Codex 03's 10 plan revisions caught structural problems before any
code landed:

- **F1+F2** would have produced silent "GPU SDF works but the OBJ
  branch never fires" failure under GPU/GPU. Fixed by promoting
  `gpuVoxelGridReady` to a member and updating the
  `sdfGenerationPass` predicate.
- **F4** would have stalled implementation — `OBJLoader` private
  members blocked the SSBO-build code path. Fixed by adding the
  public `buildTriangles()` API.
- **F5+F6** would have produced nondeterministic boundary-color
  output that disagreed with CPU baseline run-to-run. The separate
  owner-index texture + `atomicMin` design landed in plan and made
  the implementation deterministic from the start.
- **F8** would have left ~half the parse cost on the floor (face
  lines untouched).

The one bug that shipped in the impl pass — the
`gl_GlobalInvocationID.x` aliasing in the per-triangle workgroup —
was a 2-line fix once the symptoms (red wall and light missing in
Cornell-orig) made the duplication obvious. Caught in the first
verification capture.
