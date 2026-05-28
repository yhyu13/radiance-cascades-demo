# M1 Stage 11c Plan - Light-Type Discriminator (first bake-side probe)

**Date:** 2026-05-28.
**Predecessor:** [v3_m1_stage11b_cornell_consumer_audit_impl.md](v3_m1_stage11b_cornell_consumer_audit_impl.md) (verdict `BAKE_UNDER_EMITS`).
**Goal:** falsify or confirm the "Cornell point light is poorly sampled by the cascade bake" hypothesis by re-running Cornell with a directional-style light and comparing the per-pixel + per-cell `cascade_gi / pt_gi` ratio. Cheapest of the three Stage 11b candidate bake-side probes.

## Why this probe first

Stage 11b confirmed Cornell cascade GI is ~50% of PT GI at the probe-atlas level. The bake is responsible. The three bake-side candidates from Stage 11b's "Improved next direction":

1. **Light-type discriminator** (this stage) — 1 new capture, no engine code changes. Quickest to run, broadest discrimination.
2. Bake-side energy probe — needs shader instrumentation OR atlas readback.
3. Per-bin α audit — needs shader instrumentation OR atlas readback.

Cornell uses a small point light at `lightPosition = (0, 0.8, 0)`. Sponza uses `--use-directional-light=true` with direction `(-0.3, -1, -0.4)` (set automatically on Sponza load — see `demo3d.cpp:7137`). Sponza per-pixel ratio at g=0.10 is 1.04 (well-calibrated); Cornell at g=1.0 is 0.49 (broken). One of the things that differs is the light type.

**Important architectural note (from `demo3d.h:545-549` code-read):** the engine's "directional light" mode is **not** a separate shader branch — it derives a far-away point light from `lightDirection` so the existing point-light bake/raymarch shaders naturally degenerate to directional behavior. So this probe discriminates between:

- **near-point light** (Cornell baseline: light at (0, 0.8, 0), one unit above the floor)
- **far-quasi-directional light** (synthesized: light at ~infinity along a unit vector)

