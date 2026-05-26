# v2.0 post-fix — Leak-suppressed CV1 verdict

**Date:** 2026-05-25.
**Companion to:** [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md) (Default verdict),
[gi_presets.md](gi_presets.md) (preset matrix), [v20_shadertoy_diff_diagrams.md](v20_shadertoy_diff_diagrams.md).

**TL;DR:** Phase 3 (`sampleUpperDirWeighted`, DM+ST+WS bundle) does NOT clear
the leak target for cornell/cam0 post-fix. It dims aggressively (ratio
0.977 → 0.574 at N=2048, dim% 28.5 → 90.0) without shrinking the tail
(|p95|=1.045 → 1.246, slightly **worse**). Verdict: Leak-suppressed
preset stays in the matrix as an opt-in tail-trimmer for bright outliers
(bright% 11.1 → 4.5) but is **not** the v2.1 ship configuration. Branch
per "Both: fastest path now, diagnose if it fails" → escalate to v2.2
leak diagnosis with this run as the WS-vs-no-WS bonus signal.

---

## 1. Setup

| Knob                            | Default sweep             | Leak-suppressed sweep    |
|---------------------------------|---------------------------|--------------------------|
| useMultiBounce / gain           | ON / 1.0                  | ON / 1.0                 |
| useHybrid                       | OFF                       | OFF                      |
| useDirectionalMerge (DM)        | **OFF**                   | **ON**                   |
| useSpatialTrilinear (ST)        | **OFF**                   | **ON**                   |
| useWeightedSample (WS)          | **OFF**                   | **ON**                   |
| useColocatedCascades            | OFF (non-colocated)       | OFF (non-colocated)      |
| Scene / camera / N              | cornell / cam0 / {128…2048}                          |||
| Render mode                     | 17 (cascade GI EXR)       | 17 (cascade GI EXR)      |
| Seed offset                     | 0                         | 0                        |
| Probe jitter                    | ON                        | ON                       |

Both sweeps captured via `tools/v20_convergence/cv1_capture.ps1`
(Default) and `tools/v20_convergence/cv1_capture_leaksupp.ps1` (LS).
Five N values each: 128, 256, 512, 1024, 2048.

