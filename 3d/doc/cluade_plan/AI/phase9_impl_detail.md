# Phase 9 — Implementation Detail

**Date:** 2026-05-02

Implements B+C from `phase9_improvement_plan.md`:
- **B**: temporal probe accumulation (EMA blend of fresh bake into history buffers)
- **C**: stochastic probe jitter (random sub-cell offset each rebuild cycle)

B+C together reduce spatial aliasing banding by integrating probe samples across
multiple world positions via the running average. B alone has no banding benefit.

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/temporal_blend.comp` | **New**: EMA blend compute shader |
| `res/shaders/radiance_3d.comp` | `uProbeJitter` uniform + `probeToWorld()` offset |
| `src/demo3d.h` | `probeAtlasHistory`, `probeGridHistory` per cascade; 4 new Demo3D fields |
| `src/demo3d.cpp` | Constructor, destroy, initCascades, updateRadianceCascades, updateSingleCascade, raymarchPass, renderUI (existing settings panel — new controls only, no new debug mode or window) |

---

## 1. temporal_blend.comp (new shader)

```glsl
#version 430 core
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(rgba16f, binding = 0) uniform image3D oHistory;  // R+W: running average
layout(rgba16f, binding = 1) uniform image3D uCurrent;  // R:   fresh bake output

uniform float uAlpha;   // blend weight for current frame (0=keep history, 1=replace)
uniform ivec3 uSize;    // texture dimensions for bounds check

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, uSize))) return;
    vec4 cur = imageLoad(uCurrent, coord);
    vec4 his = imageLoad(oHistory, coord);
    imageStore(oHistory, coord, mix(his, cur, uAlpha));
}
```

Called twice per cascade per rebuild: once for the directional atlas, once for the
isotropic probe grid. `binding=0` is always the history (READ_WRITE); `binding=1`
is always the fresh bake output (READ_ONLY).

---

## 2. radiance_3d.comp — probe jitter

**New uniform** (added to uniforms section):
```glsl
uniform vec3 uProbeJitter;  // Phase 9: sub-cell jitter in [-0.5,0.5]^3 probe-cell units
```

**Updated `probeToWorld()`:**
```glsl
vec3 probeToWorld(ivec3 probePos) {
    // Phase 5d: use uProbeCellSize (per-cascade) not uBaseInterval (always C0's 0.125)
    // Phase 9: uProbeJitter offsets the sample position within [-0.5,0.5] of the cell.
    // With temporal accumulation, each rebuild samples at a different world position,
    // widening the effective spatial footprint without adding probes.
    return uGridOrigin + (vec3(probePos) + 0.5 + uProbeJitter) * uProbeCellSize;
}
```

The jitter is in probe-cell-unit space (not world space), so it scales correctly
across all cascade levels (C1 has 2× larger cells than C0 but same ±0.5 cell
fraction). One jitter vector is generated per rebuild cycle and broadcast to all
cascades.

---

## 3. demo3d.h additions

**In `RadianceCascade3D` struct:**
```cpp
GLuint probeGridHistory;   // Phase 9: temporal history for isotropic grid
GLuint probeAtlasHistory;  // Phase 9: temporal history for directional atlas
```
Initialized to `0` in constructor, destroyed in `destroy()`.

**In `Demo3D` private section:**
```cpp
bool       useTemporalAccum;    // B: enable EMA blend of history buffers
float      temporalAlpha;       // B: blend weight (0=keep, 1=replace); default 0.1
bool       useProbeJitter;      // C: enable random sub-cell jitter each rebuild
glm::vec3  currentProbeJitter;  // C: current frame's jitter vector, updated each rebuild
```
Defaults: `useTemporalAccum=false`, `temporalAlpha=0.1f`, `useProbeJitter=false`,
`currentProbeJitter=vec3(0)`.

---

## 4. demo3d.cpp — key code sections

### 4a. initCascades() — history texture allocation

After `probeAtlasTexture` is created, allocate history with identical format and
GL_NEAREST sampling (same as atlas, to prevent bin bleed):
```cpp
// Phase 9: temporal history atlas — same layout as probeAtlasTexture (zero-initialized)
cascades[i].probeAtlasHistory = gl::createTexture3D(
    atlasXY, atlasXY, probeRes,
    GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
glBindTexture(GL_TEXTURE_3D, cascades[i].probeAtlasHistory);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
glBindTexture(GL_TEXTURE_3D, 0);

// Phase 9: temporal history for isotropic probe grid — same dims as probeGridTexture
cascades[i].probeGridHistory = gl::createTexture3D(
    probeRes, probeRes, probeRes,
    GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
```

History textures are zeroed at allocation. `initCascades()` is called on structural
changes (probe resolution change, cascade count change), which resets history —
correct behavior since the new grid layout is incompatible with old history.

### 4b. updateRadianceCascades() — jitter generation

One jitter vector per rebuild, broadcast to all cascades:
```cpp
void Demo3D::updateRadianceCascades() {
    // Phase 9: generate one jitter vector per rebuild cycle (same for all cascades).
    if (useProbeJitter) {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        currentProbeJitter = glm::vec3(dist(rng), dist(rng), dist(rng));
    } else {
        currentProbeJitter = glm::vec3(0.0f);
    }
    // ... cascade loop follows
}
```

Using the same jitter for all cascades ensures inter-cascade merge consistency:
C1 reads C0 probes that were baked at the same world-space offset, so the merge
direction lookups remain valid.

### 4c. updateSingleCascade() — jitter uniform + temporal blend dispatch

**Jitter uniform** (before bake dispatch):
```cpp
// Phase 9: probe jitter — offsets probe world positions by ±0.5 cell for temporal supersampling
glUniform3fv(glGetUniformLocation(prog, "uProbeJitter"), 1, glm::value_ptr(currentProbeJitter));
```

**Temporal blend dispatch** (after reduction pass):
```cpp
// Phase 9: temporal blend — mix fresh bake into history buffers.
// Atlas history: (res*D)x(res*D)x res
// Grid history:  res x res x res
auto tb = shaders.find("temporal_blend.comp");
if (useTemporalAccum && tb != shaders.end() &&
    c.probeAtlasHistory != 0 && c.probeGridHistory != 0) {
    GLuint tbProg = tb->second;
    glUseProgram(tbProg);
    glUniform1f(glGetUniformLocation(tbProg, "uAlpha"), temporalAlpha);

    // Blend directional atlas
    int D    = cascadeDirRes[cascadeIndex];
    int axyz = c.resolution * D;
    glUniform3i(glGetUniformLocation(tbProg, "uSize"), axyz, axyz, c.resolution);
    glBindImageTexture(0, c.probeAtlasHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(1, c.probeAtlasTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
    glm::ivec3 wgA = calculateWorkGroups(axyz, axyz, c.resolution, 4);
    glDispatchCompute(wgA.x, wgA.y, wgA.z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Blend isotropic grid
    glUniform3i(glGetUniformLocation(tbProg, "uSize"), c.resolution, c.resolution, c.resolution);
    glBindImageTexture(0, c.probeGridHistory, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(1, c.probeGridTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
    glDispatchCompute(wg.x, wg.y, wg.z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}
```

Dispatch order: **bake → reduction → temporal blend**. The blend reads the freshly
reduced `probeAtlasTexture` / `probeGridTexture` and writes into the history.
`GL_READ_WRITE` on binding=0 (history) enables simultaneous read (old EMA) and write
(new EMA) from within the same dispatch — correct because each invocation reads and
writes its own texel with no overlap.

### 4d. raymarchPass() — history texture binding

When temporal accumulation is active, the display pass reads from the history
(temporally averaged) rather than the single-frame bake:
```cpp
// Isotropic grid:
GLuint gridTex = (useTemporalAccum && cascades[selC].probeGridHistory != 0)
                 ? cascades[selC].probeGridHistory
                 : cascades[selC].probeGridTexture;
glBindTexture(GL_TEXTURE_3D, gridTex);

// Directional atlas:
GLuint atlasTex = (useTemporalAccum && cascades[selC].probeAtlasHistory != 0)
                  ? cascades[selC].probeAtlasHistory
                  : cascades[selC].probeAtlasTexture;
glBindTexture(GL_TEXTURE_3D, atlasTex);
```

### 4e. renderUI() — controls

Location: existing settings panel in `renderUI()`, after the "Dir resolution (D)"
slider, before "Interval Blend". This is **not** a new debug visualization mode —
the existing mode 0–7 range is unchanged and there is no history-atlas viewer.
Phase 9 adds only three standard panel controls:

```cpp
// ── Temporal accumulation + probe jitter (Phase 9) ──────────────────────
ImGui::Separator();
ImGui::Text("Temporal Accumulation (Phase 9):");
HelpMarker("B+C: temporal probe accumulation + stochastic jitter. ...");
ImGui::Checkbox("Temporal accumulation##phase9", &useTemporalAccum);
if (useTemporalAccum) {
    ImGui::SliderFloat("Temporal alpha", &temporalAlpha, 0.01f, 1.0f);
    // tooltip: 1.0=no accum, 0.1=~22 frames, recommended 0.05-0.1 with jitter
    ImGui::Checkbox("Probe jitter (requires temporal)", &useProbeJitter);
    // tooltip: jitter is sub-cell, only useful with accumulation
} else if (useProbeJitter) {
    useProbeJitter = false;  // auto-disable jitter when accum is off
}
```

Jitter is auto-disabled when the accumulation checkbox is toggled off — prevents
the user from accidentally running with jitter but no accumulation (adds noise,
fixes nothing).

**What Phase 9 did NOT add to the UI:**
- No new render mode beyond the existing 0–7 range
- No radiance debug sub-mode for temporal history inspection
- No separate visualization of `probeAtlasHistory` / `probeGridHistory`

The only way to observe that accumulation is working is to watch the rendered output
soften over ~30 rebuild cycles. There is no direct visual confirmation that history
buffers contain anything meaningful.

---

## 5. Memory layout

Per cascade (at C0 32³, D=4):
- `probeAtlasTexture`:  128×128×32 RGBA16F = 128×128×32×8 bytes ≈ 4.2 MB
- `probeAtlasHistory`:  same ≈ 4.2 MB  ← **new**
- `probeGridTexture`:   32×32×32 RGBA16F  ≈ 0.26 MB
- `probeGridHistory`:   same ≈ 0.26 MB   ← **new**

Total new VRAM per cascade: ~4.5 MB. With 4 cascades at varying resolutions
(32³/16³/8³/4³) total additional VRAM is dominated by C0 ≈ 4.5 MB.

---

## 6. Convergence estimate

With `alpha=0.1`, the weight of a sample from N frames ago is `(1-0.1)^N = 0.9^N`.

| Frames | Old sample weight |
|---|---|
| 10 | 35% |
| 22 | 10% |
| 44 | 1% |

At 30 fps continuous rebuild, history effectively integrates ~22 frames of probe
positions. With jitter ON, this samples ~22 distinct spatial offsets in the
[-0.5,0.5]³ cube around each nominal probe position, substantially widening the
effective probe footprint.

Expected result: concentric banding should soften noticeably after ~30 rebuild
cycles. The banding will not disappear completely — the EMA integrates a finite
set of offset samples and the GI gradient near the point light is steep — but
the high-frequency band pattern should become a lower-frequency, smoother gradient.

---

## 7. What does NOT reset history

History textures persist until `initCascades()` is called. Structural changes that
call `initCascades()` (and therefore reset and re-zero history):
- Cascade count change
- Probe resolution change (cascadeC0Res slider)
- Shader reload (F5)

Changes that do NOT reset history (history continues accumulating, which is correct):
- Light position change
- Blend fraction change
- Alpha / jitter toggle

**Limitations — history is not directly inspectable or controllable from the UI:**
- There is no dedicated "Reset temporal history" button. The workaround is to
  change and revert any structural parameter (e.g., flip cascadeC0Res and back),
  which triggers `initCascades()` and zeros both history textures. This is awkward
  and not visible in the UI.
- There is no visualization of what is currently accumulated in `probeAtlasHistory`
  or `probeGridHistory`. Verifying accumulation requires either a GPU debugger or
  observing output drift over rebuild cycles.

**Future improvement (not currently planned):** add a "Reset history" button that
calls a new `clearTemporalHistory()` function using `glClearTexImage()` to zero both
history textures per cascade in-place, without tearing down and reallocating the full
cascade stack. This would give the user a clean A/B reset path without structural
parameter changes.
