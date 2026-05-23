# MBRC v2.0 — (h) source disambiguation: MB-ON vs MB-OFF at b=2

**Date**: 2026-05-23 (immediately follows
[v20_pt_bounce_ladder_impl.md](v20_pt_bounce_ladder_impl.md), commit
`a169672`).

**Motivation**: bounce-ladder §6 recommended a 2-minute capture to
disambiguate (h)'s source between MB feedback equilibrium amplifier
and first-bounce 3-way merge over-integration. Captured MB-OFF cascade
at b=2 on cam0+cam2 (reused MB-ON b=2 from the ladder for the other
half of the pair). Pre-committed rule: if MB-OFF cam0 ratio ∈ [0.90,
1.10], MB feedback is the +39% source; if MB-OFF ratio ≈ MB-ON ratio
and both > 1.20, first-bounce merge is the source.

## 1. The experiment

[h_source_disambig_capture.ps1](../../tools/v20_arch_diagnostic/h_source_disambig_capture.ps1)
captures cascade-WITHOUT-MB-feedback at b=2 via `--use-multi-bounce=0`
on cam0+cam2 (engine M4 merge default unchanged). MB-ON captures at
b=2 reused from `captures_pt_bounce_ladder/`. 2 new EXR triplets, 0.4 min.

[analyze_h_source_disambig.py](../../tools/v20_arch_diagnostic/analyze_h_source_disambig.py)
reads the 4 cells and reports per-cell `|Σ+/Σ−|`, integrated cascade/PT
energy ratio.

## 2. Results

|cam | MB  |  Σ+   |  Σ−   | \|+/-\|  | casc/PT  |
|---:|----:|------:|------:|---------:|---------:|
|  0 |  ON | 1592  |  502  | **3.17** | **1.393** |
|  0 | OFF |  139  | 1058  | **0.13** | **0.669** |
|  2 |  ON |  786  | 1494  | 0.53     | 0.770    |
|  2 | OFF |   60  | 2113  | 0.03     | 0.333    |

**MB ratio movement:**
- cam0: 0.669 → 1.393 (**+0.724**, ×2.08 multiplier)
- cam2: 0.333 → 0.770 (**+0.437**, ×2.31 multiplier)

### 2.1. Pre-committed verdict → MIXED

cam0 MB-OFF = 0.669 falls in *neither* band:
- Not in [0.90, 1.10] (the MB_FEEDBACK_IS_SOURCE band).
- Not "≈ MB-ON" (delta 0.72, far above the 0.10 threshold for
  FIRSTBOUNCE_MERGE_IS_SOURCE).

By the analyzer's pre-committed rule, verdict is **MIXED**. But the
underlying numbers reveal a structure neither hypothesis-source label
captured cleanly: both effects are stacked, not competing.

## 3. The actual finding: stacked, not competing

The pre-committed dichotomy assumed (h.1) MB-amplifier and (h.2)
first-bounce-merge-over-integration were mutually exclusive root causes.
The data shows they're **multiplicative**:

### 3.1. (h.2) Single-bounce cascade under-integrates asymmetrically

At b=2 with MB OFF (pure single-bounce-vs-single-bounce comparison):
- **cam0**: cascade delivers **67%** of PT single-bounce.
- **cam2**: cascade delivers only **33%** of PT single-bounce — half of cam0.

This is the *single-bounce 3-way merge formula at*
[radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656)
*delivering 33–67% of PT's single-bounce integration*. (h.2) is
confirmed in spirit (merge under-integrates) but reversed in sign from
the original framing (cam0 OVER → the merge over-integrates). At
single-bounce the merge UNDER-integrates uniformly; only with MB does
cam0 cross over to over-bright.

### 3.2. (h.1) MB feedback as multi-bounce compensator

