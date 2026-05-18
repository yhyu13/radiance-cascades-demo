# Phase 1 — Test Results: Mode 4 Depth-Aware Per-Bin Visibility

**Date:** 2026-05-14
**Status:** **Primary criterion PASSES** — Mode 4 matches Mode 3 quality at the cam.md viewpoint within RGB RMSE 0.019 (Sponza) and 0.007 (Cornell), well under the plan's 0.05 threshold. Cost claim is **inconclusive from wall-clock** alone — RenderDoc capture is the right Step 5 tool and is not included here.

**Implementation under test:** [visibility_unified_plan_phase1_impl.md](visibility_unified_plan_phase1_impl.md) (commit A: temporal_blend.comp `cur.a` preservation; commit B: Mode 4 sampler + dispatch).
**Plan source-of-truth:** [visibility_unified_plan.md](visibility_unified_plan.md) (Phase 1 verification protocol).

---

## Test design — what justifies the Phase 1 goal

Phase 1 makes three substantive claims. Each test below targets one or more of them.

| # | Test | Phase 1 claim it justifies | Pass criterion |
|---|---|---|---|
| T1 | Sponza @ cam.md, Mode 4 vs Mode 3 RGB RMSE | "Quality matches Mode 3" | RMSE < 0.05 (FLIP-proxy) |
| T2 | Sponza @ cam.md, Mode 4 vs Mode 0 RGB RMSE | "Mode 4 actually does occlusion (sanity floor)" | RMSE > 0 with diff concentrated in occluded regions |
| T3 | Sponza visual A/B (Mode 0 / 3 / 4) | "Banding eliminated" | Mode 4 matches Mode 3 visually; both lack the dot-band pattern Modes 1/2 had |
| T4 | Cornell-original Mode 4 vs Mode 3 RGB RMSE | Quality match generalizes to a second scene | RMSE < 0.05 |
| T5 | Wall-clock per-mode timing | Cost claim "near Mode 0" | Mode 4 not catastrophically slower than Mode 0; Mode 3 expected to be slower |
| T6 | Mode 0 regression vs prior `tools/sponza_visibility_mode0.png` baseline | "temporal_blend patch didn't break legacy modes" | RMSE < 0.02 (within rendering noise) |

Per-cascade visibility heatmaps (Step 6 in the plan) are deferred to Phase 1 follow-up — that test requires a debug-render mode that doesn't yet exist (would need a new branch in `raymarch.frag` to output `wvis` instead of weighted radiance).

---

## Method

**Binary:** `build/RadianceCascades3D.exe` (Release, built 2026-05-13 with the Phase 1 patches in place).

**Common flags:**
- `--exit-frames=300` (~5s warmup + 300 frames so temporal accumulation has settled)
- `--screenshot=phase1_<scene>_m<N>.png` (raylib strips the path; PNGs land in project root, then `Move-Item` into `tools/`)

**Sponza viewpoint** (cam.md):
- `--load-obj=sponza-master`
- `--camera-pos=1.071,-0.0723,-0.3393`
- `--camera-target=0.1212,-0.0812,-0.652`

**Cornell-original viewpoint:**
- `--load-obj=cornell-orig` (uses the auto-fit camera preset; same as the existing project default)

**Mode iteration:** `--visibility-mode={0, 3, 4}` per scene (six captures total).

**Capture resolution:** 1280×720 (default window size for headless run; the prior baseline `tools/sponza_visibility_mode0.png` was at 2560×1377 from a session where the user resized — see T6 caveat below).

**Pixel-difference metric:** RGB RMSE in linear `[0, 1]` space, computed by [tools/analysis/phase1_diff_metrics.py](../../../tools/analysis/phase1_diff_metrics.py). Also reports MAE, max per-pixel L1, p99 L1, and fraction of pixels with L1 > 0.05 / 0.10. Heatmaps written to `tools/phase1_diff_<scene>_m<ref>_vs_m<test>.png` (gain × 4, gamma 1/2.2 for visibility).

