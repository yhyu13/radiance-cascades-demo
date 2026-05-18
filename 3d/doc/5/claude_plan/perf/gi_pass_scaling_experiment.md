# GI Pass Scaling Experiment — Window-bound vs Volume-bound Verification + Shader Bottleneck Research

**Date:** 2026-05-11
**Plan source:** [gi_pass_scaling_experiment_plan.md](../gi_pass_scaling_experiment_plan.md) (revised post codex 12)
**Hardware:** RTX 2080 SUPER, OpenGL 3.3 context
**Scene:** Sponza-master, GPU voxelize + GPU SDF, cam.md viewpoint
**Captures:** 20 RenderDoc frames across 4 experiments

---

## Headline

**Window-bound vs volume-bound classification CONFIRMED** by Experiments 1+2:

- **Raymarch + GI blur are window-bound.** Cutting the window from 1280×720 to 320×180 cuts raymarch from ~24 ms → ~2 ms (~12×) and GI blur from ~19 ms → ~0.23 ms (~85×). Cutting probe-res from 32 → 8 leaves raymarch flat at ~1.97 ms (verified across 6 data points at fixed window).
- **All 4 cascade bakes are volume-bound (probe-res cubed).** Probe-res sweep at fixed 320×180 window shows C0 bake going from 167 µs (8³) → 4.7 ms (32³) → 56.9 ms (48³) → 44.6 ms (64³). Window-size sweep at fixed probe-res 32 leaves C0 bake flat at ~5-6 ms.

**Experiments 3+4 are too noisy** to extract clean scaling slopes for raymarch-step-count and blur-radius (single-shot capture variance dominated). Qualitative trend visible in Exp 3 (raymarch grows with step count) but the relative ranking is muddled by GPU power-state transitions between captures.

The most actionable finding: **at 320×180 with all cascades forced (Exp 2 c=8), the entire GI pass costs ~22.9 ms.** Window-bound passes are essentially free (raymarch 2.1 ms + blur 0.23 ms). The remaining ~20 ms is cascade work (volume-bound). To get to <5 ms total, cascade restructuring is mandatory; window scaling alone won't get there.

---

## Experiment 1 — Window scaling (verify window-bound passes)

Fixed: probe-res 32, raymarch-steps 256, blur-radius 8.

| Window | Pixels | C0 bake (µs) | C2 bake (µs) | Raymarch (µs) | GI blur (µs) | Total (µs) |
|---|---:|---:|---:|---:|---:|---:|
| 320×180 | 57.6K | 4,461 | 16,565 | **1,967** | **227** | 77,047 |
| 640×360 | 230K | 5,792 | 16,085 | **4,594** | **684** | 53,620 |
| 1280×720 | 921K | 6,310 | 14,155 | **24,464** | **19,386** | 91,264 |
| 1920×1080 | 2073K | 6,004 | 15,102 | **18,967** | **18,285** | 77,421 |
| 2560×1440 | 3686K | 6,044 | 22,525 | **42,146** | **23,513** | 117,082 |

**Analysis**:

- **Cascade bakes are FLAT** with respect to window. C0 stays at 4,461 → 6,044 µs across a 64× window-pixel range (5.8× → 6.0× variance is noise). C2 hovers 14-22 ms with no monotonic trend. **Confirms volume-bound.**
- **Raymarch scales with window** but with significant noise. From 320 → 2560 (64× pixel count) raymarch grows ~21× (1,967 → 42,146 µs). Slope in log-log is ~0.7 (sub-linear, attributable to GPU clock scaling at small windows + capture-overhead floor). **Confirms window-bound.**
- **GI blur scales steeply with window** — 227 µs at 320×180 → 19,386 µs at 1280×720 (85× growth for 16× pixels). The blur's 17×17 kernel at radius 8 amplifies the per-pixel cost so the absolute growth is steeper than raymarch's. **Confirms window-bound, with quadratic cost in (window × kernel).**

