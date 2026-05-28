# M1 Stage 11d Plan - Light Distance/Intensity Ladder (refining Stage 11c Hypothesis A)

**Date:** 2026-05-28.
**Predecessor:** [v3_m1_stage11c_light_type_discriminator_impl.md](v3_m1_stage11c_light_type_discriminator_impl.md) (verdict `LIGHT_TYPE_DOMINANT`).

**Hypothesis-tree update from Stage 11c (code-read result):** Stage 11c's "Hypothesis A — probe rays under-sample the small point light" was **falsified by code-read**. The cascade bake (`radiance_3d.comp:535-543`) does explicit per-probe-ray-hit direct-light evaluation: `lightDir = normalize(uLightPos - pos); diff = max(dot(n, lightDir), 0) * (1 - inShadow(...)); color = albedo * (diff * uLightColor + ambient)`. PT's direct-light evaluation (`pt_reference.comp:272-285`) is mathematically equivalent. So probe rays do not "miss the lit area" — every probe-ray surface hit gets its own direct visibility computation. The 2× under-emit must come from somewhere else.

**Revised hypothesis (now H-A'):** the under-emit is in cascade's **multi-bounce chain**. Cornell point light produces strong multi-bounce GI (closed box, point source illuminates everything; surfaces bounce energy onto every other surface). PT captures multi-bounce fully via cosine-sampled hemisphere paths. Cascade captures multi-bounce via `sampleC0AtlasStochastic` (single stochastic atlas sample per bake hit, temporally accumulated), and that path may under-converge or have a normalization bug specific to scenes where MB is the dominant indirect-energy term. Under directional Cornell, MB matters less (side walls aren't directly lit; bounce-2 is dim regardless), so cascade-vs-PT gap shrinks.

**Stage 11d goal:** test H-A' by varying the **light distance** (point light moved progressively farther from the scene). This is a continuous interpolation between "near point" (Stage 11c baseline) and "far point" (Stage 11c directional). If cascade ratio improves monotonically with distance, H-A' is consistent. If it jumps discretely or doesn't track distance, look elsewhere.

## Plan

### 1. Light-distance ladder (Cornell, single camera, hybrid=0, gain=1.0)

Override `lightPosition` via the existing `--light-position=x,y,z` CLI (verify it exists; if not, use `--light-direction=` for the far-distance cases since that derives `effLightPos` at distance 100). Six distance variants — start with the existing baseline (0, 0.8, 0) and move the light progressively higher:

| Tag       | `lightPosition` or `lightDirection` | effective distance from floor center | Notes |
|-----------|---|---:|---|
| `near_baseline`  | (0, 0.8, 0)            | 1.8 (inside box) | Stage 11b/c baseline; reuse |
| `mid_0p8_x4`     | (0, 3.2, 0) — 4× higher | 4.2 | inside-box → near-box-roof boundary |
| `far_5`          | (0, 5, 0)              | 6.0 | outside box, modest distance |
| `far_25`         | (0, 25, 0)             | 26 | far point, close to directional limit |
| `directional`    | `--light-direction=0,-1,0` (effLightPos ≈ (0, 100, 0)) | ~101 | Stage 11c capture; reuse |

5 distance points; 2 already captured (`near_baseline` reused from Stage 11b cornell_baseline, `directional` reused from Stage 11c). **3 new captures (~9 min).**

### 2. Metric

For each variant, the existing Stage 11b/11c analyzer signature: per-pixel `cascade_gi / pt_gi` mean, per-cell weighted ratio, absolute cascade luma, absolute pt_gi luma, **cascade vs PT change asymmetry vs baseline** (the cleanest Stage 11c signal — measures whether cascade gains more or less than PT when the light moves).

### 3. Decision rule

- **Monotonic improvement** (ratio rises smoothly from 0.49 → 0.93 as distance increases): supports H-A' (multi-bounce concentration depends on light-source proximity).
- **Discrete jump** (ratio stays at 0.5 then jumps to 0.93 once distance exceeds some threshold): suggests a code path that triggers at a specific distance (e.g. a clamp).
- **Non-monotonic / noisy**: light distance alone isn't the discriminator; reframe.

### 4. Cross-check: PT absolute luma vs distance

Under directional, PT_gi dropped 0.17× vs baseline (Stage 11c). If distance affects PT_gi smoothly, the inverse-square-ish falloff of light reaching surfaces should produce monotonic PT_gi decay with distance. If PT_gi shape is *not* monotonic, the engine has a non-standard light-falloff behavior (Stage 11c note: "NO 1/r² falloff" in pt_reference.comp:273 — so PT is unbiased by distance for light *brightness* but distance still affects geometry of which surfaces are lit).

### 5. Output

`tools/v3_m1_cornell_light_distance/distance_ladder_results.json` per-variant metrics + verdict.

## Self-critique and improvements

### SC1 — `--light-position` may not exist as a CLI flag

Quickly check `src/main3d.cpp` for `--light-position=`. If absent, two options: (a) add it (~5 lines, similar to Stage 11c's plumbing fix), or (b) use `--light-direction=` for all variants by encoding distance as `(0, -1, 0) * scale`, but the engine's directional path hardcodes distance to 100, so this doesn't actually vary distance. **Improvement:** if the CLI doesn't exist, add it as a 5-line block in `main3d.cpp` (same pattern as Stage 11c `--light-direction=`, including post-load re-apply).

### SC2 — Cornell baseline `lightPosition = (0, 0.8, 0)` may not actually be the OBJ-load default

Stage 11b cornell baseline was the engine default after `--load-obj=cornell`. Cornell OBJ load doesn't go through `setScene` (which sets lightPosition for analytic scenes), so `lightPosition` for Cornell-OBJ depends on the constructor default OR any prior setting. **Improvement:** explicitly set `--light-position=0,0.8,0` in the `near_baseline` re-capture to lock the baseline at a known light position. Don't rely on whatever was the default.

### SC3 — Moving the light outside the SDF volume may change shadow-ray behavior

For `far_5`, `far_25`, the light is above the Cornell box (which is inside the SDF volume). Shadow rays cast from a floor point traverse free space, exit the volume (sampleSDF returns INF), and the loop terminates. cascade's `inShadow` falls through to `return false` (correct: "not in shadow" because the ray exited the volume without hitting anything). PT's `isDirectlyLit` explicitly handles INF by returning `true` (correct). Both should agree. **Improvement:** verify visually that cascade shadow on the floor is consistent across distances (no sudden black bands suggesting shadow path break).

### SC4 — `inShadow` 32-step budget may exhaust at moderate distances

For `mid_0p8_x4` (light at y=3.2, floor at y=-1, distance = 4.2), shadow ray of length 4.2 must complete in 32 sphere-tracing steps. Each step is the SDF (up to ~1m in free Cornell). 32 × 1m = 32m capacity, plenty. **Improvement:** monitor the analyzer for any anomalous discontinuity; if seen, instrument with shadow-step debug.

### SC5 — Confounding factor: light intensity scales with distance²-equivalent in PT

`pt_reference.comp:273` comment: "NO 1/r² falloff — matches cascade renderer's infinite-reach shading (H2)". So both PT and cascade use intensity-independent-of-distance lighting. Then moving the light farther does NOT reduce brightness at the floor (no 1/r²). Only the *geometry* of "which surfaces see the light directly" changes. **Improvement:** this simplifies interpretation — any cascade-vs-PT asymmetry that emerges from distance is purely about which surfaces are direct-lit vs indirectly-lit, not about brightness scaling.

### SC6 — Multi-bounce chain in cascade may not exhibit monotonic distance behavior

H-A' predicts a smooth monotonic improvement. But cascade MB is a stochastic temporal accumulator. At each distance, it converges to a different equilibrium that depends on bake-side energy distribution. There's no analytical reason MB convergence rate should scale smoothly with light distance. **Improvement:** if the data is noisy/non-monotonic, that's also an informative signal — flagging MB as the source even more strongly because of its stochastic nature.

### SC7 — Hypothesis H-A' competes with simpler alternatives I haven't tested

- **H-B':** the bake has a normalization off by 2 that activates when a specific subset of probe-ray hits succeed. Possible but I have no direct evidence.
- **H-C':** the cascade's atlas alpha gating (α convention) eats half the energy on Cornell. Stage 11b consumer code-read showed `Σ(L·wcos·α) / Σ(wcos·α)` — if α is ~0.5 across Cornell bins, the denominator is half, the result is unchanged (since both numerator and denominator are scaled). So α can't produce a 2× under-emit at the consumer. But it could at the BAKE stage if the bake writes α=0.5 for hits that should be α=1.0.

**Improvement:** this stage doesn't test H-B' or H-C' directly, but if H-A' is supported by the distance ladder, those can be ruled out. If H-A' isn't supported, Stage 11e moves to H-C' (atlas alpha audit) or a true bake instrumentation.

### SC8 — Three new captures × 3 min = 9 min — acceptable

No SC9-10 concerns this round; the test is fast.

### SC9 — Sponza is not retested in this stage

Sponza uses directional light by default. Sponza ratio at gain=0.10 is 1.04 (Stage 9). If H-A' is right, Sponza-with-point-light should regress. We don't test Sponza here because the Sponza point-light test would require modifying Sponza's auto-set `useDirectionalLight=true` at load time. Filed for Stage 11e if H-A' is confirmed.

### SC10 — The Stage 11c CLI-plumbing fix (post-load light re-apply) is also needed for `--light-position`

If we add `--light-position=` for this stage, it should also be in the post-load re-apply block (`loadOBJMesh` may also overwrite `lightPosition` for OBJ scenes). **Improvement:** mirror the Stage 11c pattern — store `cliLightPosSet` + `cliLightPos` globals; re-apply post-load.

## Acceptance

- 3 new captures (`mid_0p8_x4`, `far_5`, `far_25`) at N=2048.
- `--light-position=` CLI flag added (with post-load re-apply pattern) if it doesn't already exist.
- `tools/v3_m1_cornell_light_distance/distance_ladder_results.json` records per-variant per-pixel + per-cell ratios + absolute luma.
- Verdict: H-A' confirmed (monotonic) / inconsistent / falsified.
- Stage 11e direction selected based on verdict.

## Out of scope

- Sponza point-light test (Stage 11e if H-A' confirmed).
- Bake-shader instrumentation (Stage 11e if H-A' falsified).
- Atlas alpha audit / H-C' test (Stage 11e).
- Stage 11a Fork A revival (independent track).
- Diag-rgb leak fix (independent track).