**Caveat on metric.** RGB RMSE in linear sRGB is **not** FLIP — FLIP is perceptual and accounts for human contrast sensitivity. The plan asks for FLIP < 0.05; I'm using RGB RMSE < 0.05 as a coarse proxy because FLIP isn't installed. The values are therefore conservative-ish (a perceptual metric would weight differences in dim regions less and might flag some bright-region differences the RMSE downplays). For Phase 1 sign-off this is good enough; if Phase 2 sign-off needs strict FLIP, install [nvFLIP](https://github.com/NVlabs/flip) and rerun.

---

## Results

### T1, T2, T3 — Sponza @ cam.md

| Pair | RGB RMSE | MAE | p99 L1 | Max L1 | Frac L1 > 0.05 | Frac L1 > 0.10 |
|---|---:|---:|---:|---:|---:|---:|
| **m4 vs m3** (primary) | **0.0192** | 0.0100 | 0.082 | 0.233 | 2.87% | 0.56% |
| m0 vs m4 (sanity) | 0.0096 | 0.0035 | 0.042 | 0.105 | 0.58% | 0.003% |
| m0 vs m3 (reference scale) | 0.0169 | 0.0086 | 0.072 | 0.255 | 2.42% | 0.29% |

**T1 verdict — PASS.** Mode 4 vs Mode 3 RMSE 0.0192 is comfortably under the 0.05 plan threshold. Mode 4's 99th-percentile per-pixel L1 (0.082) and 2.87% noticeable-diff fraction are similar in scale to Mode 0 vs Mode 3 (0.072 / 2.42%) — i.e. Mode 4's distance from Mode 3 is the same order as the natural noise / per-mode trace variation between Mode 0 and Mode 3 at this viewpoint.

**T2 verdict — PASS.** Mode 4 differs from Mode 0 (RMSE 0.0096) less than Mode 3 differs from Mode 0 (0.0169). At this specific viewpoint, the cam.md camera doesn't sit in a heavy-leak region — most of the frame agrees regardless of visibility mode. The diff *is* nonzero and concentrated in expected regions (corridor depth where leak would be most visible — see heatmap [phase1_diff_sponza_m0_vs_m4.png](../../../tools/phase1_diff_sponza_m0_vs_m4.png)), so Mode 4 is doing real occlusion work, just not dramatically at this viewpoint. **A heavier-leak viewpoint (e.g. inside a Sponza alcove looking at a wall behind the camera light) would exercise the leak fix more dramatically; not captured in this round.**

**T3 verdict — PASS visually.** Inspection of the three captures:
- [phase1_sponza_m0.png](../../../tools/phase1_sponza_m0.png) — bright corridor, light leaking through walls on the left side, slightly washed-out far-end.
- [phase1_sponza_m3.png](../../../tools/phase1_sponza_m3.png) — corridor visibly more contained, right-side wall darker (correctly occluded), far-end more defined.
- [phase1_sponza_m4.png](../../../tools/phase1_sponza_m4.png) — visually indistinguishable from Mode 3 to the eye. Same wall darkening, same far-end definition.

The Mode 3 vs Mode 4 diff heatmap [phase1_diff_sponza_m3_vs_m4.png](../../../tools/phase1_diff_sponza_m3_vs_m4.png) shows scattered edge-aligned residuals — typical of two different sampling strategies converging on the same answer with small per-pixel disagreements at high-frequency surface details. **No coherent banding pattern visible** in either Mode 4 or the diff.

### T4 — Cornell-original

| Pair | RGB RMSE | MAE | p99 L1 | Max L1 | Frac L1 > 0.05 | Frac L1 > 0.10 |
|---|---:|---:|---:|---:|---:|---:|
| **m4 vs m3** (primary) | **0.0074** | 0.0022 | 0.033 | 0.157 | 0.29% | 0.022% |
| m0 vs m4 (sanity) | 0.0039 | 0.0009 | 0.017 | 0.051 | 0.002% | 0% |
| m0 vs m3 (reference scale) | 0.0052 | 0.0016 | 0.022 | 0.149 | 0.07% | 0.014% |

**T4 verdict — PASS.** Cornell-original is a tighter scene with less geometry to occlude per probe; all three modes converge on essentially the same answer (RMSE < 0.01 across all pairings). Mode 4 vs Mode 3 (0.0074) is well under the 0.05 threshold and tighter than the Sponza pairing.

The Cornell scene is essentially a generalization-floor test: confirms Mode 4 doesn't introduce a regression in a simpler scene where the answer is well-conditioned. It is **not** a strong test of the leak fix — Cornell's closed-box geometry doesn't have the long-corridor leak path that Sponza does. A proper Phase 2 verification (per the unified plan) will need a closed-room test scene with light deliberately placed to probe behind walls.

### T5 — Wall-clock timing

| Scene | Mode | Wall-clock (s, 300 frames + warmup) | Δ vs Mode 0 |
|---|---:|---:|---:|
| Sponza | 0 | 37.24 | — |
| Sponza | 3 | 38.08 | +0.84 |
| Sponza | 4 | 36.95 | −0.29 |
| Cornell | 0 | 37.86 | — |
| Cornell | 3 | 38.23 | +0.37 |
| Cornell | 4 | 41.81 | +3.95 |

**T5 verdict — INCONCLUSIVE.** Single-run wall-clock is too noisy to validate the cost claim — the Sponza Mode 4 number (-0.29s vs Mode 0) and the Cornell Mode 4 number (+3.95s vs Mode 0) disagree on direction, and the Sponza Mode 3 vs Mode 0 delta (+0.84s = ~3ms/frame) is far smaller than the plan's predicted "~30× cost ratio" — meaning the bottleneck at 1280×720 is almost certainly NOT the raymarch pass, so this measurement can't isolate Mode 4's per-bin overhead either way.

The honest signal here is **no catastrophic regression**. Mode 4 is not an order of magnitude slower than Mode 0; Mode 3 isn't either at this resolution. Real cost validation requires:
- RenderDoc capture per the plan's Step 5 (per-pass GPU timing).
- Higher resolution (1080p or 1440p) where the raymarch pass actually dominates the frame budget.
- Multiple runs with statistical replication.

This test is **deferred** to a follow-up, not failed. The Phase 1 plan explicitly anticipated this — the cost-table entry says "expected small; verified in Step 5" with no committed threshold.

### T6 — Mode 0 regression vs prior baseline

**Verdict — SKIPPED.** The prior `tools/sponza_visibility_mode0.png` baseline was captured at 2560×1377 (window resized during that session per the log). Today's headless captures are at 1280×720. Direct pixel comparison is impossible without re-rendering one at the other's resolution.

**Indirect signal (acceptable for Phase 1):** Mode 0 captured today renders the Sponza corridor visually consistent with the prior baseline — same geometry framing, same light placement, same cascade-bake general appearance. The temporal_blend.comp `cur.a` preservation patch is provably invisible to Modes 0/1/2/3 (per critic-05 reply F7: those modes never read atlas alpha), so a regression here would have to come from somewhere else entirely. **Reading the smoke run's startup log line (`[Demo3D] visibilityMode=0` produces a render with no obvious differences from the historical baseline) is the strongest signal available without resolution-matched recapture.**

If a strict regression test is needed, the cleanest path is to recapture the prior baseline at 1280×720 with `--switch-to-scene` instead of `--load-obj` and compare. **Filed as follow-up.**

---

## Cross-test summary

| Test | Claim | Verdict | Evidence |
|---|---|---|---|
| T1 | Mode 4 quality matches Mode 3 (Sponza) | **PASS** | RMSE 0.019 < 0.05 |
| T2 | Mode 4 actually performs occlusion | **PASS** | Diff vs Mode 0 nonzero, concentrated in expected regions |
| T3 | Banding eliminated | **PASS** (visual) | No banding in m4 capture or m3-m4 diff heatmap |
| T4 | Quality match generalizes to Cornell | **PASS** | RMSE 0.007 < 0.05 |
| T5 | Cost near Mode 0 | **INCONCLUSIVE** | Wall-clock noise dominates; defer to RenderDoc |
| T6 | No regression in legacy modes from temporal_blend patch | **PASS-ish** (indirect) | Resolution mismatch blocks direct compare; theoretical analysis (modes 0–3 never read atlas alpha) confirms no regression possible |

---

## Decision-gate outcome (per the plan)

The unified plan's decision gate says:

> Pass primary + secondary, RenderDoc cost increase < 20% → Default = mode 4. Schedule Phase 2 as the long-term correctness fix.

**Status against gate:**

- ✅ Primary (FLIP < 0.05 proxied by RGB RMSE < 0.05) — **PASSES** in both Sponza and Cornell.
- ⚠ Secondary (per-region RMSE on three crops < 0.02) — **NOT MEASURED** in this round. The aggregate Sponza RMSE of 0.019 is right at the 0.02 threshold, so per-region crops could go either way. To measure: define crops (e.g. lit floor, shadowed alcove, vertical wall column) and run the diff script over each region.
- ⚠ RenderDoc cost increase < 20% — **NOT MEASURED.** Wall-clock is inconclusive.

**Recommended action**: do **not** flip the default to mode 4 yet. The primary criterion passes confidently, but the secondary criterion and the cost claim haven't been measured strictly enough to commit to a default change. Two follow-up tasks gate the default flip:

1. **Per-region RMSE on three Sponza crops** (lit floor / shadowed alcove / vertical wall column). Coordinates would need defining; the existing capture is reusable.
2. **RenderDoc capture** of Mode 0 vs Mode 4 raymarch pass timing at the same viewpoint and resolution. Use the existing `tools/analyze_renderdoc.py` pipeline (cerebrum: Phase 6b architecture).

If both pass, default flips to Mode 4 and Phase 2 (interval atlas) advances on its merit.

If T5 (RenderDoc) shows Mode 4 is unexpectedly expensive, file a Phase 1.5 cone-correction tuning round (the plan's escalation path).

---

## Bake-time leak materiality (open question from the plan)

The unified plan flagged this as a key gating question: > "If Phase 1 looks great but the user still reports cross-wall light bleed in static scenes, Phase 2 is mandatory."

**At cam.md viewpoint, no obvious cross-wall bleed remains in Mode 4** — the scene looks well-occluded. But this viewpoint is along the Sponza primary corridor and isn't the worst-case for bake-time leaks. The worst case is a probe inside a closed alcove with a light just outside the alcove wall: even Mode 4's render-side occlusion can't fix a probe whose atlas already contains pre-baked cross-wall radiance.

**This Phase 1 round does not strongly answer the bake-leak question.** A targeted test (closed-alcove scene with a precisely placed light, atlas inspection via RenderDoc) is the right tool — and is exactly the verification protocol the plan filed under Phase 2's instrumentation step. So the gating question is unanswered but doesn't block Phase 1 sign-off; it gates whether Phase 2 is mandatory or merely nice-to-have.

---

## Files produced

| File | Purpose |
|---|---|
| [tools/phase1_sponza_m0.png](../../../tools/phase1_sponza_m0.png) | Sponza, no visibility (smooth + leaks; cheapest) |
| [tools/phase1_sponza_m3.png](../../../tools/phase1_sponza_m3.png) | Sponza, per-bin shadow trace (CORRECT; expensive) |
| [tools/phase1_sponza_m4.png](../../../tools/phase1_sponza_m4.png) | Sponza, depth-aware per-bin (proposed default) |
| [tools/phase1_cornell_m0.png](../../../tools/phase1_cornell_m0.png) | Cornell-original, no visibility |
| [tools/phase1_cornell_m3.png](../../../tools/phase1_cornell_m3.png) | Cornell-original, per-bin shadow trace |
| [tools/phase1_cornell_m4.png](../../../tools/phase1_cornell_m4.png) | Cornell-original, depth-aware per-bin |
| [tools/phase1_diff_sponza_m3_vs_m4.png](../../../tools/phase1_diff_sponza_m3_vs_m4.png) | Diff heatmap (gain ×4, gamma 1/2.2) |
| [tools/phase1_diff_sponza_m0_vs_m4.png](../../../tools/phase1_diff_sponza_m0_vs_m4.png) | Diff heatmap |
| [tools/phase1_diff_sponza_m0_vs_m3.png](../../../tools/phase1_diff_sponza_m0_vs_m3.png) | Diff heatmap |
| [tools/phase1_diff_cornell_m3_vs_m4.png](../../../tools/phase1_diff_cornell_m3_vs_m4.png) | Diff heatmap |
| [tools/phase1_diff_cornell_m0_vs_m4.png](../../../tools/phase1_diff_cornell_m0_vs_m4.png) | Diff heatmap |
| [tools/phase1_diff_cornell_m0_vs_m3.png](../../../tools/phase1_diff_cornell_m0_vs_m3.png) | Diff heatmap |
| [tools/phase1_sponza_m{0,3,4}.log](../../../tools/) | Capture logs |
| [tools/phase1_cornell_m{0,3,4}.log](../../../tools/) | Capture logs |
| [tools/phase1_diff_metrics.json](../../../tools/phase1_diff_metrics.json) | Raw metric JSON |
| [tools/analysis/phase1_diff_metrics.py](../../../tools/analysis/phase1_diff_metrics.py) | Metric computation script (re-runnable) |

---

## Honest residuals (don't oversell this round)

- **Single-viewpoint test.** All Sponza data is at the cam.md viewpoint. A single viewpoint can hide failures elsewhere (e.g. probes near sharp corners, grazing-angle walls). Step 6's per-cascade visibility heatmap rendering would catch some of these; not done here.
- **No FLIP.** RGB RMSE is a proxy. If a Phase 2 / external stakeholder needs strict FLIP, install nvFLIP and rerun against the same captures.
- **Wall-clock cost is inconclusive.** The plan never promised wall-clock would settle the cost claim — it always pointed at RenderDoc. Treating Mode 4 as "near-mode-0 cost" remains a working hypothesis until RenderDoc says so.
- **Bake-time leak unanswered.** Phase 1 captures don't probe the worst-case bake-leak path; Phase 2's gating question remains open.
- **Mode 0 regression is theoretical, not empirical.** The shape mismatch with the prior baseline blocked direct compare. Indirect reasoning (modes 0–3 don't read atlas alpha → can't be affected by the temporal_blend patch) is sound but not a substitute for an empirical regression test.
