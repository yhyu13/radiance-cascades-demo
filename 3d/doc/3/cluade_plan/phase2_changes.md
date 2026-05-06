# Phase 2 Changes — Root-Cause Analysis & Implementation

**Date:** 2026-04-22  
**Branch:** 3d  
**Status:** Implemented; build succeeds; runtime verified (no crash, no spam)

---

## What Phase 2 Adds

A single 32³ radiance cascade that stores averaged Lambertian radiance at every probe.
The `raymarch.frag` fragment shader can sample this probe grid to add indirect lighting.
An ImGui "Cascade GI" checkbox in the Settings panel toggles the blend on/off.

---

## Bugs Found and Fixed

### Bug A — `sampleSDF()` in `radiance_3d.comp` used probe-space coords (CRITICAL)

**Old code:**
```glsl
float sampleSDF(vec3 worldPos) {
    ivec3 voxelPos = worldToProbe(worldPos);   // divides by cascade cell size, not SDF UV
    return texelFetch(uSDF, voxelPos, 0).r;
}
```
**Mechanism:** `worldToProbe()` divided by `baseInterval * 4^cascadeIndex`. For cascade 0 with baseInterval=0.2 (old default), this maps world unit 1.0 to probe voxel 5. The SDF texture is indexed 0..63 in UV space, not in probe space. Result: every SDF lookup returned garbage or zero → raymarchSDF() never detected surfaces → every probe stored zero radiance.

**Fix:**
```glsl
float sampleSDF(vec3 worldPos) {
    vec3 uvw = (worldPos - uGridOrigin) / uGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return INF;
    return texture(uSDF, uvw).r;
}
```

---

### Bug B — `raymarchSDF()` returned white `vec4(1,1,1,1)` on any hit (CRITICAL)

**Old code:**
```glsl
if (dist < 0.001) {
    return vec4(1.0, 1.0, 1.0, 1.0);  // white, no shading
}
```
**Mechanism:** All probe rays that hit anything stored pure white. Indirect contribution would be a flat white bias regardless of lighting or surface color.

**Fix:** Compute Lambertian shading at the hit point, using SDF gradient for normal estimation (eps=0.06, matching `raymarch.frag`):
```glsl
if (dist < 0.002) {
    const float e = 0.06;
    vec3 n = normalize(vec3(
        sampleSDF(pos+vec3(e,0,0)) - sampleSDF(pos-vec3(e,0,0)),
        sampleSDF(pos+vec3(0,e,0)) - sampleSDF(pos-vec3(0,e,0)),
        sampleSDF(pos+vec3(0,0,e)) - sampleSDF(pos-vec3(0,0,e))
    ));
    vec3 lightDir = normalize(uLightPos - pos);
    float diff = max(dot(n, lightDir), 0.0);
    return vec4(diff * uLightColor + vec3(0.02), 1.0);
}
```

---

### Bug C — `main()` used temporal reprojection + coarse cascade (unbound samplers)

**Old code:** `main()` read from `uPrevRadiance` and `uCoarseCascade` (both unbound → undefined).

**Fix:** Simplified `main()` — no temporal, no coarse cascade. Fixed interval = `length(uGridSize)` (full volume diagonal) so probes in the room center can reach walls:
```glsl
void main() {
    ivec3 probePos = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(probePos, uVolumeSize))) return;
    vec3 worldPos = probeToWorld(probePos);
    vec3 totalRadiance = vec3(0.0);
    float intervalEnd = length(uGridSize);  // full volume diagonal
    for (int i = 0; i < uRaysPerProbe; ++i) {
        vec4 hit = raymarchSDF(worldPos, getRayDirection(i), 0.05, intervalEnd);
        if (hit.a > 0.0) totalRadiance += hit.rgb;
    }
    totalRadiance /= float(uRaysPerProbe);
    imageStore(oRadiance, probePos, vec4(totalRadiance, 1.0));
}
```

---

### Bug D — `RadianceCascade3D::initialize()` was a stub — never created probe texture

**Old code:**
```cpp
void RadianceCascade3D::initialize(...) {
    // TODO: Implement cascade initialization
}
```
**Effect:** `probeGridTexture == 0` → `updateSingleCascade()` and `injectDirectLighting()` both returned early silently. The cascade was never populated.

**Fix:** Create 32³ `GL_RGBA16F` texture:
```cpp
void RadianceCascade3D::initialize(int res, float cellSz, const glm::vec3& org, int rays) {
    resolution = res; cellSize = cellSz; origin = org; raysPerProbe = rays; active = false;
    probeGridTexture = gl::createTexture3D(res, res, res, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
    if (probeGridTexture != 0) active = true;
}
```

---

### Bug E — `initCascades()` created 6 cascades (MAX_CASCADES) with sub-voxel cells

**Old code:** Looped `cascadeCount = MAX_CASCADES = 6`, called stub `initialize()` on each.  
**Effect:** No textures were created (stub). Cascade count was 6 but all inactive.  
**Fix:** Phase 2 uses exactly one cascade at 32³, `cellSize = volumeSize.x / 32 = 0.125`:
```cpp
void Demo3D::initCascades() {
    cascadeCount = 1;
    cascades[0].initialize(32, volumeSize.x / 32.0f, volumeOrigin, 4);
}
```
Also updated constructor: `cascadeCount(1)`, `baseInterval(0.125f)`.

