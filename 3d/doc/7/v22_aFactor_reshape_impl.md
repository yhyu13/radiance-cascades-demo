# v2.2 — aFactor bright-tail reshape (KILLED at Step 0 — premise rejected)

**Status:** Branch killed 2026-05-25 by the precondition test below.
`r_attenuation = 1.023` means LS attenuates bright pixels and
non-bright pixels equally on average — there is no signal a
luminance-gated `aFactor` could exploit. Successor work is v2.3
attribution; see [v23_leak_attribution_impl.md](v23_leak_attribution_impl.md).

**Date:** 2026-05-25 (scope locked, execution started).
**Companion to:** [v20_postfix_leaksupp_cv1_impl.md](v20_postfix_leaksupp_cv1_impl.md)
(LS verdict that motivated this), [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md)
(Default baseline), [gi_presets.md](gi_presets.md) (preset matrix).

**Premise:** LS proved Phase 3's `aFactor = upperDir.a` works as a bright-tail
clamp (bright% 11.1 → 4.5) but pays in catastrophic dim regression (28.5 →
90.0) because it attenuates the upper-cascade contribution everywhere, not
just where the contribution is bright. v2.2 conditions the attenuation on
the upper-cascade luminance so the dim regions ride at `aFactor = 1`
(Default behavior) while bright regions retain LS's gating.

**Pre-commit posture:** ALL decision rules and verdict bands below are
locked before any capture runs (cerebrum DNR 2026-05-21). The first
substantive step is a precondition test that can kill the entire branch
in 15 minutes if the premise doesn't hold.

---

## Step 0 — Precondition test (no shader code)

Goal: verify the bright tail of Default actually overlaps with pixels
where LS substantially attenuated the upper-cascade contribution. If
the bright outliers are orthogonal to the WS code path (e.g. they come
from `hit.rgb*l`, the lower-cascade surface-hit term), then no `aFactor`
reshape can address them and v2.2 is dead.

**Method** (no new captures — reuses existing post-fix sweeps):

1. Read Default (`captures_cv1_postfix/N2048`) and LS
   (`captures_cv1_postfix_leaksupp/N2048`) cascade_gi EXRs (downsampled
   2×2 to 640×360) and the shared PT_indirect (= PT_full − PT_direct).
2. Compute `delta = Default_lum − LS_lum` per pixel. Where positive,
   LS dimmed Default — i.e. aFactor had bite there.
3. Define `bright_mask = (Default_lum / PT_indirect_lum) > 1.3` (the
   bright% definition the analyzer uses).
