# Plan: Phase 4 Step 8 — GPU JFA SDF + Dynamic Sphere Overlay (revised after codex 01)

## Changelog (post codex `01_gpu_sdf_step8_plan_review.md`)

All 10 findings accepted. Plan revised before implementation:

- **F1 (high) — Dirty-state contract.** The render loop owns
  `static bool sdfReady` / `cascadeReady`; flipping `meshSDFReady` from
  UI/`update()` would no-op after the first static bake. Plan now
  promotes both to `Demo3D` members and changes the render condition
  to `if (!sdfReady || (useOBJMesh && !meshSDFReady))` so any of the
  three triggers (scene change, GPU/CPU toggle, dynamic-sphere update)
  invalidates the SAME source of truth.
- **F2 (high) — Cascade + temporal policy.** Dynamic-sphere mode
  explicitly forces ALL cascades to rebuild this frame
  (`forceCascadeRebuild = true`) AND seeds temporal history
  (`historyNeedsSeed = true`) every frame the sphere moves. Plan
  documents this as "demo mode pays full per-frame cost; staggered
  cascades + EMA stay disabled while the sphere is on" rather than
  promising "automatic" cascade tracking.
- **F3 (high) — `addVoxelSphere` math.** Mirroring `addVoxelBox`
  literally would clip negative-coordinate spheres because that helper
  assumes origin (0,0,0). Plan now uses the OBJ-style world→voxel
  formula `(world - volumeOrigin) / volumeSize * resolution` with
  clamped bounds — same convention as the working CPU triangle
  voxelizer.
- **F4 (medium) — Batched upload.** `addVoxelBox` does per-voxel
  `glTexSubImage3D` calls (thousands per frame). Plan now builds a
  CPU sub-volume covering the sphere bbox (max ~16³ voxels at
  radius=0.08·size.y) and uploads it in ONE `glTexSubImage3D` call.
- **F5 (medium) — A/B parity tolerances.** Standard JFA is approximate
  vs Felzenszwalb's exact EDT; albedo paths differ structurally
  (CPU 6-neighbor flood vs GPU nearest-seed lookup). Plan replaces
  "near-zero pixel diff" with: max/mean SDF voxel-error ≤ 1 voxel,
  hit-mask Jaccard ≥ 0.99, image SSIM ≥ 0.9 with a documented
  tolerance threshold.
- **F6 (medium) — Texture-fetch barrier.** GPU writes images then
  shaders sample as `sampler3D`; need
  `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT`
  after finalize, matching the analytic SDF path.
- **F7 (medium) — Shader binding model.** Plan replaces the imaginary
  `sdfJFAProgram` with `shaders["sdf_3d.comp"]` map access (project
  convention). Image bindings declared via `layout(binding=N)` in
  GLSL so no `glUniform1i` plumbing is needed; missing-shader path
  returns false with a clear error.
- **F8 (medium) — GPU timing.** `std::chrono` around dispatches only
  measures CPU submission. Plan switches to `GL_TIME_ELAPSED` query
  objects wrapping the JFA debug group; logs both CPU-submission and
  GPU-execution times distinctly so `< 5 ms GPU` is a real claim.
- **F9 (low) — Resource lifecycle.** Cleanup, RenderDoc labels,
  allocation failure handling, and mode-leave-clear behavior added to
  Phase 1b's checklist for the 3 new textures.
- **F10 (low) — Deterministic dynamic capture.** New CLI hook
  `--sphere-time=X` (and existing `--exit-frames`) lets the verify
  script reproducibly capture sphere at time X without depending on
  wall-clock framerate.

## Context

Steps 2-3 chose a CPU EDT path (Felzenszwalb separable, ~67ms/scene)
for static OBJ mesh SDF baking. That was the right call for correctness
and one-shot bake cost, but it locks us out of dynamic objects:
re-baking 2M voxels (128³) per frame on CPU is a 4-fps frame killer.

The user wants real-time dynamic objects in the GI scene. Two GPU
shaders sit in `res/shaders/` since project init for exactly this — but
both have bugs and have never been dispatched: `sdf_3d.comp` (Jump
Flooding Algorithm SDF) and `voxelize.comp` (triangle voxelizer). The
just-landed Step 7 cleanup commit removed their `loadShader()` calls
because they were dead noise; this step un-disables the GPU SDF path
specifically and proves it with a moving-sphere demo overlaid on the
loaded OBJ.

User decisions captured during planning:
- **Coexist, not replace**: a runtime toggle picks GPU vs CPU. CPU
  stays the default baseline so we can A/B test correctness.
