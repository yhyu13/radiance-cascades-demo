# Phase 9d — Full-Frame Bilateral Postfilter + Mode 5/8 Fix + Resolution Options

**Date:** 2026-05-02
**Trigger:** User request after Phase 9c — add depth+normal aware screen-space blur,
restore mode 5 to original step count, move probe boundary to mode 8, add 24/48 probe
resolution options.

---

## Summary of Changes

| Change | Files |
|---|---|
| Mode 5 restored to step-count heatmap | `res/shaders/raymarch.frag` |
| Mode 8 = probe cell boundary viz | `res/shaders/raymarch.frag` |
| Mode 8 radio button added to UI | `src/demo3d.cpp` |
| Probe resolution options 24 and 48 added | `src/demo3d.cpp` |
| GBuffer second output (normal+depth) | `res/shaders/raymarch.frag` |
| Bilateral GI blur shader | `res/shaders/gi_blur.frag` (new) |
| Bilateral GI blur vertex passthrough | `res/shaders/gi_blur.vert` (new) |
| GI blur FBO + methods | `src/demo3d.h`, `src/demo3d.cpp` |
| GI blur UI controls | `src/demo3d.cpp` |

---

## Mode 5 / Mode 8 Swap

### Why

The previous Phase 9c accidentally put the probe cell boundary visualization in mode 5
and moved the original step-count heatmap to mode 8. The user wanted the original
numbering restored: mode 5 = step count (original behavior), mode 8 = probe boundary.

### Mode 5 — SDF Step Count Heatmap (restored)

**Location in shader:** post-loop (after the raymarching loop ends), not inside
`dist < EPSILON`. This is required because `stepCount` is the total loop iteration count —
it's only fully accumulated after the loop exits.

```glsl
// Debug mode 5: SDF step count heatmap (green=few, yellow=moderate, red=many/miss).
if (uRenderMode == 5) {
    float t8 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
    vec3 heatColor = (t8 < 0.5)
        ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t8 * 2.0)
        : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t8 - 0.5) * 2.0);
    fragColor = vec4(heatColor, 1.0);
    return;
}
```

Non-surface pixels (sky) show red because `stepCount → max`.

### Mode 8 — Probe Cell Boundary Visualization

**Location in shader:** inside `dist < EPSILON` block. Requires `pos` (the world-space
surface hit point) to compute the probe-grid coordinate.

```glsl
// Debug mode 8: probe cell boundary visualization.
// fract(pg) RGB — R=x, G=y, B=z probe-grid fractional coordinate.
// Color transitions at probe CENTER positions; fract=0.5 = cell boundary.
if (uRenderMode == 8) {
    vec3 uvw5 = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    vec3 pg5  = clamp(uvw5 * vec3(uAtlasVolumeSize) - 0.5,
                      vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    fragColor = vec4(fract(pg5), 1.0);
    return;
}
```

**How to use modes 6 and 8 together:**

Mode 8 is a correlated diagnostic, not a definitive classifier. Alignment of mode 6
banding with mode 8 transitions is *consistent with* Type A (probe-cell-size limited)
but does not rule out Type B or a mixed source. Use mode 8 to form a hypothesis, then
test it: increasing probe resolution (32→48) or enabling tricubic interpolation would
confirm Type A if banding improves; increasing D would confirm Type B.

| Observation | Hypothesis (not proof) | Test |
|---|---|---|
| Mode 6 banding aligns with mode 8 transitions | Consistent with **Type A** (cell-size limited) | Raise C0 res to 48³; if banding reduces → confirmed |
| Mode 6 banding misaligned with mode 8 | Consistent with **Type B** (directional D quantization) | Raise D from 8 to 16; if banding reduces → confirmed |
| Mixed / partial alignment | Both sources, or coincidental spatial correlation | Test both fixes independently |

### UI