4. Report:
   - `r_attenuation = mean(delta[bright_mask]) / mean(delta[~bright_mask])`
     — how much more aFactor attenuates bright outliers than the rest
   - `corr_spearman(Default_ratio, delta)` over the analyzer mask
   - Bright-pixel vs all-pixel histogram of `delta` (proxy for upper-lum
     distribution we'd gate on in Step 1)

**Pre-committed gate:**

| Outcome   | `r_attenuation` | Action                                          |
|-----------|-----------------|-------------------------------------------------|
| STRONG    | ≥ 2.0           | Proceed to Step 1                               |
| MARGINAL  | [1.2, 2.0)      | Proceed BUT add a 2nd seed capture at the end   |
| DEAD      | < 1.2           | Skip to v2.3 attribution; document v2.2 as N/A  |

(Spearman corr is informational, not gating — orientation check on the
attenuation signal.)

## Step 1 — Sweep range from histogram (only runs if Step 0 ≠ DEAD)

Set 4 sweep values for `--bright-gate-lum` to the 30/50/70/90th
percentiles of `delta` on bright pixels (computed from Step 0). This
gates the shader at "where most bright pixels' upper-cascade
contributions live" rather than at hand-waved round numbers.

## Step 2 — Shader change + dual-variant sweep (~3h plumbing, ~15 min capture)

In [radiance_3d.comp:755-800](../../res/shaders/radiance_3d.comp#L755-L800):

```glsl
// v2.2: aFactor gated on upper-cascade luminance (only attenuate bright merges).
// Mode 0 = today's LS unconditional; mode 1 = smoothstep gate; mode 2 = floor.
float a_raw = upperDir.a;
float aFactor;
if (uUseWeightedSample == 0) {
    aFactor = 1.0;
} else if (uAFactorMode == 1) {
    float L_upper = dot(upperDir.rgb, vec3(0.2126, 0.7152, 0.0722));
    float gate    = smoothstep(uBrightGateLum * 0.5, uBrightGateLum, L_upper);
    aFactor = mix(1.0, a_raw, gate);
} else if (uAFactorMode == 2) {
    aFactor = max(uAFactorFloor, a_raw);
} else {
    aFactor = a_raw;  // today's LS
}
```

CLI: `--afactor-mode={0,1,2}`, `--bright-gate-lum=<f>`, `--afactor-floor=<f>`.

Sweep, all at DM+ST+WS=1 + cornell + cam0 + N=2048:
- mode 1 (smoothstep): 4 values at `bright_gate_lum ∈ {p30, p50, p70, p90}`
- mode 2 (floor): 3 values at `afactor_floor ∈ {0.25, 0.50, 0.75}`

Null variants caught up-front: a simple floor might match the
smoothstep gate's performance with less code. If it does, ship the
floor — Occam wins.

## Step 3 — Decision rules (delta-anchored to Default)

All comparisons vs Default's measured @ N=2048: ratio_mean 0.977,
|p95| 1.045, dim% 28.5, bright% 11.1.

| Metric           | PASS                      | MARGINAL                 | FAIL                    |
|------------------|---------------------------|--------------------------|-------------------------|
| `\|p95\|`        | < 1.00                    | [1.00, 1.045] (=Default) | > 1.045 (regression)    |
| `ratio_mean`     | within ±5% of 0.977       | within ±10%              | > ±10%                  |
| `dim%`           | ≤ 29.5 (Default + 1pp)    | (29.5, 33.5] (+1..+5pp)  | > 33.5 (> +5pp)         |
| `bright%`        | ≤ 7.0 (Default − 4pp)     | (7.0, 10.1] (−4..−1pp)   | > 10.1 (no improvement) |

**Composite verdict** (relaxed):
- **PASS** = `|p95|` PASS AND no metric in FAIL
- **MARGINAL** = `|p95|` MARGINAL AND no metric in FAIL
- **FAIL** = any metric in FAIL OR `|p95|` doesn't improve

`|p95|` is load-bearing (it's the metric the LS test put on the
agenda). The other three are non-regression guards.

## Step 4 — Stability (multi-seed + multi-cam, at winning config only)

- Re-capture winning config with `--noise-seed-offset=42`. If `|p95|`
  shifts > 0.03 between seeds → MARGINAL one tier down (tail
  luck-dominated).
- Capture cam2 (known asymmetric). Apply Step 3 rules. If cam2 lands
  two tiers worse than cam0 → MARGINAL one tier down (cam0-overfit).

## Step 5 — Ship rule

- **PASS** at both seeds and both cams → ship as v2.1+v2.2,
  hybrid-retirement ready, freeze MBRC
- **MARGINAL** → ship + file v2.3 attribution follow-up
- **FAIL** → revert v2.2 code, pivot to v2.3 as primary work

## Cross-reference

- LS verdict (motivates v2.2): [v20_postfix_leaksupp_cv1_impl.md](v20_postfix_leaksupp_cv1_impl.md)
- Default baseline: [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md)
- Preset matrix: [gi_presets.md](gi_presets.md)
- ShaderToy reference & non-portable deltas: [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
- Captures: `tools/v20_convergence/captures_cv1_postfix*/`
- v2.2 captures (TBD): `tools/v22_aFactor/captures_v22_*/`

## Execution log

- **2026-05-25** Scope locked. Step 0 starting.
- **2026-05-25** Step 0 returned **DEAD** (`r_attenuation = 1.023`,
  Spearman = +0.361 over the analyzer mask). Branch killed. v2.3
  attribution scoped as successor.

## Step 0 results (raw)

`tools/v22_aFactor/precondition.py` over N=2048 Default + LS captures
(cornell/cam0). Output JSON at
`tools/v22_aFactor/precondition_results.json`. Key numbers:

| Quantity                                | Value     |
|-----------------------------------------|-----------|
| Mask pixels                             | 37 901    |
| Bright pixels (Default ratio > 1.3)     | 4 204 (11.1%) |
| Non-bright pixels                       | 33 697 (88.9%) |
| mean(Default_lum − LS_lum) on bright    | **0.151** |
| mean(Default_lum − LS_lum) on non-bright| **0.148** |
| **r_attenuation**                       | **1.023** |
| Spearman(Default_ratio, delta)          | +0.361    |

Delta-on-bright vs delta-on-non-bright percentiles (cross-check on the
mean): the two distributions overlap heavily across p10..p99 and the
non-bright class actually has a HIGHER right tail (p70 0.205 vs 0.178,
p90 0.247 vs 0.204). The mean equality isn't an averaging artifact —
the distributions are genuinely entangled.

## Autopsy — why v2.2 was wrong

LS attenuates per `aFactor = upperDir.a` where `upperDir.a =
wVisible / wTotalSpatial` from `sampleUpperDirWeighted`. This is a
**visibility** quantity, not a brightness quantity. The pixels with
low `upperDir.a` (heavily attenuated by LS) are those whose 8 upper-
cascade corners' look-back rays suggest occlusion — driven by scene
geometry near the probe, not by the probe's outgoing radiance.

The leak signature (bright outliers vs PT) is driven by a different
mechanism — most likely color-bleed overshoot from saturated wall probes
or C0 over-fire in tight corners — and those pixels are uncorrelated
with low-visibility merges. So gating `aFactor` on `L_upper` would
either:

- Match the LS distribution (if the gate stays low and most pixels
  cross threshold) → re-creates the dim regression
- Approach Default (if the gate stays high and few pixels cross) →
  bright tail returns

There is no middle setting that lifts dim while keeping bright clamped
— because the two effects ride on the same pixels in approximately
equal measure.

## Implications for v2.3

The bright tail is **probe-attribution-driven**, not merge-formula-
driven. v2.3 needs a render mode that says "this bright pixel got most
of its excess radiance from cascade C_i probe at (px, py, pz)" — then
we can either fix the offending probes' bake (e.g. better C0 sampling
near saturated walls) or gate them per-source at consume.

Cost saved by killing v2.2 at Step 0 (no shader code, no sweep, no
multi-seed/multi-cam captures): ~3.5h of engineering + capture time.
**This is exactly the failure mode the precondition gate was designed
to catch** — the LS sweep itself was a 1.8min capture that would have
been MUCH more expensive to discover empty-handed.