- **Sphere overlay (not standalone scene)**: the moving sphere is
  injected ON TOP of whichever OBJ is loaded each frame. More visually
  compelling than a single floating sphere, but requires caching the
  static OBJ voxels and re-merging each frame (CPU re-voxelize per
  frame would re-introduce the 67ms cost we're trying to escape).
- **Conservative-band UDF match**: the GPU finalize subtracts
  `voxelSz × √3/2` so `raymarch.frag`'s existing thresholds (EPSILON,
  0.002) land identically. The toggle is then truly drop-in and
  pixel-diff vs CPU becomes a meaningful regression check.
- **GPU triangle voxelizer (`voxelize.comp`) deferred to Step 9**.
  This step uses the existing CPU triangle voxelizer for the static
  OBJ base and a small CPU sphere voxelizer (`addVoxelSphere`) for the
  dynamic overlay. That isolates "fix JFA" from "fix triangle
  voxelizer" — two separable problems with their own failure modes.

Outcome: a runtime toggle ("GPU SDF (dynamic)"), a second toggle
("Dynamic sphere overlay") that's only meaningful with GPU SDF on,
and a visibly orbiting sphere whose lighting is correctly captured by
the radiance cascades (because the SDF is now re-baked every frame).

---

## Approach

### Phase 0 — SDF/Cascade dirty-state contract (codex 01 F1, F2)

Before any GPU work lands, replace the lifecycle gap that would let a
toggle/dynamic-update silently no-op.