```cpp
// Debug Render Mode section in renderSettingsPanel():
ImGui::RadioButton("Steps (5)",       &raymarchRenderMode, 5); // step count
ImGui::RadioButton("ProbeCell (8)",   &raymarchRenderMode, 8); // probe cell boundary
// tooltip on ProbeCell (8):
//   "Mode 8: probe cell boundary (fract of probe-grid coord as RGB).\n
//    Compare with Mode 6 (GI-only): aligned banding = Type A;\n
//    misaligned banding = Type B (directional D quantization)."
```

---

## Probe Resolution Options

### Before

```cpp
static const int kC0Options[]   = { 8, 16, 32, 64 };
static const char* kC0Labels[]  = { "8^3  (fast, coarse)", "16^3", "32^3 (default)", "64^3  (slow)" };
```

### After

```cpp
static const int kC0Options[]   = { 8, 16, 24, 32, 48, 64 };
static const char* kC0Labels[]  = { "8^3  (fast, coarse)", "16^3", "24^3", "32^3 (default)", "48^3", "64^3  (slow)" };
static const int kC0Count = 6;
```

Default index is now `3` (32³). Added 24³ and 48³ as intermediate options for gradual
Type A banding investigation without jumping straight to 64³ (8× memory vs 32³).

Cell sizes for each option (Cornell box = 4m, 4 cascades):

| C0 Res | Cell size | Probe count (C0) | Memory note |
|---|---|---|---|
| 8³ | 0.5m | 512 | trivial |
| 16³ | 0.25m | 4096 | fast |
| 24³ | 0.167m | 13824 | intermediate |
| **32³** | **0.125m** | **32768** | **default** |
| 48³ | 0.083m | 110592 | ~3.4× slower than 32³ |
| 64³ | 0.0625m | 262144 | ~8× slower than 32³ |

---

## Bilateral GI Blur — Indirect-Only Separated Architecture

### What it does

Phase 9d adds a depth+normal aware bilateral blur that operates on the **indirect
(GI) term only**, keeping the direct lighting term unblurred. Direct shadows and
specular highlights are passed through unchanged; only the probe-grid indirect
contribution is bilaterally smoothed.

This required separating the two terms inside `raymarch.frag` (mode 0 only):

```
FBO layout (3 color attachments, mode 0 + useGIBlur only):
  [0] giDirectTex   — linear direct:  albedo * (diff * lightColor + ambient)
  [1] giGBufferTex  — normal+depth:   normal*0.5+0.5 (rgb), linearDepth (a)
  [2] giIndirectTex — linear indirect: albedo * sampleDirectionalGI(pos, normal)

gi_blur.frag composites:
  toneMapACES(direct + bilateralBlur(indirect, gbuffer)) → gamma → screen
```

For debug modes (1–8), `useGIBlur` has no effect — they render directly to the
default framebuffer with no FBO indirection.

### What the bilateral weights preserve

The depth+normal weights prevent GI blurring across surface discontinuities:
- Probe-grid stepping within a flat wall face (same depth, same normal) → blurred
- Shadow edge, wall corner, box silhouette (depth or normal discontinuity) → preserved
- Direct shadows are never touched (they live in `giDirectTex`, bypassing the blur)

### Architecture

Two-pass rendering when `useGIBlur = true` AND `raymarchRenderMode == 0`:

```
Pass 1: raymarchPass()  [uSeparateGI=1]
  → render to giFBO (3 color attachments):
     [0] giDirectTex   (RGBA16F): linear direct lighting
     [1] giGBufferTex  (RGBA16F): normal*0.5+0.5 (rgb), linearDepth (a). Sky: a=0.
     [2] giIndirectTex (RGBA16F): linear indirect/GI
  → NO tone mapping or gamma in this pass (linear outputs)

Pass 2: giBlurPass()
  → bilateral blur on giIndirectTex using giGBufferTex weights
  → composite: toneMapACES(direct + blurred_indirect) → gamma → screen
```

