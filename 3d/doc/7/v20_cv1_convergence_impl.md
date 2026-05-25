# v2.0 CV1 — Absolute cascade-vs-PT convergence sweep (cornell, cam0, MB-ON, hybrid OFF)

**Status:** First measurement of the re-anchored v2.0 program. Replaces
the parked cam0/cam2 P2 framing (falsified by P2-E, commit 8007296).

**Verdicts** (pre-committed in
[cv1_capture.ps1](../../tools/v20_convergence/cv1_capture.ps1) header):

| Band | Value | Verdict |
|---|---:|---|
| BAND 1 (asymptotic ratio @ N=2048, analysis B)   | **0.6499** | **`CV1_CASCADE_DIM_MILD`** |
| BAND 2 (trend N=128→2048, analysis B)            | Δ = 0.1597 | **`CV1_SLOW_CONVERGENCE`** |
| BAND 3 (PT self-drift, mean(ptGI@128)/mean(ptGI@2048)) | 1.0022 | **`CV1_PT_WELL_CONVERGED_AT_N_MIN`** |

**Headline:** at MB-ON g=1.0, hybrid OFF, cornell cam0 view, the cascade
asymptotes to **65.0% of PT GI energy** by N≈1024 frames. The remaining
**35% mean-energy gap** is the cascade-only-vs-PT deficit that v2.0 must
close to retire hybrid. Per-pixel error tails are wider than the mean
suggests: 28.6% of valid GI pixels are still dim (cascade < 0.5× PT) at
N=2048 and 5.4% are bright (cascade > 2× PT).

**Date:** 2026-05-25

## 1. Pre-committed bands ([cv1_capture.ps1](../../tools/v20_convergence/cv1_capture.ps1) header)

**BAND 1** — Asymptotic ratio (analysis B at N=N_max, `meanCasc@2048 / meanPT@2048`):

| ratio_B at N=2048 | band |
|---|---|
| [0.85, 1.15] | `CV1_CASCADE_NEAR_PT` |
| [0.60, 0.85) | `CV1_CASCADE_DIM_MILD` ✓ |
| [0.30, 0.60) | `CV1_CASCADE_DIM_MODERATE` (matches v1.3.1 cam0=0.474 on cornell-orig-alcove) |
| (0, 0.30)    | `CV1_CASCADE_DIM_SEVERE` |
| > 1.15       | `CV1_CASCADE_BRIGHT` |

**BAND 2** — Convergence trend (|Δratio_B| from N=128 to N=2048):

| |Δ| | band |
|---|---|
| ≤ 0.05         | `CV1_TIGHT_CONVERGENCE` (cascade ~converged by N=128) |
| (0.05, 0.20]   | `CV1_SLOW_CONVERGENCE` ✓ |
| > 0.20 toward 1.0 | `CV1_FAST_CONVERGENCE` |
| ratio_2048 farther from 1 than ratio_128 | `CV1_DIVERGING` |

**BAND 3** — PT self-convergence sanity (mean(ptGI@128) / mean(ptGI@2048)):

| pt_drift | band |
|---|---|
| within 1 ± 0.10  | `CV1_PT_WELL_CONVERGED_AT_N_MIN` ✓ |
| else             | `CV1_PT_STILL_CONVERGING` |

## 2. Setup

**Capture** ([cv1_capture.ps1](../../tools/v20_convergence/cv1_capture.ps1)):

- `--load-obj=cornell` (full Cornell box, not cornell-orig or cornell-orig-alcove)
- cam0 from [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json)
  (position [0,1,3.4], target [0,1,0] — front-on)
- `--use-multi-bounce=1 --multi-bounce-gain=1.0` (engine default once MB is on)
- `--use-hybrid=0` (cascade-only baseline; the v2.0 hybrid-retirement target)
- `--cascade-scaled-dir-res=1` (engine default — direction count scales per cascade level)
- `--noise-seed-offset=0`, `--use-probe-jitter=1` (bug-234 mitigation for measurement-camera mode)
- `--render-mode=17 --screenshot-exr=1` (emits paired cascade_gi + pt_full + pt_direct EXRs)
- 5 frame counts: N ∈ {128, 256, 512, 1024, 2048}

**Analyzer** ([analyze_cv1.py](../../tools/v20_convergence/analyze_cv1.py)):

- ptGI = pt_full − pt_direct, clipped to ≥0
- cascadeGI downsampled 2x2-avg to PT's 640×360
- valid pixel mask: Lpt > 1e-3 (EPS_PT, same as existing
  [analyze_hdr_exr.py](../../tools/v20_pre_measurement/analyze_hdr_exr.py))
- Reports per-N: mean ratios (self-paired A, truth-anchored B), pt_drift,
  per-pixel |p50|/|p95| relative error, dim%/bright% pixel counts

## 3. Results

### Headline table

