# v2.0 P2-E — P2 framework validation on symmetric control scene

**Status:** Risk-reduction control test before any Option A fix engineering.
Re-runs the P2 mode-22 dominant-bin diagnostic on **cornell-orig** (symmetric
variant, no alcove) using the same cam0+cam2 measurement cameras and same
MB-OFF D=8 config as the P2 baseline. Asks: is the cam0/cam2 per-bin
asymmetry a property of the **scene** (alcove geometry) or of the **camera
poses** themselves?

**Verdict: `P2_FRAMEWORK_BIASED`** — per-row weighted JS on **symmetric**
cornell-orig = **0.1465**, within 5% of the alcove scene's **0.1534**.
Removing the alcove geometry barely changes the cam0/cam2 dominant-bin
asymmetry. The cam2 dx=0 collapse persists at 52.8% (vs alcove 51.0%).

**Architectural implication: the cam0/cam2 dominant-bin asymmetry the P2
framework was measuring is dominated by camera pose, not scene geometry.**
The P2 method is comparing the per-bin distribution of pixels visible in
two views that sample different mixes of surface normals — it is not a
fair cross-camera test of cascade GI parity. **All three Option A fix
candidates (a/b/c) are premature** — they would target a measurement
artifact, not a scene property.

**Date:** 2026-05-24

## 1. Pre-committed bands ([p2_dombin_control.ps1](../../tools/v20_arch_diagnostic/p2_dombin_control.ps1) header)

| band | predicate (per-row JS, cornell-orig cam0 vs cam2) | interpretation |
|---|---|---|
| `P2_FRAMEWORK_VALIDATED`  | ≤ 0.05 | Framework unbiased; alcove genuinely causes the alcove case 0.153. Option A warranted. |
| `P2_FRAMEWORK_PARTIAL`    | (0.05, 0.10] | Framework mostly unbiased; ~30% residual cam-bias. |
| `P2_FRAMEWORK_BORDERLINE` | (0.10, 0.13] | Framework substantially biased. |
| `P2_FRAMEWORK_BIASED`     | > 0.13 | Framework biased to a degree matching the alcove case 0.153. Option A premature. |

Apples-to-apples reference: cornell-orig-alcove (same cam0+cam2, same MB-OFF
D=8) per-row weighted JS = 0.1534 (B step §4).

## 2. Setup

### Capture

Per [p2_dombin_control.ps1](../../tools/v20_arch_diagnostic/p2_dombin_control.ps1):

- **`--load-obj=cornell-orig`** (symmetric variant — only differs from
  P2/B/D baseline by REMOVING the alcove geometry)
- cam0 + cam2 from [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json)
  (same world-space poses as alcove case — cameras.json was originally
  authored for cornell-orig-alcove geometry, but the poses are
  scene-independent world positions)
- `--use-multi-bounce=0` (MB-OFF, single-bounce only, matching B baseline)
- `--blend-mode=0` (smoothstep), `--noise-seed-offset=0`, `--use-hybrid=0`,
  engine default D=8
- `--render-mode=22` (dominant-bin viz, nearest-parent atlas readout)
- `--screenshot-exr=1 --exit-frames=256`

### Analyzer

Reused [analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py)
unchanged from B/D steps.

## 3. Results

### Headline table — cornell-orig (symmetric) vs cornell-orig-alcove

| metric | alcove (B baseline) | cornell-orig (control) | Δ |
|---|---:|---:|---:|
| histogram overlap            | 0.6293 | **0.6230** | -0.0063 |
| 2D JS divergence             | 0.1568 | **0.1478** | -0.0090 |
| per-row weighted overlap     | 0.6329 | **0.6253** | -0.0076 |
| **per-row weighted JS**      | **0.1534** | **0.1465** | **-0.0069** |
| cam0 mean dominance          | 0.119  | 0.112  | -0.007 |
| cam2 mean dominance          | 0.116  | 0.106  | -0.010 |
| cam0 valid GI pixels         | 138332 | 152510 | +14178 |
| cam2 valid GI pixels         | 116390 | 170229 | +53839 |
| cam0 top-1 bin               | (3, 3) 0.285 | (3, 3) **0.341** | sharper |
| cam2 top-1 bin               | (0, 3) 0.510 | (0, 3) **0.528** | sharper |

**Removing the alcove changes per-row JS by 0.0069 (4.5%).** Both cameras
sample MORE pixels (cam2 +46% pixel count — alcove had been occluding
content visible in the symmetric scene). The dominant-bin shares are
slightly SHARPER on the symmetric scene (cam0 top-1 0.285→0.341; cam2
top-1 0.510→0.528). The asymmetry is unchanged in kind and magnitude.

### Per-row breakdown (cornell-orig)

