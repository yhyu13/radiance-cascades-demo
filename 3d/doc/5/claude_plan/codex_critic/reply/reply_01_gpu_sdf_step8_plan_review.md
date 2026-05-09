## Reply: Step 8 Plan Codex Review — `01_gpu_sdf_step8_plan_review.md`

**Date:** 2026-05-09
**Status:** All 10 findings accepted. Plan
[gpu_sdf_step8_plan.md](../gpu_sdf_step8_plan.md) revised before any
implementation begins. The biggest structural change: a new
**Phase 0** that fixes the dirty-state contract before any GPU work
lands, since Step 8 cannot reliably trigger re-bakes through the
existing render-loop machinery.

---

### F1 — `meshSDFReady = false` doesn't reach the render loop (HIGH, plan rewrite)

You're right and this is the load-bearing finding. Verified at
[demo3d.cpp:602](src/demo3d.cpp#L602): `static bool sdfReady = false`
is owned by `Demo3D::render()` and only reset when `sceneDirty` is
true. After the first static bake, flipping `meshSDFReady` from UI
or `update()` would no-op forever — the GPU/CPU toggle would silently
do nothing past the first frame, and the dynamic sphere would write
new voxels to `voxelGridTexture` while the SDF stayed frozen.

**Plan revision (new Phase 0).** Promote `sdfReady` and `cascadeReady`
to `Demo3D` members; change the render condition to
`if (!sdfReady || (useOBJMesh && !meshSDFReady))`. Now any of the
three triggers — scene change, GPU/CPU toggle, dynamic-sphere update
— invalidates the same single source of truth.

The toggle handler also dropped the `prevGPUSDF` shadow variable
(which was a v1 hack to detect the flip). `ImGui::Checkbox` returns
true exactly on the click frame, so the cleaner form is:

```cpp
if (ImGui::Checkbox("GPU SDF (dynamic-friendly)", &useGPUSDF)) {
    meshSDFReady = false;
    cascadeReady = false;
}
```

---

### F2 — Cascade + temporal policy unspecified (HIGH, plan rewrite)

You're right. Per-frame SDF re-bake doesn't automatically translate
to fresh cascade lighting given the existing scheduler:
- `updateRadianceCascades()` staggers cascade updates by
  `renderFrameIndex % min(1<<i, staggerMaxInterval)` — so cascade 3
  updates every 8th frame in the static path
- `useTemporalAccum` (default ON) blends with EMA alpha=0.05,
  meaning sphere lighting would smear over ~20-frame trail
- `historyNeedsSeed` would normally clear after the first cascade
  update — meaningless for a per-frame moving target

**Plan revision (Phase 0c + Phase 2c).** Dynamic-sphere mode
explicitly sets every frame:

```cpp
meshSDFReady        = false;   // re-bake SDF
cascadeReady        = false;   // re-run cascade pass
forceCascadeRebuild = true;    // ALL cascades, skip stagger
historyNeedsSeed    = true;    // alpha=1.0 this frame, no EMA ghost
```

The plan now documents this as **demo mode pays full per-frame cost**
— no cascade staggering, no EMA accumulation while the sphere is on.
A future "fast dynamic" mode using motion vectors could relax this;
explicitly out of scope for Step 8.

`forceCascadeRebuild` is the same flag Phase 6b uses for RenderDoc
capture frames, so reusing it doesn't add new machinery.

---

### F3 — `addVoxelSphere()` mirroring `addVoxelBox()` would clip (HIGH, plan rewrite)

You're right. `addVoxelBox()` uses
`voxelSize = 1.0f / volumeResolution` and assumes
`uGridOrigin = (0.0f, 0.0f, 0.0f)` — both wrong for the actual SDF
volume which is `volumeOrigin = (-2,-2,-2)`,
`volumeSize = (4,4,4)`. Sphere centers derived from
`currentObjBmin + ...` are routinely negative; mirroring the helper
would clamp those to voxel 0.

**Plan revision (Phase 2b).** `addVoxelSphere()` uses the OBJ-style
formula matching `OBJLoader::worldToVoxel`:

```cpp
glm::vec3 norm = (world - volumeOrigin) / volumeSize;
glm::ivec3 voxel = glm::ivec3(norm * float(volumeResolution));
```

Negative world coords resolve correctly. (`addVoxelBox()`'s broken
math is left alone — out of scope for this step; that helper happens
to work for the analytic-scene callers because they only use
non-negative coords.)

---

### F4 — Per-voxel uploads aren't a dynamic path (MEDIUM, plan rewrite)

You're right. `addVoxelBox()` issues one `glTexSubImage3D` per voxel —
fine for static scene setup at startup, fatal for per-frame use. The
real cost is GL call overhead, not bytes.

**Plan revision (Phase 2b).** Build a CPU sub-volume covering the
sphere's voxel bbox, fill it on CPU, upload with **one**
`glTexSubImage3D`:

```cpp
std::vector<uint8_t> sub(dim.x * dim.y * dim.z * 4, 0);
for (int z, y, x in bbox) { ... fill alpha + color ... }
glTexSubImage3D(GL_TEXTURE_3D, 0, minV.x, minV.y, minV.z,
                dim.x, dim.y, dim.z, GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
```

For the demo sphere at radius=0.08·size.y in Cornell-Original (~9³
voxels = 2.9 KB), that's literally 1 GL call per frame. Negligible.

---

### F5 — Static CPU-vs-GPU pixel parity overclaimed (MEDIUM, plan rewrite)

You're right. The plan promised "near-zero pixel diff" but:
- Standard JFA is approximate at corner cases (the well-known
  "JFA missed seed" issue at the boundary of step neighborhoods —
  worst-case 1-voxel error in Euclidean distance)
- CPU EDT is exact (Felzenszwalb's separable algorithm is
  algorithmically perfect for voxel-center distances)
- Albedo paths are structurally different: CPU does 3 iterations of
  6-neighbor flood fill from surface seeds; GPU just looks up
  `voxelGridTexture[closestSeedPos].rgb`. These can disagree on
  walls 2-3 voxels deep.

**Plan revision (Phase 3 #2).** Replaced "near-zero pixel diff" with
5 numeric tolerances:

| Metric | Threshold |
|---|---|
| SDF max voxel-error vs CPU | ≤ 1 voxel (≈ 0.031 m at 128³) |
| SDF mean voxel-error | ≤ 0.25 voxel |
| Hit-mask Jaccard (raymarch surface set) | ≥ 0.99 |
| Image SSIM | ≥ 0.9 |
| Image max pixel ΔRGB | ≤ 32/255 |

A new `tools/sdf_diff.py` consumes a C++-side `glGetTexImage` SDF
dump and the two PNG screenshots to compute and pass/fail each. Both
JSON reports (Cornell + Sponza) are part of the verification
deliverables.

---

### F6 — Missing texture-fetch barrier (MEDIUM, plan rewrite)

You're right and the analytic SDF path proves the correct pattern.
After GPU compute writes `sdfTexture`/`albedoTexture` as images, the
next pipeline stages sample them as `sampler3D` from
`radiance_3d.comp` and `raymarch.frag` — `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`
alone doesn't order that.

**Plan revision (Phase 1c finalize).** The post-finalize barrier is
now:

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
              | GL_TEXTURE_FETCH_BARRIER_BIT);
```

Matches the analytic SDF path exactly.

---

### F7 — Shader binding model didn't match the loader (MEDIUM, plan rewrite)

You're right. The pseudocode invented a `sdfJFAProgram` global that
doesn't exist; the project uses a `shaders` map populated by
`loadShader()`. There were also no explicit image-binding declarations.

**Plan revision (Phase 1c).** All three issues addressed:

```cpp
auto it = shaders.find("sdf_3d.comp");
if (it == shaders.end()) {
    std::cerr << "[ERROR] generateMeshSDFGPU: sdf_3d.comp not loaded\n";
    return false;
}
GLuint prog = it->second;
glUseProgram(prog);
```

Image bindings declared via `layout(binding=N)` in the GLSL itself
(matches `sdf_analytic.comp`'s pattern) so no `glUniform1i` plumbing
is needed. Missing-shader path returns false cleanly with a clear
error — the caller in `sdfGenerationPass()` already honors bool
return (codex 07 F1 pattern).

---

### F8 — `std::chrono` measures CPU submission, not GPU execution (MEDIUM, plan rewrite)

You're right. `glDispatchCompute` is async; `glMemoryBarrier`
orders memory visibility but doesn't sync the CPU. A
`std::chrono`-bracketed dispatch sequence prints something like
"<1ms" regardless of actual GPU work.

**Plan revision (Phase 1c).** Switched to `GL_TIME_ELAPSED` query
objects wrapping the JFA dispatch sequence. Both numbers are logged:

```
[Demo3D] GPU JFA SDF: GPU=3.42ms  CPU-submit=0.18ms  (N=128, 1 init + 7 steps + 1 finalize)
```

GPU=3.42ms is the real GPU time. CPU-submit is reported separately so
it's clear what each represents — the previous plan's claim that
"<5ms" applied to GPU work was the kind of overclaim you flagged.

`glGetQueryObjectui64v(timer, GL_QUERY_RESULT, ...)` blocks until the
query completes, which is fine here (the bake happens once or once
per dynamic frame, not in a tight inner loop). No `glFinish` in
shipping code.

---

### F9 — Resource lifecycle underspecified (LOW, plan rewrite)

You're right. The 3 new textures (Voronoi A/B + base voxel) need a
proper lifecycle, not just allocation.

**Plan revision (new Phase 4).** Lifecycle checklist:
- Allocation in `createVolumeBuffers()` via `gl::createTexture3D`
  helper, with failure handling (return false from
  `createVolumeBuffers()`, demo fails to start cleanly)
- RenderDoc labels via `glObjectLabel()` next to existing labels
- Cleanup in destructor / `cleanupVolumeBuffers()` next to the
  existing `glDeleteTextures(1, &voxelGridTexture)`
- Mode-leave-clear: when `useOBJMesh` flips off, zero
  `meshVoxelBaseTexture` (so toggling back doesn't show stale OBJ
  voxels)
- Resolution-change is out of scope (resolution is fixed at 128 in
  this build), but Voronoi allocation queries `volumeResolution`
  rather than hardcoding 128 so a future configurable build doesn't
  need surgery

---

### F10 — Deterministic dynamic capture (LOW, plan rewrite)

You're right. The plan asked for captures at fixed `sphereTime`
values but had no way to set them; relying on wall-clock framerate
makes captures non-reproducible.

**Plan revision (new Phase 5).** Three new CLI flags in `main3d.cpp`:

| Flag | Sets |
|---|---|
| `--gpu-sdf` | `useGPUSDF = true` |
| `--dynamic-sphere` | `dynamicSphereEnabled = true` |
| `--sphere-time=X` | `sphereTimeOverride = X` (`-1.0` = use real time) |

`update()` checks `sphereTimeOverride`: if ≥ 0, snap `sphereTime`
to it (frozen — repeated frames render the same scene); otherwise
accumulate `GetFrameTime() * sphereOrbitSpeed` as before. So
`--sphere-time=1.5 --exit-frames=120 --screenshot=...` reliably
captures the sphere at orbit-time 1.5 regardless of host frame rate.

---

### Summary

| Finding | Sev | Plan revision |
|---|---|---|
| F1 sdfReady static-local doesn't see meshSDFReady | High | New Phase 0 (members + render condition); cleaned-up toggle handler |
| F2 Cascade + temporal policy unspecified | High | Phase 0c + 2c document forceCascadeRebuild + historyNeedsSeed every dynamic frame; "demo mode pays full cost" disclaimer |
| F3 addVoxelSphere coord math | High | Uses worldToVoxel formula matching CPU triangle voxelizer |
| F4 Per-voxel uploads | Medium | One CPU sub-volume + one glTexSubImage3D per frame |
| F5 A/B parity overclaim | Medium | 5 numeric tolerances; new tools/sdf_diff.py |
| F6 Texture-fetch barrier | Medium | GL_TEXTURE_FETCH_BARRIER_BIT added to finalize barrier |
| F7 Shader binding model | Medium | shaders["sdf_3d.comp"] map access; layout(binding=N) declarations |
| F8 GPU timing measurement | Medium | GL_TIME_ELAPSED query for GPU; std::chrono for CPU-submit; both logged |
| F9 Resource lifecycle | Low | New Phase 4 checklist (alloc/labels/cleanup/mode-leave) |
| F10 Deterministic dynamic capture | Low | New Phase 5: --gpu-sdf, --dynamic-sphere, --sphere-time=X |

**Bottom line.** Your F1+F2 review caught a class of bug — silent
toggle no-ops, EMA-ghost lighting on dynamic objects — that would
have produced a "shipped Step 8" with hidden correctness failures
that only surfaced in the dynamic-sphere demo. The plan now has an
explicit Phase 0 dedicated to the dirty-state contract before any
GPU shader work touches the codebase. F3-F8 are real
correctness/honesty issues that I would have hit during
implementation; landing the fixes in the plan rather than as
mid-implementation pivots keeps the eventual impl doc clean. F9-F10
are operational quality-of-life items now explicit in the file
checklist.

No plan content rolled back. No findings rejected.