| N    | valid% | meanCasc | meanPT | ratio_A | ratio_B | pt_drift | \|p50\| | \|p95\| | dim% | bright% |
|----:|------:|---------:|-------:|--------:|--------:|---------:|-------:|-------:|-----:|-------:|
|  128 | 19.7% | 0.1843 | 0.3767 | 0.4891 | 0.4902 | 1.0022 | 0.543 | 1.000 | 55.8% | 4.4% |
|  256 | 19.8% | 0.2207 | 0.3762 | 0.5867 | 0.5871 | 1.0007 | 0.461 | 1.070 | 36.3% | 5.1% |
|  512 | 19.8% | 0.2408 | 0.3759 | 0.6407 | 0.6407 | 0.9999 | 0.419 | 1.290 | 29.6% | 5.4% |
| 1024 | 19.8% | 0.2438 | 0.3759 | 0.6486 | 0.6486 | 1.0000 | 0.412 | 1.281 | 28.8% | 5.4% |
| 2048 | 19.8% | 0.2443 | 0.3759 | 0.6499 | 0.6499 | 1.0000 | 0.411 | 1.278 | 28.6% | 5.4% |

`ratio_A` (self-paired: `meanCasc@N / meanPT@N`) and `ratio_B`
(truth-anchored: `meanCasc@N / meanPT@2048`) agree to 4 decimals — a
direct consequence of `CV1_PT_WELL_CONVERGED_AT_N_MIN` (PT is essentially
flat across all N).

### Convergence curve (analysis B)

```
ratio_B
  0.65 |                      ___________X========X========X    <-- asymptote 0.6499
       |                  ___/
       |              ___/
  0.55 |          ___/
       |      ___/
       |  ___/
  0.45 |X/
       |
  0.40 +----+----+----+----+----+----+
       128  256  512 1024 2048
                N (frames)
```

- N=128→256: +0.097 (largest single step)
- N=256→512: +0.054
- N=512→1024: +0.008
- N=1024→2048: +0.001 (essentially noise)

**Cascade is converged for practical purposes by N=1024.** Frames beyond
that buy <0.2% mean-ratio movement.

### PT-ref self-convergence sanity

`pt_drift @ N=128 = 1.0022` → PT-ref is within 0.22% of its asymptote at
just 128 frames. This is a useful future-cost finding: PT-ref captures
for v2.0 quality work can be run at **N=128**, not N=2048, with
negligible truth loss. **Future PT captures: budget 128 frames unless
testing very low-variance dim-tail effects.**

### Per-pixel variance tells a different story than the mean

The mean ratio of 0.65 hides considerable per-pixel spread:

- **|p50| rel = 0.411** — median pixel is ~41% wrong (under or over PT)
- **|p95| rel = 1.278** — worst 5% of pixels have >128% absolute relative
  error (cascade more than 2× away from PT in either direction)
- **dim% = 28.6%** — over a quarter of valid pixels are cascade < 0.5× PT
- **bright% = 5.4%** — small but persistent overshooting tail

The mean-ratio of 0.65 is partially a coincidence of dim and bright
contributions partially canceling. **A fix that only lifts the mean ratio
without narrowing the per-pixel spread would not necessarily improve
visual quality.**

## 4. Interpretation

### What this measures vs what it doesn't

CV1 measures the **mean-energy GI gap between cascade-only-MB-ON and PT
on a fixed scene/camera/config** — it is a single point in (scene, cam,
MB-on, hybrid-on, frame-count) hyperspace, not a global cascade quality
metric. It tells us:

- ✓ how far the asymptotic cascade is from PT at this config
- ✓ how many frames cascade needs to converge here
- ✓ how many frames PT-ref needs to be useful as truth
- ✗ whether the gap is the same on cornell-orig-alcove (different scene)
- ✗ whether hybrid currently closes this gap or only partially closes it
- ✗ whether the gap is camera-dependent (cam1, cam2)
- ✗ what kind of pixels are missing energy (back wall vs corners vs
  alcove proxies)

The next CV measurement should pin down one of these axes — most
informative next single-step is likely the cornell + hybrid-ON A/B
(quantifies what hybrid currently buys, which is the v2.0 retirement
budget).

### v1.3.1 comparison

The v1.3.1 hdr_relitigation_impl.md §4.1 reports cam0 g=1.0 MB-ON ratio
**0.474** on cornell-orig-alcove with 512 frames. CV1 at the same
frames/config on cornell (full Cornell box) reads **0.6407** — a
**+35.1% improvement (0.6407/0.474)** purely from scene geometry change.
This is the cleanest evidence to date that:

1. **alcove geometry is materially harder for cascade-MB-ON than a
   symmetric Cornell box** — even though P2-E showed alcove geometry
   only shifted P2 viewport-composition by 4.5%, alcove costs cascade
   ~35% of its closed-PT-gap on a true energy measurement.
2. **the v1.3.1 +136% MB-lift number was measured at a scene-specific
   pessimal point** — generalizing v1.3.1 conclusions across scenes
   needs scene-specific CV captures, not extrapolation.

### Architectural read: 35% mean-energy gap is the v2.0 retirement budget

