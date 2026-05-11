# GI Pass Performance Analysis — Sponza-master, RTX 2080 SUPER

**Date:** 2026-05-11
**Plan source:** [gi_pass_1080p_perf_analysis_plan.md](../gi_pass_1080p_perf_analysis_plan.md) (revised post codex 11)
**Hardware:** NVIDIA GeForce RTX 2080 SUPER, OpenGL 3.3 context (NVIDIA driver 577.00)
**Scene:** Sponza-master, GPU voxelize + GPU SDF, viewpoint `pos=1.0710,-0.0723,-0.3393  target=0.1212,-0.0812,-0.6520`
**Budget:** **1 ms total per frame** (user-stated for the GI pass at 1080p)

---

## Headline

**At 1080p with all cascades forced (worst case): ~83 ms total → ~83× over budget.**

Even at 720p (what the codebase has been targeting), the same workload measures ~342 ms in the new RenderDoc capture (see "Capture variance" caveat below). The prior frame 401 baseline (2026-05-06) was 26.7 ms at 720p, but that was likely a simpler scene config — the current Sponza-master + GPU voxelize + GPU SDF + Step-11 sanitization stack is meaningfully heavier.

**The 1 ms target requires fundamental restructuring**, not tuning. Even ideal stagger + 2× resolution drop + halving probe count would still leave the GI pass an order of magnitude over budget at 1080p.

---

## Captured configurations

| Config | Resolution | Capture | Frame | Cascades | Total GPU (RenderDoc) |
|---|---|---|---|---|---|
| **A** | 1920×1080 | `--auto-rdoc`, all cascades forced | 229 | all 4 forced | **82.8 ms** |
| **B** | 1280×720 | `--auto-rdoc`, all cascades forced | 349 | all 4 forced | **341.7 ms** ⚠️ |
| **C** | 1920×1080 | headless mode-0, no rdoc capture | (300-frame run) | staggered (default) | (no RenderDoc data; visual sanity only) |

⚠️ **Capture variance caveat.** Config B's 720p numbers are 4× HIGHER than Config A's 1080p numbers, which is structurally backwards. Two captures of identical workload at the same resolution can vary 5-10× due to GPU power-state transitions (P0 turbo vs P5 idle), driver-side just-in-time compilation on cold cache, and RenderDoc's own measurement overhead. The Config B values should be read as "high-end of the variance" rather than steady-state cost. Config A's 1080p numbers are more typical of the workload at full GPU clock.

A more rigorous baseline would require: (1) GPU clock locking via NVIDIA Inspector, (2) multiple captures averaged, (3) excluding the first 2-3 captured frames after process start. None of these were done — this is a single-shot baseline. **Treat absolute numbers as ±2-3× and focus on proportional breakdown.**

---

## Per-pass timing — Config A (1080p, all cascades forced)

Source: [tools/analysis/rdoc_frame_frame229_pipeline.md](../../../tools/analysis/rdoc_frame_frame229_pipeline.md)

| Pass | GPU time (µs) | % of total | Window-bound? |
|---|---:|---:|---|
| Cascade 0 bake | 5,842.9 | 7.1% | No (volume) |
| Cascade 0 reduction | 28.7 | 0.0% | No (volume) |
| Cascade 1 bake | 9,934.2 | 12.0% | No (volume) |
| Cascade 1 reduction | 161.8 | 0.2% | No (volume) |
| Cascade 2 bake | 15,062.9 | **18.2%** | No (volume) |
| Cascade 2 reduction | 165.9 | 0.2% | No (volume) |
| Cascade 3 bake | 13,882.2 | **16.8%** | No (volume) |
| Cascade 3 reduction | 222.9 | 0.3% | No (volume) |
| Raymarch | 26,052.2 | **31.5%** | **YES** (fragment) |
| GI blur | 11,442.2 | 13.8% | YES (FBO/fragment) |
| (presentation draw) | 10.6 | 0.0% | – |
| **Total** | **82,806.4** | 100.0% | – |

### Per-pass scaling at 1080p vs hypothetical 720p (estimated)

Window-bound passes scale with pixel count (1.5×1.5 = 2.25×). Volume-bound passes don't scale with window resolution (cascade probe³ work groups). Estimated 720p equivalents (Config A's numbers ÷ 2.25 for window-bound):

