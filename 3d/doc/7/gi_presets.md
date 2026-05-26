# GI Quality Presets — MBRC v2.0 post-fix rebuild

**Last updated:** 2026-05-25 (post Deltas #1+#2 land).
**Replaces:** the 2026-05-19 sweep-derived presets (cornell-orig + directional),
which silently went stale on 2026-05-24 when `useSpatialTrilinear` flipped
default-OFF (Phase 3 became dormant under default config).

## The 4 buttons

Source: [demo3d.cpp:5417-5460](../../src/demo3d.cpp#L5417-L5460).

| Preset            | MB  | gain | ST  | WS  | Intent                                          |
|-------------------|-----|------|-----|-----|-------------------------------------------------|
| Cheap             | OFF | –    | OFF | OFF | Single-bounce only, fastest                      |
| **Default**       | ON  | 1.0  | OFF | OFF | Post-fix CV1 config (ratio 0.846, dim% 16.7%)   |
| Color-bleed       | ON  | 1.5  | OFF | OFF | Stronger color transport, no Phase 3            |
| Leak-suppressed   | ON  | 1.0  | ON  | ON  | Phase 3 active — leak-gated tail, ratio trades  |

Tokens:
- **MB** = `useMultiBounce` — temporal feedback C0-atlas multi-bounce.
- **ST** = `useSpatialTrilinear` — 8-neighbor merge (vs nearest-parent).
- **WS** = `useWeightedSample` — Phase 3 bake-side per-corner visibility gating.

## Why the rebuild

Two staleness issues with the 2026-05-19 presets, both rooted in architectural
changes that landed AFTER that sweep ran:

### 1. Phase 3 (WS) was silently dormant on Balanced & Max GI

WS only activates under `useWeightedSample && !useColocatedCascades &&
useSpatialTrilinear` (see [demo3d.cpp:6559](../../src/demo3d.cpp#L6559)).
On 2026-05-24, `useSpatialTrilinear` default flipped 1→0 because cross-scene
A/B (cornell, cornell-orig) showed ST=1 dilutes mean cascade ratios by
averaging dim neighbor probes (-14% relative, RMSE-vs-PT also higher under
ST=1). Refs:
[v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md),
[v20_cprime3_st0_mitigation_impl.md](v20_cprime3_st0_mitigation_impl.md).

Consequence: clicking the old "Balanced" or "Max GI" set
`useWeightedSample=true` but the bake never called `sampleUpperDirWeighted`.
Empirically confirmed today: a CV1 sweep with `--use-weighted-sample=1` (no
ST flag) produced EXRs byte-identical (MD5 match across all 5 N) to a sweep
with `--use-weighted-sample=0`. The toggle was wired but ineffective.

Fix: presets that *promise* Phase 3 must flip BOTH ST=1 and WS=1.
"Leak-suppressed" is the only preset where Phase 3 truly runs.

### 2. Tooltip ratios were pre-fix

The old tooltip cited "Cheap 1.0x / Balanced 1.35x / Color-bleed 2.0x / Max
GI 1.8x" — measurements taken before today's paired Deltas #1+#2 fix
([raymarch.frag:444-485](../../res/shaders/raymarch.frag#L444-L485)) which
removed the consumer-side `* a.a` weighting and replaced
normalize-by-Σ(cos·a) with the proper Riemann sum `(4/D²) × Σ(L · cos⁺)`.

Post-fix, cornell cam0 MB-ON g=1.0 hybrid-OFF measures ratio 0.846 (was
0.650 pre-fix) at N=2048. Absolute cascade luminance shifted +30%; the
pre-fix tooltip numbers are no longer meaningful as guidance.

Fix: tooltip now cites the post-fix CV1 measurement (cornell, cam0) and
defers to [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md) for full table.

## Trade-off: "Default" vs "Leak-suppressed"

Both use MB g=1.0. The difference is whether Phase 3 activates.

Phase 3 needs ALL of **DM + ST + WS** + non-colocated
([radiance_3d.comp:667](../../res/shaders/radiance_3d.comp#L667)).
DM was a silently-required third gate (discovered 2026-05-25, see
[v20_postfix_leaksupp_cv1_impl.md](v20_postfix_leaksupp_cv1_impl.md)
§1). The Leak-suppressed preset and the `p3effective` indicator now
enumerate all 4 flags.

| metric (N=2048) | Default (DM=0, ST=0, WS=0) | Leak-suppressed (DM=1, ST=1, WS=1) |
|---|---|---|
| ratio_mean     | **0.977** | 0.574 (out of band — below DIM_HARD floor 0.60) |
| dim%           | 28.5%     | **90.0%** (regression)                            |
| bright%        | 11.1%     | 4.5% (improvement — Phase 3 trims bright tail)  |
| \|p95\| log    | 1.045     | 1.246 (slightly **worse**, not better)            |

(Numbers use analyzer `analyze_cv1_ws.py`'s mask `pti>0.05 & casc>0.001`.
The 0.846 / |p95| 2.27 figures in older docs use a different mask;
both are valid views of the same EXRs.)

**Verdict:** Phase 3 in this scene attenuates ~40% of indirect without
shrinking the tail — the `aFactor` multiplier in `radiance_3d.comp`
gates visibility globally, but cornell's open-volume geometry has few
real occluders aligned with the bake direction, so most attenuation is
spurious. LS works as a **bright-tail clamp** (bright% −6.6 pp) but
regresses everything else.

**Recommendation:** Default (no Phase 3) is the v2.1 ship configuration.
LS stays in the matrix as an opt-in tool for users who want to suppress
bright outliers and accept aggressive dimming.

**Next step (v2.2):** reshape `aFactor` so it gates only the bright tail
without dimming the mean. Candidates listed in
[v20_postfix_leaksupp_cv1_impl.md §5](v20_postfix_leaksupp_cv1_impl.md).

## Capture procedure (already executed 2026-05-25)

```powershell
& ./tools/v20_convergence/cv1_capture_leaksupp.ps1
# captures land in tools/v20_convergence/captures_cv1_postfix_leaksupp/
python tools/v20_convergence/analyze_cv1_ws.py
# results JSON: tools/v20_convergence/captures_cv1_postfix_leaksupp/cv1_leaksupp_results.json
```

## Cross-reference

- Fix landing: [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md)
- Theoretical derivation: [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
- ST flip rationale: [v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md)
- WS bake impl: [doc/6/claude_plan/visibility_phase3_impl.md](../6/claude_plan/visibility_phase3_impl.md)
- Preset code: [demo3d.cpp:5417-5460](../../src/demo3d.cpp#L5417-L5460)
- LS verdict + v2.2 candidates: [v20_postfix_leaksupp_cv1_impl.md](v20_postfix_leaksupp_cv1_impl.md)
