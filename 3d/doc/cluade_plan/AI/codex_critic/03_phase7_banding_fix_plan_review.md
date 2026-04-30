# Phase 7 Banding Fix Plan Review

## Verdict

The plan is aiming at real visible artifacts, but it is too confident about the dominant root cause and it mixes one reasonable low-cost experiment with one expensive default shift that is underspecified and probably mis-prioritized.

The biggest issue is that it treats one screenshot's contouring as proof that the Phase 4c blend-zone math is the main culprit. That is not established by the available evidence, and it cuts against earlier branch findings that the blend-zone toggle had weak visible effect compared with the larger directional/storage issues.

## Findings

### 1. High: the root-cause section overcommits to cascade blend math from one screenshot

The plan says the large-scale contour banding is caused by:

- the linear cascade blend ramp
- the current `blendFraction = 0.5`
- and a hard switch at the end of the blend region

Evidence:

- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:14-33`

That is too strong.

The screenshot does show large rectangular contouring on the back wall and ceiling, but from the current branch state there are several competing contributors:

- probe-grid spatial quantization,
- directional-GI approximation at low angular resolution,
- soft-shadow bake shaping the probe signal,
- and general low-frequency irradiance contouring in the indirect field.

Also, earlier branch analysis already found the blend-zone work to be weak as a primary explanation for the visible artifact class. So "test smoothstep/wider blend zone as an experiment" is reasonable. "root cause A is the linear ramp" is not yet justified.

### 2. High: the file paths are wrong for the main shader edits

The plan points to:

- `src/shaders/radiance_3d.comp`

Evidence:

- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:60`
- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:157`
- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:207`

In this repo the live shader path is:

- `res/shaders/radiance_3d.comp`

This is a simple issue, but it matters because the proposed code changes are all anchored to that file.

### 3. High: raising C0 from `D=4` to `D=8` is much more expensive than the plan implies for the current 5g path

The plan presents `dirRes = 8` as a medium-impact, moderate-cost fix.

Evidence:

- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:112-151`

But in the current branch, when directional GI is enabled, the final renderer's C0 path does:

- 8-probe spatial trilinear
- per-probe `sampleProbeDir()`
- loop over all `D^2` bins

So the display cost scales from:

- `8 * 16 = 128` atlas fetches per shaded pixel at `D=4`

to

- `8 * 64 = 512` atlas fetches per shaded pixel at `D=8`

That is not a small step. It is a 4x multiplier on the already-expensive directional final-render path.

The plan mentions this numerically, but the language "measurable but not severe" is too casual for a branch whose directional GI path is already the expensive option.

### 4. Medium: the plan ignores the current Phase 5e reasoning for keeping C0 at `D=4`

Current branch semantics intentionally keep:

- C0 = `D=4`
- C1 = `D=8`
- C2/C3 = `D=16`

Evidence:

- `src/demo3d.cpp:2371-2387`

That does not prove `D=8` at C0 is wrong. But it does mean the plan should explicitly acknowledge that the branch already chose a different angular-budget strategy:

- near field gets more spatial density
- upper levels get more angular density

This Phase 7 note skips that context and jumps straight to "make C0 D=8" as if the project had no prior reason to keep C0 lower.

### 5. Medium: Fix 4 dither is described too optimistically

The optional dither proposal says:

- residual banding becomes noise rather than a sharp line

Evidence:

- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:155-173`

But as written, this is a static per-probe hash in the bake shader. Without temporal accumulation or some other averaging mechanism, it does not really turn structured error into convergent noise in a robust sense. It risks producing a stable textured pattern instead of a clean fix.

Also, the cost description as "one hash per probe" is not quite right in spirit. The expression sits inside the bake shader path that already runs per probe invocation and participates in the per-direction bake workflow, not as some isolated one-time probe setup.

### 6. Low: the plan undersells that Fix 2 is already available as an A/B test without code changes

The plan proposes changing the default constructor value:

- `blendFraction(0.75f)`

Evidence:

- `doc/cluade_plan/AI/phase7_banding_fix_plan.md:85-108`

But `blendFraction` is already exposed in the UI today. So the right validation order is:

1. run a direct A/B at `0.5`, `0.75`, `0.9`,
2. confirm whether the screenshot artifact class actually responds,
3. only then consider changing the default.

That ordering matters because it is exactly the plan's root-cause claim that is still unproven.

## Where the plan is strong

- Fix 1 is a reasonable low-cost experiment.
- The plan correctly recognizes that color-bleeding banding is likely downstream of larger spatial/angular quantization issues rather than a separate storage bug.
- The staged implementation order is sensible if downgraded from "known fix" to "hypothesis-driven test order."

## Bottom line

This should be revised before implementation.

I would fix it by:

1. downgrading the root-cause claims to hypotheses,
2. correcting the shader file paths to `res/shaders/...`,
3. treating wider `blendFraction` as an immediate A/B experiment rather than a default change,
4. reframing `D=8` at C0 as a costly quality experiment that competes with the current Phase 5e design, and
5. treating the dither idea as a last-resort artifact masking step, not a clean fix.