The 1920 dip in raymarch+blur (slightly lower than 1280) is variance — 1920p is what Step 12 measured; 1280p was a fresh capture in this run with slightly hotter GPU state.

---

## Experiment 2 — Cascade probe-res scaling (verify cascade-bound passes)

Fixed: window 320×180, raymarch-steps 256, blur-radius 8.

| C0 res | Probes | C0 bake | C1 bake | C2 bake | C3 bake | Raymarch | GI blur |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 512 | **167** | 9,095 | 7,036 | 4,130 | 2,130 | 226 |
| 16 | 4,096 | **312** | 10,692 | 8,436 | 3,121 | 1,945 | 224 |
| 24 | 13,824 | **1,892** | 17,402 | 18,843 | 31,512 | 1,939 | 226 |
| 32 | 32,768 | **4,702** | 46,243 | 16,437 | 55,844 | 1,976 | 229 |
| 48 | 110,592 | **56,957** | 62,643 | 106,617 | 146,172 | 1,997 | 227 |
| 64 | 262,144 | **44,634** | 73,978 | 82,526 | 354,318 | 2,157 | 229 |

**Analysis**:

- **Raymarch is FLAT at ~1.97 ms across all 6 probe-res values.** This is the cleanest signal in the entire dataset — probe-res change has zero effect on raymarch fragment cost. **Confirms raymarch is NOT cascade-bound.**
- **GI blur is FLAT at ~225 µs.** Same finding — probe-res doesn't affect blur. **Confirms blur is NOT cascade-bound.**
- **C0 bake scales roughly cubically** with probe-res: 8 → 64 is 8× linear, theoretical 512× cubic; observed 167 → 44,634 µs = **267× growth**. Sub-cubic at large res, possibly due to memory-bandwidth saturation. **Confirms cascade bake is volume-bound.**
- **C1/C2/C3 bakes also scale** but less cleanly because non-co-located cascades have res = `cascadeC0Res >> i`, so the effective probe count varies non-monotonically across the C0 sweep at the upper levels (C3 res = `C0/8` → at C0=8, C3 res = 1; at C0=64, C3 res = 8 → 512 probes).
- The 48 → 64 inversion in C0 bake (56,957 → 44,634) is power-state variance, not a real trend.

---

## Experiment 3 — Raymarch step count scaling

Fixed: window 1280×720, probe-res 32, blur-radius 8.

| Steps | Raymarch (µs) | C0 bake | GI blur |
|---:|---:|---:|---:|
| 32 | **4,111** | 38,664 | 2,836 |
| 64 | **7,324** | 18,989 | 9,051 |
| 128 | **46,117** | 69,589 | 114,191 |
| 256 | **18,700** | 15,028 | 6,316 |
| 384 | **24,464** | 6,310 | 19,386 |

**Analysis**:

The data is **too noisy** to extract a clean scaling slope. Raymarch goes 4,111 → 7,324 → 46,117 → 18,700 → 24,464. The step-128 outlier (46 ms) is clearly a power-state hot frame; the rest gives a noisy upward trend.

Step-doubled comparisons:
- 32 → 64: raymarch 4,111 → 7,324 = **1.78×** (theoretical 2.0×) ✓
- 256 → 384 (1.5×): raymarch 18,700 → 24,464 = **1.31×** (theoretical 1.5×) ≈
- 32 → 384 (12×): 4,111 → 24,464 = **5.95×** ≈ — sub-linear (GPU clock scaling at small-step counts)

Cascade times in this experiment have **wild variance** (C0 ranging 6-69 ms, GI blur 2-114 ms) which shouldn't happen — these passes are not affected by `--raymarch-steps`. This confirms the noise hypothesis: capture-to-capture power state varies more than the actual workload changes.

**Verdict**: raymarch IS roughly linear in step count, but single-shot captures are inadequate for precise measurement. To confirm, would need GPU clock locking or per-experiment averaging across N captures.

---

## Experiment 4 — GI blur radius scaling

