# v2.4.b — Per-pixel indirect symptom clamp

**Date:** 2026-05-26.
**Predecessor:** [v24_c0_dirres_bump_impl.md](v24_c0_dirres_bump_impl.md) — DEAD verdict on bake-bin discretization. After v2.2 (merge formula) and v2.4 (C0 directional resolution) both returned DEAD, the principled mechanism-targeted axes are exhausted.
**User direction:** "go b and then c" — proceed with the per-pixel symptom clamp as v2.4.b, then scope v2.5 architectural regardless of outcome.

## Premise

This is **explicitly symptom treatment, not a mechanism fix.** The bright-tail leak has resisted v2.0 (consumer integrand), v2.2 (merge-formula reshape), and v2.4 (C0 directional resolution). All three were principled mechanism-targeted fixes. They're done.

A screen-space clamp `indirect ≤ K × direct` is a firefly leash: it caps cascade indirect at K times the local direct lighting. If the bright tail is dominated by cascade over-firing on directly-lit surfaces near a saturated wall (the green-wall reflection signature we've measured in v2.3 cluster inspection), then a moderate K should suppress those outliers without touching the bulk of dim/mid pixels.

**What this is not:** a principled GI fix. Surfaces in deep shadow with zero direct light will have their indirect floored to zero. This is an explicit tradeoff. The v2.x metric (PT-indirect mask `L_pti > 0.05`) excludes pure-shadow pixels from the gate, so the gate scores it favorably; the visual cost shows up only in shadowed regions the gate ignores.

## What changes

1. **One new uniform** `uIndirectClampK` (float, default 0 = disabled).
2. **One new CLI flag** `--indirect-clamp-k=K` (forwards to `Demo3D::setIndirectClampK`).
3. **Six new lines** in `raymarch.frag` between the hybrid blend (line 789) and the mode dispatch (line 792). The clamp is luminance-based, not per-channel:

```glsl
if (uIndirectClampK > 0.0) {
    float lumDirect   = dot(directColor,   vec3(0.2126, 0.7152, 0.0722));
    float lumIndirect = dot(indirectColor, vec3(0.2126, 0.7152, 0.0722));
    float lumCap      = uIndirectClampK * lumDirect;
    if (lumIndirect > lumCap && lumIndirect > 1e-4) {
        indirectColor *= lumCap / lumIndirect;
    }
}
```

**Why luminance, not per-channel:** A per-channel clamp `min(indirect_r, K * direct_r)` would distort hue. The green-wall bounce we're targeting carries strong green; clamping `indirect_g` independently of `indirect_r/b` would tint the output reddish. Luminance-based scaling preserves chromaticity.

**Why no shadow floor:** A small additive floor (`lumCap = K * lumDirect + EPS`) would preserve indirect in shadow but adds a knob and a confound. v2.4.b is a pure premise test — does ANY screen-space clamp move the gate? Polish (floor, soft clamp, gamma-aware) is a v2.4.c if signal exists.

**K value (pre-committed):** K = 2.0. Justification: typical firefly leash range is K ∈ [1, 10]; K = 2 is "indirect can be up to 2× direct" which is moderate (a green-wall reflection bouncing onto a lit white surface should reasonably stay below 2× the direct light hitting that surface). If K = 2 doesn't move |p95|, then either (a) bright cascade pixels also have high direct light so the cap doesn't bite, or (b) the leak is in screen regions with low direct light — either signature is informative for v2.5 scoping.

## Pre-committed verdict bands

Baseline (Default v2.0 postfix, cornell/cam0/MB-ON g=1.0/hybrid-OFF/N=2048, from `tools/v20_convergence/captures_cv1_postfix`):

- ratio = 0.977
- |p95| = 0.883
- bright% = 11.1%, dim% = 5.1%

| Outcome  | \|p95\| change                | ratio change       | bright% change | dim% change | Action |
|----------|-------------------------------|--------------------|----------------|-------------|--------|
| STRONG   | drops by ≥30% (\|p95\| ≤ 0.62) | shift ≤ 0.10       | drops by ≥3pp  | not worse by >3pp | Ship as opt-in preset `IndirectClamp_K2`; document tradeoff |
| MARGINAL | drops 10-30% (\|p95\| 0.62-0.79) | shift ≤ 0.15      | drops by 1-3pp | not worse by >5pp | Document; opt-in preset only if user requests |
| DEAD     | drops <10% (\|p95\| > 0.79)    | OR shift > 0.15    | OR worsens     | OR worsens by >5pp | Revert; symptom-clamp axis exhausted; pivot to v2.5 |

**Critical asymmetry:** Even STRONG verdict ships as opt-in only, never as Default. This is a symptom clamp; it does not fix the mechanism and it harms shadow-region indirect (a region the gate doesn't measure). Default stays at v2.0-postfix.

**Asymmetric tolerance vs v2.4:** allowed ratio shift bumped from 0.10/0.10/0.10 to 0.10/0.15/0.15 because the clamp globally dims (by construction — it can only reduce indirect, never increase it). The baseline ratio 0.977 is already 2.3% under PT; pushing it to ratio 0.82-0.87 is acceptable for STRONG/MARGINAL if |p95| drops enough.

**Lock:** these bands are committed BEFORE the sweep runs. The verdict script reads `v24b_results.json` and emits STRONG/MARGINAL/DEAD per these thresholds; no post-hoc reading.

## Execution plan

1. **Wire** (~15 min): uniform + setter + CLI flag + shader clamp block.
2. **Build** (~30 s): `./build.ps1 Release` — clean, no shader changes outside the 6-line block.
3. **Capture A/B** (~5 min):
   - Baseline: reuse `tools/v20_convergence/captures_cv1_postfix/N2048` (K=0)
   - Variant: new capture with `--indirect-clamp-k=2` at cornell/cam0/N=2048
   - Save to `tools/v24b_clamp/captures/`
4. **Analyze** (~30 s): `tools/v24b_clamp/analyze_v24b.py` (clone of v24's analyzer pattern). Emit `v24b_results.json`.
5. **Apply gate** (~immediate): emit STRONG/MARGINAL/DEAD per pre-committed bands.

## Risk register

- **Globally dims**: the clamp can only attenuate, never amplify. Mean cascade output will drop. If the bright tail is small (~11% of pixels) and the mean drops by more than ~3% (the ratio shift bar), the bright tail likely wasn't the cause and the clamp is dimming bulk pixels — DEAD verdict.
- **Shadow regions broken**: not measured by the gate. Visual A/B in mode 0 PNG should be inspected post-gate if STRONG, to confirm shadow indirect didn't go fully black.
- **Modes 11/12/13/18/19/20 all see clamped indirect**: heatmap diagnostics will report on the clamped values. Acceptable; the clamp is the intended cascade output when enabled.
- **No DEAD-pivot risk**: if DEAD, v2.5 architectural scope is the next step regardless. User pre-committed via "go b and then c."

## Execution log

(append-only)

### 2026-05-26 — Wiring + K=2 sweep

- Wired `uIndirectClampK` uniform (raymarch.frag), field + setter (demo3d.h), upload (demo3d.cpp), CLI `--indirect-clamp-k=K` (main3d.cpp). Clamp block lives after hybrid blend, before mode dispatch.
- Build clean (Release).
- Capture: `tools/v24b_clamp/capture_variant.ps1` → cornell/cam0/MB-ON g=1.0/hybrid-OFF/N=2048/mode-17, K=2. 55.8 s wall.
- Analyzer: `tools/v24b_clamp/analyze_v24b.py` → `v24b_results.json`.

**Metrics:**

| Arm        | ratio | \|p50\| | \|p95\| | dim%  | bright% |
|------------|-------|---------|---------|-------|---------|
| Default K0 | 0.977 | 0.255   | 0.883   |  5.1  | 11.1    |
| Clamp K2   | 0.769 | 0.315   | 0.878   | 24.3  |  7.2    |
| Δ          | -0.208 | +0.060 | -0.005 (-0.5%) | +19.22 pp | -3.91 pp |

**Verdict: DEAD.** Three of four bars failed:

- `|p95|` drop: +0.5% vs ≥10% MARGINAL floor — clamp barely touched the bright tail.
- ratio shift: 0.208 vs ≤0.15 MARGINAL ceiling — global dimming dominated.
- dim%: +19.22pp vs ≤+5pp MARGINAL ceiling — clamp pushed ~1/5 of pixels into the dim band.
- bright%: −3.91pp passed the STRONG bar in isolation, but only because the clamp dragged bulk pixels down past the bright threshold, not because fireflies were neutralized.

**Mechanism interpretation:** luminance-proportional scaling is a global dimmer, not a firefly leash. The leak appears as a luminance *bias* across a broad pixel population, not as wild spikes against a dark direct, so `lum_indirect / lum_direct > 2` triggers far beyond firefly pixels. Each triggered pixel is reduced by a modest factor, producing a small ratio shift everywhere → bulk pixels cross the dim threshold; the bright tail (the actual problem) is not isolated by this ratio because the direct term is non-trivial in those regions too.

**Action taken:** CLI flag retained for debug only; default stays K=0. No preset created. v2.4.b axis closed.

**Pivot:** v2.5 architectural pass. Symptom clamps on the *output* (v2.4.b) and on the *bake bin* (v2.4 C0 dirRes) are both DEAD. Remaining hypothesis class is structural: how the cascade integral itself is formed (probe placement, ray budget, cone weighting, merge geometry), not how its outputs are post-processed.