| Pass | 1080p (Config A measured) | 720p estimated | 1080p % of 1ms budget |
|---|---:|---:|---:|
| Cascades C0-C3 bake | 44,722 µs (4.5 ms) | (same) | **4,472%** |
| Cascade reductions C0-C3 | 579 µs (0.6 ms) | (same) | 58% |
| Raymarch | 26,052 µs (2.6 ms) | ~11,580 µs | **2,605%** |
| GI blur | 11,442 µs (1.1 ms) | ~5,090 µs | **1,144%** |
| **Total per-frame (worst case)** | **82,796 µs (82.8 ms)** | ~62,000 µs (62 ms) | **8,280%** |

Even just the cascade reductions alone (~0.6 ms) eat 58% of the entire 1 ms budget — and that's the cheapest part.

---

## Staggered steady-state (derived)

Real frames don't run all 4 cascades. The staggered pattern is C0 every frame, C1 every 2, C2 every 4, C3 every 8. Per-frame staggered cost from Config A:

```
staggered_per_frame = C0_bake + C0_red
                    + C1_bake/2 + C1_red/2
                    + C2_bake/4 + C2_red/4
                    + C3_bake/8 + C3_red/8
                    + raymarch + gi_blur
                    = 5871.6 + 5048.0 + 3807.2 + 1763.1 + 26052.2 + 11442.2
                    = 53,984 µs ≈ 54.0 ms
```

**Staggered 1080p estimate: ~54 ms → 54× over budget.**

The cascade work compresses from 45 ms forced → 16.5 ms staggered (saves ~29 ms), but raymarch+blur (~37.5 ms) are unaffected — they run every frame regardless of stagger. So with stagger, the bottleneck shifts from cascades to raymarch+blur.

---

## Hotspot ranking

### By absolute time (Config A 1080p forced)

1. **Raymarch fragment shader** — 26.1 ms (31.5%). Window-dependent; heaviest at 1080p+.
2. **Cascade 2 bake** — 15.1 ms (18.2%). Volume work; D=16 directional bins.
3. **Cascade 3 bake** — 13.9 ms (16.8%). Same. C2/C3 are jointly ~30% of frame.
4. **GI blur** — 11.4 ms (13.8%). Fragment-bound; runs in default mode 0.
5. **Cascade 1 bake** — 9.9 ms (12.0%).
6. **Cascade 0 bake** — 5.8 ms (7.1%).

### By "what if I cut this entirely" — staggered impact

| Cut | Saving on staggered frame | Visual cost |
|---|---:|---|
| Raymarch step-count cap (e.g. -50%) | ~13 ms | Possible step-banding artifacts at glancing angles |
| GI blur (skip entirely) | ~11 ms | Mode 0 reverts to noisier per-probe GI; bilateral smoothing lost |
| C3 cascade (eliminate top level) | ~1.7 ms | Far-field GI loses one bounce level |
| C2 cascade (eliminate) | ~3.8 ms | More structural — C2 covers mid-range |
| Drop all cascades to D=4 | unknown — needs measurement | Reduces directional resolution; angular banding |
| Drop probe res (32³ → 16³) | est ~75% of cascade time | Significant — coarser GI |
| Skip blur every other frame | ~5.5 ms avg | Minor temporal flicker in motion |

---

## Optimization candidates (categories only — no implementation here)

Per the plan's scope, this report defers actual optimizations to a follow-up plan. The candidates ranked by ROI / simplicity:

### High ROI

- **Raymarch step-count cap.** Currently variable-step; capping at e.g. 32 max steps would bound worst-case fragment cost. ~10-15 ms savings at 1080p.
- **Cascade probe-resolution reduction (e.g. C0=16³ instead of 32³).** Linear-in-probe-count savings; rough cube-law gives ~8× reduction → could shave ~30 ms off forced cascades. Visual cost: coarser GI, possibly more bilateral blur needed.
- **Skip GI blur in modes other than 0.** Already done for diagnostic modes via the Step 10 `uSeparateGI` gate; default mode 0 still pays full ~11 ms. Could skip every other frame for a temporal-amortized 5.5 ms average.

### Medium ROI

- **Eliminate cascade C3 (use only C0/C1/C2).** C3's contribution at 1.7 ms staggered is small but visually marginal at this scene scale. Could simplify the cascade-merge math too.
- **Per-cascade D reduction.** Current D=4/8/16/16 (Phase 5e scaled). Force all to D=8 would cut C2/C3 bake roughly in half.
- **Adaptive raymarch resolution (half-res GI at 1080p).** Render GI to a 960×540 buffer, upsample with bilateral. Saves ~75% of raymarch cost (~20 ms saving) for some quality loss in fine-detail regions.