| dy | cam0 share | cam2 share | within-row overlap | within-row JS |
|---|---:|---:|---:|---:|
| 0  | 0.024 | 0.036 | 0.809 | 0.068 |
| 1  | 0.005 | 0.005 | 0.584 | 0.188 |
| 2  | 0.016 | 0.013 | 0.543 | 0.221 |
| **3**  | **0.864** | **0.844** | **0.615** | **0.153** |
| 4  | 0.050 | 0.055 | 0.680 | 0.084 |
| 5  | 0.022 | 0.028 | 0.513 | 0.202 |
| 6  | 0.012 | 0.009 | 0.900 | 0.027 |
| 7  | 0.006 | 0.011 | 0.838 | 0.036 |

dy=3 (the horizontal-hemisphere row) carries 84-86% of GI mass and shows
within-row JS = 0.153 — virtually identical to the alcove case dy=3 row.

### cam2 top bins on symmetric scene (vs alcove)

| rank | bin (dx, dy) | cornell-orig share | alcove share | Δ |
|---|---|---:|---:|---:|
| 1 | (0, 3) | **0.528** | 0.510 | +0.018 |
| 2 | (3, 3) | 0.202 | 0.156 | +0.046 |
| 3 | (4, 3) | 0.043 | 0.038 | +0.005 |
| 4 | (0, 4) | 0.026 | 0.039 | -0.013 |
| 5 | (2, 3) | 0.024 | 0.024 | 0.000 |

The cam2 (dx=0, dy=3) "collapse" is **identical** between the alcove
and symmetric scenes. The collapse is a property of the cam2 viewing
pose, not the alcove geometry.

## 4. Verdict + interpretation

**`P2_FRAMEWORK_BIASED`**: per-row JS 0.1465 vs threshold > 0.13.
Removing the alcove only shaved 0.0069 (4.5%) off the per-row JS —
nowhere near the framework-validation threshold of ≤ 0.05.

### Why the P2 framework is cam-pose-biased

The P2 mode-22 viz colors each visible surface pixel by the argmax-over-bins
of its NEAREST-PARENT probe's directional atlas. The histogram counts
**how many visible pixels** map to each (dx, dy) bin.

For two different camera poses to produce identical histograms, the views
would need to expose **identical mixes of surface normals and surface
positions in the scene**. cam0 (axis-aligned, straight at the back wall)
sees a dominantly +z-facing back wall plus equal portions of left/right
side walls. cam2 (off-axis, from left, viewing toward (0.5, 0.7, 0)) sees:
- more of the LEFT side wall (normal +x) at oblique angles
- the back wall obliquely
- more of the floor (normal +y) at oblique angles

Each visible surface pixel's "dominant bin" reflects which direction its
**octahedral-mapped probe atlas** has the strongest incoming radiance from
— which is largely a function of WHICH surface normal the pixel has and
WHERE in the scene the pixel sits. cam2's view sees more left-wall pixels
than cam0 → cam2's histogram is dominated by bins reflecting left-wall
geometry → mass concentrates in (dx=0, dy=3).

### What P2 is actually measuring

P2 is measuring "given the **specific pixels** visible in this view, how
does their per-pixel argmax bin distribute?" This is a **viewport
composition** statistic, not a measurement of cascade GI accuracy or
parity. Two cameras viewing the same scene from different poses **should
not be expected to produce similar dominant-bin histograms** unless they
happen to expose the same mix of surface normals — which is unusual.

This means:
- The original P2 verdict `P2_OVERLAP_MEDIUM` (0.6293) was measuring view
  composition difference, not GI artifact.
- The B step's `P2_DSWEEP_SHARPEN` was measuring how D-quantization
  sharpens the view-composition asymmetry (real, but not a GI bug).
- The D step's `MB_NO_CLASSIFICATION_EFFECT` was measuring that MB
  temporal feedback doesn't change which surface a visible pixel hits.

The B+D step CONCLUSIONS (D-sweep cheap-fix elim, MB cheap-fix elim) are
intact — those rule out their respective fix candidates regardless of
what P2 actually measures. **What's invalidated is the implicit premise
that the P2 OVERLAP MEDIUM is a GI bug requiring an Option A bake-side
fix.**

### Comparison reframe

A fair cascade-GI cross-camera test would either:
1. **Probe-space measurement**: compare per-probe atlas contents at probes
   in the alcove region between MB-OFF and a reference. The current
   probe-volume-overlay diagnostics in `tools/v20_diagnostic_baseline/`
   are closer to this.
2. **PT-reference per-pixel deltas**: compare per-pixel GI (mode 17)
   between cam0 and a path-traced ground-truth at cam0; same for cam2.
   Ratios cam0_cascade/cam0_PT vs cam2_cascade/cam2_PT would isolate
   whether cascade UNDERESTIMATES one camera relative to truth. The
   v1.3.1 [hdr_relitigation_impl.md §4.2](hdr_relitigation_impl.md)
   measurement (cam0 ratio lift +136%) was on this footing — relative
   to PT reference, not relative to the other camera.