**0a. Promote `sdfReady` and `cascadeReady` to `Demo3D` members.**
Currently both are `static bool` locals inside `Demo3D::render()`
([demo3d.cpp:602](src/demo3d.cpp#L602), [:611](src/demo3d.cpp#L611)).
Move to header members initialized to `false`. No external semantics
change for the static-OBJ path; this is a refactor that gives Step 8
something to invalidate.

**0b. Render condition.** Change

```cpp
if (!sdfReady) { ... sdfGenerationPass(); ... }
```

to

```cpp
if (!sdfReady || (useOBJMesh && !meshSDFReady)) { ... sdfGenerationPass(); ... }
```

Now any of these triggers the next bake:
- `sceneDirty` (existing — voxelizationPass clears `sdfReady`)
- GPU/CPU toggle flip (`useGPUSDF` change → `meshSDFReady = false`)
- Dynamic-sphere update (`update()` clears `meshSDFReady` per frame)

**0c. Cascade invalidation contract for dynamic mode (F2).** When
`dynamicSphereEnabled && useOBJMesh && useGPUSDF`, every frame:

```cpp
meshSDFReady       = false;       // re-bake SDF
forceCascadeRebuild = true;       // ALL cascades (skip stagger)
historyNeedsSeed   = true;        // reseed temporal history (no EMA ghost)
```

`forceCascadeRebuild` is consumed by `updateRadianceCascades()` —
existing flag (Phase 6b uses it for RenderDoc capture frames; reusing
the same machinery). Document explicitly that **dynamic mode pays
full per-frame cost**: no cascade staggering, no EMA accumulation.
A future "fast dynamic" mode could relax this with motion vectors;
out of scope for Step 8.

**0d. ImGui toggle handler.** When `useGPUSDF` flips, clear
`meshSDFReady` so the next frame re-bakes through the new path:

```cpp
if (ImGui::Checkbox("GPU SDF (dynamic-friendly)", &useGPUSDF)) {
    meshSDFReady = false;
    cascadeReady = false;            // SDF will change → cascades stale
}
```

ImGui::Checkbox returns true on the click frame, so this fires exactly
when the user flips it. No `prevGPUSDF` shadow needed (was a v1 hack).

---

### Phase 1 — Fix and dispatch GPU JFA

#### 1a. Rewrite [res/shaders/sdf_3d.comp](res/shaders/sdf_3d.comp)

Three separate kernels driven by a `uniform int uPass` switch (one
file, three branches; matches the project's existing single-shader
convention):

| Pass | Reads | Writes | Description |
|---|---|---|---|
| 0 init | `uVoxelGrid` (R) | `oVoronoi` (W) | For each voxel: if alpha > threshold, store `(pos.xyz, 1.0)`; else `(-1,-1,-1, 0.0)` |
| 1 step (×log₂N=7) | `iVoronoi` (R), ping | `oVoronoi` (W), pong | 27-neighbor JFA: pick neighbor whose stored seed-position is closest to `pos`; write `(closestSeedPos, 1.0)` |
| 2 finalize | `iVoronoi` (R) + `uVoxelGrid` (R) | `oSDF` (W) + `oAlbedo` (W) | distance = `length(pos - closestSeedPos)` × voxelSize; subtract `voxelSz × √3/2` (conservative band, codex 04 F1 logic); albedo = `imageLoad(uVoxelGrid, closestSeedPos).rgb` |

Removes the `safeLoad(image3D img, ivec3 pos)` helper that caused the
compile error — `image3D` as a function parameter triggers an overload
mismatch on this driver. Inline bounds checks at each call site.

JFA stepSize sequence: N/2, N/4, ..., 1. At N=128 that's 7 passes.

#### 1b. Ping-pong Voronoi textures

Two `voronoiTextureA`, `voronoiTextureB` (RGBA32F, 128³, 8 MB each =
16 MB total). JFA reads from A writes to B, swap, repeat. Strictly
deterministic vs single-texture-with-memory-barrier (which can race at
boundaries).

Allocated in `Demo3D::init()` next to existing `sdfTexture`/`voxelGridTexture`.

#### 1c. New `Demo3D::generateMeshSDFGPU()` (codex 01 F6, F7, F8)

Mirrors the bool-returning shape of CPU `generateMeshSDF()` so the
caller in `sdfGenerationPass()` doesn't care which path ran. Three
codex 01 fixes baked in:

- **F7 — shader access via map.** Fetch `shaders["sdf_3d.comp"]`
  (project convention via `loadShader`). Image bindings declared via
  `layout(binding=N)` in GLSL so no `glUniform1i` plumbing.
- **F6 — texture-fetch barrier.** The final `glMemoryBarrier()` adds
  `GL_TEXTURE_FETCH_BARRIER_BIT` so subsequent `sampler3D` reads in
  `radiance_3d.comp` and `raymarch.frag` see the new SDF / albedo.
- **F8 — real GPU timing.** Wrap the JFA dispatches in a
  `GL_TIME_ELAPSED` query. CPU submission time logged separately so
  we don't conflate the two.

```cpp
bool Demo3D::generateMeshSDFGPU() {
    auto it = shaders.find("sdf_3d.comp");
    if (it == shaders.end()) {
        std::cerr << "[ERROR] generateMeshSDFGPU: sdf_3d.comp not loaded\n";
        return false;
    }
    GLuint prog = it->second;

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "GPU JFA SDF");

    // codex 01 F8: real GPU-side timing via GL_TIME_ELAPSED query.
    GLuint timer = 0;
    glGenQueries(1, &timer);
    glBeginQuery(GL_TIME_ELAPSED, timer);

    auto cpuT0 = std::chrono::high_resolution_clock::now();
    const int N = volumeResolution;
    const int wg = (N + 7) / 8;

    glUseProgram(prog);
    GLint uPassLoc = glGetUniformLocation(prog, "uPass");
    GLint uStepLoc = glGetUniformLocation(prog, "uStepSize");
    GLint uVoxLoc  = glGetUniformLocation(prog, "uVoxelSizeWorld");

    // Pass 0: init Voronoi from voxelGridTexture (binding=0 -> uVoxelGrid R,
    //         binding=1 -> oVoronoi W). All bindings declared in GLSL via
    //         layout(binding=N).
    glUniform1i(uPassLoc, 0);
    glBindImageTexture(0, voxelGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(1, voronoiTextureA,  0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(wg, wg, wg);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 1: log2(N) JFA steps with ping-pong (binding=2 -> iVoronoi R,
    //         binding=1 -> oVoronoi W; swap each pass).
    GLuint readTex = voronoiTextureA, writeTex = voronoiTextureB;
    glUniform1i(uPassLoc, 1);
    int passCount = 0;
    for (int step = N/2; step >= 1; step /= 2) {
        glUniform1i(uStepLoc, step);
        glBindImageTexture(2, readTex,  0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA32F);
        glBindImageTexture(1, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(wg, wg, wg);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        std::swap(readTex, writeTex);
        ++passCount;
    }

    // Pass 2: finalize -> sdfTexture + albedoTexture (with conservative band).
    const float voxelSize = volumeSize.x / float(N);
    glUniform1i(uPassLoc, 2);
    glUniform1f(uVoxLoc, voxelSize);
    glBindImageTexture(2, readTex,         0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA32F);
    glBindImageTexture(0, voxelGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(3, sdfTexture,      0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
    glBindImageTexture(4, albedoTexture,   0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(wg, wg, wg);

    // codex 01 F6: texture-fetch + image-access barriers BOTH needed before
    // radiance_3d.comp / raymarch.frag sample sdfTexture + albedoTexture as
    // sampler3D. Matches the analytic SDF path's barrier set.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    glEndQuery(GL_TIME_ELAPSED);
    glPopDebugGroup();

    // CPU-submission timing: returns immediately after dispatches queued.
    float cpuMs = std::chrono::duration<float, std::milli>(
                    std::chrono::high_resolution_clock::now() - cpuT0).count();

    // GPU timing: blocks until the timer query completes (cheap if dispatches
    // already finished by next frame). Don't poll in a tight loop.
    GLuint64 gpuNs = 0;
    glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &gpuNs);
    glDeleteQueries(1, &timer);
    float gpuMs = gpuNs * 1.0e-6f;

    std::cout << "[Demo3D] GPU JFA SDF: GPU=" << gpuMs << "ms  "
              << "CPU-submit=" << cpuMs << "ms  "
              << "(N=" << N << ", 1 init + " << passCount << " steps + 1 finalize)\n";
    return true;
}
```

The CPU-submit timing being far smaller than GPU time is the
expected/correct behavior; both are reported so a regression in
either direction is visible.

#### 1d. Toggle wiring in `sdfGenerationPass()`

```cpp
if (useOBJMesh && !meshVoxelData.empty()) {
    if (!meshSDFReady) {
        bool ok = useGPUSDF ? generateMeshSDFGPU() : generateMeshSDF();
        if (!ok) {
            std::cerr << "[ERROR] sdfGenerationPass: bake failed (path="
                      << (useGPUSDF ? "GPU" : "CPU") << ")\n";
            return false;
        }
        meshSDFReady = true;
    }
    return true;
}
```

#### 1e. Re-add `loadShader("sdf_3d.comp")` calls

Reverts the loadShader removal from the just-cleaned-up paths in
`Demo3D::init()` and the reload-shaders path. The shader is now
actually used; the cleanup commit no longer makes sense after Phase 1.
`voxelize.comp` stays unloaded (deferred to Step 9).

#### 1f. ImGui checkbox

In the existing scene/control panel, near the OBJ buttons. Per Phase 0
(codex 01 F1), the toggle invalidates the SAME flags any other
trigger uses:

```cpp
if (ImGui::Checkbox("GPU SDF (dynamic-friendly)", &useGPUSDF)) {
    meshSDFReady = false;     // re-bake via the new path next frame
    cascadeReady = false;     // SDF will change -> cascades stale
}
```

Default OFF. (`ImGui::Checkbox` returns true exactly on the click
frame, so the no-op-when-unchanged behavior is automatic — no
`prevGPUSDF` shadow needed.)

---

### Phase 2 — Dynamic sphere overlay

#### 2a. Cache static OBJ voxels in `meshVoxelBaseTexture`

New persistent texture (RGBA8, 128³, 8 MB), allocated in
`Demo3D::init()`. Populated **once** in `loadOBJMesh()`'s commit block
from `meshVoxelData` (so the existing meshVoxelData CPU buffer keeps
its current role; this is a pure GPU-side cache for fast re-merge).

```cpp
// in loadOBJMesh commit block, after the existing voxelGridTexture upload:
glBindTexture(GL_TEXTURE_3D, meshVoxelBaseTexture);
glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                volumeResolution, volumeResolution, volumeResolution,
                GL_RGBA, GL_UNSIGNED_BYTE, meshVoxelData.data());
glBindTexture(GL_TEXTURE_3D, 0);
```

#### 2b. `addVoxelSphere(center, radius, color)` (codex 01 F3, F4)

DOES NOT mirror `addVoxelBox` ([demo3d.cpp:2901](src/demo3d.cpp#L2901)) —
that helper's coord math assumes `gridOrigin = (0,0,0)` and
`voxelSize = 1/resolution`, which silently clips negative-coordinate
inputs. The actual SDF/radiance volume is at `volumeOrigin = (-2,-2,-2)`
with `volumeSize = (4,4,4)`, and sphere centers from
`currentObjBmin + ...` are routinely negative.

Instead, use the OBJ-style world→voxel formula (matches
`OBJLoader::worldToVoxel`):

```cpp
glm::ivec3 worldToVoxel(const glm::vec3& world) const {
    glm::vec3 norm = (world - volumeOrigin) / volumeSize;
    return glm::ivec3(norm * float(volumeResolution));
}
```

And **upload the whole sub-volume in ONE `glTexSubImage3D` call**
(codex 01 F4 — `addVoxelBox` issues per-voxel uploads, which is
thousands of GL calls per frame, not bytes-per-frame, that hurts):

```cpp
void Demo3D::addVoxelSphere(const glm::vec3& center, float radius,
                            const glm::vec3& color) {
    const float voxelSz = volumeSize.x / float(volumeResolution);
    glm::vec3 minPt = center - glm::vec3(radius);
    glm::vec3 maxPt = center + glm::vec3(radius);
    glm::ivec3 minV = worldToVoxel(minPt);
    glm::ivec3 maxV = worldToVoxel(maxPt);
    minV = glm::clamp(minV, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
    maxV = glm::clamp(maxV, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
    glm::ivec3 dim = maxV - minV + glm::ivec3(1);
    if (dim.x <= 0 || dim.y <= 0 || dim.z <= 0) return;

    // Build sub-volume on CPU; alpha=255 inside sphere, color=Kd.
    std::vector<uint8_t> sub(dim.x * dim.y * dim.z * 4, 0);
    const float r2 = radius * radius;
    for (int z = 0; z < dim.z; ++z)
    for (int y = 0; y < dim.y; ++y)
    for (int x = 0; x < dim.x; ++x) {
        glm::ivec3 v(minV.x + x, minV.y + y, minV.z + z);
        glm::vec3 wp = volumeOrigin + (glm::vec3(v) + 0.5f) *
                       (volumeSize / float(volumeResolution));
        glm::vec3 d  = wp - center;
        if (glm::dot(d, d) <= r2) {
            int i = ((z * dim.y + y) * dim.x + x) * 4;
            sub[i+0] = uint8_t(color.r * 255.0f);
            sub[i+1] = uint8_t(color.g * 255.0f);
            sub[i+2] = uint8_t(color.b * 255.0f);
            sub[i+3] = 255;
        }
    }

    // ONE upload (codex 01 F4).
    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, minV.x, minV.y, minV.z,
                    dim.x, dim.y, dim.z, GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}
```

For the demo sphere at radius = 0.08·size.y ≈ 0.13m in a 4m volume:
- voxel size = 4/128 = 0.03125m
- sphere bbox = ~9 voxels each axis ≈ 9³ = 729 voxels = 2.9 KB upload
- ONE GL call

`addVoxelSphere` writes **solid** sphere voxels (alpha=255 inside),
not just surface. This matches what `OBJLoader::voxelize` produces for
mesh surfaces — JFA treats every alpha>0 voxel as a seed regardless.
The sphere enters JFA on equal footing with OBJ surface voxels.

#### 2c. Per-frame dynamic update (codex 01 F2 cascade/temporal contract)

Add to `Demo3D::update()`:

```cpp
if (dynamicSphereEnabled && useOBJMesh && useGPUSDF) {
    // 1. Restore static OBJ voxels (fast GPU copy, ~1ms at 128³ RGBA8).
    glCopyImageSubData(meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0,0,0,
                       voxelGridTexture,    GL_TEXTURE_3D, 0, 0,0,0,
                       volumeResolution, volumeResolution, volumeResolution);

    // 2. Animate sphere (orbit around bounds center). sphereTime is fed by
    //    GetFrameTime() in normal play; --sphere-time=X CLI overrides it
    //    for deterministic capture (codex 01 F10).
    if (sphereTimeOverride < 0.0f) sphereTime += GetFrameTime() * sphereOrbitSpeed;
    else                            sphereTime = sphereTimeOverride;
    glm::vec3 center = (currentObjBmin + currentObjBmax) * 0.5f;
    glm::vec3 size   = currentObjBmax - currentObjBmin;
    float orbitR = std::min(size.x, size.z) * 0.3f;
    dynamicSphereCenter = center + glm::vec3(
        orbitR * std::cos(sphereTime),
        size.y * 0.2f,
        orbitR * std::sin(sphereTime)
    );

    // 3. Inject sphere via correct world->voxel math + ONE upload.
    addVoxelSphere(dynamicSphereCenter, /*radius=*/ size.y * 0.08f,
                   /*color=*/ glm::vec3(1.0f, 0.4f, 0.1f));   // warm orange

    // 4. codex 01 F1+F2: invalidate the SAME flags any other trigger uses.
    //    Sphere moved -> SDF stale, all cascades stale, EMA history stale.
    meshSDFReady        = false;   // -> sdfGenerationPass() re-bakes via GPU JFA
    cascadeReady        = false;   // -> updateRadianceCascades() runs
    forceCascadeRebuild = true;    // -> ALL cascades, skip stagger
    historyNeedsSeed    = true;    // -> alpha=1.0 this frame, no EMA ghost trail
}
```

**Triple gate** (`dynamicSphereEnabled && useOBJMesh && useGPUSDF`)
prevents accidentally turning on per-frame re-bake on the CPU path
(would drop to ~4 fps). The ImGui checkbox for `dynamicSphereEnabled`
is greyed via `ImGui::BeginDisabled(!useGPUSDF || !useOBJMesh)`.

**Cost contract.** Dynamic mode pays:
- 1× `glCopyImageSubData` (~1 ms)
- 1× `addVoxelSphere` (sub-µs CPU + 1 sub-volume upload, < 0.1 ms)
- 1× GPU JFA re-bake (target < 5 ms)
- 4× cascade rebuild (vs the staggered ~1× steady state) — could be
  the dominant cost; budget check during verification

Total budget target: **< 15 ms/frame** (66 fps) for Cornell-Original
with sphere on. If we miss, document it; we don't auto-tune in
Step 8.

#### 2d. ImGui controls

Adjacent to the GPU SDF checkbox:

```cpp
ImGui::BeginDisabled(!useGPUSDF || !useOBJMesh);
ImGui::Checkbox("Dynamic sphere overlay", &dynamicSphereEnabled);
if (dynamicSphereEnabled) {
    ImGui::SliderFloat("Sphere orbit speed", &sphereOrbitSpeed, 0.1f, 5.0f);
}
ImGui::EndDisabled();
```

---

### Phase 3 — Verification (codex 01 F5, F8, F10)

1. **Build clean.** `cmake --build . --config Release --clean-first` →
   0 errors. Warning count expected ≤ 39 (38 baseline + maybe 1 new
   line-shift C4819 like Step 7).

2. **Static A/B with measured tolerances (codex 01 F5).** Load
   Cornell-Original with dynamic sphere OFF; capture mode 0 with GPU
   toggle ON, then with it OFF. Standard JFA is approximate, the GPU
   nearest-seed albedo is structurally different from CPU 6-neighbor
   flood, so pixel-identical is the wrong bar. Acceptance:

   | Metric | Threshold | Notes |
   |---|---|---|
   | SDF max voxel-error vs CPU | ≤ 1 voxel (≈ 0.031 m at 128³) | Worst-case JFA-vs-EDT corner-case |
   | SDF mean voxel-error | ≤ 0.25 voxel | Average should be sub-half-voxel |
   | Hit-mask Jaccard (raymarch surface set) | ≥ 0.99 | Same surfaces hit |
   | Image SSIM | ≥ 0.9 | Visual closeness, not byte-identity |
   | Image max pixel ΔRGB | ≤ 32/255 | Local artifacts allowed; no hot-spots |

   A small Python script in `tools/sdf_diff.py` (new) does the SDF
   readback (via `glGetTexImage` save in C++) and computes the four
   numeric metrics. SSIM via `scikit-image`. Captures:
   `tools/step8_cornell_orig_cpu.png`, `tools/step8_cornell_orig_gpu.png`,
   `tools/step8_cornell_orig_diff.json` (the metric report).

3. **GPU vs CPU timing (codex 01 F8 honest).** Both the
   GPU-execution time (via `GL_TIME_ELAPSED`) and CPU-submit time
   (via `std::chrono`) are logged distinctly. Target:
   - GPU JFA: < 5 ms at 128³
   - CPU EDT: ~67 ms (baseline)
   - CPU submit for GPU path: < 1 ms (just dispatch + barrier issuance)

4. **Dynamic sphere visual (codex 01 F10 deterministic).** Use the new
   `--sphere-time=X` CLI hook to capture at fixed times:
   ```
   --load-obj=cornell-orig --gpu-sdf --dynamic-sphere --sphere-time=0.0
                            --exit-frames=120 --screenshot=tools/step8_dynamic_t0.png
   --load-obj=cornell-orig --gpu-sdf --dynamic-sphere --sphere-time=1.5
                            --exit-frames=120 --screenshot=tools/step8_dynamic_t1.png
   ```
   Repeat for t=3.0, t=4.5. Each capture is reproducible regardless
   of host framerate. The orange sphere should visibly orbit through
   the room AND its lighting + the GI it casts on the walls/floor
   should be visibly different at each time slice.

5. **Reset-helper still works.** `--load-obj=cornell-orig --gpu-sdf
   --test-reset-helper` → expected behavior unchanged (R-key reset
   only touches camera/light, not SDF).

6. **Sponza scene with GPU.** `--load-obj=sponza-master --gpu-sdf
   --exit-frames=120 --screenshot=tools/step8_sponza_master_gpu.png`
   → confirm 147,593-seed Sponza bakes correctly. Same SDF
   error/SSIM tolerances as Cornell test #2.

7. **Frame budget for dynamic mode.** With dynamic sphere on:
   measure end-to-end frame time. Target: **< 15 ms** (66 fps).
   Budget breakdown logged: copy + sphere-inject + JFA + cascade-rebuild
   + raymarch.

8. **Logs preserved.** `tools/app_run_step8_*.log` for each verify
   variant. No new pre-existing-shader noise (sdf_3d.comp now
   compiles clean — replaces what was codex 12 F4 / 13 F6 noise).

---

### Phase 4 — Resource lifecycle checklist (codex 01 F9)

For each of the 3 new textures (`voronoiTextureA`, `voronoiTextureB`,
`meshVoxelBaseTexture`):

- **Allocation** in `Demo3D::createVolumeBuffers()` next to existing
  textures via `gl::createTexture3D` helper
  ([demo3d.cpp:2334](src/demo3d.cpp#L2334) pattern). Voronoi textures
  use RGBA32F format; base voxel uses RGBA8 (matches `voxelGridTexture`).
- **Failure handling.** `gl::createTexture3D` returns 0 on failure;
  log + return false from `createVolumeBuffers()`; demo fails to
  start cleanly rather than running with null textures.
- **RenderDoc labels** via `glObjectLabel(GL_TEXTURE, tex, -1, "name")`
  next to existing label calls ([demo3d.cpp:2365](src/demo3d.cpp#L2365)).
- **Cleanup** in `Demo3D::~Demo3D()` / `cleanupVolumeBuffers()` next
  to existing `glDeleteTextures(1, &voxelGridTexture)`
  ([demo3d.cpp:2403](src/demo3d.cpp#L2403)).
- **Mode-leave clear.** When `useOBJMesh` flips off (e.g. user clicks
  Empty Room / Cornell Box analytic): clear `meshVoxelBaseTexture`
  to all-zero (avoids stale OBJ voxels lingering if user toggles
  back). The dynamic sphere update path is gated on `useOBJMesh` so
  no per-frame writes happen in analytic scenes anyway.
- **Resolution change.** Out of scope (resolution is fixed at 128 in
  this build); but Voronoi texture allocation should query
  `volumeResolution` so a future configurable build doesn't hardcode
  128.

---

### Phase 5 — CLI test hooks (codex 01 F10)

Three new `main3d.cpp` flags so verification scripts don't need to
flip ImGui state:

| Flag | Sets |
|---|---|
| `--gpu-sdf` | `useGPUSDF = true` (default OFF) |
| `--dynamic-sphere` | `dynamicSphereEnabled = true` |
| `--sphere-time=X` | `sphereTimeOverride = X` (float; -1.0 means "use real time") |

Both read the same paths the ImGui checkboxes do; no separate code
path. `sphereTimeOverride` is the new member referenced in
`update()` (Phase 2c).

---

## Files to modify

- [res/shaders/sdf_3d.comp](res/shaders/sdf_3d.comp) — full rewrite
  (init + step + finalize via `uPass`); image bindings via
  `layout(binding=N)` (codex 01 F7).
- [src/demo3d.h](src/demo3d.h) — promote `sdfReady`, `cascadeReady`
  from render-local statics to members (codex 01 F1). New members:
  `useGPUSDF`, `voronoiTextureA/B`, `meshVoxelBaseTexture`,
  `dynamicSphereEnabled`, `dynamicSphereCenter`, `sphereTime`,
  `sphereTimeOverride` (codex 01 F10), `sphereOrbitSpeed`. New method
  declarations: `generateMeshSDFGPU()`, `addVoxelSphere(center,
  radius, color)`, `worldToVoxel(world)` helper (or static-inline).
- [src/demo3d.cpp](src/demo3d.cpp):
  - Re-add `loadShader("sdf_3d.comp")` in init + reload paths
    (reverts part of the just-landed cleanup).
  - Allocate the 3 new textures in `createVolumeBuffers()` with
    failure handling + RenderDoc labels (codex 01 F9).
  - `loadOBJMesh()` commit block: upload `meshVoxelBaseTexture` once.
  - `setScene(...)` (or wherever `useOBJMesh = false` flips): clear
    `meshVoxelBaseTexture` to zero.
  - New `Demo3D::generateMeshSDFGPU()` with `GL_TIME_ELAPSED` query
    (codex 01 F8) and texture-fetch barrier (codex 01 F6).
  - New `Demo3D::addVoxelSphere()` with correct world→voxel math
    (codex 01 F3) and one batched upload (codex 01 F4).
  - `sdfGenerationPass()`: branch on `useGPUSDF`.
  - `render()`: replace `static bool sdfReady/cascadeReady` with
    member access; gate condition becomes
    `if (!sdfReady || (useOBJMesh && !meshSDFReady))` (codex 01 F1).
  - `update()`: dynamic sphere overlay path with cascade/temporal
    invalidation (codex 01 F2).
  - ImGui: 2 new checkboxes, 1 slider; toggle handler invalidates
    `meshSDFReady + cascadeReady` (codex 01 F1).
  - Destructor: delete the 3 new textures (codex 01 F9).
- [src/main3d.cpp](src/main3d.cpp) — 3 new CLI flags
  (codex 01 F10): `--gpu-sdf`, `--dynamic-sphere`, `--sphere-time=X`.
- [tools/sdf_diff.py](tools/sdf_diff.py) (new) — Python script
  computing the codex 01 F5 numeric tolerances (SDF max/mean error,
  hit-mask Jaccard, image SSIM, max ΔRGB) for CPU-vs-GPU A/B
  comparison.

No changes to the rest of the pipeline (cascade compute kernel,
raymarch, temporal blend, blur). `raymarch.frag`'s thresholds are
unchanged because the conservative-band match makes GPU output
drop-in compatible.

---

## Reuse from existing code

- `Demo3D::generateMeshSDF()` ([demo3d.cpp:1420](src/demo3d.cpp#L1420)) —
  matched bool-returning signature; conservative-band subtraction
  formula; failure-path printing pattern.
- `addVoxelBox()` ([demo3d.cpp:2901](src/demo3d.cpp#L2901)) — direct
  template for `addVoxelSphere()` (CPU rasterization → glTexSubImage3D).
- `currentObjBmin`/`currentObjBmax` (Step 7) — sphere orbit center +
  radius derived from these.
- `sdfGenerationPass()` mesh-branch failure-honoring pattern
  (codex 07 F1) — preserved for the GPU path.
- `glPushDebugGroup`/`glPopDebugGroup` Phase 6b convention — wrap each
  GPU JFA pass.
- `--test-reset-helper` CLI (codex 11/12) — reused as-is to verify
  reset path still works under GPU SDF.

---

## Out of scope (deferred to Step 9+)

- GPU triangle voxelizer (`voxelize.comp`). The CPU triangle voxelizer
  is the static-OBJ baseline; the sphere demo uses CPU
  `addVoxelSphere`. Step 9 fixes `voxelize.comp` so OBJ meshes can
  also be re-voxelized per frame (skinned characters, deforming
  meshes).
- Optimized JFA variants (1+JFA, JFA√, banded). 7-pass standard JFA
  is fast enough at 128³.
- Multiple dynamic objects. Adding a second moving primitive is
  trivial once the path works (just call `addVoxelSphere` twice in
  the update); not worth scoping into Step 8.
- Higher SDF resolution (256³). 128³ matches the rest of the cascade
  pipeline; raising it is a separate decision.

---

## Verification deliverables

- Logs: `tools/app_run_step8_{static_cpu,static_gpu,sponza_gpu,dynamic_t{0,1,2,3},F1_resethelper}.log`
- Captures: `tools/step8_cornell_orig_{cpu,gpu}.png`,
  `tools/step8_dynamic_t{0,1,2,3}.png`,
  `tools/step8_sponza_master_gpu.png`.
- Numeric A/B reports (codex 01 F5):
  `tools/step8_cornell_orig_diff.json`,
  `tools/step8_sponza_master_diff.json`. Each contains the 5 metrics
  (SDF max/mean voxel-error, hit-mask Jaccard, image SSIM, image max
  ΔRGB) plus a pass/fail per metric vs the documented threshold.
- Frame-budget log line per dynamic frame:
  `[Demo3D] Step 8 dynamic frame: copy=Xms inject=Xms jfa-gpu=Xms
  jfa-cpu-submit=Xms cascades=Xms total=Xms`.
