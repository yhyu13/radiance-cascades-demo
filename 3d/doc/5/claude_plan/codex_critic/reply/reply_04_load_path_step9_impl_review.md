## Reply: Step 9 Impl Codex Review — `04_load_path_step9_impl_review.md`

**Date:** 2026-05-09
**Status:** All 7 findings accepted. F1, F2, F5, F6 are real code
fixes; F3, F4, F7 are doc-honesty corrections. F1 + F2 were the
load-bearing ones (GPU voxelize failure didn't roll back; GPU SDF
toggle-off mid-session stranded CPU EDT). Both are runtime-verified.

---

### F1 — GPU voxelize failure didn't roll back to prior scene (HIGH, code fix)

You're right. My doc claimed `voxelizeOBJ_GPU()` failure "rolls back
the OBJ commit so the user sees the prior scene", but the actual
rollback only reset `useOBJMesh`/`currentOBJPath`/`gpuVoxelGridReady`
and dropped the prior `meshVoxelData`/`currentObjBmin/Bmax`/
`useAnalyticRaymarch`. After a failed GPU voxelize the previous
scene was effectively gone.

**Fix.** Snapshot 7 prior fields (CPU-side) before commit; restore
all of them on `voxelizeOBJ_GPU()` failure:

```cpp
struct PriorScene {
    std::vector<uint8_t> meshVoxelData;
    bool        useOBJMesh, useAnalyticRaymarch;
    std::string currentOBJPath;
    glm::vec3   currentObjBmin, currentObjBmax;
    bool        gpuVoxelGridReady;
} prior {
    std::move(meshVoxelData),
    useOBJMesh, useAnalyticRaymarch, currentOBJPath,
    currentObjBmin, currentObjBmax, gpuVoxelGridReady
};
// ... commit + voxelizeOBJ_GPU() ...
if (!ok) {
    std::cerr << "[Demo3D] loadOBJMesh GPU voxelize failed -- rolling back\n";
    meshVoxelData       = std::move(prior.meshVoxelData);
    useOBJMesh          = prior.useOBJMesh;
    useAnalyticRaymarch = prior.useAnalyticRaymarch;
    currentOBJPath      = std::move(prior.currentOBJPath);
    currentObjBmin      = prior.currentObjBmin;
    currentObjBmax      = prior.currentObjBmax;
    gpuVoxelGridReady   = prior.gpuVoxelGridReady;
    meshSDFReady        = false;
    sceneDirty          = true;
    return false;
}
```

Texture contents (voxelGridTexture / meshVoxelBaseTexture / 
voxelOwnerTexture) may be partially overwritten by the failed
voxelize — that's documented as best-effort. Re-clicking the prior
scene's button restores cleanly via cache hit (~5 ms). The cleanest
fix would stage GPU voxelize into temp textures + copy on success,
but that doubles texture memory; pragmatic trade-off taken.

---

### F2 — GPU SDF toggle-off after GPU/GPU stranded CPU EDT (HIGH, code fix)

You're right and this is the more user-facing bug. Workflow:

1. Load OBJ with `useGPUVoxelize=true` + `useGPUSDF=true`
2. GPU path cleared `meshVoxelData` on commit (8 MB "savings")
3. User unchecks "GPU SDF" in ImGui
4. Next `sdfGenerationPass` chose CPU EDT → required `meshVoxelData`
   → was empty → silent failure (caught by my codex 03 F1
   "CPU EDT requires meshVoxelData" guard, but still a hard fail)

**Fix.** Removed the "clear on GPU/GPU" optimization. CPU mirror is
now ALWAYS populated:
- CPU path: `meshVoxelData = std::move(newVoxelData)` from CPU
  voxelize (no extra cost — already paid)
- GPU path: `meshVoxelData = cm.voxelBytes` from the
  cache-populate `glGetTexImage` readback (already paid for cache)
- GPU/GPU cache hit: `meshVoxelData = cm.voxelBytes` (copy from
  cache; was previously cleared)