3. **Same-pixel cross-camera projection**: for visible pixels that exist
   in BOTH cam0 and cam2 views (project from world space), compare their
   GI values. Avoids viewport-composition confound entirely.

### Combined B+D+E conclusion

| step | what it ruled out | what it doesn't establish |
|---|---|---|
| B (D-sweep)   | D ∈ {8, 16} as a fix | Whether the asymmetry being measured is a real GI bug |
| D (MB-ON)     | MB as a fix          | Whether the asymmetry being measured is a real GI bug |
| **E (control)** | **The P2 metric itself as a valid cross-camera GI parity test** | — |

The cheap-fix elimination from B+D stands. The case for Option A as a fix
for "the P2 asymmetry" does not. Option A may still be defensible on
other grounds (e.g., the firefly-clamp option (b) is good bake-side
robustness independent of any cam2 issue) but it should not be motivated
as "fixing the cam0/cam2 per-bin atlas asymmetry."

## 5. Recommendations

**Immediate**: do NOT proceed to Option A on the P2 framing. Instead:

1. **Re-frame the v2.0 architectural pivot question.** The hdr_relitigation
   measurement (v1.3.1) showed cam0 cascade-to-PT ratio +136% but did
   NOT show cam2 ratio. Need cam2 PT-reference ratio before claiming any
   per-camera GI deficit.
2. **Capture the missing cam2 PT-reference data** to establish whether
   cam2 has a real cascade-vs-PT deficit at all. Cost: ~5-10 min PT capture
   on cam2.
3. If cam2 ratio is similar to cam0's (both off PT in same direction),
   the v2.0 pivot motivation reduces to "cascade is uniformly off PT,
   not asymmetric between cameras" — different problem statement.
4. If cam2 ratio is materially WORSE than cam0's (e.g., cam2 only +50%
   while cam0 +136%), THEN there's a real asymmetric-camera GI deficit
   to investigate — but probe-space or same-pixel-cross-camera
   methodology should be used, not P2.

**Deferred**: Option A is parked until the v2.0 problem statement is
re-grounded on a valid cross-camera measurement.

## 6. Self-critique

**Strengths:**

- Pre-committed verdict bands were sharp: ≤0.05 vs >0.13 with three
  intermediate bands. The actual 0.1465 lands cleanly in the
  `P2_FRAMEWORK_BIASED` band — no ambiguity.
- Single-knob change (`--load-obj=cornell-orig` vs `cornell-orig-alcove`)
  isolates "alcove geometry" as the **sole** difference, making the
  cam-pose-as-cause attribution airtight.
- The control test was zero-cost (~15s capture, reused analyzer
  unchanged). Per the user's "validate framework before committing fix
  engineering" judgment call — the ~15min cost paid for itself by
  preventing 2-8h of Option A engineering that would have been targeting
  an artifact.

**Weaknesses:**

- The conclusion "P2 measures viewport composition, not GI parity" is
  inferred from the architectural argument in §4, not directly demonstrated.
  A stronger version of this test would project the same world-space
  surface points from BOTH cameras and compare their atlas reads (would
  isolate viewport-composition from atlas-content differences). Defer
  this — the negative result here (no alcove effect) is already
  sufficient to gate Option A.
- Only one symmetric scene tested. A second control (e.g., cornell, or
  Empty Room) would further harden the negative result. Defer because
  the magnitude of the alcove→cornell-orig delta (0.0069) is small
  enough that the room geometry contribution is bounded — additional
  symmetric scenes would not change the qualitative finding.
- The B+D step conclusions are unaffected (those rule out fixes on their
  own merits) but the **narrative motivation** for B+D in their impl
  docs implicitly took P2 as a real GI bug needing investigation. Those
  docs should either be revised or accompanied by a top-level pivot note
  pointing here. Deferred to status update — adding a "P2 framework
  caveat" note to project_phase_status.md is sufficient.

## 7. Cross-reference

- Parent (P2 baseline): [v20_p2_dombin_impl.md](v20_p2_dombin_impl.md)
- Parent (B step): [v20_p2b_dsweep_impl.md](v20_p2b_dsweep_impl.md)
- Parent (D step): [v20_p2d_mbon_impl.md](v20_p2d_mbon_impl.md)
- Capture: [tools/v20_arch_diagnostic/p2_dombin_control.ps1](../../tools/v20_arch_diagnostic/p2_dombin_control.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py)
- Results: `tools/v20_arch_diagnostic/captures_p2_dombin_control/p2_dombin_control_results.json`
- Camera defs: [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json)
- v1.3.1 cam0 ratio reference: [doc/7/hdr_relitigation_impl.md §4.2](hdr_relitigation_impl.md)
- **Next step: re-ground v2.0 motivation via cam2 PT-reference capture**