The MB ON→OFF delta is **+0.72 on cam0** and **+0.44 on cam2** — MB
adds roughly equal absolute energy to both cams. MB feedback at g=1.0
delivers a ×2.08 multiplier on cam0 (0.67 → 1.39) and ×2.31 on cam2
(0.33 → 0.77). Both multipliers are consistent with a geometric series
`1 / (1 - r)` for `r ≈ 0.5`, i.e. an effective combined-albedo MB-gain
of ~0.5 per bounce. This is *exactly* what MB feedback is designed to
do — substitute for the missing multi-bounce delivery (g) hypothesis.

### 3.3. Why cam0 over-shoots at MB+single-bounce-merge stack

cam0's single-bounce delivery floor (0.67) is high enough that the
×2.08 MB multiplier pushes it above PT-single-bounce (1.39 > 1.0).
This is NOT "MB is wrong" and NOT "single-bounce-merge is wrong"
individually — it's that the comparison baseline (PT_b=2 =
direct + 1 indirect bounce) is the *wrong baseline* for a system
that's intrinsically integrating multi-bounce via MB. PT_b=2 truncates
at 1 indirect bounce; cascade+MB equilibrium effectively delivers a
~2-bounce integration (since `1/(1−0.5) = 2`) which lands above
PT_b=2 on the brighter cam.

cam2's single-bounce floor (0.33) is so low that even ×2.31 MB only
reaches 0.77, under-bright vs PT_b=2.

## 4. Architectural implication

The "cam0 over / cam2 under" asymmetry at MB-ON, b=2 is a **derived
symptom of cam2's single-bounce-merge floor being half of cam0's**.
The actionable target is the *single-bounce merge at*
[radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656)
— specifically, *why does it deliver 67% on cam0 and only 33% on cam2*?
The merge formula reads the same atlas data; the asymmetry must come
from one of:

1. **Probe-grid projection asymmetry.** cam2's viewport oversamples
   surfaces farther from the densest probe coverage. The MB-OFF result
   directly measures this without MB obscuring it — it's the cleanest
   geometric-asymmetry signal we've captured.
2. **Probe alignment with cam2-visible geometry.** Alcove surfaces
   that cam2 sees most may fall at probe-cell boundaries where the
   merge formula's smoothstep/blend zone math under-weights them.
3. **Hidden cosine-weighting in M4_iso_nearest.** Even though M4 was
   thought to bypass cosine-weighted hemisphere integration, some
   per-direction-bin weighting may persist that disadvantages
   cam2-visible surface normals.

## 5. Updated hypothesis space

| Hypothesis | Pre-disambig | Post-disambig |
|------------|--------------|---------------|
| (a) bake-side leak | REJECTED | unchanged |
| (b) smoothstep blend zone | P4 | **promoted to P3** — one of the explicit candidates for the cam0/cam2 single-bounce-merge asymmetry |
| (c) camera-projection / surface-mix | CONFIRMED | **promoted to P2** — the MB-OFF cam0=0.67 vs cam2=0.33 spread IS this hypothesis, now quantified at the merge step |
| (d) basis-representation error | FALSIFIED | unchanged |
| (e) thin-merge shader | P5 | unchanged |
| (f) bake-time energy loss | FALSIFIED | unchanged |
| (g) multi-bounce delivery gap | P1 | **demoted to P3** — MB feedback effectively delivers ×2.08–2.31 multi-bounce equivalent already; the "missing bounces" are mostly substituted by MB |
| (h) single-bounce integration asymmetry | P2 | **CONFIRMED at single-bounce baseline; promoted to P1** — MB-OFF cam0=0.67 / cam2=0.33 spread IS the asymmetry, exposed cleanly without MB |

## 6. Recommended next step

**Diagnose the cam0=0.67 vs cam2=0.33 single-bounce-merge asymmetry
directly.** Three tractable sub-experiments, all cheap (~10 min each):

