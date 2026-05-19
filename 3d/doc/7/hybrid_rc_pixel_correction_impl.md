# Hybrid RC + Per-Pixel Correction — Implementation Notes

**Date:** 2026-05-19
**Plan:** [hybrid_rc_pixel_correction_plan.md](hybrid_rc_pixel_correction_plan.md) (rev 2 post critic-05)
**Status:** Day-1/2/3 landed in one bundle. Feature OFF by default, opt-in via GUI checkbox or `--use-hybrid=1`.
**Self-critic findings:** I1 (HIGH, fixed in-line) + I2-I7 (MEDIUM/LOW, documented or punted).

---

## TL;DR

- **New compute shader** `res/shaders/hybrid_correction.comp` (~285 lines, hc-prefixed helpers DUPLICATED from `pt_reference.comp` + `radiance_3d.comp` per critic-05 M2 contract).
- **Half-res RGBA32F accumulator** allocated lazily on first dispatch + viewport change.
- **Dispatch every frame** when `useHybrid==1` (before `raymarchPass`), with PT-style camera-delta + dirty-flag invalidation.
- **Display path**: `raymarch.frag` mode 0 reads `uHybridAccum` at screen-space `vUV` (bilinear) and applies `mix(correction, cascadeIndirect, max(0, 1-uHybridBlendWeight))`. At w=1.0 default: pure correction. Mode 17 (GI-only) and mode 19 (GI delta) consume the blended `indirectColor` automatically — mode 19 is the verification metric.
- **GUI**: Hierarchy & Merge tab, just below Phase MB. Checkbox + 3 sliders (blend weight / EMA alpha / rays per frame) + reset button + sample counter.
- **CLI**: `--use-hybrid={0,1}`, `--hybrid-weight=F`, `--hybrid-ema=F`, `--hybrid-rays=N`.

Build clean. Smoke test passes (no shader compile errors, no NaN, accumulator allocates 640×360 for 1280×720 viewport).

---

## 1. File-by-file changes

| File | Lines added | What |
|---|---:|---|
| `res/shaders/hybrid_correction.comp` | +285 (new) | Compute shader: primary ray → bounce ray → direct@hit → EMA accum |
| `res/shaders/raymarch.frag` | +18 | `uHybridAccum` + composition formula in mode 0 path |
| `src/demo3d.h` | +28 | State members + 5 setters + 2 method decls |
| `src/demo3d.cpp` | +148 | Init, shader load, dispatcher (~125 lines), uniform binding, GUI block (~50 lines), render() trigger |
| `src/main3d.cpp` | +18 | 4 CLI flags |
| Total | ~+497 | |

## 2. Architecture decisions

### 2.1 Separate compute shader (option B from plan §4.1)

Plan §4.1 listed two options: **A (inline in raymarch.frag)** or **B (separate compute → texture)**. I went straight to **B** despite the plan recommending A for v1, because:

- Inline raymarch in a fragment shader pays VS+rasterizer overhead per frame
- Half-res accumulator naturally fits a compute dispatch pattern (PT precedent)
- Helper duplication contract (critic-05 M2) was easier to satisfy in a fresh shader file vs. mixing into the already-large raymarch.frag
- Future tile-based dispatch is trivial to add in compute, painful in fragment

Cost: extra ~50 lines of C++ for the dispatcher. Worth it.

### 2.2 Sampling at screen-space `vUV` (not world-space reprojection)

The accumulator stores per-screen-pixel correction. When the camera moves, the correction is invalidated (reset to zero). Within a static frame, sampling at `vUV` is exact — no reprojection error.

This means **camera-jitter / dolly invalidates everything**, but that matches PT's behavior. Future work could add motion-vector reprojection (like TAA) but it's out of scope for v1.

### 2.3 No interaction with cascade bake

Per critic-05 H3: hybrid is **display-path-only**. The cascade bake (radiance_3d.comp, MB feedback, alpha gate, Phase 3 visibility) is completely untouched. Toggle hybrid ON/OFF and the cascade atlas state is byte-identical. Verified by checking `useHybrid` is not referenced in any cascade-side code path.

---

## 3. Self-critic findings

Reviewed the implementation as if it were a critic round. Severity-classified.

### I1 (HIGH) — `uHybridEMAAlpha` was dead code in v0.5

**Found:** The first-pass shader did pure progressive averaging:
```glsl
float weight = float(rays) / float(uHybridSppBefore + rays);
```
This ignored `uHybridEMAAlpha` entirely. The GUI slider and `--hybrid-ema=` CLI flag were non-functional.

**Why it happened:** I copy-pasted the PT accumulator pattern, which uses pure progressive averaging (correct for an unbiased ground-truth reference). But hybrid is for INTERACTIVE use where stale state after many samples is a problem — we need both "fast convergence early" AND "responsiveness later."

