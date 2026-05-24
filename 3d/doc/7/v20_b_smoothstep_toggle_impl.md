# v2.0 (h.b) Smoothstep Blend-Zone Toggle — Implementation & Falsification

**Status:** Falsification complete. Hypothesis (b) **REJECTED**.
**Date:** 2026-05-24
**Predecessor:** [v20_h3_mb_factorial_impl.md](v20_h3_mb_factorial_impl.md)

## 1. Hypothesis & test design

**Hypothesis (b):** The smoothstep S-curve in the cascade→upper blend zone at
[radiance_3d.comp:771](../../res/shaders/radiance_3d.comp#L771)
compresses cascade contribution near `tMax`, under-supplying side-illuminated
regions (cam2 alcove).

**Mechanism if true:** `smoothstep(0,1,t)` has zero slope at `t=0` and `t=1`,
meaning rays whose hit distance falls into the inner half of the blend window
(`hit.a` close to `tMax - blendWidth`) keep almost their full cascade weight,
while rays in the outer half (`hit.a` close to `tMax`) hand off most weight to
the upper cascade. If cam2's geometry biases hit distances into the outer half
of the blend zone, the S-curve preferentially under-supplies it.

**Test:** A/B the blend curve at the most-feature-rich merge (M0:
`dm=1 db=1 st=1`), MB-OFF, b=2, on cam0 + cam2. Three modes:
- 0 = `1 - smoothstep(0,1,t)` (default, baseline)
- 1 = `1 - t`               (linear ramp)
- 2 = `1 - step(0.5, t)`    (hard handoff at midpoint)

6 cells total, ~1.1 min.

## 2. Implementation

### Shader (radiance_3d.comp)
Added uniform `uniform int uBlendMode;` and replaced the single smoothstep call
at line ~771 with a three-mode `if/elseif/else`:
```glsl
float t = clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
if (uBlendMode == 2)      l = 1.0 - step(0.5, t);
else if (uBlendMode == 1) l = 1.0 - t;
else                      l = 1.0 - smoothstep(0.0, 1.0, t);
```
Default = 0 preserves bit-exactness with prior behavior.

### C++ (demo3d.h / demo3d.cpp / main3d.cpp)
- `int blendMode = 0;` field with setter mirroring `setPhase3DebugMode` pattern
  (cache invalidation chain: `cascadeReady=false; forceCascadeRebuild=true;
  renderFrameIndex=0; historyNeedsSeed=true`)
- Uniform binding: `glUniform1i(glGetUniformLocation(prog, "uBlendMode"), blendMode);`
- CLI flag `--blend-mode={0,1,2}` parsed after `--use-spatial-trilinear=`

Tests pass: build clean (warnings only, all pre-existing), `--blend-mode` echo
visible in capture logs for all 6 cells.

## 3. Results

### Per-cell measurements (M0, MB-OFF, b=2)
| mode       | cam | integPT  | integCasc | casc/PT |
|------------|-----|----------|-----------|---------|
| smoothstep |   0 | 2773.872 |  1206.084 | 0.4348  |
| smoothstep |   2 | 3077.687 |   869.442 | 0.2825  |
| linear     |   0 | 2773.872 |  1201.178 | 0.4330  |
| linear     |   2 | 3077.687 |   864.973 | 0.2810  |
| step       |   0 | 2773.872 |  1210.139 | 0.4363  |
| step       |   2 | 3077.687 |   867.426 | 0.2818  |

### Deltas vs smoothstep baseline
| cam | mode   | ratio  | delta    | abs    |
|-----|--------|--------|----------|--------|
|   0 | linear | 0.4330 | -0.0018  | 0.0018 |
|   0 | step   | 0.4363 | +0.0015  | 0.0015 |
|   2 | linear | 0.2810 | -0.0015  | 0.0015 |
|   2 | step   | 0.2818 | -0.0007  | 0.0007 |

### Spread (cam2/cam0)
| mode       | cam0   | cam2   | c2/c0  | Δ vs smoothstep |
|------------|--------|--------|--------|-----------------|
| smoothstep | 0.4348 | 0.2825 | 0.6497 | —               |
| linear     | 0.4330 | 0.2810 | 0.6490 | −0.0007         |
| step       | 0.4363 | 0.2818 | 0.6460 | −0.0037         |

## 4. Verdict

**`BLEND_ZONE_NOT_THE_BUG`** (cam2 max |delta| = 0.0015 ≪ 0.02 threshold).

All three blend curves produce a cam2 cascade/PT ratio within **0.5% relative**
of one another. The cam2/cam0 spread stays at **0.65 ± 0.004** regardless of
which curve we use to weight the inner-vs-outer cascade contribution. Even the
hard `step` at midpoint — which is the maximum perturbation we can introduce
without changing the blend window — fails to move cam2 by a perceptible amount.

The pre-committed bands:
- ≤ 0.02 → `BLEND_ZONE_NOT_THE_BUG` ✓ (we are well inside this)
- 0.02..0.10 → partial contributor
- > 0.10 → primary suspect

We land cleanly in the innocent band, far from the boundary.

## 5. Architectural implication

The smoothstep S-curve is **innocent of the cam0/cam2 spread**. This rules out
the entire family of "blend zone misweighting" hypotheses. Remaining live
suspects for the spread:

1. **Cascade content itself** — the values stored in the cascade atlas before
   blending are already cam2-dim. Test path: (c) mode-8 probe-cell fract viz,
   visual A/B for probe-grid alignment issues; mode-9 ray-hit-count viz to see
   if cam2's direction bins receive fewer hit samples.
2. **Directional sampling at probe centers** — the per-direction bin lookup
   may oversample probes whose direction bins are biased away from cam2's
   incidence angles. Test path: per-direction-bin energy histograms.
3. **Atlas spatial sampling (probe-grid alignment)** — fractional probe-cell
   offsets may differ systematically between cam0 and cam2 pixels, causing
   different effective probe weights even when nominal trilinear weights agree.
   Test path: mode-8 `fract(pg)` viz (Option (c) above).

## 6. Self-critique

**Strengths of this test:**
- Three blend curves span the full reasonable design space (smooth, linear,
  discontinuous-at-midpoint).
- Bit-exactness preserved on default (no risk of regression from instrumentation).
- Same PT reference for all six cells (`integPT` identical within each cam).
- Falsification is clean and far from band boundary — none of the recurring
  "MIXED verdict" concerns from (h), (h.2), (h.3) apply here.

**Weaknesses to acknowledge:**
- **Integrated stats only.** If the blend zone only affects a small fraction of
  pixels, a large per-pixel delta could be lost in the integration. The 0.0015
  global delta is consistent with both "blend is innocent" AND "blend zone is
  small and even a big local change doesn't matter for total energy." But the
  question we asked was about cam0/cam2 *spread*, and the spread itself stays
  put — so even on the "local but invisible globally" interpretation, the blend
  curve shape isn't the spread driver.
- **M0 only.** We didn't test whether blend curve interacts with merge variants
  (e.g., maybe blend matters under M4 isotropic-nearest where the upper-cascade
  contribution has more high-frequency content). However: cam2 cascade/PT was
  ~0.33 under M0/M2/M4 in (h.2), so the spread is merge-invariant and any
  blend×merge interaction is unlikely to be the spread origin.
- **MB-OFF only.** MB-ON might amplify blend zone effects (temporal
  accumulation of small per-frame asymmetries). Not tested here. But (h.3)
  showed MB amp is merge-invariant; we don't have prior evidence that MB would
  selectively amplify blend curve choice.

**On the recurring band-boundary problem:** Unlike (h) / (h.2) / (h.3), this
verdict is decisive. The 0.0015 delta is **13× smaller** than the band's lower
edge (0.02). Pre-committed bands work fine when the underlying effect is
genuinely zero — they fail mostly when the effect is "small but real,"
which is the previous three measurements' situation.

## 7. Recommended next step

Proceed to **(c) mode-8 fract viz** per the user's "Ok do C first then A"
directive. Capture render mode 8 (probe-cell boundary fract(pg) RGB) on cam0
and cam2 to visually test whether cam2 oversamples probe-cell boundaries — the
most concrete remaining cascade-content hypothesis. Estimated ~10 min cap +
analysis.

## 8. Artifacts

- Code: `res/shaders/radiance_3d.comp` (uBlendMode uniform + 3-mode if), `src/demo3d.h` (blendMode field + setter), `src/demo3d.cpp` (init + uniform binding), `src/main3d.cpp` (CLI flag)
- Capture script: [tools/v20_arch_diagnostic/h4_smoothstep_capture.ps1](../../tools/v20_arch_diagnostic/h4_smoothstep_capture.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h4_smoothstep.py](../../tools/v20_arch_diagnostic/analyze_h4_smoothstep.py)
- Captures: `tools/v20_arch_diagnostic/captures_h4_smoothstep/` (24 files: 6 cells × 4 file types)
- Results JSON: `tools/v20_arch_diagnostic/h4_smoothstep_results.json`