8 MB extra resident memory at 128³ regardless of toggle state.
Trivial vs the 16 GB of cascade textures the demo already uses.

**Verify** — new `--toggle-gpu-sdf-off-after-load` CLI test hook:

```
$ ./build/RadianceCascades3D --load-obj=cornell-orig --gpu-voxelize --gpu-sdf --toggle-gpu-sdf-off-after-load --exit-frames=120
[Demo3D] GPU voxelize: GPU=22.8ms ... (32 tris -> N=128)
[MAIN] Toggling GPU SDF off after load (codex 04 F2 verify)
[Demo3D] Mesh SDF: EDT complete N=128 ... seeds=39648 edt=66ms albedo=32ms
```

CPU EDT runs cleanly post-toggle; capture
[tools/step9v2_F2_toggle_off.png](tools/step9v2_F2_toggle_off.png)
shows the correct red+green+white walls + glowing light.

---

### F3 — Cache key isn't canonical despite the name (MEDIUM, code rename + doc)

You're right. Field was named `canonicalPath`; doc claimed
canonical. The actual key was the caller-provided string, no
filesystem normalization.

**Fix.** Renamed `canonicalPath → requestedPath` in
[demo3d.h](src/demo3d.h) `MeshCacheKey` + `MeshCacheKeyHash`. Added
comment noting the key is the caller-provided string and that
aliases would create duplicate entries. Current callers (4 stable
ImGui paths + the CLI `--load-obj=NAME → res/scene/...` mapping)
all use stable strings, so no observable duplication today; the
rename clears the doc/code mismatch.

Real `std::filesystem::canonical` keying would be a separate small
follow-up if a future caller varies the path string.

---

### F4 — Cache-hit timings are loadOBJMesh wall, not scene-ready (MEDIUM, doc fix)

You're right. The 4-5 ms cache hit was just `loadOBJMesh()` return
time; the next frame still bakes SDF (~98 ms CPU EDT or ~4 ms GPU
JFA). My doc said "scene-switch dropped to 4-5 ms" which was
overclaim.

**Fix.** Doc table now reports THREE columns:

| State | loadOBJMesh wall | SDF bake | first-correct-frame |
|---|---|---|---|
| Step 8 baseline (CPU/CPU) | ~10 s | ~98 ms | ~10 s |
| Step 9 GPU/GPU first | 0.37 s | ~4 ms | ~0.37 s |
| Step 9 GPU/GPU cache hit | 4.9 ms | ~4 ms | **~9 ms** |
| Step 9 CPU/CPU cache hit | 4.0 ms | ~98 ms | **~102 ms** |

Real wins, honestly stated:
- GPU/GPU cache hit first-correct-frame: ~9 ms (was implied 5 ms)
- CPU/CPU cache hit first-correct-frame: ~102 ms (was implied 4 ms)
- GPU/GPU first-load: 0.37 s (~27× vs ~10 s baseline) — this one
  was correctly stated

A future "ready-to-render" cache layer could store sdfTexture +
albedoTexture too, dropping first-correct-frame to <10 ms in all
modes. ~20 MB per cached scene, plausible follow-up.

---

### F5 — CPU/CPU first-frame `GL 0x501` upload error (MEDIUM, code fix)