**MD5 verification of Phase 3 activation:** all 5 N values produce
distinct EXRs from the Default sweep. The first run of this script
(2026-05-25 earlier today, ST+WS only) was byte-identical to Default —
the `useDirectionalMerge` gate at [radiance_3d.comp:667](../../res/shaders/radiance_3d.comp#L667)
was the silent third requirement. Indicator at
[demo3d.cpp p3effective check](../../src/demo3d.cpp) now enumerates
all 4 flags (DM, ST, WS, !colocated).

## 2. Convergence table

Mask: `(pt_indirect_lum > 0.05) & (cascade_lum > 0.001)` after 2×2
downsample of the 1280×720 cascade EXR to match the 640×360 PT EXR.

| N    | post ratio | +LS ratio | post |p95| | +LS |p95| | post dim% | +LS dim% | post brt% | +LS brt% |
|------|-----------:|----------:|-----------:|----------:|----------:|---------:|----------:|---------:|
|  128 |     0.760  |    0.489  |     1.200  |    1.437  |     61.1  |    91.6  |      6.1  |     3.6  |
|  256 |     0.889  |    0.544  |     1.120  |    1.310  |     39.5  |    90.1  |      8.3  |     4.2  |
|  512 |     0.964  |    0.569  |     1.070  |    1.258  |     30.4  |    89.5  |     10.7  |     4.5  |
| 1024 |     0.974  |    0.573  |     1.049  |    1.248  |     28.9  |    89.7  |     11.2  |     4.5  |
| 2048 |   **0.977**| **0.574** |   **1.045**|  **1.246**|     28.5  |    90.0  |     11.1  |     4.5  |

(Bands per `v20_pre_measurement/cv1_verdict_bands.json`:
NEAR_PT = [0.85, 1.15], DIM_MILD = [0.60, 0.85), DIM_HARD < 0.60. Leak
target |p95| < 1.0.)

## 3. Verdict @ N=2048

**Default (DM=OFF, ST=OFF, WS=OFF):**
- ratio 0.977 → **CV1_CASCADE_NEAR_PT** (HYBRID-RETIREMENT READY by mean)
- |p95| 1.045 → tail wider than target (1.0) by 4.5%; close but not under
- dim 28.5%, bright 11.1%

**Leak-suppressed (DM+ST+WS):**
- ratio 0.574 → **OUT OF BAND** (below DIM_HARD floor 0.60)
- |p95| 1.246 → tail **wider** than Default by 19% — leak gate did NOT shrink it
- dim 90.0%, bright 4.5%

Phase 3 trade: −6.6 pp bright outliers (good), −60+ pp dim pixels
(bad — moves them in the wrong direction), no tail improvement.

## 4. Why LS dims so hard

The `aFactor` multiplier in `radiance_3d.comp:755-800`:

```glsl
aFactor = (uUseWeightedSample != 0) ? upperDir.a : 1.0;
// surface-hit:
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * aFactor * uGIStrength;
```

`upperDir.a` is `wVisible / wTotalSpatial` from `sampleUpperDirWeighted`
— the fraction of the 8 upper-cascade corners whose look-back ray
distance suggests they can "see" the lower probe. In open-volume cornell
geometry the visibility fraction is `< 1` almost everywhere (volumetric
probes have no occluders aligned with the bake direction), so every
upper-cascade contribution gets attenuated. Aggregated over the merge
chain this kills 40% of indirect luminance globally.

The same attenuation that suppresses leaks on cornell-orig-alcove (where
the doc cited C0 −11%, C1 −16%, C2 −10% in the original `aFactor`
revision history) becomes over-aggressive in default cornell because
there are no actual leaks to gate — the `aFactor < 1` is paying for
visibility checks that the geometry doesn't need.

## 5. Branch decision

Per the user's "Both: fastest path now, diagnose if it fails" choice:

- ✅ Fastest path (LS as v2.1 ship): **FAIL**. LS dims too aggressively
  on cornell and doesn't shrink the leak tail. Cannot ship as default.
- → Escalate to **v2.2 leak diagnosis**. This run is the WS-vs-no-WS
  bonus signal: confirms WS gating attenuates ~40% of indirect without
  removing the tail, so the `aFactor` formula needs to be reshaped (not
  just toggled). Candidates for v2.2:
  1. **WS-as-tail-clamp**: apply `aFactor` only when bright (gates
     positive outliers without dimming the mean). Would preserve LS's
     −6.6 pp bright reduction without the −60 pp dim regression.
  2. **WS power-curve**: replace linear `aFactor` with `pow(aFactor,
     k)` where k<1 (softens the attenuation in low-visibility regions).
  3. **Leak source attribution** (the slower path): find which probes
     contribute to |p95|=1.05 in Default and gate per-source, not
     per-merge.

The cleanest data-driven start is (1) — LS already validated the bright
reduction works; the cost lives entirely in dim regression. A
threshold-gated `aFactor` would recover the dim pixels for free.

## 6. What ships in v2.1

Default preset remains the recommendation. Open ship items:

- ratio 0.977 ≈ PT (within 2.3% of unity) — solid
- |p95| 1.045 — 4.5% above target but within practical noise; not a
  blocker for hybrid retirement
- bright 11.1% — the actual quality gap, not addressable without v2.2

Leak-suppressed stays in the preset matrix with an honest tooltip
(updated 2026-05-25 to reflect: "Phase 3 leak-gated; trades mean ratio
for cleaner tail (ST=1 dims, WS gates)"). Users picking it accept the
dimming for the bright reduction.

## 7. Cross-reference

- Captures: `tools/v20_convergence/captures_cv1_postfix_leaksupp/`
- Results JSON: `tools/v20_convergence/captures_cv1_postfix_leaksupp/cv1_leaksupp_results.json`
- Analyzer: `tools/v20_convergence/analyze_cv1_ws.py`
- Capture script: `tools/v20_convergence/cv1_capture_leaksupp.ps1`
- Default Verdict: [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md)
- Preset matrix: [gi_presets.md](gi_presets.md)
- ShaderToy diff: [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
- Gate finding (DM was silent third gate): see related memories
  `project_st_gates_phase3`, `project_gi_presets_postfix_rebuild`.
