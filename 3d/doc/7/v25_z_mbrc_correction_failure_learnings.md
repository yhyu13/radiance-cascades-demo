# MBRC correction — failure learnings (31-commit retrospective)

**Date:** 2026-05-26.
**Scope:** the 31 commits from `82e969e` (v2.0-pre measurement instrumentation, 2026-05-19) through `a350c85` (v2.5 Axis A CLEAR_ATTRIBUTION, 2026-05-26).
**Decision:** the MBRC correction program (v2.0 → v2.5) is judged FAILED as a path to closing the cascade-vs-PT gap. Pivot: fully adopt the ShaderToy RC reference implementation rather than continuing to patch the current cascade pipeline.

This document captures what was tried, what was learned, and what won't work — so the pivot doesn't re-discover the same dead ends.

---

## 1. What the program attempted

The premise was that the local 3D cascade implementation could be made PT-equivalent (or close enough that hybrid per-pixel correction could be retired) through a sequence of targeted measurement-driven fixes. The shape was always:

1. Measure a specific asymmetry or magnitude gap vs PT-reference.
2. Form a named hypothesis (Greek letter / numbered version) for the mechanism.
3. Pre-commit a verdict band before running the sweep.
4. Run the A/B at fixed config (typically cornell/cam0/MB-ON/N=2048/mode-17).
5. Apply the gate → STRONG ships, MARGINAL documents, DEAD reverts + pivots.

Six full hypothesis trees were exercised. Five returned DEAD or MARGINAL. The sixth (v2.5 Axis A) localized the leak but does not constitute a fix.

---

## 2. The hypothesis lineage (chronological)

### v2.0-pre — Greek-letter hypothesis tree (commits 82e969e → 4875124)

| Hyp | Mechanism | Verdict | Cost | Why it failed |
|-----|-----------|---------|------|---------------|
| **(γ) gamma** | angular under-sampling at C0 | REJECTED | 12 captures | D ∈ {4, 8, 16}: |p95| moved 1.0–1.4%, well inside ±10% invariance band. Pattern is angular-resolution-invariant. |
| **(β) beta** | MB-gain wrong fixed point | LEVERAGE_NOT_CURE | sweep | MB amplifies but doesn't fix the asymmetry; LDR metric initially under-reported magnitude by ~100×. |
| **(α) alpha** | merge-time directional weighting | LEVERAGE_WRONG_DIR | 20 captures | Directional-merge ON was HELPING cam2's pattern, not hurting it. Disabling it made things worse on cam2 (+19.6%). |
| **(δ) delta** | spatial probe-density | REJECT | sweep | HDR re-litigation showed the LDR rejection was an artifact; structurally cascade delivers 15–25% of PT GI regardless of probe density. |

**Engine default flip** (commit `d64ea17`): `M4_iso_nearest merge + MB g=1.0 ON` adopted as new default. +21% mean luminance lift, super-additive M4×MB stack, all 3 ship blockers passed. **This was the only "win"** of the entire program and is what the current Default baseline rests on. Even with this win the residual gap was ~32%/61% on cam0/cam2.

### v2.0 proper — measurement #1–6 + (h.a..h.c) + P2 (commits f0abf75 → 8007296)

Re-grounded methodology after v2.0-pre closed:

- **(h)** MB feedback × single-bounce merge are stacked (confirmed compounding)
- **(h.2/h.3)** merge-variant × MB factorial closes the *magnitude* gap but not asymmetry
- **(h.b)** smoothstep blend-zone falsified
- **(h.c)** probe-cell `fract` viz NEUTRAL; spatial-trilinear A/B revealed ST=1 acts as a *symmetrizer*; downstream path locked innocent
- **P2/B/D/E** dominant-bin viz: bin classification is angular-resolution-invariant AND multi-bounce-invariant (per-row JS delta = 0 for MB on/off — MB adds uniform additive bias, doesn't shift argmax bins). The P2 framework itself was then invalidated as a *fix* signal (P2_FRAMEWORK_BIASED) — it measures viewport composition, not cascade parity.

**Outcome:** the named-hypothesis tree was exhausted. Falsification was rigorous but no surviving fix candidate emerged. The methodology pivoted to absolute-residual analysis (CV1).

### CV1 — convergence sweep (commit `dc442ac`)

Cornell/cam0/N ∈ {128…2048}: cascade asymptotes at **65% of PT energy** by N≈1024; extra frames buy <0.2%. The 35% mean-energy gap is the v2.0 hybrid-retirement budget. Per-pixel spread: |p50|=0.411, |p95|=1.278, dim%=28.6%, bright%=5.4% — bidirectional variance hidden behind a deceptively close mean ratio.

### v2.0-postfix — Shader-toy diff Deltas #1+#2 (commit not in this window but referenced)

Algorithmic diff against the ShaderToy reference revealed two bugs in `sampleProbeDir`:
- **Delta #1:** spurious `* a.a` multiplier on the irradiance integrand
- **Delta #2:** wrong normalization (`Σ(cos·a)`-weighted mean vs proper Riemann sum)

Fix landed: `irrad = (4/D²) × Σ(L · cos⁺)`. Closed the dim half of the gap (dim% 28.6 → 16.7, ratio 0.65 → 0.846). **But exposed the bright tail** (|p95| 1.28 → 2.27 in the leak-suppressed comparison; bright% +1.4 pp). This is the leak that all of v2.1–v2.5 then chased.

### v2.1 → v2.4.b — bright-tail attack on every available axis (DEAD parade)

| Version | Axis | Verdict | What it told us |
|---------|------|---------|-----------------|
| **v2.2** | aFactor reshape (merge formula) | **KILLED at Step 0** | r_attenuation=1.023 → LS attenuated bright/non-bright pixels EQUALLY. Bright tail uncorrelated with upper-cascade luminance. No merge-weight reshape can isolate it. ~3.5h saved by failing fast. |
| **v2.3** | leak attribution at C0 | **MARGINAL** | Bright pixels touch only 0.7% of C0 cells (strongly localized in absolute terms) BUT within those cells mass is evenly spread (top-5%=26.1%, Gini 0.57). No outlier hot probes. Mode 23 shipped diagnostic-only. |
| **v2.4** | C0 dirRes 8→16 (bake-bin discretization) | **DEAD** | |p95| moved 0.1% vs 10% MARGINAL bar; bright% dropped 0.86 pp vs 1 pp bar. Bake cost +2.4×. Bake-bin discretization is NOT the mechanism. |
| **v2.4.b** | per-pixel output luminance clamp K=2 | **DEAD** | |p95| moved 0.5%, ratio shifted 0.208 (vs 0.15 ceiling), dim% exploded +19.22 pp. Luminance-proportional scaling is a global dimmer, not a firefly leash. Bright pixels' indirect/direct ratio overlaps with non-bright pixels — no salience at consume time. |

### v2.5 Axis A — per-cascade contribution isolation (commit `a350c85`)

Wired `--max-cascade-level=N`. Sweep at cornell/cam0/N=2048:

| Level   | ratio | \|p95\| | bright% |
|---------|-------|---------|---------|
| C0 only | 0.255 | 0.933   |  2.36   |
| +C1     | 0.745 | 0.828   |  5.70   |
| **+C2** | 0.994 | 0.898   | **13.49** |
| +C3     | 0.977 | 0.883   | 11.09   |

**Verdict: CLEAR_ATTRIBUTION.** The C1→C2 merge contributes +89.3% of total bright% growth (+7.80 pp). C2 is the only level where ratio crosses *above* PT. C3 partially attenuates (−2.40 pp) but doesn't cancel.

This localized the leak but produced no fix; the next step (Axis A.1 MB-OFF re-sweep, then bake-shader inspection at C1↔C2 boundary) is what we are choosing **not** to pursue. The mechanism narrowing is informative but every other prior attempt that landed on a specific axis also failed to convert mechanism into fix.

---

## 3. Why MBRC correction is judged failed

Six layered findings that, together, make further patching not worth the cost:

### 3.1. The bright tail is structural, not parametric

After v2.2 (merge formula), v2.4 (bin resolution), and v2.4.b (output clamp) all DEAD, no remaining "parameter to tune" axis exists. The leak survives every knob the current architecture exposes.

### 3.2. The leak is a bias, not variance

At N=2048 with MB-ON, bright pixels are *consistently* bright frame-over-frame. MC noise is long gone. v2.3 confirmed they're evenly spread (Gini 0.57) — no firefly outliers. More rays/probes won't help.

### 3.3. The leak has no per-pixel salience signal

v2.4.b's failure mode is the deepest learning: bright pixels' `lum_indirect / lum_direct` overlaps the non-bright population. There is no consume-time signal to separate "this pixel is leaking" from "this pixel is genuinely bright." Any clamp that fires on this signal hits bulk pixels too — global dimmer, not leash.

### 3.4. The leak is geometrically broad

v2.3 showed bright pixels concentrated on the green-wall side X but spread *uniformly within* the affected region. It's not a hot-probe problem; it's a pattern that affects ~11% of pixels across a continuous slab of geometry. Localized fixes don't apply.

### 3.5. Mechanism narrowing without fix conversion

v2.5 Axis A localized the leak to the C1→C2 merge — a real finding. But every prior narrowing (P2 dominant-bin, v2.3 attribution, mode-23 cluster inspection) also localized something specific, and none converted into a fix. The codebase's cascade-merge geometry, multi-bounce feedback, smoothstep visibility, and Phase 5e D-scaling all interact at the C1↔C2 boundary; isolating *which* of these is the source would take another 3–5 sweep cycles, and each prior cycle has cost ~half a session of measurement infrastructure.

### 3.6. The opportunity cost is large and the win ceiling is small

Current Default already lands ratio 0.977 (≈PT within 2.3%) with |p95| 0.883 (4.5% above target). Even a STRONG v2.5 win would close a few percent more. The ShaderToy reference, by contrast, is a known-good algorithm that closes the gap by construction. The marginal hour spent on RC corrections is now worth less than an hour spent porting the reference.

---

## 4. Reusable learnings (carry into the pivot)

Things the program *did* prove that the pivot should bank:

### 4.1. The v2.0 consumer-side contract

`irrad = (4/D²) × Σ(L · cos⁺)` is the correct Lambertian integral over D² bins covering S². Caller multiplies by albedo (no extra π). This is non-negotiable — whatever the ShaderToy port looks like at the consume side must satisfy this contract or it ships dim.

### 4.2. The "merge is bake-time" framing

Per-cascade attribution features that try to gate at consume time miss the actual radiance assembly. The merge happens once at bake; consume just reads. Any pivot that introduces per-cascade controls must rewire the bake chain, not the shading path.

### 4.3. Asymmetric filters belong on accumulators, not outputs

v2.4.b's death confirmed: HIGH-only firefly clamps must live where MC samples are accumulated (bake-time, per-bin), never on final composited radiance. The ShaderToy pivot inherits this — if it ships a firefly clamp, that clamp must be inside the integral, not after.

### 4.4. HDR-EXR is the only honest metric

Three separate hypotheses (β, δ, parts of α) had their verdicts overturned by HDR re-litigation. LDR PNG comparison silently truncates the bright tail — the exact tail this whole program then spent five months chasing. Pivot measurement must default to EXR.

### 4.5. Pre-commit verdict bands; fail fast at Step 0

v2.2 saved ~3.5h by killing at Step 0. v2.4 and v2.4.b each ran the full sweep but the bands were locked beforehand; no post-hoc band-widening. This discipline must persist; the pivot will hit its own dead ends and the pattern (scope locked → premise test in the same script that ships data → DEAD verdict triggers pivot, not retry) is the only thing that kept the program from spinning indefinitely on each hypothesis.

### 4.6. The CV1 baseline is reusable

`tools/v20_convergence/captures_cv1_postfix/N2048` at cornell/cam0 (ratio 0.977, |p95| 0.883, bright% 11.1%, dim% 5.1%) is the locked baseline for any pivot A/B. The ShaderToy port should diff against this — STRONG = ratio ∈ [0.95, 1.05], |p95| ≤ 0.50, bright% ≤ 5%, dim% ≤ 5%.

### 4.7. P2 dominant-bin viz measures viewport composition, not cascade parity

`P2_FRAMEWORK_BIASED` is a permanent DNR. Cross-camera comparisons must be in probe-space or via same-pixel cross-cam projection, not via per-camera frustum metrics.

### 4.8. Engine default flip (M4_iso_nearest + MB g=1.0) is load-bearing

The current Default's headline metrics depend on the v2.0-pre engine flip (`d64ea17`). If the pivot replaces the radiance pipeline wholesale, it inherits these defaults' semantic — the new pipeline must produce comparable absolute radiance at the same input, not just "matches PT" relative to its own arbitrary brightness scale.

---

## 5. What NOT to repeat in the pivot

- **No more named hypothesis trees on the current cascade implementation.** All four Greek letters and all six v2.x versions are documented dead ends.
- **No more output-side symptom clamps.** v2.4.b proved the salience signal isn't there.
- **No more bake-bin resolution bumps.** v2.4 proved invariance.
- **No more merge-formula reshapes targeting bright-tail isolation.** v2.2 proved the correlation isn't there.
- **No more LDR-only verdicts.** EXR or it didn't happen.
- **No "this time the consume-side fix will work" attempts.** v2.0-postfix closed the consume-side contract permanently; anything left is bake-side.
- **No fix attempts driven by P2 dominant-bin viz.** It's a viewport metric.

---

## 6. Pivot direction (set; details in a separate scope doc)

**Adopt the ShaderToy RC reference implementation as the production radiance pipeline.** Retire the current bake chain (radiance_3d.comp + the v2.0-postfix consume path) in favor of a port of the reference algorithm. Keep the current consumer's v2.0 contract (`(4/D²) × Σ L cos⁺`) and CV1 baseline as the acceptance gate. Keep hybrid per-pixel correction as the safety net during the cutover; retire it only after the ShaderToy port lands within band on cornell/cam0/N=2048.

Scope, milestones, pre-committed acceptance bands, and rollback criteria belong in a new doc (proposed name: `doc/7/v3_shadertoy_adoption_scope.md`). This document closes the v2.x line.

---

## 7. Cross-references

- v2.0-pre closeout: `doc/7/mbrc_v20_pre_measurement_report.md` §16
- v2.0-postfix landing: `doc/7/v20_postfix_cv1_impl.md`
- v2.x DEAD docs: `v22_aFactor_reshape_impl.md`, `v23_leak_attribution_impl.md`, `v24_c0_dirres_bump_impl.md`, `v24b_indirect_clamp_impl.md`
- v2.5 Axis A: `v25_architectural_scope.md` (Execution log §"2026-05-26 — Axis A run")
- ShaderToy diff (original source of the v2.0-postfix fix): `v20_shadertoy_diff_impl.md`, `v20_shadertoy_diff_diagrams.md`

## 8. Memory pointers

- `project_mbrc_v20_decisions.md` — v2.0 locked program goals (preserve)
- `project_mbrc_v20_postfix_landed.md` — v2.0-postfix baseline (preserve)
- `project_v22_killed_at_step0.md`, `project_v23_step0_marginal.md`, `project_v24_dead_c0_dirres.md`, `project_v24b_dead_indirect_clamp.md`, `project_v25_axisA_c2_attribution.md` — the DEAD/MARGINAL receipts; preserve for DNR continuity