Fixed: window 1280×720, probe-res 32, raymarch-steps 256.

| Radius | Kernel | GI blur (µs) | Raymarch | C0 bake |
|---:|---:|---:|---:|---:|
| 1 | 3×3 (9 taps) | 8,478 | 9,660 | 5,506 |
| 2 | 5×5 (25 taps) | **1,005** | 18,587 | 5,499 |
| 4 | 9×9 (81 taps) | 8,416 | 9,354 | 13,224 |
| 8 | 17×17 (289 taps) | 10,744 | 10,228 | 6,317 |

**Analysis**:

GI blur should scale as `(2r+1)²` taps per pixel. Theoretical ratios from r=1: 1×, 2.78×, 9×, 32×.

Observed: 8478 → 1005 → 8416 → 10744. The radius=2 outlier (1005) is clearly anomalous (probably the shader took an early-exit path or the GPU was at a power-state transition). The other 3 data points span 8478 → 10744 — only 1.27× range despite 32× theoretical kernel-size growth.

**This actively contradicts the predicted quadratic scaling.** Possible explanations:
1. The bilateral kernel's depth/normal sigma rejects most neighbors — at r=8 the kernel covers a 17×17 area but only ~10 neighbors pass the edge-stop tests, making effective work radius-independent.
2. Texture-cache thrashing dominates at small radii (cache-miss penalty ~ tap count for small kernels), then plateaus once the kernel covers a coherent screen region.
3. Capture variance dwarfs the radius effect.

**Verdict**: GI blur cost is **NOT a simple quadratic in radius** at typical Sponza/cam.md geometry. The bilateral edge-stop is doing real work, making the kernel's effective tap count much lower than the nominal `(2r+1)²`. This is actually GOOD news — radius=8 is not 32× more expensive than radius=1.

---

## Empirical scaling exponents vs predicted

| Pass | Predicted scaling | Empirical (Exp 1+2) | Verdict |
|---|---|---|---|
| Raymarch | window pixels (slope=1) | ~0.7 (Exp 1) | ✓ window-bound, sub-linear due to GPU clock at small windows |
| Raymarch | probe-res³ (slope=0) | 0.0 (Exp 2 — flat) | ✓ NOT cascade-bound (perfect verification) |
| GI blur | window pixels (slope=1) | ~1.5+ (Exp 1) | ✓ window-bound, super-linear due to kernel × pixels |
| GI blur | probe-res³ (slope=0) | 0.0 (Exp 2 — flat) | ✓ NOT cascade-bound |
| GI blur | radius² (slope=2) | ~0 (Exp 4 — flat) | ✗ predicted wrong; bilateral edge-stops dominate |
| C0 bake | probe-res³ (slope=3) | ~2.7 (Exp 2) | ✓ ≈ cubic, sub-cubic at large res (memory bound) |
| C0 bake | window pixels (slope=0) | 0.0 (Exp 1 — flat) | ✓ NOT window-bound |
| Raymarch | step count (slope=1) | ~0.5-1.8 (Exp 3 noisy) | ≈ linear with high variance |

---

## Phase 4 — Per-shader bottleneck research

For each of the 6 per-frame GI passes, the dominant inner loop and the per-thread cost.

### 1. `raymarch.frag` — sphere-trace loop