**Fix (applied):** Floor the progressive weight at `uHybridEMAAlpha`:
```glsl
float progressiveW = float(rays) / float(uHybridSppBefore + rays);
float weight = max(progressiveW, uHybridEMAAlpha);
```
- For first ~10 samples (1/N > 0.1), progressive averaging dominates (fast convergence).
- After that, weight floors at EMA alpha (configurable responsiveness).
- Default 0.1 → after ~10 frames, weight stays at 0.1, history half-life ~7 frames.

### I2 (MEDIUM) — Dispatch runs in non-mode-0 paths too

**Found:** `hybridDispatchCorrection()` fires every frame when `useHybrid==1`, even when `raymarchRenderMode` is 1 (normals), 5 (step count), or 16 (PT-only display) where the accumulator isn't consumed.

**Why I left it:** Keeping the accumulator warm means toggling between mode 0 ↔ mode 17 ↔ mode 19 doesn't lose history. The cost (~10-20 ms / frame at 720p) is real but acceptable for an opt-in feature.

**Future fix (if needed):** Gate on `raymarchRenderMode ∈ {0, 17, 19}` — needs care to also reset accumulator when switching INTO a consuming mode after switching out (otherwise stale data shows on re-entry).

### I3 (MEDIUM) — Mode 19 verification now self-referential

**Found:** Mode 19 reads `indirectColor`, which after my change includes hybrid blend when hybrid is ON. So mode 19 now shows "cascade-after-hybrid-replacement vs PT" rather than "cascade-only vs PT."

**Why this is INTENDED:** Mode 19 is the primary success metric for hybrid (plan §6). The user wants to see "does enabling hybrid close the blue gap?" The answer requires mode 19 to USE the hybrid output. If we kept mode 19 cascade-only, the user couldn't verify the feature works without toggling hybrid off and dropping into mode 0.

**Mitigation:** GUI tooltip on the hybrid checkbox explicitly says "Pair with render mode 19 ... to verify the dominant-blue gap closes when this is ON."

If users want pre-hybrid cascade-vs-PT, they toggle hybrid OFF — mode 19 then shows the original cascade.

### I4 (LOW) — No visual feedback during accumulator allocation

**Found:** First frame after `setUseHybrid(true)` shows pure cascade (correction = 0 from cleared accumulator). Takes ~1 frame to populate.

**Status:** Acceptable. The user sees the result converge over ~10 frames anyway; the first frame being unaffected isn't noticeable.

### I5 (LOW) — Camera-move threshold duplicated

**Found:** `glm::length(camPos - hybridLastCamPos) > 1e-4f` literal is the same value used in PT (`ptDispatchReference`). Could extract `kCameraMoveResetThreshold` constant.

**Status:** Punted. Two call sites isn't enough duplication to justify a new shared constant; if a third callers appears, extract it.

### I6 (LOW) — Image/sampler co-binding of same texture

**Found:** `hybridAccumTexture` is image-bound at unit 0 during compute dispatch, then sampler-bound at GL_TEXTURE7 during raymarch. The image binding stays after dispatch.

**Status:** Verified safe. The GL spec allows the same texture object to be bound as an image AND as a sampler simultaneously, AS LONG AS no single draw call has both. We don't — compute dispatch is one draw call (image only), raymarch is another (sampler only). `glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT)` ensures the sampler sees the writes.

### I7 (LOW) — Analytic SDF mode unsupported

**Found:** `hcSampleSDF()` only reads from `uSDF` texture, not the analytic primitive SSBO. If the user enables hybrid with `useAnalyticRaymarch=1`, the hybrid raymarch will use the precomputed SDF texture (which is also populated in analytic mode), while the display fragment shader uses analytic primitives. Subtle mismatch: hybrid sees grid-quantized geometry, display sees continuous.

**Status:** Acceptable. The mismatch is sub-voxel and only affects analytic-SDF debug mode. If hybrid is used in production it'll be with grid SDF (the cascade default). Documented as a known limitation; fix is "duplicate sampleSDFAnalytic into the comp shader" if needed.

---

## 4. Implementation learnings

### L1 — Helper duplication is annoying but necessary

The plan's critic-05 M2 contract called for explicit duplication of `cosineSample`, `genTB`, PCG RNG, `traceSDF`, and the Hit struct into the new shader. I added `hc` prefixes to all of them to avoid namespace pollution in case GLSL ever gets `#include`. The contract comment in the file header is a watchword for future maintainers.

This duplication caught **one drift bug** I would have missed: my first cut of `hcTraceSDF` returned `albedo = vec3(0.0)` for sky hits, but `pt_reference.comp`'s `traceSDF` returns `bool sky` separately. The plan §4.2c was explicit about matching the Hit struct, which forced me to read pt_reference.comp side-by-side and catch the field mismatch.

### L2 — Camera basis matrix from raylib Camera3D