When `useGIBlur = false` OR in a debug mode:
- `uSeparateGI = 0`, `raymarchPass()` renders directly to the default framebuffer
- Tone mapping and gamma applied in `raymarch.frag` as before
- All `layout(location=1/2)` writes are silently discarded (OpenGL 4.x spec)
- `giBlurPass()` is never called

### Shader Outputs in raymarch.frag

```glsl
layout(location=0) out vec4 fragColor;   // direct (linear) when uSeparateGI=1; full composite otherwise
layout(location=1) out vec4 fragGBuffer; // normal*0.5+0.5 (rgb), linearDepth (a). Sky: a=0.
layout(location=2) out vec4 fragGI;      // indirect/GI (linear) when uSeparateGI=1; vec4(0) otherwise

uniform int uSeparateGI;  // 1 = separated outputs (blur mode 0); 0 = normal composite path

// Top of main():
fragGBuffer = vec4(0.0);
fragGI      = vec4(0.0);

// At surface hit:
{
    float linearDepth = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.001, 1.0);
    fragGBuffer = vec4(normal * 0.5 + 0.5, linearDepth);  // 0.001 min so a=0 stays sky sentinel
}

// Mode 0 separated path:
if (uSeparateGI != 0) {
    fragColor = vec4(directColor,   1.0);  // linear, no tone map
    fragGI    = vec4(indirectColor, 1.0);  // linear, no tone map
    return;  // tone map happens in gi_blur.frag
}
// Normal path: composite and tone map here (unchanged behavior).
```

### gi_blur.frag — Bilateral Kernel on Indirect Only

```glsl
uniform sampler2D uDirectTex;   // giDirectTex  — linear direct (NOT blurred)
uniform sampler2D uGBufferTex;  // giGBufferTex — normal+depth weights
uniform sampler2D uIndirectTex; // giIndirectTex — linear indirect (BLURRED)
uniform int   uBlurRadius;
uniform float uDepthSigma;
uniform float uNormalSigma;

// Per-fragment:
// 1. Read direct (pass-through) and center GBuffer
// 2. If sky (centerGB.a < 1e-5): composite direct + unblurred indirect, tone map, done
// 3. Bilateral loop over (2r+1)^2 neighbors on uIndirectTex only
// 4. Composite: toneMapACES(direct + blurred_indirect) → gamma → screen
```

Uses `texelFetch()` for exact pixel-coordinate access (no bilinear interpolation of
GBuffer weights).

### FBO Management

```cpp
// demo3d.h:
GLuint giFBO;          // three-attachment FBO
GLuint giDirectTex;    // RGBA16F — linear direct lighting  (location=0)
GLuint giGBufferTex;   // RGBA16F — normal+depth            (location=1)
GLuint giIndirectTex;  // RGBA16F — linear indirect/GI      (location=2)
int giLastW, giLastH;  // screen size when FBO was created

void initGIBlur(int w, int h);  // create FBO + 3 textures, glDrawBuffers(3, ...)
void destroyGIBlur();            // delete FBO + textures
void giBlurPass();               // blur indirect, composite with direct, tone map → screen
```

FBO is created lazily on first mode-0 frame with blur enabled, and auto-recreated if
the screen size changes (checked in `raymarchPass()` before each draw).

### Rendering Integration

```cpp
// In render():
raymarchPass();             // renders to giFBO when useGIBlur
if (useGIBlur) giBlurPass(); // composites to default FB
```

```cpp
// In raymarchPass(), before the draw call:
if (useGIBlur) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    if (w != giLastW || h != giLastH) initGIBlur(w, h);
    if (giFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, giFBO);
        GLenum drawBufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, drawBufs);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}
// ... draw call ...
// After draw call:
if (useGIBlur && giFBO != 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
}
```

### UI Controls

In the Settings panel, new "GI Bilateral Blur" section:

```
[x] Enable GI Blur
    Radius:      1 — 8   (default 3)
    DepthSigma:  0.005 — 0.5  (default 0.05)
    NormalSigma: 0.05  — 1.0  (default 0.2)
```