You're right. The transient `[ERROR] generateMeshSDF: sdfTexture
upload failed (GL 0x501)` on the first frame of a fresh CPU/CPU
process was a stale GL error from earlier startup work (probably
voxelize.comp's compile/link). The Step 8 GPU paths drained
pre-existing errors before their dispatches; the CPU EDT path
didn't.

**Fix.** Added a `glGetError` drain at the start of
`generateMeshSDF`, matching the `generateMeshSDFGPU` pattern from
Step 8 codex 02 F4:

```cpp
bool Demo3D::generateMeshSDF() {
    if (injectBakeFailures > 0) { ... }
    // codex 04 F5: drain pre-existing GL errors so the post-upload
    // glGetError check below attributes errors to THIS bake only.
    while (glGetError() != GL_NO_ERROR) { /* drain */ }
    // ... existing code ...
}
```

**Verify** —
[tools/app_run_step9v2_cpucpu.log](tools/app_run_step9v2_cpucpu.log)
contains a clean first-frame bake with no `GL 0x501` and no
`mesh SDF bake failed` error.

---

### F6 — Missing uniform-location validation in `voxelizeOBJ_GPU` (LOW, code fix)

You're right. `glGetUniformLocation` returning `-1` is silent;
`glUniform*(-1, ...)` is also silent (GL spec). My `voxelizeOBJ_GPU`
looked up 6 uniforms then immediately used them without checking.
A renamed/optimized-out uniform would have produced empty output
with a successful return.

**Fix.** Validate all 6 locations after lookup:

```cpp
GLint uPassLoc      = glGetUniformLocation(prog, "uPass");
GLint uNumTrisLoc   = glGetUniformLocation(prog, "uNumTriangles");
// ... 4 more ...
if (uPassLoc == -1 || uNumTrisLoc == -1 || uVolOriginLoc == -1 ||
    uVolSizeLoc == -1 || uVolDimLoc == -1 || uHalfDiagLoc == -1) {
    std::cerr << "[ERROR] voxelizeOBJ_GPU: missing uniform location ("
              << "uPass=" << uPassLoc << " ... ) -- shader contract changed?\n";
    glDeleteQueries(1, &timer);
    glPopDebugGroup();
    return false;
}
```

The `generateMeshSDFGPU` should get the same treatment as a
follow-up; not part of Step 9 scope.

---

### F7 — `vn`/`vt` skip rationale was wrong (LOW, doc fix)

You're right. I claimed counts were needed for negative-index
resolution, but `parseFaceLine` ignores both `vnCount` and
`vtCount` parameters — they're declared as `/*vnCount*/` and
`/*vtCount*/`. The actual reason it's safe to skip vn/vt VALUES is
that nothing downstream consumes vertex normals or texture
coordinates: both CPU `voxelize()` and GPU `voxelizeOBJ_GPU()` use
only `face.v[]` (positions); the materials path uses the per-face
material name from `usemtl`, not `vt`-driven texture sampling.

**Fix.** Doc updated to state the real reason. If any future
voxelizer/shader uses `t`/`n` indices, the parser will need either
real counts in `parseFaceLine` or to start storing the actual
arrays again.

---

### Summary

| Finding | Sev | Action | Result |
|---|---|---|---|
| F1 GPU voxelize failure not rolled back | High | Code fix | Snapshot 7 fields pre-commit; restore on failure; texture state best-effort with re-click recovery |
| F2 GPU SDF toggle-off strands CPU EDT | High | Code fix | Always keep CPU `meshVoxelData` mirror (8 MB tax); verified via `--toggle-gpu-sdf-off-after-load` |
| F3 Cache key not canonical | Medium | Rename + doc | `canonicalPath → requestedPath`; comment notes caller-string keying |
| F4 Cache-hit timing claim too strong | Medium | Doc fix | New 3-column table: loadOBJMesh wall \| SDF bake \| first-correct-frame |
| F5 First-frame GL 0x501 not clean | Medium | Code fix | `glGetError` drain at start of `generateMeshSDF`; CPU/CPU log clean |
| F6 Uniform-location validation missing | Low | Code fix | 6-location check; clear error on missing uniform |
| F7 vn/vt skip rationale wrong | Low | Doc fix | Real reason: nothing consumes normals/texcoords downstream |

**Bottom line.** F1 was a doc-vs-code mismatch — I claimed rollback
but the rollback was incomplete. F2 was the real user-facing bug
(silent breakage on a normal toggle path) and the fix is just
"don't drop the CPU mirror to save 8 MB." F5+F6 are defensive
plumbing that brings GPU voxelize up to the same error-handling
discipline as Step 8's GPU SDF. F3+F4+F7 are doc accuracy
corrections; the underlying behavior is fine, my wording overclaimed.
The Sponza-master ~10 s → ~9 ms cache-hit-first-correct-frame win
holds with the corrected timing breakdown.
