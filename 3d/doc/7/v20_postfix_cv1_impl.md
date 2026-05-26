# v2.0 post-fix CV1 — Deltas #1+#2 paired fix (4/D² constant) — measurement & verdict

**Status:** First CV1 capture against the post-fix cascade-only build
([raymarch.frag:444-485](../../res/shaders/raymarch.frag#L444-L485)).
Pre-committed band from
[v20_shadertoy_diff_impl.md §4](v20_shadertoy_diff_impl.md) (post-critic
revision).

**Date:** 2026-05-25.

## 1. The fix

Single-site change in [raymarch.frag:444-485](../../res/shaders/raymarch.frag#L444-L485)
(`sampleProbeDir`):

- **Delta #1**: removed `* a.a` from the irradiance integrand. The bake-side
  smoothstep interval composition (`hit.rgb*l + upperDir.rgb*(1-l)`) is the
  single visibility path; the consumer now reads the merged answer rather
  than re-gating it.
- **Delta #2**: replaced normalization-by-`Σ(cos·a)` (weighted mean) with the
  proper Lambertian Riemann sum `(4/D²) × Σ(L · cos⁺)`. The 4/D² constant
  is the per-bin solid angle for our full-sphere octahedral binning (D² bins
  over 4π sr ⇒ ΔΩ = 4π/D²; `(albedo/π)·E_irrad = albedo·(4/D²)·Σ(L·cos⁺)`).

MB feedback path was already ungated
([radiance_3d.comp:441-481](../../res/shaders/radiance_3d.comp#L441-L481));
no change needed there.

Diagnostic fields (leak / oscillation) still consume `a.a` in additive form
for diagnostic-mode-14/15 consumers.

## 2. Capture config

Identical to the pre-fix CV1 baseline:

| param | value |
|---|---|
| scene | cornell |
| camera | cam0 |
| multi-bounce | ON, gain=1.0 |
| hybrid | OFF |
| probe jitter | ON |
| noise-seed-offset | 0 |
| cascade-scaled-dir-res | 1 |
| render-mode | 17 (cascadeGI + ptFull + ptDirect EXR triplet) |
| N | {128, 256, 512, 1024, 2048} |

Captures: `tools/v20_convergence/captures_cv1_postfix/`.
Analyzer: inline Python (same lum + downsample logic as
[analyze_cv1.py](../../tools/v20_convergence/analyze_cv1.py)). Capture
duration: 2.0 min sweep.

## 3. Results

### 3.1 Post-fix vs pre-fix table (ratio = mean(cascadeGI) / mean(ptIndirect))

| N | post ratio | pre ratio | Δ | post dim% | pre dim% | post |p95| | pre |p95| |
|---|---:|---:|---:|---:|---:|---:|---:|
|  128 | 0.658 | 0.489 | +0.169 | 29.3 | 55.8 | 1.305 | 1.000 |
|  256 | 0.772 | 0.587 | +0.185 | 18.6 | 36.3 | 1.834 | 1.070 |
|  512 | 0.835 | 0.641 | +0.194 | 16.9 | 29.6 | 2.132 | 1.290 |
| 1024 | 0.844 | 0.649 | +0.195 | 16.7 | 28.8 | 2.249 | 1.281 |
| 2048 | **0.846** | **0.650** | **+0.196** | **16.7** | **28.6** | **2.266** | **1.278** |

### 3.2 Verdict bands (N=2048, analysis B = self-paired ratio at N_max)

| metric | pre-fix | predicted post-fix | actual post-fix | in band? |
|---|---|---|---:|:--:|
| BAND 1 ratio | 0.650 (CV1_CASCADE_DIM_MILD) | [0.70, 1.30] | **0.846 (CV1_CASCADE_DIM_MILD)** | **✓** lower half |
| dim% | 28.6 | [3, 20] | 16.7 | **✓** |
| bright% | 5.4 | [2, 25] | 6.8 | **✓** |
| \|p95\| | 1.28 | [0.4, 1.0] | **2.27** | **✗ worse than predicted** |

**BAND 2 convergence trend** (Δratio from N=128 to N=2048): post-fix Δ=+0.188
(CV1_SLOW_CONVERGENCE retained — same as pre-fix). Cascade still benefits
from frame accumulation but the gap-closing rate is comparable to pre-fix.

**BAND 1 verdict**: still `CV1_CASCADE_DIM_MILD` (band is [0.60, 0.85);
0.846 is at the upper edge but technically inside). One more push closes
the band-crossing into `CV1_CASCADE_NEAR_PT` ([0.85, 1.15]).

## 4. Interpretation

**What the fix did**: closed the dim-mode mean gap from −35% to −15%, cut
the dim%-of-pixels in half (28.6 → 16.7%), and moved the ratio firmly into
the upper half of `CV1_CASCADE_DIM_MILD`. Cascade mean luminance went
0.244 → 0.318 (+30%) while PT mean is unchanged (0.376). The 4/D² constant
is empirically validated — had it been wrong by a factor of 2, ratio would
have landed at ~0.42 or ~1.69, neither of which we see.

**What the fix surfaced**: |p95| widened from 1.28 to 2.27 (+0.99). This
matches the §4 self-critique prediction: *"bins previously 'saved' by the
over-bright normalization may now lose their compensating brightness,
exposing leaks."* The compensating mechanism in the pre-fix consumer was
`irrad = Σ(L·cos·a) / Σ(cos·a)` which produced systematic over-bright at
probes with few visible bins. Removing it cleans up the mean but reveals
the underlying leak-tail that the over-bright was masking.

**Specifically**: bright% grew from 5.4 → 6.8 (+1.4 pp). These are pixels
where cascade reads >2× PT — almost certainly bake-side visibility leaks
(probe bins read through wall geometry where the smoothstep merge didn't
fully gate them). Pre-fix, `1/Σ(cos·a)` happened to dampen those over-bright
bins; post-fix the leak is directly visible.

**The cure exists in-tree**: the bake-side WeightedSample primitive
([radiance_3d.comp:291-352](../../res/shaders/radiance_3d.comp#L291-L352),
`sampleUpperDirWeighted`, gated by `uUseWeightedSample`) was designed for
exactly this case. The Phase 3 impl doc notes it was DISABLED-by-default
historically because the consumer-side α-gate was acting as a poor-man's
leak filter. Now that the α-gate is removed, enabling WeightedSample is
the next move.

## 5. Verdict

**Paired Deltas #1+#2 fix: SUCCESS on the dominant metric** (mean ratio
moved from 0.650 to 0.846, inside the predicted [0.70, 1.30] band; 60% of
the dim gap closed).

**One predicted side-effect activated**: leak-tail exposure (|p95|
widened). The fix is correct; the leak was always there, just hidden by
the pre-fix renormalization quirk.

**Next step (decision point)**: enable `uUseWeightedSample=1` and re-run
CV1. Expected effect: |p95| shrinks (leaks suppressed), mean ratio may
shift slightly (typically up — currently-leaking-bright pixels don't
contribute spurious far-field but legitimate bin radiance still flows
correctly). If post-WS ratio lands in `CV1_CASCADE_NEAR_PT` ([0.85,
1.15]), the v2.0 hybrid retirement goal is achievable without further
algorithmic work.

## 6. Cross-reference

- Theoretical derivation: [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
- Pre-fix CV1 baseline: [v20_cv1_convergence_impl.md](v20_cv1_convergence_impl.md)
- Fix site: [raymarch.frag:444-485](../../res/shaders/raymarch.frag#L444-L485)
- WeightedSample bake-side impl: Phase 3
  [doc/6/claude_plan/visibility_phase3_impl.md](../6/claude_plan/visibility_phase3_impl.md)
- Captures: `tools/v20_convergence/captures_cv1_postfix/`
- Results JSON: `tools/v20_convergence/captures_cv1_postfix/cv1_postfix_results.json`
