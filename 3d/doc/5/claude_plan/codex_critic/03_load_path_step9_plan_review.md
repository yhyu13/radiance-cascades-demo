# Critic Review 03 - load_path_step9_plan.md

Reviewed: 2026-05-09T15:07:05+08:00

Target: `doc/5/claude_plan/load_path_step9_plan.md`

## Verdict

The plan has the right high-level decomposition: speed up OBJ parsing, cache finished voxel grids, and add a GPU triangle voxelizer as a separate load-path toggle. Keeping GPU voxelization separate from GPU SDF is also a good testability choice.

It is not implementation-ready as written. The largest gap is data ownership: the proposed fastest path says GPU/GPU can skip readback into `meshVoxelData`, but the current renderer still treats a non-empty `meshVoxelData` vector as the proof that an OBJ mesh exists. The cache shape also stores only CPU voxel data while the GPU/GPU path wants to avoid producing that data. The GPU voxelizer design has additional correctness risks around private `OBJLoader` geometry access, `RGBA8` as an `R32UI` atomic image alias, nondeterministic color races, and missing synchronization for texture copy/readback.

## Evidence Checked

- Current `Demo3D::loadOBJMesh()` load/normalize/voxelize/commit path in `src/demo3d.cpp`.
- Current OBJ mesh SDF branch in `Demo3D::sdfGenerationPass()`.
- Current Step 8 GPU JFA and dynamic base-texture ownership.
- Current `OBJLoader` public/private API and CPU voxelizer implementation in `src/obj_loader.h`.
- Current dormant `res/shaders/voxelize.comp`.
- Current GL texture allocation helper in `src/gl_helpers.cpp`.
- Existing Step 7/8 logs for OBJ counts and GPU SDF timings.

## What Looks Good

- Parser/cache/GPU-voxelize is the right split; these are load-path optimizations and should not touch the GI shaders.
- Keeping CPU EDT and CPU voxelization as defaults is the right baseline-preservation policy.
- A separate GPU voxelize toggle is useful for isolating parser, voxelizer, and SDF-bake regressions.
- Porting the CPU closest-point triangle test to GLSL is the right geometric target for parity with `OBJLoader::voxelize()`.
- The planned combo matrix and voxel-grid Jaccard test are the right kind of verification for this phase.

## Findings

### 1. The GPU/GPU path cannot skip `meshVoxelData` under the current SDF contract

Severity: High

The plan says the common `useGPUVoxelize && useGPUSDF` path should skip readback into `meshVoxelData`. Current code will not treat that as an OBJ mesh SDF input. `Demo3D::sdfGenerationPass()` enters the OBJ branch only when:

```cpp
if (useOBJMesh && !meshVoxelData.empty()) {
```

at `src/demo3d.cpp:1780`. If GPU voxelization writes only `voxelGridTexture` / `meshVoxelBaseTexture` and leaves `meshVoxelData` empty, the code falls through into the analytic SDF path instead of calling `generateMeshSDFGPU()`.

This also conflicts with the current commit model in `loadOBJMesh()`, where `meshVoxelData = std::move(newVoxelData)` is part of the all-or-nothing OBJ scene commit (`src/demo3d.cpp:5012-5020`).

Suggested fix: either always keep a CPU `meshVoxelData` mirror, including the GPU/GPU path, or split the readiness contract into explicit CPU/GPU voxel-source state. For example, allow the OBJ branch when `useOBJMesh && (meshVoxelData non-empty || gpuVoxelGridReady)`, and make `generateMeshSDF()` reject the no-CPU-vector case while `generateMeshSDFGPU()` consumes the texture source.

### 2. The cache design contradicts the no-readback GPU path

Severity: High

`CachedMesh` stores only:

```cpp
std::vector<uint8_t> meshVoxelData;
glm::vec3 bmin, bmax;
```

That works for CPU voxelize and CPU EDT. It does not match the fastest path described later, where GPU voxelize + GPU SDF skips readback. If no CPU vector is produced, the cache has nothing to store. If a CPU vector is read back just to populate the cache, the plan loses one of its advertised wins.

