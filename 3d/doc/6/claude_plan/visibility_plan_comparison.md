# Comparison: `probe_visibility_acceleration_plan.md` vs `visibility_acceleration_plan.md`

Both plans address the same problem — H6's `probeVisibility()` produces dot-banding on vertical walls, and mode 3 (per-direction-bin shadow trace) is correct but ~32× too expensive. They diverge in **scope**, **recommended fix**, and **architectural philosophy**.

---

## Side-by-side summary

| Axis | `probe_visibility_acceleration_plan.md` | `visibility_acceleration_plan.md` |
|---|---|---|
| Format | Single-strategy deep-dive | Survey of 7 strategies + comparison matrix |
| Header date | none | 2026-05-12T18:17+08:00 |
| Primary recommendation | **Mode 4** — depth-aware per-bin visibility, reuses existing `hit.a` | **Strategy 1** — transparency α in bake (atlas RGB→RGBA, interval merging) |
| Bake-side changes | **None** (data already in `hit.a`) | **Required** (atlas format, store α per bin, modify inheritance merge) |
| Render-side changes | New `sampleProbeDirDepthAware`, new visibility mode 4 | Replace `sampleProbeDir` with RGBA read; **remove** `probeVisibility()` and `uVisibilityMode` entirely |
| Granularity of visibility test | Per-direction-bin (preserves mode 3's banding-elimination) | Per-direction-bin (Strategy 1, via α-gated interval merge) |
| Render-time cost vs mode 0 | ~1.05× | "Free" (Strategy 1); 1/128 (Strategy 2/4) |
| Reference inspiration | ShaderToy `WeightedSample` (Sannikov CubeA / Image GLSL) | Same ShaderToy + the RC paper's interval-merge formalism |
| Treats existing modes 1/2 as | Keep as A/B baselines, mark "not recommended" if mode 4 wins | Strategy 1 deletes the whole `uVisibilityMode` switch |
| Fallback if primary fails | **Mode 5** = mode 4 + 1 confirmation shadow ray | Multi-strategy combinations (e.g., 2+4, half-res 6, temporal 7) |

---

## Where they overlap

- **Same key insight** — the probe atlas already stores per-bin hit distance (`hit.a`); a free visibility check is possible without new SDF traces.
- **Same ShaderToy reference** — both cite `WeightedSample` from CubeA.glsl/Image.glsl as prior art.
- **Same root-cause framing** — binary `probeVisible()` flips corners between real irradiance and `vec3(0)` at trilinear cell boundaries → grid-aligned dot artifacts.
- **Same per-bin remedy direction** — preserve mode 3's per-direction granularity (the only granularity that demonstrably eliminates banding) while removing its per-bin SDF tracing cost.

---

## Where they diverge

### 1. Scope and presentation
- The **probe** plan is a focused implementation plan for one specific approach (Mode 4). It is the kind of doc you hand to an engineer and say "build this."
- The **visibility** plan is an exploratory survey with a comparison matrix and a phased roadmap (Phase A quick wins → Phase B architectural fix → Phase C combined). It is the kind of doc you hand to a tech lead before scoping the work.

The probe plan's Mode 4 corresponds approximately to the visibility plan's **Strategy 2** (WeightedSample / hit.a proxy), but with a critical refinement: the probe plan tests **per-direction-bin** (using the bin's own hit distance), while the visibility plan's Strategy 2 description tests **per-corner** (using the bin nearest to the probe-to-surface direction). The probe plan therefore inherits mode 3's per-bin granularity, which the visibility plan flags as "Partial (flatland)" — the visibility plan would only get to that quality via Strategy 1's interval merge.

### 2. Recommended primary fix
- **Probe plan → Mode 4** (a render-side-only change, reuses baked `hit.a`).
- **Visibility plan → Strategy 1** (atlas format change RGB→RGBA, store α=0 for hit / α=1 for miss, modify the bake-time inheritance merge: `rad = hit.rgb * l + upperDir.rgb * upperAlpha * (1-l)`, and remove `probeVisibility()` entirely).

The probe plan explicitly places the visibility plan's chosen fix **out of scope**: > "Bake-time visibility (radiance_3d.comp's cascade inheritance) — separate problem; … Future work if needed."

### 3. Architectural philosophy
- **Probe plan = pragmatic minimal-change.** Don't touch the bake; just stop throwing away `hit.a` in the fetch. Cone-angle correction (`cosCone`) is the fragile knob.
- **Visibility plan = paper-aligned correct.** The RC paper says intervals (radiance + transparency) are linearly interpolatable; full radiance is not. The atlas should store intervals; visibility falls out of the merge formula `radiance = near.rgb + far.rgb * near.a; alpha = near.a * far.a` for free.

### 4. Bake-time vs render-time correctness
- **Probe plan Mode 4** fixes render-side leaks but the bake-time cascade inheritance smoothstep (`rad = hit.rgb * l + upperDir * (1-l)`) still blends blindly across occluders. Light can leak into the atlas during bake.
- **Visibility plan Strategy 1** fixes both: bake-time inheritance is α-gated, and render-time interpolation uses α directly. Leaks are addressed at the source.

### 5. Treatment of existing modes 0–3
- **Probe plan**: keep all four; add mode 4 as opt-in; let A/B testing decide whether to default-flip.
- **Visibility plan Strategy 1**: rip out the entire `uVisibilityMode` switch and `probeVisibility()` — the architecturally correct atlas makes them obsolete.

### 6. Confidence and risk
- **Probe plan** explicitly flags the ShaderToy formula as a 2D approximation that may need a 3D-specific cone-angle constant. Provides a Mode 5 fallback (mode 4 + 1 shadow ray).
- **Visibility plan** flags Strategy 2 (the analog of Mode 4) as "flatland approximation, partial fix." Its Strategy 1 has no such caveat — interval merging is exact under the paper's penumbra condition.

### 7. Cost claims
| | Mode 0 | Mode 1 | Mode 3 | Probe-plan Mode 4 | Visibility-plan Strategy 1 |
|---|---:|---:|---:|---:|---:|
| Render cost | 1× | ~1.05× | ~30× | ~1.05× | ~1.0× ("free") |
| Bake cost change | 0 | 0 | 0 | 0 | small (extra α store per bin) |
| Atlas memory | RGB | RGB | RGB | RGB (RGBA fetch is free in bandwidth) | RGBA (33% more) |

---

## Tension between the two plans

The plans agree on the **diagnosis** but propose two different points on a cost/scope curve:

- **Ship-Mode-4-now** (probe plan): low-risk, render-only, lands in hours. Solves the user's "mode 3 is too expensive" complaint immediately. Doesn't address bake-time leaks. Mode 4's flatland cone-angle approximation could under- or over-occlude in 3D scenes; the plan acknowledges this and offers Mode 5 as a fallback.
- **Do-it-right-once** (visibility plan Strategy 1): higher-risk, touches bake + render + atlas format, is the architecturally correct destination per the RC paper, and **also** fixes the underlying bake-time leak that Mode 4 leaves untouched. Larger blast radius (atlas format change ripples through every fetch site).

These are **not mutually exclusive**. Mode 4 (probe plan) corresponds to the visibility plan's "Phase A quick win" tier even though it isn't enumerated there. A reasonable combined sequencing:

1. Land Mode 4 first (probe plan) as a render-side quick win — restores acceptable quality at acceptable cost.
2. Then land Strategy 1 (visibility plan) as the architectural fix — replaces both Mode 4 and the entire `uVisibilityMode` family, fixes bake-time leaks.

---

## Open questions surfaced by the contrast

- **Is the bake-time leak material?** The probe plan assumes render-side correction is sufficient. If `radiance_3d.comp`'s cascade-inheritance smoothstep already pollutes the atlas with cross-wall radiance, no render-side visibility check can recover it. The visibility plan is explicit about this; the probe plan is silent.
- **Cone-angle correction — does ShaderToy's 2D `cos(π/2 − θ)` map to 3D octahedral bins?** The probe plan flags this as the fragile knob and proposes empirical tuning. The visibility plan's Strategy 1 sidesteps the question by using α-gated interval merging instead of a geometric cone test.
- **Atlas format cost** — the visibility plan moves to RGBA (+33% memory). Acceptable at current cascade resolutions, but worth confirming against the 1080p perf-tooling work in [perf_tooling_step12_impl.md](../../5/claude_plan/perf_tooling_step12_impl.md).
- **Per-bin α=0/1 vs soft α** — the visibility plan notes the binary α can later become a soft 0..1 (similar to mode 2's smoothstep) for smoother transitions; the probe plan's cone correction is its analog of "soft visibility."

---

## TL;DR

- **Same problem, same key insight, different scope.**
- **Probe plan** = "ship Mode 4 this week, render-only, reuse `hit.a`."
- **Visibility plan** = "rebuild the atlas as RGBA intervals per the RC paper, delete `probeVisibility()`, fix bake leaks too."
- **Probe plan's Mode 4 ≈ visibility plan's Strategy 2** (WeightedSample/hit.a proxy), but with per-bin granularity that the visibility plan only achieves via Strategy 1.
- **The two plans are sequenceable, not exclusive.** Mode 4 can land first as a quick win; Strategy 1 can land after as the architectural endpoint that retires Mode 4 (and modes 1/2/3).