NOT between "two structurally different shader paths". If the gap closes, the bug is geometry-dependent (probe rays don't statistically intersect a 1-unit-distant point light enough); if it doesn't, the light position is exonerated and Stage 11c moves to probe #2.

## Plan

### 1. New capture

`tools/v3_m1_cornell_light_type/captures_cornell_directional/m1stage11c_cornell_directional_N2048_m17.*`

Same flags as Stage 11b cam0 baseline, plus `--light-direction=0,-1,0` (straight down, mimicking the spatial direction Cornell's point light comes from). The CLI flag implicitly sets `useDirectionalLight=true` so the bake places a far-away point light along `(0, -1, 0)`.

```
--load-obj=cornell
--measurement-cameras-file=tools/v20_pre_measurement/cameras.json
--measurement-camera=0
--use-multi-bounce=1
--multi-bounce-gain=1.0
--use-hybrid=0
--cascade-scaled-dir-res=1
--noise-seed-offset=0
--use-probe-jitter=1
--use-directional-gi=1
--m1-delta3-gated-trilinear=0
--light-direction=0,-1,0
--render-mode=17
--screenshot-exr=1
--auto-capture-delay=0
--exit-frames=2048
--screenshot=m1stage11c_cornell_directional_N2048_m17.png
```

Uses the Stage 10 stdout-redirect pattern (no `Select-String` pipe).

### 2. Analysis

`tools/v3_m1_cornell_light_type/analyze_light_type.py` reuses Stage 11b's per-pixel + per-cell ratio code. Two variants compared:

| Variant tag                | source                                    | description |
|----------------------------|-------------------------------------------|---|
| `cornell_point_baseline`   | reuse `tools/v3_m1_source_energy_ab/captures_cornell_baseline/m1stage8_cornell_baseline_N2048_m17_*` | Cornell point light at (0, 0.8, 0) — Stage 11b baseline |
| `cornell_directional`      | new Stage 11c capture                     | Cornell directional light (0, -1, 0) |

For each: per-pixel ratio mean/median, per-cell weighted ratio, valid pixel count.

### 3. Decision rule

Define `bridged_fraction = 1 - (1 - new_ratio) / (1 - baseline_ratio)` measuring how much of the gap-to-1.0 is closed by the change.

| `cornell_directional` per-pixel mean | `bridged_fraction` | Verdict |
|---|---:|---|
| ≥ 0.90 | ≥ 80% | `LIGHT_TYPE_DOMINANT` — point-light bake under-sampling is the bug. Stage 11d: instrument bake's direct-light handling for point sources. |
| 0.75 ≤ x < 0.90 | 50% ≤ < 80% | `LIGHT_TYPE_MAJOR` — half the bug is light-type-related, half something else. Stage 11d: split probes. |
| 0.55 ≤ x < 0.75 | 12% ≤ < 50% | `LIGHT_TYPE_PARTIAL` — minor effect; light type contributes but not the primary cause. |
| x < 0.55 | < 12% | `LIGHT_TYPE_RULED_OUT` — bug is independent of light type. Move to next bake-side probe. |

### 4. Sanity cross-check

If `LIGHT_TYPE_DOMINANT` or `LIGHT_TYPE_MAJOR`, also check that the Cornell **green-wall asymmetry** (Stage 11b SC11) shrinks under directional light. Green-wall median ratio at baseline was 0.23; if it now matches red wall, that's additional evidence the asymmetry was point-light geometry interacting badly with the bin layout. If asymmetry persists, the green-wall problem is independent and gets a separate sub-investigation.

## Self-critique and improvements

### SC1 — Changing the light changes BOTH cascade AND PT signals

PT reference also reads `uUseDirectionalLight`. So `cornell_directional` compares cascade-with-directional against PT-with-directional. The *ratio* is still the right metric (both numerator and denominator update consistently), so this is fine — just want to be explicit. **Improvement**: report not just the ratio but also the absolute mean cascade_gi and pt_gi luma so the absolute change is visible. If cascade_gi jumped 3× and pt_gi jumped 1.5× (ratio improves but pt_gi changes too), that's a real signal.

### SC2 — Cornell baseline light might not be (0, 0.8, 0) by default

`setScene()` in `demo3d.cpp:4038` sets `lightPosition = (0, 0.8, 0)` for analytic scenes. But Cornell as loaded via `--load-obj=cornell` goes through `loadOBJMesh`, not `setScene`. The light position might be different (it might inherit a previous setting or default to a different constant). **Improvement**: before drawing conclusions, verify the Cornell baseline light setup by grep-checking the loadOBJMesh path for any `lightPosition` mutation, OR just dump the lightPosition in the new analyzer from the capture's log file (already redirected).

### SC3 — `--light-direction=0,-1,0` may interact with shadow rays differently

If the engine's shadow-ray code computes `toLight = lightWorld - hitPos` where `lightWorld` derives from `lightDirection` as a far-away point, then `toLight` is essentially `-lightDirection × large_number`. Shadow rays then march away from the surface in that direction. This is equivalent in principle but in practice the far-distance numerics might differ from the near point. **Improvement**: pick a moderately-far direction (e.g. multiply derived light position by a known scale factor). Per `demo3d.cpp:1379` code-read, the engine should handle this correctly, but if results are weird, sanity-check by trying multiple `--light-direction` magnitudes — except magnitude doesn't matter for directional (normalized in setter at line 559). OK no override needed; the engine handles direction normalization.

### SC4 — PT samples might be too few at N=2048 if directional changes light source convergence

Directional light is broader (every "up" direction sees it) so PT might converge faster, not slower. **Improvement**: skip the convergence ladder; N=2048 is well past the elbow for both light types based on Stage 0-9 ladders.

### SC5 — `--light-direction=0,-1,0` straight down may produce a different ratio than the Stage 11b baseline simply because the LIGHT GEOMETRY changes the GI distribution

Even if cascade and PT both handle directional correctly, the GI under directional light is DIFFERENT (different walls illuminated, different bounce paths) — so a ratio close to 1.0 under directional doesn't prove cascade handles directional perfectly; it just means cascade-directional matches PT-directional. It's still the right test (cascade-vs-PT ratio under the same light), but the *interpretation* needs care: "ratio improved" ≠ "directional light is intrinsically easier"; it can mean either. **Improvement**: report cascade and PT absolute luma changes separately (per SC1). If cascade_gi rises 2.5× and pt_gi rises 1.2×, that says cascade specifically gained more energy under directional — strong evidence for the point-light hypothesis.

### SC6 — Stage 11b cam0 baseline used `lightPosition = (0, 0.8, 0)` because Cornell goes through analytic SDF path

Wait — Cornell with `--load-obj=cornell` loads the `cornell_box.obj` mesh, not the analytic Cornell. So `setScene()` doesn't run for `--load-obj=cornell`, and `lightPosition` stays at whatever the constructor set or the last setScene set. **Improvement**: explicitly dump `lightPosition` from the Stage 11b baseline log (already on disk) AND the new Stage 11c log so the two-variant comparison is grounded in actual light positions. Or set `--light-position` explicitly in both variants for parity (CLI flag exists per Sponza's setup).

### SC7 — A "no change" result (LIGHT_TYPE_RULED_OUT) still has high value

Even a null result rules out one of the three Stage 11b candidates, narrowing Stage 11d to two remaining probes. **Improvement**: the impl doc should record what the null result means as crisply as a positive result — don't bury it.

### SC8 — One capture; if numbers look anomalous, can't tell signal from noise

A second confirmation capture (e.g. with `--noise-seed-offset=1`) would help. **Improvement**: SKIP unless the first capture's result is borderline (0.50 ≤ ratio ≤ 0.60). At those values, add a second seeded capture; otherwise one is enough.

### SC9 — Sponza uses --use-directional-light=true on load via demo3d.cpp:7137

Sponza is loaded with `useDirectionalLight=true` automatically. So Sponza's well-calibrated `ratio_self=1.04` was already measured under directional. This is part of why I expect light-type to matter: if cascade only works well under directional and is broken under point, switching Cornell to directional should fix it. **Improvement**: state this expectation explicitly in the impl doc verdict section; if directional doesn't fix Cornell, then Sponza's good calibration is despite NOT because-of directional, and the bug is something else (e.g. scene geometry interaction).

### SC10 — Hybrid (the oracle) is also under-emitting on Cornell

Stage 8 hybrid_on Cornell ratio_self = 0.83 — hybrid only reaches 83% of PT. So even hybrid has a Cornell-specific shortfall. Hybrid replaces cascade's *bounce-1* with exact MC and keeps cascade's *bounce-2+*. If the cascade's bounce-2+ is also under-emitted, hybrid can't fix that part. **Implication for this probe**: if Stage 11c's directional Cornell pushes cascade ratio above 0.83 (hybrid's level), that's interesting — it means the cascade-with-directional surpasses hybrid-with-point, suggesting hybrid's residual problem might also be point-light-geometry. If cascade-directional stays below 0.83, hybrid's shortfall is structurally separate.

## Acceptance

- `tools/v3_m1_cornell_light_type/light_type_results.json` records per-pixel + per-cell ratios for cornell_point_baseline (reused) and cornell_directional (new), plus absolute cascade_gi and pt_gi luma means.
- Bridged-fraction verdict per §3.
- Green-wall asymmetry comparison (Stage 11b SC11 cross-check).
- Impl doc records the verdict and the Stage 11d work-order update (which of the remaining probes to pursue, or whether the light-type win closes Stage 11c entirely).

## Out of scope

- Engine source changes (still measurement-only).
- Reviving Fork A (Stage 11a — independent track).
- Bake-side energy probe / per-bin α audit (later Stage 11d probes).
- Diag-rgb leak fix (Stage 11d, separate).
- True directional-light shader path (the engine doesn't have one — it's all point-light-at-distance internally).
- Cornell using a brighter point light or area light (different probe; left for later).