The cache key also needs to include more than the requested path string. It should at least account for resolved successful path, voxelization algorithm, volume resolution, volume origin/size, and probably file/MTL mtimes if hot edits matter. The plan documents no disk invalidation, which is fine, but algorithm and resolution are not optional if both CPU and GPU voxelization can produce different voxel/color results.

Suggested fix: make cache entries source-aware. A robust entry could store a CPU voxel vector when available plus enough metadata to restore/upload a GPU texture, and it should be keyed by canonical path + voxel settings + voxelizer kind. Alternatively, accept the readback cost once on GPU cache population and state that explicitly.

### 3. Cached scene-switch timing omits the SDF bake that still follows

Severity: Medium

The plan says repeat OBJ clicks become about 50 ms, "just the GPU upload." That is only the `loadOBJMesh()` return path, not the time until the scene is render-ready. The commit sets `meshSDFReady=false` and `sceneDirty=true` (`src/demo3d.cpp:5012-5019`), so the next render still needs:

- CPU EDT + albedo dilation for the CPU SDF path, about 90-100 ms in Step 8 logs.
- GPU JFA for the GPU SDF path, about 3-4 ms.
- Cascade reseed/rebuild work.

So the CPU EDT cached baseline should not be advertised as a 50 ms ready-to-render scene switch unless the cache also stores `sdfTexture`/`albedoTexture` or the timing explicitly stops before SDF bake.

Suggested fix: separate "loadOBJMesh wall time" from "first correct rendered frame wall time" in the plan and verification.

### 4. `Demo3D::voxelizeOBJ_GPU()` cannot build triangle SSBOs from the current loader API

Severity: High

The plan's CPU-side SSBO build says:

```cpp
for (each face f in objLoader) {
    glm::vec3 kd = lookupKd(f.materialName);
}
```

But `OBJLoader` does not expose faces, vertices, face materials, or parsed materials. They are private members at `src/obj_loader.h:365-370`. The only public post-load geometry operation is `voxelize()`.

Suggested fix: add an explicit public API for normalized triangle extraction, such as `buildTriangles(std::vector<GPUTriangle>& out)` or const accessors for vertices/faces/materials. Prefer a method that reuses the CPU material-color logic so CPU and GPU voxelizers cannot drift.

### 5. The `RGBA8` texture as `R32UI` atomic alias is not safe enough as the primary design

Severity: High

The plan binds `voxelGridTexture`, allocated as `GL_RGBA8` via `glTexImage3D()` (`src/gl_helpers.cpp:20-34`, `src/demo3d.cpp:2513`), as:

```cpp
layout(r32ui, binding = 1) uniform uimage3D uVoxelGridU32;
glBindImageTexture(1, voxelGridTexture, ..., GL_R32UI);
```

This relies on image-format compatibility and on the packed integer bit pattern mapping to RGBA8 bytes the same way the CPU vector does. That is a fragile contract to make central to the feature. It can fail if the texture's image-format compatibility is by class rather than by size, and the "0xAABBGGRR matches RGBA8 layout" wording is not a portable material-color contract.

Suggested fix: use a real `GL_R32UI` voxel-owner/packed-color texture for atomics, then run a small conversion pass into the existing `GL_RGBA8` `voxelGridTexture` and `meshVoxelBaseTexture`. That makes atomics legal and keeps the existing sampler/image consumers on their normal texture format.

### 6. Atomic CAS gives first-completer-wins, not deterministic first-writer-wins

Severity: Medium

The plan says atomic CAS makes output run-to-run deterministic and preserves first-writer-wins. It does not. `imageAtomicCompSwap(..., 0u, packed)` ensures only one triangle writes a voxel, but the winner among overlapping triangles is whichever invocation reaches the atomic first. That is GPU scheduling dependent, not the CPU face-order first-writer rule in `OBJLoader::voxelizeTriangle()` (`src/obj_loader.h:440-446`).

