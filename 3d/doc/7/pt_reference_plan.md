# Plan: SDF Path-Traced Reference (compute-shader, progressive) — rev 2

**Date:** 2026-05-18 (rev 1) → 2026-05-18 (rev 2, post critic-01)
**Goal:** Implement a simple Monte Carlo path tracer as a GLSL compute shader, marching the existing `uSDF` texture for intersection. Output is a progressively-accumulated RGBA32F image displayed as a new render mode. Serves as a **ground-truth reference** for measuring cascade-GI quality.
**Inspiration:** [shader_toy_pt/BufferA.glsl](../../shader_toy_pt/BufferA.glsl) — Sannikov-style sphere-traced PT with cosine sampling, NEE, Russian roulette.
**Critic chain extends:** [critic 01](critic/01_pt_reference_plan_review.md) → revision 2 (this version). 3 HIGH + 7 MEDIUM + 5 LOW. All applied.
**Status:** Plan only. No code changes in this doc.

---

## TL;DR (rev 2, post critic-01)

- New compute shader `pt_reference.comp` (~250 lines) ports the ShaderToy reference structure but **traces the existing SDF** instead of analytic spheres.
- **Tile-based + half-res dispatch from day one**: render PT at 540p (1/4 area), advance one 4×4-tile slice per frame → ~150 ms/frame instead of seconds. Full screen converges over 16 frames per spp. (Critic-01 didn't flag this but the cost analysis stands; kept as v1 requirement to preserve UI responsiveness.)
- Outputs to a new RGBA32F accumulator texture `ptAccumTexture` (half-window-sized). Display path bilinear-upsamples to screen.
- Progressively accumulates with `mix(prev, frameMean, k/(k+N))` (precision-stable form, per critic-01 W6).
- **TWO shading modes** (per critic-01 W2): default is **UNBIASED PT** (no ambient floor — true ground truth). Opt-in `--pt-cascade-match` mode adds the cascade's `uAmbientBakeStrength` at primary hit only, for "cascade convergence target" comparison.
- **No `1/r²` falloff** in direct evaluation — matches cascade renderer's infinite-reach shading.
- **v1 already does NEE for point lights** via shadow-ray-at-every-bounce (per critic-01 W1 — my "v2 adds NEE" framing in rev 1 was wrong; v1 already has NEE for zero-area lights). v2 adds **MIS** for nonzero-area (sphere) lights to avoid double-counting.
- **Soft-shadow + directional-light + light-intensity** match the cascade renderer's uniform-by-uniform setup (per critic-01 W3, also W5 viewport-from-raymarch.frag).
- **External validation against Blender Cycles reference** before claiming PT is correct. Cycles render of cornell-orig at 1000+ samples = independent truth.
- Displayed via new render mode 16 ("PT-Reference"). Cascade pipeline continues to run (cascade overhead is ~0.5% of total frame time when PT is tile-dispatched at 1080p; quantified per critic-01 W7).
- v1 scope: diffuse-only (Lambertian), point/directional light + sky.
- Convergence target: ~10,000 samples per pixel = ~1666 frames × 16 tiles × 1 spp = ~26,000 frames at 60 fps = **~7 minutes wall-clock**. Slower than rev 1's "30 sec" claim because tile-based serializes; trade-off worth it for interactive UI.
- **Split v1 into v1a (direct-only, 2 days) + v1b (indirect, 4-5 days)**. Total ~6-7 days realistic (was 3-4 in rev 1).

---

## 1. Why we need this

The project has shipped: Phase 2 (render-side α-gate), Phase 3 v3 (bake-side WeightedSample), mode 14/15 heatmaps. All of these are **relative** measurements — we can A/B between configurations but never against an external truth.

Quality work has stalled because there's no measurable target. From the recent status check:
> The lack of a quality roadmap reflects that we don't have an external reference — every quality judgment is comparative or visual. Without a ground-truth path-traced render to A/B against, "improve quality" has no defined target.

A PT reference fixes this:
- Renders the SAME SCENE the cascade renderer renders
- Uses the SAME SDF + albedo + light setup
- Differs only in the integration method (Monte Carlo per-pixel hemispherical sampling vs cascade probe lookup)
- After enough samples, converges to the physically correct answer (modulo SDF approximation of the geometry)

Then per-pixel comparison gives a concrete signal: "cascade GI is dim by N% in region R" or "cascade GI introduces banding in region R."

## 2. Scope (v1)

### In scope

- **Sphere-traced SDF intersection** via the existing `sampleSDF()` helper from `radiance_3d.comp`.
- **Diffuse Lambertian materials only**, albedo sampled from existing `uAlbedo` texture.
- **Single point/sky light source** matching the cascade renderer's setup (`uLightPos`, `uLightColor`, optionally `uUseEnvFill` + `uSkyColor`).
- **Cosine-weighted hemispherical sampling** for indirect bounces.
- **Russian roulette termination** at fixed survival probability (e.g., 0.9, matching ShaderToy reference).
- **Progressive accumulation** across frames, reset on camera/scene change.
- **Render mode 16** ("PT-Reference") displays the accumulator.
- **Sample-count HUD overlay** showing convergence progress (frame count / total samples per pixel).

### Out of scope (v1)

- **Next-event estimation (NEE)** for direct lighting (massive variance reduction, but requires sphere-light intersection setup; defer to v2).
- **Glossy/metallic materials** (no per-voxel material data; would need additional textures).
- **Multiple lights** (single light to match cascade setup).
- **Image-based lighting / environment maps** (use single sky color).
- **Bidirectional path tracing or photon mapping** (overkill).
- **Denoising** (defeats the "reference" purpose — denoising adds bias).
- **Texture filtering / mipmaps for albedo** (use existing `uAlbedo` with default filtering).
- **Output to disk as EXR** (PNG-screenshot via existing `--screenshot=` mechanism is sufficient).
- **Bake-PT-to-atlas** (would compete with cascade bake; not a reference anymore).

### Hard non-goals

- **NOT trying to be fast.** This is an offline-style reference. 10s to 30s convergence is acceptable.
- **NOT replacing the cascade renderer.** PT is opt-in, switchable via render mode.
- **NOT trying to match every cascade detail** (e.g., the cascade's smoothstep blend, EMA-α temporal). PT is the truth, cascade is the approximation. Differences are signal, not bugs.

## 3. Architecture

### Pipeline overview

```
Frame N:
  1. CPU: check camera/scene state → if changed, reset ptSampleCount = 0
  2. CPU: increment ptSampleCount += raysPerPixelPerFrame (e.g., 6)
  3. GPU: dispatch pt_reference.comp
     - input:  uSDF, uAlbedo, uLightPos, ..., uViewMatrix, uPrevAccum (read), uSpp
     - output: ptAccumTexture (write)
     - per pixel: cast N new primary rays, accumulate into running mean
  4. GPU: if uRenderMode == 16, display path reads ptAccumTexture instead of cascade output
```

### Data flow

- **ptAccumTexture**: RGBA32F, `screenWidth × screenHeight`. RGB = accumulated linear radiance, A = sample count (or always 1.0 — alpha unused).
- **ptSampleCount** (CPU int, mirrored to `uSpp` uniform): total rays-per-pixel accumulated since last reset.
- **ptDirty** (CPU bool): set when camera / scene / settings change. On next frame, reset accumulator + sample count.

### Files to touch

| File | Change |
|---|---|
| `res/shaders/pt_reference.comp` | NEW. The PT compute shader (~250 lines). |
| `res/shaders/raymarch.frag` | Add render mode 16 branch that reads `ptAccumTexture` and tone-maps. |
| `src/demo3d.h` | New members: `ptAccumTexture`, `ptSampleCount`, `ptRaysPerFrame`, `ptDirty`, `ptMaxBounces`. New setters for invalidation hooks. |
| `src/demo3d.cpp` | Load `pt_reference.comp`, allocate `ptAccumTexture` on first PT-mode entry / resize, dispatch logic, uniform binding, GUI hookup, invalidation triggers. |
| `src/main3d.cpp` | New CLI flags: `--render-mode=16` already works (just selects PT mode). Optional: `--pt-rays-per-frame=N`, `--pt-max-bounces=N`. |

No new dependencies; no third-party PT code. All math + RNG hand-rolled in GLSL.

## 4. Detailed shader design (`pt_reference.comp`)

### 4.1 Uniforms

```glsl
// Camera (per frame; reset accumulator on change)
uniform vec3  uCamPos;
uniform mat3  uCamBasis;       // columns = (right, up, -forward) for screen-to-world
uniform float uTanHalfFovY;
uniform vec2  uViewportSize;   // pixels

// Scene (reuse cascade-renderer setup)
uniform sampler3D uSDF;
uniform sampler3D uAlbedo;
uniform vec3  uGridOrigin;
uniform vec3  uGridSize;
uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform int   uUseEnvFill;     // 0 = honest miss, 1 = sky
uniform vec3  uSkyColor;

// PT controls
uniform int   uPtRaysPerFrame; // e.g., 6
uniform int   uPtMaxBounces;   // e.g., 8 (Russian roulette usually kills earlier)
uniform float uPtRussianRoulette; // 0.9 default
uniform uint  uFrameIndex;     // for RNG seeding

// Accumulation
layout(rgba32f, binding = 0) uniform image2D oAccum;    // read+write
uniform int   uSppBefore;      // total samples per pixel accumulated BEFORE this frame
```

### 4.2 Helper functions (mostly ported from ShaderToy reference)

```glsl
// RNG (no blue-noise; just hash + LCG)
uint hash(uint x);
float rand1(inout uint state);     // → [0,1)
vec2  rand2(inout uint state);
vec3  rand3(inout uint state);

// Sampling
void genTB(vec3 N, out vec3 T, out vec3 B);       // from Common.glsl line 45
vec3 cosineSample(vec3 N, vec2 r);                 // from Common.glsl line 56

// SDF intersection — adapted from radiance_3d.comp's raymarchSDF but stripped
// of per-hit lighting (PT shades it separately) and returning richer hit info.
struct Hit {
    bool  ok;          // false = miss
    bool  sky;         // true = exited volume + sky enabled
    vec3  pos;         // world-space hit position
    vec3  normal;      // outward normal at hit
    vec3  albedo;      // albedo sampled at hit
    float t;           // ray distance to hit
};
Hit traceSDF(vec3 origin, vec3 direction, float tMin, float tMax);

// Shading
bool isDirectlyLit(vec3 pos, vec3 lightDir, float lightDist);  // shadow ray
```

### 4.3 Main PT loop (one ray, one bounce-chain) — rev 2

**Two critical matches to cascade renderer** (per critic-01 H2/H3/M6):
- **NO `1/r²` falloff** in direct-light evaluation. The cascade renderer ([radiance_3d.comp:392-396](../../res/shaders/radiance_3d.comp#L392)) treats light as infinite-reach: `diff = max(dot(n, lightDir), 0) * (1 - shadow)`. We match.
- **Ambient floor ONLY at primary hit** (bounce==0), not every bounce. Cascade adds it once per bake; PT must too. Adding per-bounce gives `ambient/(1-albedo)` inflation (~3.3× at albedo=0.7).
- **Soft shadows match `uUseSoftShadowBake`**: when toggled, PT uses cone-trace soft shadow `softShadowTrace(...)`; otherwise binary `isDirectlyLit`. Same `uSoftShadowK`.
- **Directional light handling matches `uUseDirectionalLight`**: when toggled, `effectiveLightPos = volCenter - normalize(lightDir) * 100.0` (same as [demo3d.cpp:2319-2323](../../src/demo3d.cpp#L2319)).

```glsl
vec3 tracePath(vec3 origin, vec3 direction, inout uint rng) {
    vec3 accumulation = vec3(0.0);
    vec3 throughput   = vec3(1.0);

    // Effective light position (matches cascade's directional-light handling)
    vec3 effLightPos = (uUseDirectionalLight != 0)
        ? (uVolCenter - normalize(uLightDir) * 100.0)
        : uLightPos;
    // Cascade's effective light color = base × intensity (already pre-multiplied in uLightColor)

    for (int bounce = 0; bounce < uPtMaxBounces; ++bounce) {
        Hit h = traceSDF(origin, direction, 0.001, 1e4);

        if (!h.ok) {
            // Ray escaped. Add sky if enabled (matches cascade's uUseEnvFill behavior),
            // attenuated by throughput.
            if (uUseEnvFill != 0) accumulation += throughput * uSkyColor;
            break;
        }

        // Direct lighting: cast shadow ray toward light.
        // CRITIC-01 H2: NO 1/r² falloff — match cascade's infinite-reach shading.
        // CRITIC-01 M6: soft shadow when uUseSoftShadowBake; binary otherwise.
        vec3 toLight = effLightPos - h.pos;
        float lightDist = length(toLight);
        vec3 lightDir = toLight / max(lightDist, 1e-4);
        float cosTheta = max(0.0, dot(h.normal, lightDir));
        if (cosTheta > 0.0) {
            float shadowFact = (uUseSoftShadowBake != 0)
                ? softShadowTrace(h.pos, lightDir, lightDist, uSoftShadowK)
                : (isDirectlyLit(h.pos + h.normal * 0.002, lightDir, lightDist) ? 0.0 : 1.0);
            float diff = cosTheta * (1.0 - shadowFact);
            accumulation += throughput * h.albedo * uLightColor * diff;
        }

        // CRITIC-01 W2: PT defaults to UNBIASED (no ambient floor). The cascade
        // renderer's uAmbientBakeStrength is a bias (constant radiance added regardless
        // of light reach); including it in PT would mean PT-vs-cascade A/B measures
        // integration error relative to the biased baseline, not relative to physical
        // truth. To preserve PT's "ground truth" status, ambient is OFF by default.
        //
        // The opt-in `uPtCascadeMatch` mode adds ambient at primary hit ONLY (matches
        // cascade's per-surface ambient, not per-bounce — see W2 doc note on why
        // per-bounce would inflate by ~ambient/(1-albedo)). Use this mode when the
        // intent is "does cascade converge to its own biased target?"
        if (uPtCascadeMatch != 0 && bounce == 0) {
            accumulation += throughput * h.albedo * vec3(uAmbientBakeStrength);
        }

        // Russian roulette (matches ShaderToy reference; survival probability 0.9 default)
        float rr = rand1(rng);
        if (rr > uPtRussianRoulette) break;
        throughput *= (1.0 / uPtRussianRoulette);

        // Next ray: cosine-sampled hemispherical bounce.
        // Diffuse BRDF derivation:
        //   Render eq: L_o = ∫ L_i × (albedo/π) × cosθ dω
        //   MC estimator: L_o ≈ L_i × (albedo/π) × cosθ / p(ω)
        //   Cosine PDF: p(ω) = cosθ/π
        //   Substituting: L_o ≈ L_i × albedo  (cosθ and π both cancel)
        // Therefore: throughput *= albedo.
        vec2 r2 = rand2(rng);
        direction = cosineSample(h.normal, r2);
        origin    = h.pos + h.normal * 0.002;
        throughput *= h.albedo;
    }

    return accumulation;
}
```

### 4.3b Uniforms added to match cascade (rev 2)

```glsl
uniform vec3  uLightDir;           // for directional-light mode
uniform int   uUseDirectionalLight;
uniform vec3  uVolCenter;          // = uGridOrigin + uGridSize * 0.5
uniform int   uUseSoftShadowBake;
uniform float uSoftShadowK;
uniform float uAmbientBakeStrength;
uniform int   uPtCascadeMatch;     // W2: 0 = unbiased PT (default); 1 = include ambient at primary hit for cascade-target comparison
```

All sourced from existing `Demo3D` state; nothing new on the CPU side.

### 4.3c Self-intersection / shadow-ray offset consistency (per critic-01 W4)

The existing `radiance_3d.comp` uses three magic numbers inconsistently: `0.002` (surface threshold inside `raymarchSDF`), `0.05` (shadow-ray bias inside `inShadow`), `0.002` (post-bounce offset elsewhere). For PT we standardize on **two values** with explicit rationale:

```glsl
const float kSurfaceEps    = 0.002;   // sphere-trace surface-hit threshold
const float kShadowRayBias = 0.004;   // = 2 × kSurfaceEps; small safety margin to
                                       // escape the SDF gradient near a surface,
                                       // while staying tight enough that thin
                                       // geometry doesn't produce light leaks.
                                       // (Wider than kSurfaceEps because the normal
                                       // estimate has finite-difference error.)
```

Both used uniformly:
- `traceSDF`: `if (dist < kSurfaceEps)` for surface hit.
- `tracePath`: `origin = h.pos + h.normal * kShadowRayBias` for self-intersection avoidance.
- `isDirectlyLit`: `float t = kShadowRayBias` for shadow ray start offset.

Documented in a top-of-file comment block so the consistency is explicit.

### 4.3d SDF helper duplication note (per critic-01 W8)

`pt_reference.comp` duplicates `sampleSDF` (and its `INF` constant + `uGridOrigin`/`uGridSize` dependencies) from `radiance_3d.comp`. GLSL doesn't have cross-shader code sharing without preprocessor tooling we don't have set up.

**Maintenance contract** documented in `pt_reference.comp` header:
```glsl
// SDF helpers (sampleSDF, INF constant, kSurfaceEps stepping) are DUPLICATED from
// radiance_3d.comp. Any change to SDF intersection in radiance_3d.comp MUST be
// mirrored here. The PT reference would silently diverge from cascade output
// otherwise — and "silently diverge" is the worst failure mode for a reference
// renderer (looks right, is wrong). See critic-01 W8.
```

Same shape as critic 16 W1's lesson on `sampleProbeDir` / `sampleProbeDirWithLeak`: explicit duplication with a contract is preferable to GLSL `#include` machinery we don't have.

### 4.4 Entry point — main()

```glsl
layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pix, ivec2(uViewportSize)))) return;

    // Seed RNG from (pixel index, frame index)
    uint rng = hash(uint(pix.x) ^ (uint(pix.y) << 16) ^ (uFrameIndex * 2654435761u));

    vec3 frameSum = vec3(0.0);
    for (int s = 0; s < uPtRaysPerFrame; ++s) {
        // Jittered pixel sample (one ray per s; jitter ∈ [-0.5, 0.5))
        vec2 jitter = rand2(rng) - 0.5;
        vec2 ndc = (vec2(pix) + 0.5 + jitter) / uViewportSize * 2.0 - 1.0;
        ndc.x *= uViewportSize.x / uViewportSize.y;  // aspect

        // Ray from camera
        vec3 dir = normalize(uCamBasis * vec3(ndc.x * uTanHalfFovY,
                                              ndc.y * uTanHalfFovY,
                                              -1.0));
        frameSum += tracePath(uCamPos, dir, rng);
    }
    vec3 frameMean = frameSum / float(uPtRaysPerFrame);

    // Progressive accumulation: running mean over total samples
    vec4 prev = imageLoad(oAccum, pix);
    int totalBefore = uSppBefore;
    int totalAfter  = totalBefore + uPtRaysPerFrame;
    vec3 merged = (prev.rgb * float(totalBefore) + frameSum) / float(totalAfter);
    imageStore(oAccum, pix, vec4(merged, 1.0));
}
```

### 4.5 SDF intersection helper

Port `raymarchSDF` but strip the per-hit lighting and return richer hit info:

```glsl
Hit traceSDF(vec3 origin, vec3 direction, float tMin, float tMax) {
    Hit h;
    h.ok = false; h.sky = false;
    float t = tMin;
    const int MAX_STEPS = 128;
    for (int i = 0; i < MAX_STEPS && t < tMax; ++i) {
        vec3 pos = origin + direction * t;
        float dist = sampleSDF(pos);
        if (dist >= INF * 0.5) {
            h.sky = (uUseEnvFill != 0);
            return h;
        }
        if (dist < 0.002) {
            // Surface hit
            const float e = 0.03;
            h.normal = normalize(vec3(
                sampleSDF(pos + vec3(e,0,0)) - sampleSDF(pos - vec3(e,0,0)),
                sampleSDF(pos + vec3(0,e,0)) - sampleSDF(pos - vec3(0,e,0)),
                sampleSDF(pos + vec3(0,0,e)) - sampleSDF(pos - vec3(0,0,e))));
            vec3 uvw = (pos - uGridOrigin) / uGridSize;
            h.albedo = texture(uAlbedo, uvw).rgb;
            h.pos = pos;
            h.t = t;
            h.ok = true;
            return h;
        }
        t += max(dist * 0.9, 0.001);
    }
    return h;  // miss in volume
}
```

Identical sphere-trace math to existing `raymarchSDF`. The difference is the return convention: we return raw hit data and shade externally.

### 4.6 Shadow-ray helper

```glsl
bool isDirectlyLit(vec3 origin, vec3 lightDir, float lightDist) {
    float t = 0.02;
    for (int i = 0; i < 64 && t < lightDist; ++i) {
        float d = sampleSDF(origin + lightDir * t);
        if (d >= INF * 0.5) return true;     // exited volume — assume light is visible
        if (d < 0.002) return false;          // hit geometry first
        t += max(d * 0.9, 0.001);
    }
    return true;  // reached light's distance without hit
}
```

### 4.7 Why this exact structure

- **Cosine sampling** (not uniform): better convergence; PDF cancels with cosθ term in the rendering equation, giving simple `throughput *= albedo`.
- **Russian roulette**: prevents infinite recursion; matches ShaderToy reference at 0.9 default (max scene albedo).
- **Direct lighting via shadow ray at every bounce**: faster convergence than waiting for random hemisphere ray to hit the light by chance. Standard PT optimization called "Lambertian direct" or "implicit direct light evaluation."
- **Running-mean accumulation**: numerically stable for many samples; `(prev * N + new) / (N+1)` avoids overflow vs naive sum.
- **Self-intersection offset `0.002`**: matches the existing `raymarchSDF` surface threshold; small enough that geometry isn't displaced visibly but large enough to escape the surface SDF gradient.

## 5. C++ integration

### 5.1 New `Demo3D` members ([demo3d.h](../../src/demo3d.h))

```cpp
// PT reference (doc/7/pt_reference_plan.md)
GLuint   ptAccumTexture;     // RGBA32F, screenWidth × screenHeight; allocated on demand
int      ptAccumWidth, ptAccumHeight;  // current allocated size (for resize detection)
int      ptSampleCount;      // total rays-per-pixel accumulated since last reset
int      ptRaysPerFrame;     // default 6 — rays/pixel cast per dispatch
int      ptMaxBounces;       // default 8 — Russian roulette usually terminates earlier
float    ptRussianRoulette;  // default 0.9
bool     ptDirty;            // true → reset accumulator + sample count on next dispatch
uint32_t ptFrameIndex;       // RNG seed input
```

Setters for invalidation:
```cpp
void resetPTAccumulator() {
    ptDirty = true;
    ptSampleCount = 0;
}
```

### 5.1b Camera basis derivation (per critic-01 W3)

The PT shader's `uCamBasis` (mat3) + `uCamPos` + `uTanHalfFovY` must derive from the SAME camera state the cascade renderer uses. Raylib's `Camera3D` has `{position, target, up, fovy}` — no basis matrix. Derive on CPU before each PT dispatch:

```cpp
// Pull from the same Camera3D the cascade renderer reads.
const Camera3D& cam = getRaylibCamera();
glm::vec3 pos     = glm::vec3(cam.position.x, cam.position.y, cam.position.z);
glm::vec3 target  = glm::vec3(cam.target.x,   cam.target.y,   cam.target.z);
glm::vec3 worldUp = glm::vec3(cam.up.x,       cam.up.y,       cam.up.z);

glm::vec3 forward = glm::normalize(target - pos);
glm::vec3 right   = glm::normalize(glm::cross(forward, worldUp));
glm::vec3 up      = glm::cross(right, forward);  // re-orthogonalize

// Column-major mat3; PT shader does `uCamBasis * vec3(ndc.x, ndc.y, -1)`.
// First column = right (X-axis of camera), second = up, third = -forward.
glm::mat3 basis;
basis[0] = right;
basis[1] = up;
basis[2] = -forward;

float tanHalfFovY = std::tan(glm::radians(cam.fovy * 0.5f));

glUniform3fv(uCamPosLoc,    1, &pos[0]);
glUniformMatrix3fv(uCamBasisLoc, 1, GL_FALSE, &basis[0][0]);
glUniform1f(uTanHalfFovYLoc, tanHalfFovY);
```

**Cross-check**: render a debug pattern in mode 16 that shows world-space x/y/z color-coded. Compare with mode 1 (normals). Axes must match — if X is red in mode 1, X-facing surfaces should be red in the debug pattern. Inverted basis would visibly mismatch.

**Up-axis caveat**: raylib defaults to Y-up. Existing scene transforms (cornell-orig OBJ, Sponza) use Y-up. If we ever import a Z-up scene the worldUp logic above generalizes via `cam.up` correctly.

### 5.1c Viewport resolution sourcing (per critic-01 W5)

PT renders at HALF resolution by default. The half-res buffer must match `raymarch.frag`'s actual viewport, NOT the OS window size (in case the GL viewport is smaller than the window due to ImGui docking, DPI, etc.).

```cpp
// Use the SAME source raymarch.frag uses for viewport size.
// raymarch.frag implicitly gets viewport from gl_FragCoord; CPU side it's set by
// glViewport() in the main render path. Mirror that source:
int viewportW, viewportH;
GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
viewportW = vp[2];
viewportH = vp[3];

int ptW = viewportW / 2;
int ptH = viewportH / 2;
// Allocate/resize ptAccumTexture at (ptW, ptH); display path bilinear-upsamples.
```

This guarantees the PT viewport matches the cascade renderer's. If a future feature changes viewport handling (e.g., render-target multiplexing), both PT and cascade share the same source of truth.

### 5.2 Dispatch logic ([demo3d.cpp](../../src/demo3d.cpp))

In the main render path, before `raymarch.frag` runs:

```cpp
if (raymarchRenderMode == 16 || /* always-on flag for debugging */) {
    // Allocate / resize accumulator on first use or screen resize
    if (ptAccumTexture == 0 ||
        ptAccumWidth != GetScreenWidth() || ptAccumHeight != GetScreenHeight()) {
        ensurePTAccumAllocated();
        ptDirty = true;
    }
    // Reset on camera/scene change (see invalidation triggers below)
    if (ptDirty) {
        glClearTexImage(ptAccumTexture, 0, GL_RGBA, GL_FLOAT, nullptr);
        ptSampleCount = 0;
        ptDirty = false;
    }
    // Bind + dispatch
    dispatchPTReference();
    ptSampleCount += ptRaysPerFrame;
    ptFrameIndex++;
}
```

### 5.3 Invalidation triggers

Reset accumulator (`ptDirty = true`) when ANY of:
- Camera position or target changes (any keyboard/mouse input that moves it)
- Scene reload / OBJ swap
- Light position, color, or intensity change
- SDF rebuild (e.g., scene parameter change)
- Ambient floor (`uAmbientBakeStrength`) change
- `--use-env-fill` toggle
- Sky color change
- Settings that would alter the trace result

Wrap the relevant setters with `resetPTAccumulator()` calls. Cleanest: introduce `markPTDirty()` and call it from each setter that affects scene appearance.

### 5.4 Display mode integration ([raymarch.frag](../../res/shaders/raymarch.frag))

Add render mode 16 branch:

```glsl
if (uRenderMode == 16) {
    vec3 rgb = texture(uPtAccum, gl_FragCoord.xy / vec2(uPtAccumSize)).rgb;
    // Match the existing display convention: tonemap then sRGB
    rgb = toneMapACES(rgb);
    fragColor = vec4(pow(rgb, vec3(1.0 / 2.2)), 1.0);
    return;
}
```

The cascade pipeline still runs (wasteful) — see "Performance follow-ups" below for the eventual bypass.

### 5.5 GUI

Add to render-mode picker:
```
"16 PT-Reference (path-traced ground truth)",
```

When mode 16 is selected, show an inline panel:
- Current sample count: `ptSampleCount` (e.g., "1,248 samples/pixel")
- Convergence indicator: progress bar to a target (default 10,000)
- Slider: rays per frame (1 — 64, default 6)
- Slider: max bounces (1 — 16, default 8)
- Slider: Russian roulette (0.5 — 0.99, default 0.9)
- Button: "Reset accumulator"
- Status text: "Camera locked. Move camera or change scene to reset."

## 6. Sequencing — v1a (direct, 2 days) + v1b (indirect, 4-5 days) (per critic-01 cross-cutting)

### v1a — Direct-only PT (ships as standalone milestone)

#### Day 1 — Shader skeleton + plumbing + tile dispatch

- Write `pt_reference.comp` core: uniforms, RNG, `traceSDF`, `isDirectlyLit`, single-bounce direct shading
- Allocate `ptAccumTexture` at half-resolution; tile dispatch (4×4 = 16 tiles, rotate one per frame)
- Add render mode 16 to `raymarch.frag` reading `uPtAccum` with bilinear upsample
- Camera basis derivation (per §5.1b)
- Verify: mode 16 shows direct shading; image converges over ~16 frames (one full tile cycle)

#### Day 2 — Invalidation + GUI + sanity tests + external validation

- Wire CPU sample count + dirty flag; invalidation hooks (debounced)
- GUI panel: sample counter, rays/frame/bounces/RR sliders (each invalidates on change), reset button, "estimated convergence time" text
- Sanity test 7.1.1-7.1.2: test pattern + direct shading matches cascade mode 4
- **External validation** (§7.2): set up Blender Cycles reference for cornell-orig; run acceptance test
- v1a ship gate: per-region RMSE < 0.05 vs Cycles reference

### v1b — Indirect bounces (after v1a ships)

#### Day 3 — Cosine sampling + bounces + Russian roulette

- Add `cosineSample` + extended RNG
- Implement full `tracePath` loop with bounces + RR
- Verify: single-frame render is noisier (expected; more variance per pixel); accumulated across many frames converges

#### Day 4 — Mode-gated ambient (W2) + indirect validation

- Implement `uPtCascadeMatch` toggle: default UNBIASED (no ambient); opt-in adds ambient at primary hit only
- Verify both modes converge cleanly
- Re-run external validation in BOTH modes against Cycles

#### Day 5 — Precision / convergence / polish

- Verify accumulator stability at 10k+ spp (per W6)
- Verify RNG quality at low/mid spp (per W9)
- GUI polish: convergence indicators, mode labels, helper text
- Document `tools/compare_pt_vs_cycles.py` (Python helper for §7.2 + §7.3)

#### Days 6-7 — Buffer / iteration

Real PT debugging is rarely first-time-right. Reserve 1-2 days for:
- SDF gradient artifacts (normal estimation issues)
- Self-intersection at sharp corners
- Convergence anomalies (some regions not converging)
- Tonemap / sRGB mismatch with mode 0

## 7. Validation

### 7.1 Sanity tests — internal (in order)

1. **Mode 16 shows test pattern** (Day 1 — just dispatch+display works).
2. **Mode 16 with single bounce shows direct shading** that matches cascade mode 4. RMSE < 0.05 vs mode 4 capture (in `uPtCascadeMatch=1` mode so ambient is matched).
3. **Mode 16 with bounces enabled converges over 10,000 samples**. Captures at 100 / 1000 / 10000 spp should show monotonic noise reduction (variance halves every ~4× spp).
4. **Camera move resets accumulator** — visible "reset" of noise, then re-convergence.
5. **`uPtCascadeMatch=1` PT agrees qualitatively with cascade mode 0** for diffuse-only scenes. Color bleeding, indirect bounces, soft shadows match in direction/intensity. ⚠️ This sanity test is necessary but NOT sufficient — see 7.2 external validation.

### 7.2 External validation — Blender Cycles reference (per critic-01 M3)

**This is load-bearing**: validating PT only against cascade output is circular (cascade has known issues; we built PT to measure them). PT must be cross-checked against an INDEPENDENT renderer before it's trusted as truth.

**Procedure** (one-time setup; repeated for each canonical scene):

1. **Export the SDF-baked geometry to a triangle mesh.** Easiest: use the OBJ source files directly (`res/scene/CornellBox-Original/CornellBox-Original.obj`). Blender Cycles will render the OBJ; the SDF is what our PT traces. Mesh-vs-SDF fidelity gap is documented separately (§10 risks).
2. **Match Blender's camera to our scene's camera.** Open `cornell-orig` in our app, write down camera position/target/fovy (visible in GUI's camera panel). In Blender: set the active camera to the same position/lookat/fovy. Cycles uses Y-up by default; matches our convention.
3. **Match lighting**: place a point light at the same world position as our `uLightPos`; intensity matches `uLightColor × uLightIntensity` (Blender uses watts; calibrate by visual brightness on a reference white wall, ~50 W = our default 1.0 intensity for cornell-scale).
4. **Disable ambient** in Blender Cycles World settings (`Strength = 0`) to match our unbiased PT (`uPtCascadeMatch=0`).
5. **Disable refraction / glass / metals** — diffuse-only to match v1 scope.
6. **Render at 1080p, 4096 samples per pixel, no denoiser.** ~30 minutes on a modern GPU.
7. **Save as `tools/cycles_reference_cornell_orig_1080p_4096spp.png`** (linear EXR also recommended for later precision work).

**Acceptance test**:

```
./RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=16 \
    --pt-rays-per-frame=8 --exit-frames=2000 --screenshot=tools/our_pt_ref.png

python tools/compare_pt_vs_cycles.py \
    tools/our_pt_ref.png tools/cycles_reference_cornell_orig_1080p_4096spp.png
```

**Pass criterion**: per-region RMSE < 0.05 (out of 1.0 normalized) across 5 viewpoint regions. Larger discrepancies indicate either:
- Shading model mismatch (cascade/PT does something Cycles doesn't or vice versa)
- SDF-vs-mesh fidelity gap (SDF quantizes at ~3 cm; OBJ mesh is exact triangles)
- Color/intensity calibration drift between the two setups

Any failure here is a **blocker** for treating PT as truth. Fix until pass.

**Then and only then** can step 5 of §7.1 ("PT agrees with cascade") be reframed as a quality measurement: "PT — Cycles agreement confirms PT is correct; cascade — PT disagreement quantifies cascade-integration error."

### 7.3 Quantitative comparison — cascade vs PT (once PT is trusted)

Once PT passes 7.2:

```
# Save unbiased PT reference (the truth)
./RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=16 \
    --pt-rays-per-frame=8 --exit-frames=2000 --screenshot=pt_ref_unbiased.png

# Save cascade-target PT (cascade's converged target with ambient floor)
./RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=16 \
    --pt-cascade-match=1 --pt-rays-per-frame=8 --exit-frames=2000 \
    --screenshot=pt_ref_cascade_match.png

# Save cascade output
./RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=0 \
    --exit-frames=300 --screenshot=cascade.png

# Diff cascade against BOTH references — separates integration error from bias
python tools/compare_pt_vs_cascade.py cascade.png \
    --reference-unbiased pt_ref_unbiased.png \
    --reference-cascade-match pt_ref_cascade_match.png
```

Metrics to report:
- **Cascade vs cascade-match PT** = integration error (does cascade converge to its own target?)
- **Cascade vs unbiased PT** = integration error + ambient bias (combined effect)
- **Cascade-match PT vs unbiased PT** = pure ambient bias (quantifies the floor's contribution)
- Per-region RMSE (split frame into 3-5 regions of interest)
- Per-channel mean brightness ratio
- SSIM
- Visual side-by-side + diff heatmaps

This becomes the **quality baseline** for any future work.

## 8. Performance — cost analysis + follow-ups (per critic-01 H1 + W7)

### v1 cost (estimated)

Per-ray cost:
- Primary `traceSDF`: ~128 SDF lookups (worst case; surface hit usually < 50)
- Per bounce: ~128 (next-bounce trace) + ~64 (shadow ray) = ~192 lookups
- With Russian roulette at 0.9, effective ~5 bounces (truncated by `uPtMaxBounces=8`)
- Per-ray total: 128 + 5×192 ≈ **~1,088 SDF lookups per ray**

At 1080p × 6 spp = 12.4M rays × 1,088 = **~13.5B SDF lookups per frame** — that's the per-frame full-screen cost.

Modern GPU 3D-texture rate ~5-10 GTexels/s → full-screen would be **~1.3-2.7 seconds per frame** if dispatched in one shot. UI freezes; user input lost mid-dispatch.

**v1 mitigation = tile-based + half-res**:
- Half-res cuts ray count 4× → ~3.1M rays/frame
- Tile-based (16 tiles, 1 tile/frame) cuts another 16× → ~195k rays/frame
- ~195k rays × 1,088 lookups = ~212M SDF lookups/frame at 1 spp/tile
- Wall-clock: ~30-60 ms/frame at 5 GTexels/s — interactive

Convergence time: 16 tiles × 10,000 spp = 160,000 dispatches at 60 fps = **~45 minutes wall-clock** at default settings. The plan's TL;DR "~7 minutes" was for 1,000-spp target; 10k-spp target is ~7× longer.

**Workflow consequence**: PT reference is "load scene, set up camera, walk away for an hour." Not interactive iteration. The user must understand this trade-off; surface it in the GUI ("Estimated time to converge: 45 minutes at 60 fps").

### Cascade-pipeline overhead while PT is selected (per critic-01 W7)

The cascade pipeline runs even when render mode is 16 (since `raymarch.frag` is the display path and the cascade bake feeds it). Quantified:
- Cascade bake at 1080p (Phase 5d, all 4 cascades, staggered): **~16.5 ms/frame** (from §perf doc)
- Raymarch.frag at 1080p (mode 16 branch — just reads `ptAccumTexture` + tonemap, much cheaper than mode 0): **~3-5 ms/frame** estimated
- GI blur skipped in mode 16 (gated on `uRenderMode == 0`): **0 ms**

Total cascade-side overhead while PT-mode active: ~20-22 ms/frame.

PT dispatch at v1 settings (half-res, 1 tile, 1 spp): ~30-60 ms/frame.

**Overhead ratio**: cascade overhead is ~25-40% of PT-side cost. Not 0.5% as I initially claimed in the TL;DR — that was wrong; the cost was for FULL-screen PT (10s/frame), not tile-dispatched. Corrected.

**Implication**: at v1 settings, full bypass of cascade pipeline would save ~20 ms/frame, dropping PT total frame time from ~80 ms → ~60 ms. Worthwhile but not urgent — defer to a follow-up commit. Acceptable for v1.

### v2+ follow-ups

The current plan keeps the cascade pipeline running even when PT mode is selected. At 1080p this wastes ~50 ms per frame (cascade bake + raymarch.frag). For v1 this is fine — PT is offline-style and that 50 ms is small compared to PT's own cost.

If PT becomes a regular workflow tool:
- Add `if (raymarchRenderMode == 16) skipCascadePipeline()` in `update()`.
- Skip `raymarch.frag` entirely; copy `ptAccumTexture` to backbuffer with a quad pass.
- Saves the cascade work + raymarch cost; PT itself remains the only cost.

Also: PT cost depends on `raysPerFrame × maxBounces × pixelCount`. At 1080p × 6 spp × 8 bounces × ~200 µs/ray, that's ~10 seconds per frame. Need to think about throttling — likely PT should dispatch in tiles per frame to keep the app responsive (e.g., 1/4 of the screen per frame, full screen converges over 4 frames).

## 9. Future extensions (not v1)

- **NEE (next-event estimation)**: sample area light directly each bounce, MIS-weight with hemispherical sample. Requires a sphere-or-disc light primitive in addition to the current point light. ~10× variance reduction.
- **Glossy/metallic materials**: per-voxel material texture (or per-mesh material ID baked into SDF channel). Add Fresnel + GGX importance sampling.
- **Spectral / wavelength sampling** (for caustics, dispersion): probably overkill.
- **Tile-based dispatch with priority** (more samples on visibly-noisy tiles).
- **Image-based environment lighting** (HDR sky maps) — adds asset dependency.
- **Multi-light NEE** (single light limit only for v1's simplicity).

## 10. Risks + open questions

### Risks

- **SDF-vs-mesh fidelity**: PT result is "ground truth FOR THE SDF GEOMETRY," not for the original OBJ mesh. The SDF quantizes geometry at 128³ resolution (~3 cm per voxel at 4-unit volume). Differences vs a triangle-PT reference would be from SDF approximation, not from PT vs cascade. **Acceptable** because the cascade renderer also uses the SDF, so we're A/B'ing the integration method, not the geometry representation.
- **Normal estimation noise**: `sampleSDF`-based normals can be noisy at low SDF gradients. The cascade renderer has the same limitation, so this is also a fair A/B.
- **Shadow ray bias**: `0.02` start distance can cause self-shadowing on small features. Tune empirically.
- **RNG quality**: simple LCG/hash may show patterns at low spp. Acceptable for a reference (use enough samples). Future: import the same blue-noise texture ShaderToy uses.
- **Convergence time**: 10s-30s at 60fps; user must be patient. Mitigation: clearer GUI progress indicator + lower default sample target.

### Open questions (rev 2)

1. **Should PT use the temporal-α-blended atlas at all?** Definitively no — PT must not depend on cascade state. Confirmed.
2. **Should ambient floor (`uAmbientBakeStrength`) be added to PT?** **Revised per critic-01 W2: NO by default** (unbiased PT = ground truth). Opt-in `--pt-cascade-match` / `uPtCascadeMatch` mode adds ambient at primary hit only for "cascade convergence target" comparison. Two modes serve two questions: (1) does cascade converge? (use cascade-match), (2) is the rendering physically correct? (use unbiased).
3. **Output linear or sRGB?** Store linear in `ptAccumTexture`. Tonemap + sRGB on display only, matching the existing display pipeline.
4. **Reset on probe-jitter / temporal toggle?** No — PT doesn't use these. They only affect cascade output.

### Precision considerations (per critic-01 W6)

At 10k spp the running-mean update `mix(prev, frameMean, k/(k+N))` keeps both operands in [0, max-radiance] range (no growing accumulator). RGBA32F gives ~7 decimal digits; per-frame contribution at N=10k is ~0.01% of the running mean, well above the precision floor for visual reference work.

**v2 precision note**: if PT output is later used for per-pixel pixel-exact RMSE comparison at <0.001 precision, consider storing running sum + sample count in separate channels (or a separate uint texture) and computing the mean only at display time. Float32 accumulation drift compounds slowly but measurably at 100k+ spp.

### RNG correlation note (per critic-01 W9)

The hash-LCG seed `hash(pix.x ^ (pix.y << 16) ^ (uFrameIndex * 2654435761u))` is standard but at low spp (< ~100 per pixel) may show correlation patterns between adjacent pixels. **Visually expected at low spp; vanishes by ~1k spp.** If pattern visibility becomes annoying for interactive preview, options are:
- Import the same blue-noise texture ShaderToy uses (~16 KB asset, sampled via wraparound)
- Pre-randomize the seed via a higher-quality hash (PCG-3D, xxhash)
- Owen-scrambled Sobol sequence per-pixel

None are blockers for v1; documented as v2 polish.

## 11. Success criteria (rev 2)

### v1a ship gate (direct-only)

- ☐ Render mode 16 toggles cleanly between PT and cascade output
- ☐ Tile-based dispatch keeps frame time < 80 ms at 1080p
- ☐ Mode 16 (direct-only) shows correct Lambertian shading + shadows
- ☐ Camera move resets accumulator (debounced; doesn't fire on every mouse-move frame)
- ☐ Sample-count HUD shows convergence progress with estimated wall-clock time
- ☐ **External validation passes**: per-region RMSE < 0.05 vs Blender Cycles reference for cornell-orig at 4096 spp (direct-only)
- ☐ Bit-exact verification: mode 0/14/15 unaffected when mode 16 is not selected

### v1b ship gate (indirect)

- ☐ All v1a criteria still hold
- ☐ Mode 16 with bounces enabled converges over 10,000 samples (monotonic noise reduction)
- ☐ `uPtCascadeMatch` toggle works: default mode = unbiased PT (no ambient); opt-in = adds ambient at primary hit
- ☐ External validation passes in unbiased mode against Cycles
- ☐ Three-way A/B tooling lands (per §7.3): cascade vs cascade-match-PT vs unbiased-PT, separates integration error from bias
- ☐ GUI sliders all invalidate accumulator on change
- ☐ Accumulator precision documented; no measurable drift at 10k spp

### v2 (later, not blocking v1)

- ☐ MIS (multiple importance sampling) for nonzero-area lights — replaces shadow-ray NEE
- ☐ Sphere light primitive
- ☐ Blue-noise RNG (lower correlation at <100 spp)
- ☐ Optional float64 / split-channel accumulator for sub-0.001 precision
- ☐ Cascade-pipeline bypass when mode 16 is active (saves ~20 ms/frame)

## 12. What this unlocks downstream

Once landed, future work can quantify quality with concrete numbers:
- "Phase 3 v3 reduces RMSE-to-PT by X% on cornell-orig-alcove" (replaces the suspect bake-leak metric)
- "Cascade GI is dim by Y% in Sponza atrium" (replaces "looks too dark to me")
- "Per-bin visibility (C4 follow-up) reduces RMSE-to-PT by Z%" (concrete go/no-go gate)
- "Default-OFF Phase 3 toggle ships as default-ON because: PT-RMSE drops by W%" (data-driven flip)

The PT reference is the missing piece that turns "quality" from a vibe into a metric.