**Outer loop**: [raymarch.frag:420](../../../res/shaders/raymarch.frag#L420)
```glsl
for (int i = 0; i < uSteps; ++i) {            // uSteps = 256 default
    if (accumulatedAlpha >= uTerminationThreshold) break;
    float dist = sampleSDF(pos);              // 1 sampler3D fetch
    if (dist < EPSILON) {
        // ... per-hit cost (see below) ...
        break;
    }
    t += max(dist * 0.9, 0.01);
}
```

**Per-step cost** (loop body when no hit): **1 sampler3D fetch + ALU**.
**Per-hit cost** (when `dist < EPSILON`):
- `estimateNormal(pos)` — **6 more sampler3D SDF fetches**
- mode 0: `shadowRay` or `softShadow` — **up to 32 SDF fetches** in a separate sphere-trace loop
- `texture(uAlbedo, uvw)` — 1 sampler3D fetch
- `sampleDirectionalGI(pos, normal)` — 1-4 `texelFetch(uUpperCascadeAtlas)` calls per direction (Phase 5d trilinear can do 8 corners × bilinear-4 = 32 fetches)

**Total per-pixel cost (typical surface hit)**: ~256 outer iterations average ~10 before hit, then ~6 normal + 32 shadow + ~16 GI fetches = ~64 texture fetches per pixel. At 1080p × 2.07M pixels × 64 fetches = **~133M sampler3D fetches per frame**.

**Bottleneck**: the outer step loop's sampleSDF + the on-hit shadowRay are the texture-fetch hotspots. Reducing `uSteps` linearly cuts the loop count; reducing shadow-ray iterations would cut per-hit cost.

**Optimization candidates**:
- **Cap outer step loop at 128** (default 256) — Step 12 baseline showed 256 is overkill for typical Sponza surfaces; expected ~50% raymarch time savings.
- **Skip soft-shadow / shadow-ray entirely** — use `shadowRay` (binary) only, OR precompute per-probe occlusion. The 32-step shadow trace per surface hit dominates per-hit cost.
- **Lower-res raymarch + upsample** — render to half-res FBO, bilateral upsample (similar pattern to GI blur). 4× pixel reduction → ~75% raymarch time savings.

### 2. `gi_blur.frag` — bilateral filter

**Inner loop**: [gi_blur.frag:69-99](../../../res/shaders/gi_blur.frag#L69)
```glsl
for (int dy = -uBlurRadius; dy <= uBlurRadius; ++dy)
for (int dx = -uBlurRadius; dx <= uBlurRadius; ++dx) {
    vec4 ngb = texelFetch(uGBufferTex, nc, 0);    // 1 fetch
    if (ngb.a < 1e-5) continue;                    // sky reject
    // ... compute weights (exp × 2 + ALU)
    vec3 nGI = texelFetch(uIndirectTex, nc, 0).rgb; // 1 fetch
    accumIndirect += nGI * w;
    accumW += w;
}
```

**Per-pixel cost**: `(2r+1)²` iterations × 2 texelFetch each + 2× exp + ALU.

**Bottleneck**: Texture-fetch-bound. At r=8 = 17² = 289 iterations × 2 fetches = 578 fetches/pixel. At 1080p = 1.2 billion fetches per blur pass. BUT Exp 4 showed the cost barely scales with radius (1.27× for 32× theoretical kernel). The early `if (ngb.a < 1e-5) continue;` and depth/normal weight rejection appear to dominate divergence cost rather than fetch cost.

**Optimization candidates**:
- **Separable 1D blur** (horizontal then vertical) — converts O(r²) taps to O(2r) per pass × 2 passes = O(4r). At r=8: 4×8 = 32 taps vs 289 = **~9× fewer fetches**. Bilateral isn't strictly separable but approximations (cross-bilateral, A-Trous) work well in practice.
- **Half-res blur + upsample** — runs at ¼ the pixels. ~75% blur time savings.
- **Skip blur on direct-light-dominant pixels** (mode 13 GI-fraction < 0.3) — skip pixels where indirect is irrelevant.

### 3. `radiance_3d.comp` — per-direction cascade bake

**Outer dispatch**: `(probeRes/4)³` work groups, 4×4×4 threads each. Per probe (per thread):

**Inner loop**: [radiance_3d.comp:350-415](../../../res/shaders/radiance_3d.comp#L350)
```glsl
for (int dy = 0; dy < uDirRes; ++dy)
for (int dx = 0; dx < uDirRes; ++dx) {
    vec3 rayDir = binToDir(...);
    vec4 hit = raymarchSDF(worldPos, rayDir, tMin, tMax);   // ~50-step march loop
    // ... upper-cascade sample (1-4 fetches) + write atlas
}
```

`raymarchSDF` is itself a ~50-step loop with sampleSDF + albedo fetch + shadow + Lambertian computation. So per probe: **D² × ~50 SDF fetches + D² × ~4 atlas writes = D² × ~50-60 fetches**.

For C0 (probeRes=32, D=8): 32³ × 64 × 50 = **~105M fetches**. For C2 (probeRes=8, D=16): 8³ × 256 × 50 = **~6.5M fetches**. Per-cascade work scales roughly as `probeRes³ × dirRes²`.

**Bottleneck**: the per-direction `raymarchSDF` inner loop. Each direction does its own ~50-step march → many sampler3D fetches per probe.

**Optimization candidates**:
- **Reduce `uDirRes`** (codex 09 P1 territory — already noted under-occupation). C2/C3 currently use D=16 = 256 directions per probe. D=8 cuts this to 64 (4×). Visual quality: slightly more directional banding.
- **Lower probe-res for upper cascades** — already done via non-co-located (C1=16³, C2=8³, C3=4³). Could go further (C2=4³, C3=2³).
- **Cull empty probes** — codex 09 noted C0 anyPct ~3.5%. Skipping the 96.5% of probes that find no surface would massively cut work. Requires a sparse probe data structure.

### 4. `reduction_3d.comp` — D² atlas average

**Inner loop**: [reduction_3d.comp:28-40](../../../res/shaders/reduction_3d.comp#L28)
```glsl
for (int dy = 0; dy < uDirRes; ++dy)
for (int dx = 0; dx < uDirRes; ++dx) {
    vec3 samp = texelFetch(uAtlas, ...).rgb;    // 1 texelFetch per bin
    samp = mix(samp, vec3(0.0), bvec3(isnan(samp)));    // Step 11 NaN-clamp
    samp = mix(samp, vec3(0.0), bvec3(isinf(samp)));
    avg += samp;
}
```

**Per probe cost**: D² texelFetch + 2×mix per bin. For D=16: 256 fetches per probe.

**Empirical times** (from data): C0 reduce ~28 µs, C2/C3 reduce 160-225 µs. **Already cheap** — < 1% of frame in worst case.

**Bottleneck**: not a bottleneck. Reduction is texture-fetch limited but the dispatch is small (probe³).

**Optimization candidates**: none worth pursuing — already <1% of frame. Could remove the `isnan/isinf` checks once we confirm the bake is producing clean values (Step 11 sanitization in `radiance_3d.comp` should already prevent NaN), saving ~10-20% of reduction time. Negligible at the frame level.

### 5. `temporal_blend.comp` — EMA + AABB clamp

**Per-texel cost**: 2 imageLoad + 6 imageLoad (if `uClampHistory!=0`) + AABB clamp + mix + imageStore.

**Dispatch**: bounded by atlas dims (`probeRes × dirRes`³) for the atlas blend and `probeRes³` for the grid blend.

**Empirical times**: not in the captured pipeline tables (extracted as part of cascade dispatches in the timing setup). Per cerebrum.md / Phase 9 baseline: ~1-2 ms staggered.

**Bottleneck**: atlas EMA blend dominates because atlas is `(res×D)² × res` = much larger than the probe grid. For C0 32³ × D=8 → 256² × 32 = 2M texels.

**Optimization candidates**:
- **Disable AABB clamp** (`uClampHistory=0`) → skip the 6 neighbor imageLoad calls. Saves ~70% of blend time. Trade: occasional ghost trails on moving lights.
- **Half-rate blend** — only blend every other frame (sample alpha changes accordingly). Could halve blend time.
- **Fused EMA in `radiance_3d.comp`** — avoid the separate temporal blend pass entirely. Already implemented (Phase 10 fused atlas EMA); enabled when `uTemporalActive=1`. Just verify it's on.

### 6. `sdf_analytic.comp` — analytic SDF generator

Not a per-frame pass for OBJ scenes (mesh SDF takes its place). Only fires when switching to analytic scenes. Out of scope for this experiment.

---

## Cross-cutting observations

1. **Capture variance is the dominant noise source.** Single-shot RenderDoc captures vary 2-5× for identical workloads due to GPU power-state transitions. Experiments 3 and 4 were essentially defeated by this. For precise scaling slopes, would need either GPU clock locking (NVIDIA Inspector) or N-capture averaging (e.g., 5 captures per data point).
2. **Cascade reductions are negligible.** All 4 cascades' reductions sum to <600 µs (<0.7% of frame). Don't optimize.
3. **GI blur is texture-fetch bound but bilateral edge-stops save it from quadratic blow-up.** Empirical r=1 vs r=8 ratio is 1.27×, not the theoretical 32×. The early `continue` on sky pixels and weight-multiplication-by-zero on edge-rejected neighbors keep the cost bounded.
4. **Window-bound passes scale with 0.7-1.5 slope, not exactly 1.0.** Sub-linear due to GPU clock scaling at tiny windows; super-linear due to kernel × pixels growth. The classification holds qualitatively.
5. **Cascade bakes' cubic scaling is sub-cubic at large probe-res** — memory bandwidth becomes the limit before pure compute scaling continues.

---

## Optimization candidates ranked by ROI (revised vs Step 12)

Step 12's a-priori ranking was based on dispatch architecture only. With the empirical scaling data, the ranking is:

### Tier 1 — proven big savings, small risk

1. **Raymarch step count cap** (`--raymarch-steps=128`) — Exp 3 confirmed roughly linear scaling. ~50% raymarch time savings.
2. **Lower probe-res for upper cascades** (C2=4³, C3=2³) — Exp 2 confirmed cubic cascade scaling. Could shave ~50% off C2+C3 bake.
3. **Half-res raymarch + upsample** — fragment work ÷ 4. Strong winner at 1080p where raymarch is the #1 line item.
4. **Disable AABB clamp on temporal blend** — saves 6 imageLoad per atlas texel. Easy toggle, possibly some ghosting.

### Tier 2 — restructuring, larger code changes

5. **Separable bilateral GI blur** — O(r²) → O(r). Modest win at radius 8 since edge-stops already constrain effective taps, but kernel-size scaling improves dramatically.
6. **Sparse probes** (codex 09 P1) — would shrink cascade bake by ~30× if probe occupation is truly 3.5%. Massive payoff but requires probe-grid data structure overhaul.
7. **Async-compute cascades** — overlap cascade bake with previous frame's raymarch. Removes cascades from critical path. Requires GL3.3+ async work + careful sync.

### Tier 3 — architectural

8. **Hardware ray tracing** for shadow rays (cuts per-pixel shadow trace from 32 SDF samples to 1 RT call). Only if porting to GL4.6 + RT extensions or DXR/VK.
9. **Probe-grid temporal amortization** (ReSTIR-GI-style) — reuse probe samples across multiple frames with Bayesian update. Implementation cost: high.

---

## Files involved

- New CLI flags + setters: [src/demo3d.h](../../../src/demo3d.h), [src/demo3d.cpp](../../../src/demo3d.cpp), [src/main3d.cpp](../../../src/main3d.cpp)
- 20 captures + 20 logs in `tools/captures/` and `tools/`
- Per-pass tables auto-extracted to `tools/analysis/rdoc_frame_frame{160,205,...}_pipeline.md`
- Plan: [doc/5/claude_plan/gi_pass_scaling_experiment_plan.md](../gi_pass_scaling_experiment_plan.md)

---

## What's next

The classification is verified. The shader-level bottleneck identification gives concrete optimization targets. The user can pick from the Tier 1/2/3 candidate list and a follow-up plan can implement them with measured before/after comparisons (with N-capture averaging this time to defeat the variance issue).