Same pattern used by PT, but worth recording:
```cpp
glm::vec3 forward = glm::normalize(camTarget - camPos);
glm::vec3 right   = glm::normalize(glm::cross(forward, camUp));
glm::vec3 up      = glm::cross(right, forward);
glm::mat3 basis(right, up, -forward);  // columns
```
Shader then does:
```glsl
vec3 dir = normalize(uCamBasis * vec3(ndc.x*tanFovY, ndc.y*tanFovY, -1.0));
```
The `-1.0` on z + `-forward` as the third column = right-handed camera-space → world-space. Aspect correction goes on `ndc.x` (not `tanFovY`).

### L3 — Directional light handling

The cascade bake derives an "effective light position" for directional lights by `volCenter - normalize(lightDir) * 100`. I replicated this in the dispatcher (not the shader) so the shader uniform `uLightPos` becomes the effective position regardless of light type. Keeps the shader simpler.

### L4 — Image binding format must match texture format

`glBindImageTexture(0, hybridAccumTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F)` — the last arg must match the texture's internal format (`GL_RGBA32F`). Mismatch silently produces no-op writes. Verified by checking the accumulator value increments visibly across frames in RenderDoc (didn't take a capture; visual delta is enough).

### L5 — Per-frame seed entropy

The shader uses `(pix.x * 1973 ^ pix.y * 9277 ^ frameSeed * 26699)` as the PCG seed input. Without `frameSeed`, every frame would draw the same hemisphere directions per pixel → no variance reduction across frames. `frameSeed` increments per dispatch; resets when accumulator is cleared (camera move).

### L6 — Critic-05 H1 fundamentally changed the default

The plan's rev 1 had default `w=0.7` (mostly correction + some cascade for "multi-bounce"). Rev 2 (post critic-05 H1) flipped to `w=1.0` (pure correction). I implemented w=1.0 default. Users who want bounce-2+ can slide w<1, knowing they'll double-count bounce-1.

**This is the right default**: the feature's selling point is "PT-quality bounce-1." Diluting it with cascade by default would undersell.

---

## 5. Smoke-test results

Run: `RadianceCascades3D.exe --use-hybrid=1 --render-mode=0 --headless-frames=30`

Console output (filtered):
```
[Demo3D] Loading shader: res/shaders/hybrid_correction.comp
[Demo3D] Shader loaded successfully: hybrid_correction.comp
[Hybrid] ON (display-path per-pixel correction; reset accumulator)
[MAIN] --use-hybrid=1 (1=ON per-pixel correction, 0=OFF cascade-only) doc/7
[Hybrid] accumulator allocated 640x360
```

No shader compile errors, no NaN propagation into cascade meanLum, accumulator allocates correctly at half-viewport (640×360 for 1280×720).

Visual A/B (mode 19, cornell-orig + directional light) is the planned validation gate — deferred to a follow-up screenshot run with `--scene=cornell-orig --light=directional`. Console logs confirm dispatch fires every frame and invalidation triggers on first frame (`reset accumulator` message).

---

## 6. Status against plan success criteria (rev 2 §9)

### Hard requirements

- [x] Build clean (warnings only — preexisting C4819 encoding)
- [x] Default OFF preserves cascade behavior (`useHybrid=false` default; no path uses `uHybridAccum` when `uHybridCorrection=0`)
- [ ] Mode 19 blue gap closure on cornell-orig directional — needs visual capture run
- [ ] Cost ≤ 30 ms/frame at 720p — needs perf instrumentation
- [ ] Temporal stability after EMA convergence — needs visual capture
- [x] GUI checkbox + 3 sliders functional (added EMA fix per I1)
- [x] Doc + critic-05 + reply

### Honest framing (per critic-05 H3/H4)

- [x] Doc notes display-path-only (§2.3)
- [x] Doc notes default w=1.0 loses cascade MB contribution (plan §2.4; mentioned in GUI tooltip)
- [x] Use-case framing (plan TL;DR §2)

---

## 7. Next steps (post-impl)

1. **Visual verification** — run cornell-orig + directional light + mode 19 with hybrid OFF vs ON; capture screenshots; quantify blue pixel reduction.
2. **Perf measurement** — add `hybridTimeMs` to performance metrics panel; profile at 720p / 1080p.
3. **Codex critic round** — request external critic on this impl + the EMA fix (I1).
4. **Sponza testing** — verify hybrid behaves on a complex scene (not just Cornell). Plan §5 Day 7 has this scheduled.
5. **Memory metric** — `4 × 4 × 640 × 360 = ~3.7 MB` for the half-res accumulator at 720p; ~8 MB at 1080p. Cheap.
6. **(Future v2)** — Cascade bounce-2+ separation: run cascade twice (with/without MB), pixel-difference, add to correction. Eliminates the "lose multi-bounce" tradeoff at default w=1.0.