To retire hybrid, cascade-only must converge to a ratio_B in
`CV1_CASCADE_NEAR_PT` band ([0.85, 1.15]). Today it sits at 0.65 on the
easier of two test scenes. The work is closing ~20-35 percentage points
of mean ratio AND tightening per-pixel spread (|p95|=1.28 must drop
below ~0.3-0.5 for visual parity with PT). This is not a small tweak —
it is a substantial gap.

The next decision is whether to: (a) measure hybrid-ON to see how much
hybrid currently buys (sets the floor of what we cannot afford to lose
by retiring hybrid), (b) extend CV to cornell-orig-alcove to bound the
gap on the harder scene, (c) extend CV across cameras to test
camera-dependence, or (d) pursue a per-pixel diagnostic (which pixels
contribute the dim%/bright% tails).

## 5. Self-critique

**Strengths:**

- Pre-committed bands with sharp boundaries — verdict landed cleanly in
  `CV1_CASCADE_DIM_MILD`, no ambiguity at the boundary.
- Both self-paired (A) and truth-anchored (B) analyses run in parallel;
  their agreement to 4 decimals validates BAND 3 (PT well-converged)
  with zero extra capture cost.
- Sweep cost was within prediction (2.0 min observed vs 2.6 min
  estimated). PT mode-17 cost per frame is ~0.024s on this rig, not the
  0.04s assumed; PT capture is cheap enough to scale to broader scene
  sweeps without budget concern.
- Found a free future-cost lever: PT-ref captures can be 128 frames
  going forward, not 512+ as in v1.3.1 hdr_relitigation. Saves ~75%
  per-PT-ref capture cost on subsequent CV work.

**Weaknesses:**

- Single scene + single camera. The +35.1% cornell-vs-alcove finding is
  inferred from comparing CV1 cornell N=512 to v1.3.1's documented
  cornell-orig-alcove N=512 number; would be stronger as an in-harness
  A/B (one more capture). Defer because the v1.3.1 ratio is well-cited
  and CV1's pivot motivation didn't require alcove until the next step.
- Mean-ratio metric collapses dim and bright spreads. The 0.65 number
  understates the per-pixel quality gap. The dim%/bright% columns
  partially compensate, but a per-pixel diagnostic (which pixels are
  dim? which are bright?) would be more actionable. Defer to CV2 or
  later.
- No measurement of CASCADE temporal noise floor — at N=2048 cascade
  movement was 0.0013 mean ratio over the last doubling, which may be
  cascade convergence OR may be noise-floor jitter. Distinguishing
  needs a multi-seed sweep at N=2048; defer (single-seed acceptable
  for a baseline measurement).

## 6. Recommendations

**Immediate**: report findings, surface next-CV decision to user.

**Likely next measurements** (in rough cost order):

1. **CV2 hybrid-ON A/B** — same config + `--use-hybrid=1`, single capture
   at N=1024 (cascade-converged frame count). Quantifies what hybrid
   buys on top of cascade-only on cornell. Cost: 30s.
2. **CV3 cornell-orig-alcove cam0 hybrid OFF** — directly verifies the
   "alcove costs ~35%" inference. Cost: ~2 min if same frame sweep, or
   ~30s at N=1024 only.
3. **CV4 cross-camera at cornell** — N=1024, cam0/cam1/cam2. Tests
   whether the asymptotic ratio depends on camera (rules camera-pose
   confound in or out for CV-style metrics). Cost: ~1 min.
4. **CV5 dim-pixel attribution** — render-mode-19-style cascade-PT
   delta map at N=1024 cornell. Identifies WHICH pixels carry the dim%.
   Cost: ~30s + manual eye-balling.

**Deferred:**

- Multi-bounce gain sweep (β-style) at CV asymptote — to test whether
  raising g=1.0 → 1.5 closes the gap with bounded cost. v1.3.1 §4.2
  showed g=1.5 on cornell-orig-alcove produced ratio=2.1 (catastrophic
  overshoot), so this is unlikely to work but would close the door
  cleanly on a "cheap fix via gain tune."
- Multi-seed noise-floor estimation at cascade asymptote (N≥1024).

## 7. Cross-reference

- Capture: [tools/v20_convergence/cv1_capture.ps1](../../tools/v20_convergence/cv1_capture.ps1)
- Analyzer: [tools/v20_convergence/analyze_cv1.py](../../tools/v20_convergence/analyze_cv1.py)
- Results: `tools/v20_convergence/captures_cv1/cv1_results.json`
- Parent (P2-E falsification that motivated this pivot): [v20_p2e_control_impl.md](v20_p2e_control_impl.md)
- v1.3.1 comparison reference: [hdr_relitigation_impl.md §4.1](hdr_relitigation_impl.md)
- Camera defs: [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json)
- Analyzer kin (single-config HDR ratio): [tools/v20_pre_measurement/analyze_hdr_exr.py](../../tools/v20_pre_measurement/analyze_hdr_exr.py)