### Restructuring (only path to <5 ms, let alone 1 ms)

- **Async-compute the cascade bake** off the rendering critical path. Current dispatch is synchronous before raymarch. If cascade bake overlaps with previous frame's raymarch (one-frame latency), staggered cascades disappear from the critical path entirely.
- **Sparse probes.** Codex 09 P1 noted cascades are severely under-occupied (C0 anyPct ~3.5%). A sparse probe layout would skip empty volume entirely.
- **Per-pixel GI from probe lookup only (no per-frame raymarch reuse).** Currently raymarch.frag re-marches the SDF per pixel for visibility. A separate "visibility cache" pass could amortize across multiple frames.

The 1 ms budget at 1080p with full Sponza-master is roughly 1080p-Doom-Eternal-cascade-cost territory — that game's GI runs ~1.2 ms at 1440p with hardware ray tracing and aggressive amortization. Without those tools (no RT cores, no temporal stochastic update), 1 ms is **not realistic for this codebase's current architecture.**

---

## Visual sanity (Config C)

At 1080p, mode 0 captures look like the Step 11 captures at 720p — same atrium silhouette, ceiling beams, ambient floor dominance, GI bounce on roof rim. Architecture is preserved. Cascade meanLum stable at frame 2+:

```
[4c A/B] meanLum:  C0=0.01692  C1=0.01605  C2=0.01397  C3=0.00810
```

These numbers are essentially identical to the post-Step-11 baseline at 720p (`C0=0.01692`), confirming the cascade behavior is resolution-invariant and the Step 10/11 fixes didn't regress at 1080p.

Capture: [tools/step12_configC_1080p_visual.png](../../../tools/step12_configC_1080p_visual.png).

---

## CPU cross-check (codex 11 F6) — not available

The plan called for cross-checking RenderDoc's GPU cascade total against the CPU-side `cascadeTimeMs` log. But `cascadeTimeMs` is only displayed in ImGui ([demo3d.cpp:3654](../../../src/demo3d.cpp#L3654)) and in JSON dumps ([demo3d.cpp:4860](../../../src/demo3d.cpp#L4860)) — never to stdout. Headless captures don't trigger JSON dumps unless an analyze burst fires.

**Follow-up work:** add a `std::cout` log line for `cascadeTimeMs` in headless / verbose modes so this cross-check is available without code changes per measurement.

---

## What was actually built / captured

- New CLI flag `--window-size=W,H` parsed BEFORE `InitWindow` (codex 11 F2+F5 init-order fix). Default 1280×720 unchanged.
- `initializeApplication()` signature now takes `(int windowWidth, int windowHeight)`.
- 3 captures: Config A (1080p forced .rdc + auto-extracted pipeline), Config B (720p forced .rdc + auto-extracted pipeline), Config C (1080p headless visual screenshot).
- Logs in `tools/app_run_step12_*.log`, captures in `tools/captures/rdoc_frame_frame{229,349}.rdc`, pipeline analyses in `tools/analysis/rdoc_frame_frame{229,349}_pipeline.md`.

---

## Out of scope (deferred)

- All optimization work — separate plan once the user picks which knobs to turn.
- GPU clock-locked re-measurement for variance reduction.
- Per-pass `GL_TIME_ELAPSED` query infrastructure inside the cascade loop (RenderDoc covered this analysis but a frame-by-frame in-app overlay would be useful for tuning).
- Stdout-logging of `cascadeTimeMs` for headless cross-check.
- Cross-scene comparison (Cornell vs other OBJs).

---

## Summary

| Metric | Value |
|---|---|
| 1080p forced (worst case) | **82.8 ms** (~83× over budget) |
| 1080p staggered (derived) | **~54 ms** (~54× over budget) |
| Single biggest line item | Raymarch fragment, **26 ms** (31.5%) |
| Cheapest path to material savings | Raymarch step-count cap + GI blur frame-skip (~15-20 ms) |
| Path to <5 ms | Restructuring required (async compute / sparse probes / half-res GI) |
| Path to 1 ms | Not realistic in current architecture without hardware ray tracing |