**Tuning guide:**

| Parameter | Effect of increasing |
|---|---|
| Radius | Larger kernel — more smoothing, more cost (O(r²) taps) |
| DepthSigma | Softer depth edges — blur crosses depth discontinuities more |
| NormalSigma | Softer normal edges — blur crosses surface-orientation boundaries more |

Start with Radius=3, DepthSigma=0.05, NormalSigma=0.2. Tighten sigma values if
shadow edges or box corners become soft.

### Shader Loading

`gi_blur.frag` uses the same loadShader() path as other fragment shaders.
`loadShader("gi_blur.frag")` auto-pairs with `gi_blur.vert` (identical to `raymarch.vert`
— simple clip-space passthrough with `vUV` varying).

```cpp
loadShader("gi_blur.frag");  // Phase 9c: Bilateral GI blur
```

---

## Mode Numbering Reference (updated from Phase 9c)

| Mode | Description | Location in shader |
|---|---|---|
| 0 | Final rendering (direct + indirect) | surface hit |
| 1 | Surface normals as RGB | surface hit |
| 2 | Depth map | surface hit |
| 3 | Indirect radiance × 5 (magnified) | surface hit |
| 4 | Direct light only | surface hit |
| 5 | **SDF step count heatmap** (original, restored) | post-loop |
| 6 | GI-only — `albedo * indirect` | surface hit |
| 7 | Ray travel distance heatmap (continuous) | surface hit |
| 8 | **Probe cell boundary** — `fract(pg)` RGB (was mode 5 briefly) | surface hit |

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/raymarch.frag` | Mode 5 restored (step count, post-loop); mode 8 added (probe boundary, surface hit); `layout(location=1) out vec4 fragGBuffer` added; GBuffer written at surface hit; sky init to `vec4(0.0)` |
| `res/shaders/gi_blur.frag` | **New (untracked)** — bilateral indirect-only blur + composite + tone map |
| `res/shaders/gi_blur.vert` | **New (untracked)** — vertex passthrough (identical to raymarch.vert) |
| `src/demo3d.h` | Added `giFBO`, `giDirectTex`, `giGBufferTex`, `giIndirectTex`, `giLastW/H`, `useGIBlur`, `giBlurRadius`, `giBlurDepthSigma`, `giBlurNormalSigma`; added `initGIBlur()`, `destroyGIBlur()`, `giBlurPass()` declarations |
| `src/demo3d.cpp` | Constructor: init all new members; `loadShader("gi_blur.frag")`; FBO bind (3 attachments, mode 0 only) in `raymarchPass()`; `uSeparateGI` uniform set; `initGIBlur()`, `destroyGIBlur()`, `giBlurPass()` implementations; `if (useGIBlur && mode==0) giBlurPass()` in `render()`; ProbeCell (8) radio button; probe resolution combo 6 options; GI Blur UI sliders |

> **Verification note:** `gi_blur.frag` and `gi_blur.vert` are new untracked files at
> the time of this phase. Use `git status` (not `git diff`) to confirm their presence.

---

## Verification Checklist

| Check | Procedure |
|---|---|
| Mode 5 = step count | Switch to mode 5 — green walls (few steps), red sky/misses |
| Mode 7 vs mode 5 | Mode 7 smooth but mode 5 banded → integer step-count quantization |
| Mode 8 = probe boundary | Switch to mode 8 — RGB color gradient cycling at probe-cell frequency on surfaces |
| Mode 8 + mode 6 comparison | Overlay mentally: if GI banding aligns with mode 8 transitions → Type A |
| GI blur: no edges | Enable blur — wall GI smoother; shadow edges and box corners still sharp |
| GI blur: sky unchanged | Sky background unchanged (no blurring into scene) |
| Probe res 24/48 | Combo shows new options; switching rebuilds cascades |
| Screen resize | GI blur FBO recreates without crash when window is resized |