The occupancy set may still match well, but material colors at overlaps can differ nondeterministically. This matters for Cornell wall/box boundaries and any overlapping Sponza geometry.

Suggested fix: if deterministic material parity matters, encode a priority key with triangle index and use an atomic min/max scheme, or use a separate owner-index texture followed by a deterministic color resolve pass. If only occupancy matters, state that material/color parity is best-effort and verify it separately.

### 7. The dispatch-to-copy/readback barriers are incomplete

Severity: Medium

The plan uses:

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
glCopyImageSubData(... voxelGridTexture -> meshVoxelBaseTexture ...);
```

`GL_TEXTURE_FETCH_BARRIER_BIT` covers later sampler fetches. It is not the right named barrier for texture update/copy/readback operations after shader image writes. The GPU voxelize path also plans a `glGetTexImage` readback for `useGPUVoxelize && !useGPUSDF`, which needs explicit synchronization with the preceding shader writes.

Suggested fix: add the correct texture-update/readback barriers around copy/readback operations, and validate with GL errors. At minimum, do not assume the Step 8 sampler barrier is sufficient for `glCopyImageSubData` and `glGetTexImage`.

### 8. The parser rewrite does not yet address the face-token hotspot

Severity: Medium

The parser phase targets `std::from_chars` for float lines and skipping `vn`/`vt` values. That should help, but Sponza's face lines are also a large part of the file. Current face parsing still uses `std::string tok`, `std::istringstream`, and `std::stoi()` in `parseVertexIndex()` (`src/obj_loader.h:378-398`). If Step 9 only optimizes `v` lines, the claimed 2-3 s to 300-500 ms parse drop is not well supported.

Also, seed-count equality alone will not catch material regressions. A parser can produce the same occupied voxel count while assigning wrong `usemtl` colors or failing to load `.mtl`.

Suggested fix: parse face tokens with `from_chars`/`string_view` too, preserve fan triangulation and negative indices, and verify vertex count, face count, dropped-index count, material count, seed count, and at least a color/material histogram or image diff.

### 9. The GPU voxelizer timing target is optimistic without large-triangle accounting

Severity: Medium

Per-triangle threading with a bbox loop is plausible, but the plan's "typically 10-30 voxels" statement is not a safe general assumption. Large Cornell wall/floor triangles and large Sponza architectural triangles can cover thousands of voxel centers each at 128^3. That can create severe per-invocation imbalance even if the average triangle is small.

Suggested fix: keep the per-triangle approach as a first implementation, but frame the ~10 ms target as a hypothesis. Add logs for total tested voxel centers, max triangle bbox voxel count, and GPU time per asset. If large triangles dominate, split large bboxes or switch to voxel/tile-based rasterization for those cases.

### 10. Toggle semantics need to distinguish "next load" from "current scene"

Severity: Low

The proposed ImGui handler clears `meshCache` and invalidates the SDF when `useGPUVoxelize` changes. That does not re-voxelize the current OBJ. It will only affect the next load. Invalidating `meshSDFReady` immediately just rebakes the existing voxel grid with the new SDF setting, which can make the user think the voxelizer toggle affected the active mesh when it did not.

Suggested fix: either make the checkbox label explicit, such as "GPU voxelize on next OBJ load", or if an OBJ is active, reload the current OBJ after confirmation / on the next frame. For the CLI path, ensure `--gpu-voxelize` is applied before `--load-obj` execution, as `--gpu-sdf` currently is.

## Verification Gaps To Add

- Verify `useGPUVoxelize=true,useGPUSDF=true` works without CPU readback only after the SDF branch no longer depends on `meshVoxelData.empty()`.
- Verify cache hit timing both for `loadOBJMesh()` return time and first correct rendered frame.
- Add GPU/CPU voxel occupancy and material-color comparison, not just Jaccard occupancy.
- Add a validation path for GL image-format compatibility if the R32UI alias design is kept.
- Preserve logs for parser time, voxelizer GPU time, tested voxel count, occupied voxel count, SDF bake time, and first-frame time.

