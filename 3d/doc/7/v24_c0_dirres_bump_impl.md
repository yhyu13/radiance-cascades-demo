# v2.4 — C0 directional-resolution bump (D=8 → D=16)

**Date:** 2026-05-25.
**Predecessor:** [v23_leak_attribution_impl.md](v23_leak_attribution_impl.md) (MARGINAL — 0.7% volume support, no hot probes; pattern consistent with bake-bin discretization at C0).
**User direction:** "go for v2.4" — proceed past Step 0 MARGINAL gate into a code-level fix attempt.

## Premise

After v2.0 paired Deltas fixed the consumer-side integrand, the residual
bright tail (|p95|=1.045, bright%=11.1% on cornell/cam0/N=2048) was
hypothesized to be **bake-bin discretization at C0**:

- C0 has the finest spatial resolution (32³ probes, cellSize=0.125) but the
  COARSEST directional quantization (D=8 → 64 bins/probe).
- C1–C3 already run at D=16 (256 bins) — the engine caps at D=16 via
  [demo3d.cpp:4049](../src/demo3d.cpp#L4049) `min(16, dirRes<<i)`.
- A bright reflected-radiance lobe (green wall light bouncing onto box
  faces, floor) lands in one D=8 bin. The integrand `Σ(L · cos⁺)` then
  receives that bin's radiance scaled by its 1/64 solid-angle weight.
  If the lobe is narrower than 1/64 sr (very likely for a planar wall
  reflection), the in-bin average over-estimates the directional radiance.
- Bumping C0 to D=16 quadruples the bin count, sharpening the lobe by
  ~4× and reducing the in-bin over-estimate.

v2.3 cluster inspection ([tools/v23_attribution/cluster_inspection.json](../tools/v23_attribution/cluster_inspection.json))
supports this: the dominant X-cell is 23 (interior side of green wall,
1733 bright pixels = 41%); leak density per pixel peaks at cells 14-15
(box-surface cells receiving wall reflection). Both signatures are
consistent with "C0 atlas over-fires at receiving surfaces near a
saturated wall."

## What changes

ONE flag flip at capture time: `--cascade-dir-res=16` (was 8 default).
With `--cascade-scaled-dir-res=1` (default), this becomes:

| Cascade | Before (D, bins) | After (D, bins) | Change |
|---------|------------------|-----------------|--------|
| C0      | 8,  64           | 16, 256         | **+4×**|
| C1      | 16, 256          | 16, 256         | none   |
| C2      | 16, 256          | 16, 256         | none   |
| C3      | 16, 256          | 16, 256         | none   |

No shader code changes. No new uniforms. The cascade-rebuild watcher
re-allocates C0's atlas from 256×256×32 (16 MB RGBA16F) to 512×512×32
(32 MB). Bake compute for C0 quadruples; consumer-side cost unchanged
(it samples one bin per ray and the bin lookup is O(1)).

## Pre-committed verdict bands

Captured baseline (Default v2.0 postfix, cornell/cam0/MB-ON g=1.0/hybrid-OFF/N=2048):

- ratio = 0.977 (PT within 2.3%)
- |p95| = 1.045
- bright% = 11.1%, dim% = 28.6%

| Outcome  | |p95| change                | ratio change       | bright% change | dim% change | Action |
|----------|-----------------------------|--------------------|----------------|-------------|--------|
| STRONG   | drops by ≥30% (|p95| ≤ 0.73) | shift ≤ 0.05       | drops by ≥3pp  | not worse by >3pp | Ship as Default; v2.x terminus |
| MARGINAL | drops 10-30% (|p95| 0.73–0.94)| shift ≤ 0.10       | drops by 1-3pp | not worse by >5pp | Ship as opt-in preset "C0HD"; document the bake-cost trade |
| DEAD     | drops < 10% (|p95| > 0.94)   | OR shift > 0.10    | OR worsens     | OR worsens by >5pp | Revert; document; pivot to v2.5 (architectural) |

The asymmetric "ratio shift ≤" gate is critical: if D=16 sharpens the
lobe but the cascade now under-fires (cooler bright pixels, dimmer dim
pixels, lower ratio overall), the fix has TRADED bright-tail for
dim-tail — same Pareto frontier, no net improvement.

**Lock**: these bands are committed BEFORE the sweep runs. The verdict
script reads `cv1_results.json` and emits STRONG/MARGINAL/DEAD per these
thresholds; no post-hoc reading.

## Execution plan

1. **Capture A/B** (~5 min)
   - Baseline: existing `tools/v20_convergence/captures_cv1_postfix/N2048` (no recapture)
   - Variant:  new capture with `--cascade-dir-res=16` at cornell/cam0/N=2048
   - Save to `tools/v24_c0_hd/captures/`

2. **Analyze** (~30 s)
   - Reuse `tools/v20_convergence/analyze_cv1.py` (point at both dirs)
   - Emit `v24_results.json` with ratio, |p95|, dim%, bright%, deltas

3. **Apply gate** (~immediate)
   - Compare against pre-committed bands; emit verdict
   - Update this doc's Execution log + memory

4. **Confirmation arm if STRONG** (~5 min)
   - Re-capture at cornell/cam2 to check cam-invariance
   - If cam2 also clears STRONG band → ship as Default
   - If cam2 regresses → demote to opt-in preset

## Risk register

- **Memory**: +16 MB for C0 atlas — fits well within RGBA16F budget for cornell.
- **Bake cost**: C0 bake quadruples (~+0.8 ms typical). MB-ON convergence
  may shift (more bins to populate means more frames to settle). Capture
  uses N=2048 which is far past PT/cascade asymptote per
  [[project_mbrc_v20_postfix_landed]] — should not be load-bearing.
- **C1 reach mismatch**: C0 and C1 now have the same D (16) but C0 has
  4× the spatial resolution. The bake-merge at C0 still pulls from C1's
  16² bins. This is already the established geometry — no new mismatch.
- **DEAD verdict**: if it happens, the residual leak is NOT bake-bin
  discretization. The remaining mechanism candidates would be (a) C1's
  cell footprint smearing wall light into adjacent voxels (would need
  C1 spatial resolution bump — way more expensive), or (b) the merge
  formula's visibility term — but v2.2 already ruled THAT out at
  Step 0. DEAD here implies v2.5 architectural.

## Execution log

- **2026-05-25** Scope locked. Execution starting.
- **2026-05-25** Capture executed. Variant: `tools/v24_c0_hd/captures/v24_cornell_cam0_mbon_g100_hyb0_N2048_m17_c0d16_*`.
  Variant bake took 132s vs baseline 56s (~2.4× — consistent with C0-only
  quadrupling of bin count). C0 atlas: 256×256×32 → 512×512×32 as expected.
- **2026-05-25** Analysis ran via `tools/v24_c0_hd/analyze_v24.py`.

### Results

| Metric        | Default (D=8) | C0HD (D=16) | Δ          | Gate band |
|---------------|---------------|-------------|------------|-----------|
| ratio         | 0.977         | 0.980       | +0.002     | STRONG (≤0.05) |
| \|p50\|       | 0.255         | 0.234       | −0.021     | n/a (informational) |
| \|p95\|       | 0.883         | 0.882       | **−0.001 (−0.1%)** | DEAD (<10%) |
| dim%          | 5.1           | 5.0         | −0.09 pp   | STRONG (≤+3pp) |
| bright%       | 11.1          | 10.2        | −0.86 pp   | DEAD (<1pp) |

### Verdict: **DEAD** — bake-bin discretization is NOT the bright-tail mechanism.

|p95| barely moved (the headline failure: 0.1% drop vs 30% STRONG bar
and 10% MARGINAL bar). The slight |p50| improvement (8%) and bright%
nudge (−0.86 pp) suggest D=16 modestly cleans up median pixels — but
the bright tail itself is structurally unaffected by directional
resolution. Quadrupling C0 bins is *correctly more expensive bake* but
not a leak fix.

### Mechanism autopsy

Pre-commit reasoning: bright outliers are receiving surfaces near a
saturated wall; D=8 binning groups the bright wall-direction lobe too
coarsely; D=16 should sharpen it. Reality: |p95| sits on the same 235
contributing cells regardless of D. The wall lobe IS being captured
correctly already — going finer in direction doesn't reduce per-bin
over-fire because the radiance INTO each bin is what it is; finer
quantization just slices the same incident lobe into more bins. The
issue is upstream of binning.

### Remaining v2.x candidates (for user decision)

After v2.2 (merge-formula reshape) DEAD and v2.4 (C0 dirRes) DEAD,
three theoretical candidates remain for the bright-tail mechanism:

1. **Per-pixel post-process clamp** (`indirect ≤ K × direct` in screen
   space). Simple, addresses symptom not cause. ~30 min to wire,
   ~5 min A/B. Not principled — it's a firefly leash on indirect GI.
2. **C1 spatial smoothing reduction** (currently trilinear; try nearest
   parent only). Tests whether C1-to-C0 interpolation is the smear
   source. Could trade dim%/bright% asymmetrically.
3. **Architectural v2.5** — bigger redesign (e.g., visibility-aware
   bake-merge with per-bin opacity, or a separate "specular indirect"
   path). Multi-day scope.

### Action per pre-committed gate

Revert: no CLI flag promoted, no preset added. Doc captures the result
so the C0 dirRes bump isn't re-tried later. Memory updated.
