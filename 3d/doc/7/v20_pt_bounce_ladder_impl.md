# MBRC v2.0 — PT bounce-ladder self-critique (hypothesis (f) FALSIFIED)

**Date**: 2026-05-23 (immediately follows
[v20_absolute_residual_impl.md](v20_absolute_residual_impl.md), commit
`42596ae`).

**Motivation**: user asked for self-critique of direction before doing the
~1h unit-luminance-sphere spot-test recommended in §6 of the absolute-
residual impl doc. Self-critique surfaced one cheap experiment that could
falsify the *premise* of the spot-test (and the (f) hypothesis) before it
runs: re-render PT at `--pt-max-bounces=N` for several N to disentangle
"bake-time energy loss" from "multi-bounce delivery gap."

## 1. The critique

The absolute-residual analyzer compared cascade_GI vs PT_GI where
`PT_GI = pt_full(b=8) − pt_direct(b=1)` — i.e. PT integrated to 7 indirect
bounces. Cascade integrates a single bounce per frame plus MB temporal
feedback at g=1.0. These are NOT integrating the same mathematical object:
cascade is structurally near-single-bounce; PT is multi-bounce. A "cascade
< PT" verdict on this comparison can be:

- (f) cascade losing energy at integration time (the verdict the analyzer
  drew), OR
- (g) cascade correctly delivering ~single-bounce + a bounded MB feedback
  equilibrium, while PT keeps adding bounces; the gap is *structural
  bounce-count mismatch*, not a bug.

These have different fixes: (f) is a shader-side audit; (g) is either
"accept the equilibrium" or "find a way to safely raise MB without g=2.0
runaway" — already eliminated by v2.0-pre (β) §13.

## 2. The bounce-ladder experiment

[pt_bounce_ladder_capture.ps1](../../tools/v20_arch_diagnostic/pt_bounce_ladder_capture.ps1)
re-renders mode 17 EXR triplets at cam0+cam2 under new defaults with
`--pt-max-bounces ∈ {2, 4, 8}`. PT b=2 = direct + 1 indirect bounce =
the apples-to-apples comparison for cascade. b=4 and b=8 climb the ladder.
6 captures, 1.6 min.

[captures_pt_bounce_ladder/](../../tools/v20_arch_diagnostic/captures_pt_bounce_ladder/)
18 EXRs + 6 PNGs.

## 3. Results

|cam| b | Σ+   | Σ−   | \|Σ+/Σ−\|  | casc/PT energy | verdict           |
|--:|--:|-----:|-----:|------:|---------------:|-------------------|
| 0 | 2 | 1592 |  502 | **3.17** | **1.39** | **NET_OVER_BRIGHT** |
| 0 | 4 |  536 | 1770 | 0.30  | 0.76          | NET_UNDER_BRIGHT  |
| 0 | 8 |  478 | 2682 | 0.18  | 0.64          | NET_UNDER_BRIGHT  |
| 2 | 2 |  786 | 1494 | **0.53** | **0.77** | **BORDERLINE_UNDER** |
| 2 | 4 |  359 | 3340 | 0.11  | 0.44          | NET_UNDER_BRIGHT  |
| 2 | 8 |  270 | 4134 | 0.07  | 0.38          | NET_UNDER_BRIGHT  |

### 3.1. (f) energy-loss hypothesis — FALSIFIED on cam0

At b=2 (apples-to-apples cascade-equivalent single-bounce PT), **cam0
cascade is *over*-bright by 39%** in integrated energy and by 3.17× in
positive-vs-negative residual ratio. The bake is not losing energy — at
single-bounce equivalent it's *adding* energy. This is the literal
opposite of (f)'s prediction.

### 3.2. Dominant gap is multi-bounce delivery, not integration loss

cam0 energy ratio walks 1.39 → 0.76 → 0.64 as b goes 2 → 4 → 8. cam2
walks 0.77 → 0.44 → 0.38. The ratio drops monotonically because PT keeps
adding bounces while cascade stays at its MB equilibrium. The headline
"cam0=0.65 / cam2=0.38" gap reported by the absolute-residual analyzer
and the v2.0-pre §16.5 triple-stack ceiling **conflates two distinct
effects**:

- A *delivery gap* (cascade's MB-equilibrium ≈ 1.5–2 effective bounces;
  PT's b=8 ≈ 7 bounces).
- A *single-bounce integration asymmetry* (at b=2: cam0 +39% over,
  cam2 −23% under).

### 3.3. cascade MB-equilibrium effective-bounce-count estimate

Cascade-with-MB-g=1.0 at frame 512 (EMA-converged):
- **cam0**: equals PT somewhere between b=2 (1.39×) and b=4 (0.76×) →
  linearly interpolated, cascade ≈ PT_b≈2.5–3.
- **cam2**: equals PT between b=2 (0.77×) and b=4 (0.44×) → cascade ≈
  PT_b≈2.5.

So cascade-with-MB-g=1.0 delivers approximately the radiance of PT with
2.5–3 bounces. PT default is 8 bounces. The "missing" 5+ bounces account
for most of the ~35–60% gap. They are *physically present* in PT and
*physically absent* from cascade, by architectural choice (g=1.0 in a
single-bounce-per-frame MB feedback loop).

### 3.4. Why cam0 over-shoots at b=2

Plausible explanations, ranked by likelihood given the diagnostic
context:

1. **MB feedback at g=1.0 is super-additive against M4_iso_nearest
   merge.** v2.0-pre [alpha_m4_deepdive_impl.md §4.2](alpha_m4_deepdive_impl.md)
   measured M4×MB super-additivity (+16.8% on cam0). The MB equilibrium
   gain on the merge-exposed atlas radiance can climb above 1.0
   single-bounce — exactly what we measure here. It's not "over-counting"
   in a buggy sense; it's the geometric series 1 + g·a·m + (g·a·m)² + …
   summing above 1.0 when the per-step factor g·a·m is close to 1.
2. **cam0's viewport is dominated by main-room surfaces that get the
   first-bounce + MB-feedback chain at full strength.** cam2's alcove-
   weighted viewport hits surfaces that are deeper in the bounce-path
   tree, so MB feedback hasn't been propagating as long when sampled.
   This is the same camera-projection / surface-mix effect already
   confirmed by hypothesis (c).

## 4. Updated hypothesis space

| Hypothesis | Pre-ladder | Post-ladder |
|------------|------------|-------------|
| (a) bake-side leak | REJECTED | unchanged |
| (b) smoothstep blend zone | P4 | unchanged |
| (c) camera-projection / surface-mix | CONFIRMED | **strengthened** — explains both the single-bounce-cam-split (cam0 +39% / cam2 −23%) AND the differential climb rate of cascade-vs-PT as b grows |
| (d) basis-representation error | FALSIFIED | unchanged |
| (e) thin-merge shader | P5 (demoted) | **unchanged at P5** — basis-error precondition still failed; the b=2 finding doesn't re-open it |
| **(f) bake-time energy loss / under-bake** | NEW P1 (from absolute-residual analyzer) | **FALSIFIED** — cam0 over-bright at b=2 contradicts the loss prediction |
| **(g) multi-bounce delivery gap** (NEW) | not on list | **NEW P1** — cascade MB-equilibrium ≈ 2.5–3 bounces vs PT default 8; accounts for most of the headline 0.65/0.38 gap |
| **(h) single-bounce integration asymmetry** (NEW) | not on list | **NEW P2** — at b=2, cam0 +39% over, cam2 −23% under; ~0.62 energy spread is a real per-cam asymmetry that exists *under* the bounce-count gap |

## 5. Implications for next steps

The unit-luminance-sphere spot-test recommended in
[v20_absolute_residual_impl.md §6](v20_absolute_residual_impl.md) was
designed to disambiguate "integration math wrong" vs "scene-interaction
wrong" for the (f) hypothesis. With (f) falsified, the spot-test's
premise is mooted. However:

- The spot-test is *still useful* for hypothesis (h) — it would
  cleanly measure whether the single-bounce integration over-counts on
  one geometry class (a single sphere is geometry-uniform, so any
  cam0/cam2-style asymmetry on it would point at view-angle-dependence
  in the integration itself, not the scene-specific Cornell-alcove
  layout). Demoted from "next step" to "useful diagnostic for (h) if (h)
  becomes the active branch."
- (g) is **not actionable as a shader fix** — v2.0-pre already eliminated
  the obvious paths (g=2.0 runaway in (β); thin-merge precondition
  already failed in (d)). The remaining moves for (g) are architectural:
  (g1) accept current MB equilibrium as the shipping floor and revise the
  cascade-vs-PT reporting baseline to use PT_b=2 or PT_b=3 instead of
  PT_b=8 (re-frames the gap as ~15–30% instead of 36–62%); (g2) make MB
  feedback stable at higher g (root-cause the (β) g=2.0 runaway to see if
  it's a numerical-instability fix or a fundamental ceiling).
- (h) is more *actionable* as an architectural target — at b=2,
  cam0 cascade is +39% over and cam2 is −23% under, a 0.62 energy spread
  on a *single-bounce* comparison. Diagnosing the cam0-over half (where
  exactly is the +39% coming from? is it M4-exposes-too-much-atlas? is
  it MB-converging-above-target?) is a tractable shader-side
  investigation.

## 6. Recommended next step

**Investigate hypothesis (h): single-bounce integration asymmetry between
cams.** Specifically:

1. Run the absolute-residual analyzer at b=2 with hybrid breakdown
   options that isolate cascade-WITHOUT-MB-feedback vs cascade-WITH-MB
   (toggle `--use-multi-bounce=0` for one capture pair, keep MB ON for
   the other). If MB-OFF cascade-vs-PT_b=2 is roughly symmetric (|Σ+/Σ−|
   near 1.0), then **MB feedback is the +39% over-bright source on
   cam0** and the architectural target is the MB-feedback equilibrium
   on M4 merge. If MB-OFF is already +39% over on cam0, then **the
   first-bounce merge formula itself is over-integrating** and the
   target is `radiance_3d.comp:656-682` 3-way merge branch.
2. Cost: ~30s capture × 2 + 1 min analysis = 2 min. Disambiguates the
   (h) source cleanly before any shader work.

Then, depending on (h)'s source:

- **If MB is the cam0-over source** → root-cause the (β) g=2.0 runaway:
  is it numerical instability (denormal flush, NaN propagation through
  the feedback loop) or fundamental over-coupling? If fixable, the (g)
  path becomes "MB at safe-higher-g" and a much larger fraction of the
  PT_b=8 gap closes naturally.
- **If first-bounce merge is the cam0-over source** → the v2.0-pre
  (α) M4 deep-dive finding that "M4 removes attenuators" is partially
  the *wrong* finding: removing one attenuator that was load-bearing
  for cam0 may have over-corrected. Worth a 3-way M0/M2/M4 +
  cam0/cam2-at-b=2 stack to see if M2 (intermediate) is the sweet spot
  that cam0 was meant to use.

## 7. Self-critique of this self-critique

- **Bounce-ladder didn't sweep MB gain.** I held MB g=1.0 (engine
  default) constant. If MB g were 0 (no temporal feedback), cam0 cascade
  vs PT_b=2 might be symmetric — separating "merge over-counts" from "MB
  amplifies." Recommended in §6.1 as the next experiment; should have
  pre-empted it here. Adds 30s, would have answered (h) source in one
  step instead of two.
- **PT reference at low b still has its convergence question** — at b=2
  with 512 spp the variance might be noticeable on hard pixels; the
  absolute-residual numbers at b=2 have a wider error bar than at b=8.
  This *strengthens* the cam0 over-bright finding (the +39% is well
  above any plausible noise band) but weakens the cam2 BORDERLINE_UNDER
  finding (0.77 is close enough to 1.0 to be PT-noise-shifted). Worth a
  re-run at 1024 or 2048 spp on cam2 b=2 to confirm.
- **The "cascade ≈ PT_b=2.5–3" linear interpolation is an
  approximation.** PT_b=k energy doesn't scale linearly with k; it
  asymptotes to a value determined by scene albedo geometric series.
  The interpolation gives the right *qualitative* answer (cascade is
  delivering a low-single-digit effective bounce count) but the exact
  fractional bounce number shouldn't be reported as load-bearing.
- **"FALSIFIED" is correct for the energy-loss claim but doesn't mean
  the bake-side is bug-free.** Cam2 at b=2 still reads 0.77 energy
  ratio (BORDERLINE_UNDER); a clean bake would read closer to 1.0. So
  there *is* something on cam2 that's under-integrating even at single-
  bounce — just not the dominant cause of the headline ~35–60% gap.

## 8. Artefacts

- Bounce-ladder capture: [tools/v20_arch_diagnostic/pt_bounce_ladder_capture.ps1](../../tools/v20_arch_diagnostic/pt_bounce_ladder_capture.ps1)
- Captures (24 files): [tools/v20_arch_diagnostic/captures_pt_bounce_ladder/](../../tools/v20_arch_diagnostic/captures_pt_bounce_ladder/)
- Analyzer reused: [tools/v20_arch_diagnostic/analyze_absolute_residual.py](../../tools/v20_arch_diagnostic/analyze_absolute_residual.py) — invoked via inline override script (no committed bounce-ladder analyzer; reuse the original with `CAP` overridden).