1. **M0/M2/M4 × cam0/cam2 at b=2 with MB OFF** (12 captures, ~3 min).
   Identifies which merge variant minimizes the cam0/cam2 spread at
   single-bounce. M0 vs M4 isolates "cosine-weighted hemisphere
   averaging" as the asymmetry contributor; M2 (hardware bilinear)
   identifies whether the spread is spatial or directional. The
   alpha_m4_deepdive headline was "M4 maximizes brightness" but that
   was measured at MB-ON; at MB-OFF the optimal merge for the
   asymmetry may differ.
2. **D=8 vs D=16 at b=2 with MB OFF on cam2** (4 captures, ~1 min).
   If higher angular resolution closes the gap, the asymmetry is in
   directional-bin coverage at the merge step.
3. **Smoothstep blend-zone toggle** — needs new shader `uUseSmoothstepBlend`
   uniform (~30 min eng work, ~5 min capture). Tests whether the blend
   math at [radiance_3d.comp:771](../../res/shaders/radiance_3d.comp#L771)
   is the asymmetry source.

Run in order 1→2→3; the first two require zero engine work and may
already point at the source. If sub-1 shows M0 closes the cam0/cam2
spread at MB-OFF, the architectural conclusion shifts to "M4 was an
asymmetry-introducer hidden by MB-ON brightness gain" — which would
revise the engine-default flip from M4 back to M0 (or M2 as a
compromise).

## 7. Self-critique

- **The pre-committed dichotomy was too rigid.** "MB is the source
  OR merge is the source" missed the multiplicative-stacking case
  the data revealed. Future disambig rules should include a MIXED
  branch with a *quantitative* sub-classifier (e.g. "MB delta dominates
  ratio difference" vs "single-bounce baseline dominates"), not just
  a label.
- **The MIXED verdict is still load-bearing — it tells us where to
  look.** Even though neither pre-committed source matched cleanly,
  the MB-OFF data isolated the single-bounce-merge asymmetry (cam0=0.67
  / cam2=0.33) which had been masked by MB's brightness gain in every
  prior measurement. This is the cleanest "scene-uniform geometric
  asymmetry" signal we have.
- **PT_b=2 reference question lingers.** PT_b=2 is the apples-to-apples
  comparison for cascade-WITHOUT-MB, but for cascade+MB the right
  comparison is PT_b≈2.5–3 (per ladder §3.3). The 1.39 cam0 over-bright
  finding from MB-ON b=2 is partly an artifact of using PT_b=2 as
  baseline when cascade+MB equilibrates higher. A re-run with cascade+MB
  vs PT_b=3 would land cam0 closer to 1.0 (apples-to-apples re-stated)
  while the cam0/cam2 SPREAD (0.62 at single-bounce) would remain the
  underlying signal.
- **The "MB as compensator" framing changes (g)'s priority.** Pre-disambig
  doc had (g) multi-bounce delivery gap as P1, recommending architectural
  acceptance. The MB-OFF data shows MB IS already delivering ~2x
  multi-bounce equivalent on both cams — it's just that the
  single-bounce baseline (h.2) is asymmetric and (g)'s remaining gap
  is small after MB compensates. (g) demotes to P3; (h) on the bare
  single-bounce merge is now the dominant remaining lever.

## 8. Artefacts

- Capture script: [tools/v20_arch_diagnostic/h_source_disambig_capture.ps1](../../tools/v20_arch_diagnostic/h_source_disambig_capture.ps1)
- New captures (8 files): [tools/v20_arch_diagnostic/captures_h_disambig/](../../tools/v20_arch_diagnostic/captures_h_disambig/)
- Reused captures (from bounce-ladder): [tools/v20_arch_diagnostic/captures_pt_bounce_ladder/alcove_cam{0,2}_b2_m17_*](../../tools/v20_arch_diagnostic/captures_pt_bounce_ladder/)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h_source_disambig.py](../../tools/v20_arch_diagnostic/analyze_h_source_disambig.py)
- Results JSON: [tools/v20_arch_diagnostic/h_source_disambig_results.json](../../tools/v20_arch_diagnostic/h_source_disambig_results.json)