---

### Bug F — `updateSingleCascade()` was a stub — never dispatched compute

**Old code:**
```cpp
void Demo3D::updateSingleCascade(int cascadeIndex) {
    if (...guards...) return;
    // TODO: Phase 2 - dispatch radiance_3d.comp compute shader
}
```
**Fix:** Full dispatch implemented — binds uniforms, SDF sampler, probe image, dispatches 4³ work groups:
```cpp
glUseProgram(prog);
// ... bind uniforms ...
glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_3D, sdfTexture);
glUniform1i(glGetUniformLocation(prog, "uSDF"), 0);
glBindImageTexture(0, c.probeGridTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
glm::ivec3 wg = calculateWorkGroups(c.resolution, c.resolution, c.resolution, 4);
glDispatchCompute(wg.x, wg.y, wg.z);
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
```

---

### Bug G — `injectDirectLighting()` ran every frame after cascade init, flooding console

**Mechanism:** After `initialize()` was fixed, `cascades[0].probeGridTexture != 0`, so `injectDirectLighting()` stopped returning early. It dispatched `inject_radiance.comp` every frame AND printed "Direct lighting injected with 3 lights" at 60 fps → console flood + GPU overload. Also overwrote probe texture before `updateSingleCascade()` could replace it (though overwrote correctly in sequence).

**Fix:** Return immediately — `inject_radiance.comp` is frozen for Phase 2; `updateSingleCascade()` handles all lighting:
```cpp
void Demo3D::injectDirectLighting() {
    // Phase 2: inject_radiance.comp frozen; updateSingleCascade() handles lighting.
    return;
}
```

---

### Bug H — Cascade dispatched every frame for static scene (performance)

**Old code:** `render()` called `updateRadianceCascades()` unconditionally every frame.  
**Effect:** 8³ work groups × 64 threads = 32K invocations per frame, running 60 fps for unchanged data.

**Fix:** Mirror the `sdfReady` pattern with `cascadeReady`. Cascade re-dispatches only when SDF changes:
```cpp
static bool cascadeReady = false;
if (!sdfReady) {
    sdfGenerationPass();
    sdfReady = true;
    cascadeReady = false;  // SDF changed → cascade stale
}
if (!cascadeReady) {
    updateRadianceCascades();
    cascadeReady = true;
}
```

---

## Files Changed

| File | Change |
|------|--------|
| `src/demo3d.cpp` | `initialize()`, `destroy()`, constructor, `initCascades()`, `updateSingleCascade()`, `render()`, `injectDirectLighting()`, `raymarchPass()`, `renderSettingsPanel()` |
| `src/demo3d.h` | Added `bool useCascadeGI;` |
| `res/shaders/radiance_3d.comp` | Full rewrite: sampleSDF UV, real shading, simplified main, light uniforms, MAX_STEPS guard |
| `res/shaders/raymarch.frag` | `uniform bool uUseCascade;`, indirect sampling block in hit path |

---

## Key Learnings

### Probe grid vs SDF grid are different coordinate systems
The probe grid (32³) and SDF grid (64³) both live in the same world-space volume `[-2, 2]³` but use different voxel sizes. SDF sampling must use UV coordinates `(worldPos - gridOrigin) / gridSize`, not probe-space voxel indices. These two grids must never be confused.

### Ray interval must span the room for center probes to see walls
With a 4-unit room and 32 probes, each probe is 0.125 world units apart. Probes near the center are ~1.9 units from any wall. Using `intervalEnd = length(uGridSize) ≈ 6.93` guarantees every probe can reach every wall regardless of position.

### OpenGL context version vs extension support
The GPU (RTX 2080 SUPER) reports OpenGL 3.3 via the Raylib/GLFW context. Compute shaders, image load/store, and GLSL 4.30 features all work because the NVIDIA driver exposes ARB_compute_shader and ARB_shader_image_load_store extensions. `#version 430 core` in GLSL compiles correctly on this driver.

### `imageLoad`/`imageStore` require different barrier bits than `texture()`
- After imageStore in compute: need `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` for subsequent imageLoad
- For `texture()` sampling in fragment/compute: need `GL_TEXTURE_FETCH_BARRIER_BIT`
- Both are needed when the compute output will be read as a sampler in the next pass

### Static scenes: cache GPU compute results
SDF and cascade data are constant when the scene doesn't change. Running them every frame wastes GPU time. The `sdfReady` / `cascadeReady` static-bool pattern defers expensive dispatches to only when data is stale.

---

## Phase 2 Stop Condition Check

| Condition | Status |
|-----------|--------|
| `probeGridTexture` non-zero after init | ✅ Confirmed (`active=1` logged) |
| `radiance_3d.comp` compiles | ✅ Confirmed |
| `raymarch.frag` compiles | ✅ Confirmed |
| No per-frame GL errors | ✅ Confirmed (runtime clean) |
| "Cascade GI" checkbox visible | ✅ Implemented in Settings panel |
| Toggle changes image visibly | ⬜ Needs smoke test with running window |
