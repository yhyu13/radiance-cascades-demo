# MBRC v2.0-pre — Measurement Plan

**Status:** REV 2 (2026-05-20) — first deliverable in the MBRC v2.0 quality push. Per
[mbrc_quality_plan.md §1 (motivation) + §9 answer 5 (deliverable-form decision)](mbrc_quality_plan.md),
this measurement report ships as a **standalone signed-off deliverable** before any
v2.0a/b/c feature code lands.

**Revision history:**
- rev 1 (DRAFT) — initial methodology, self-critique §6, improved plan §7.
- rev 2 — applied [critic 06](critic/06_mbrc_v20_pre_measurement_plan_review.md) findings;
  see [reply 06](critic/reply/reply_06_mbrc_v20_pre_measurement_plan_review.md) for the
  full per-issue rationale. Material changes: H1 (skip-in-merge exclusion specified),
  H2 (per-bin PT convergence), **H3 user-overridden — variance-tuned primary kept,
  stderr guard added**, H4 (cascade-dominance primary partition axis), plus
  M1-M5 + L1-L5.

**Scope:** No shipping code. Only diagnostic render modes, capture harness, analysis
script, and the written report itself.

**Effort estimate (rev 2):** 2 days (~16h). Original rev 1 estimate was 1.5 days;
critic-06 added 0.5 day for H1 skip-in-merge rework, H2 per-bin convergence with
possible alcove-high-spp capture, H4 cascade-dominance shader work, and infrastructure
(cascade-config.json, dual PT mode capture, EXR writer). See [reply 06 §"Revised effort"](critic/reply/reply_06_mbrc_v20_pre_measurement_plan_review.md).

---

## 1. The question this report must answer

`mbrc_quality_plan.md §1` claimed two named GI quality gaps that motivate retiring the
hybrid PT correction:

- **(a) Long-distance spatial:** C2/C3 cascades (8³, 4³ probes) blur distant occluders.
- **(b) Short-distance radial:** D=4 (16 bins/probe) under-resolves cones in alcove /
  grazing-incidence pixels.

These are hypotheses based on architectural reasoning, not measurement. The v1.3.1
variance harness (a00 vs a05 −0.01% RMSE TIE) already proved once in this codebase
that visually-obvious quality intuitions can be wrong. This report's job is:

> **Q1.** Is gap (a) real and material on cornell-orig-alcove vs Mode 16 PT reference?
> **Q2.** Is gap (b) real and material on cornell-orig-alcove vs Mode 16 PT reference?
> **Q3.** If both are real, which dominates the RMSE budget?
> **Q4.** What is the **noise floor** of cascade-RC's own per-frame variance? RMSE
>        deltas smaller than ~2× noise floor are meaningless.
> **Q5.** Does the PT reference itself have enough samples to serve as ground truth, or
>        is the reference noise contaminating the measurement?

The verdict drives v2.0a/b/c selection. If gap (a) dominates → C1 cubemap and A2
bicubic become primary. If gap (b) dominates → A3 SH2 and B2 inverted-angular-bins
become primary. If neither dominates → the §1 diagnosis is wrong and the plan replans.

## 2. Methodology — five measurements

Each measurement produces one or more artifacts (PNG / EXR / CSV / table). All
measurements use the same cornell-orig-alcove scene with three camera positions
(see §2.6).

**RMSE definition (rev 2, M1 fix):** **Luminance-RMSE is the primary scalar** (matches
v1.3.1 variance harness; per §8 answer 2). **Per-channel R/G/B RMSE is always reported
alongside as a co-primary table column** — not as a footnote. If
`max(R_rmse, G_rmse, B_rmse) > 1.5 × min(R_rmse, G_rmse, B_rmse)`, the report MUST flag
color-channel-asymmetric error in the verdict and propose corresponding lever
investigation (M1).

### 2.1 Per-cascade error attribution (REV 2 — H1)

**Hypothesis tested:** Q3 (which cascade carries the dominant error).

**Method (REV 2 — replaces isolation with leave-one-out via skip-in-merge):** New
uniform `uCascadeExclude ∈ {-1, 0, 1, 2, 3}` in `raymarch.frag`. `−1` = baseline
(no exclusion). `0..3` = skip cascade i in the consume-time merge with **weight
renormalization** — when cascade i is excluded, the smoothstep blend between i−1
and i+1 closes (i+1's near band extends to i−1's far band; smoothstep transitions
skip the excluded bracket entirely). This measures pure "without C_n," not
`C_n loss + merge-formula disturbance`. See [reply 06 H1](critic/reply/reply_06_mbrc_v20_pre_measurement_plan_review.md)
for why atlas-zero gating was rejected.

Render five frames per camera position:
- baseline (uCascadeExclude = −1)
- exclude-C0 / exclude-C1 / exclude-C2 / exclude-C3

`RMSE(exclude-Cn) − RMSE(baseline)` = C_n's contribution to error-reduction (its
"impact"). This is the correct leave-one-out attribution.

**Sanity assertion (rev 2):** `RMSE(exclude=none) == RMSE(baseline) ± 0.1%` —
proves the new code path is a no-op when nothing is excluded. Failure here means
the renormalization itself has a bug; do NOT proceed to capture until this holds.

**Artifact:** `cascade_loo_rmse.csv` — rows = camera position, columns = exclude
mode, cells = luminance-RMSE vs cascade-match PT (see §2.5, L4); per-channel
RGB-RMSE in adjacent columns.

### 2.2 Error decomposition by cascade dominance (REV 2 — H4 primary axis)

**Hypothesis tested:** Q1 + Q2 (long-distance spatial vs short-distance radial), but
recast in terms of the architectural feature actually being diagnosed: **which
cascade is doing the work for each pixel.**

**Method (REV 2 — replaces scene-geometry thresholds as primary axis with
cascade-dominance):** New render mode **20 — Error Decomposition Heatmap** plus
**Mode 21 — Cascade Dominance Visualization** (or sub-mode of 20).

For each pixel, compute `dominant_cascade = argmax_i (consume_weight_i)` using the
consume-time smoothstep `(1 − l_i)`-style factors already computed in the existing
GI consume path. Bin pixels into **C0-dominated / C1-dominated / C2-dominated /
C3-dominated.**

Per cascade-dominance bin:
- Pixel count (% of frame)
- Luminance-RMSE vs cascade-match PT
- Per-channel R/G/B RMSE
- Mean linearDepth, mean sdfDist (for context, not for the partition)

If gap (a) "long-distance" is real → C2-dominated and C3-dominated bins show
disproportionately high RMSE. If gap (b) "short-distance radial" is real →
C0-dominated and C1-dominated bins show disproportionately high RMSE (cones
under-resolved in near-field).

### 2.2.1 Secondary: scene-geometry partition (G3-rescaled — diagnostic only)

For cross-check, also report scene-geometry bins using thresholds derived from
scene bounds (not absolute meters):
- `farThreshold = 0.4 × sceneDiag`
- `nearThreshold = 0.1 × sceneDiag`
- **far-distant** = `linearDepth > farThreshold AND sdfDist > nearThreshold`
- **near-grazing** = `sdfDist < nearThreshold AND |dot(N,V)| < 0.5`
- **other** = everything else

cornell-orig-alcove has `sceneDiag ≈ 5.2m`, so `farThreshold ≈ 2.1m` and
`nearThreshold ≈ 0.5m` — substantially lower than the rev-1 hardcoded 4m and 0.3m,
and large enough that the bins are not near-empty on this scene.

This partition is **diagnostic, not load-bearing for the verdict** — the C5 verdict
rule (§5) uses cascade-dominance bins only.

**Artifacts per camera (4 files each, 12 total across 3 cameras):**
- `error_decomp_<cam>_cascadeDom.png` — color-coded by dominant cascade index
- `error_decomp_<cam>_geomFarDist.png` — red-only far-distant heatmap
- `error_decomp_<cam>_geomNearGraze.png` — green-only near-grazing heatmap
- `error_decomp_<cam>_bins.csv` — per-bin pixel-count + per-bin luminance-RMSE +
  per-bin per-channel RGB-RMSE; one CSV per camera, both partition axes

### 2.3 PT reference adequacy — per-bin convergence (REV 2 — H2)

**Hypothesis tested:** Q5 (PT reference sample-count adequacy).

**Method (REV 2 — replaces whole-frame mean with per-bin convergence):** Mode 16
PT reference accumulates progressively. Capture frames at
`spp ∈ {64, 256, 1024, 4096}` per camera position. Compute pair-RMSE
`RMSE(spp=N, spp=N/4)` **within each cascade-dominance bin from §2.2** (C0-dominated,
C1-dominated, C2-dominated, C3-dominated).

C1 sign-off (per-bin, NOT whole-frame): for each bin B and each camera,
`pair-RMSE(N_max, N_max/4) in B < 0.5 × cascade-RMSE in B`.

**Alcove-high-spp escalation:** cornell-orig-alcove's alcove pixels are bounce-2+
dominated and may need 16k-64k spp to converge while open-room pixels converge by
4096. If cam1 (alcove-dominated) or any bin fails C1 at 4096:
- Escalate to **16k spp** for that camera (estimated +1h capture time per camera).
- If 16k still fails → **64k spp** as fallback (escalate as needed).
- Report records "PT reference converged at N spp for bin B" explicitly per
  per-camera-per-bin cell.

cam0 and cam2 may remain at 4096 spp if their bins independently pass C1; not all
cameras need the same spp.

**Artifact:** `pt_convergence.csv` — rows = camera × cascade-dominance bin,
columns = spp tier (64, 256, 1024, 4096, 16384, 65536), cells = incremental
pair-RMSE.

**Choice:** Use the **lowest spp tier per bin that passes C1** as the canonical
reference for §2.1, §2.2, §2.4, §2.7 RMSE — recorded in
`pt_cache/reference_manifest.json`.

### 2.4 Cascade-RC own variance (noise floor) (REV 2 — M2 + M5)

**Hypothesis tested:** Q4 (noise floor for meaningfulness threshold).

**Method (REV 2 — adds explicit independence-source and temporal-state spec):**
Cascade GI uses stochastic single-sample MC per phase-MB-v2.

- **Per-run capture:** 64 frames at fixed camera (was 16 in rev 1). Compute
  per-pixel temporal stdev → mean stdev = **within-run noise floor**.
- **Cross-run capture:** 4 independent runs. Each run gets a different PCG seed
  offset via new CLI `--noise-seed-offset=N` (N ∈ {0, 1, 2, 3}). Each run starts
  from **clean cascade state and clean MB-feedback state** (cascade-clear flag +
  MB-clear flag toggled at run-start) and accumulates 64 frames before recording
  the final-frame buffer. Compute inter-run pair-RMSE across the 4 final-frame
  buffers → **between-run noise floor**.
- **Noise floor for sign-off:** `nf = max(within_run_stdev, between_run_rmse)`.
  The larger of the two is the meaningfulness threshold for all RMSE comparisons.

**Capture frame gate (M5):** All non-PT cascade captures (§2.1, §2.2, §2.4, §2.7)
wait until **frame ≥ 100** with MB-EMA + TAA + accumulator buffers warm. Records
`captureFrame = 100` in `cascade-config.json`. This makes cascade vs PT
apples-to-apples in temporal state (PT at 4096 spp is fully converged; cascade at
frame 100 is MB-EMA-converged at α=0.05).

Any RMSE difference between two measurement modes smaller than `2 × nf` is not
significant.

**Artifact:** `noise_floor.csv` — rows = camera, columns = `within_run_stdev`,
`between_run_rmse`, `nf_max` (the maximum used for sign-off).

### 2.5 PT reference cache (REV 2 — L1 + L4)

To avoid recomputing PT references per measurement, capture once per camera
position (per L4: TWO modes per camera) and store as **EXR (float32 RGB)** — not
16-bit PNG. Cornell-orig has light-source / direct-fall pixels > 1.0 linear-RGB
that 16-bit PNG would clip and silently contaminate downstream RMSE.

Two PT modes per camera (L4):
- **Unbiased PT** (`uPtCascadeMatch=0`) — true reference; used for §2.3 PT
  convergence test.
- **Cascade-match PT** (`uPtCascadeMatch=1`) — cascade's "converged target"; used
  for §2.1, §2.2, §2.7 RMSE (apples-to-apples with what cascade tries to compute).

**Artifacts (6 base EXRs + per-camera high-spp escalation if any):**
- `pt_cache/<cam>_unbiased_spp<N>.exr`
- `pt_cache/<cam>_cascadeMatch_spp<N>.exr`
- `pt_cache/reference_manifest.json` — per-camera-per-bin chosen spp tier
  (populated after §2.3 convergence test)

### 2.6 Camera positions (three)

To avoid happenstance from one viewpoint, sweep three:
- **cam0** = current cornell-orig-alcove default (light pours into alcove from above).
- **cam1** = camera inside alcove looking out (alcove walls fill frame — tests gap b).
- **cam2** = camera at far end of room looking down corridor toward alcove (alcove is
  distant — tests gap a).

Concrete cam1 / cam2 positions captured in `tools/v20_pre_measurement/cameras.json`
at implementation time, derived from cornell-orig-alcove scene bounds. CLI flag
`--measurement-camera=N` (F4) sets camera to the corresponding pinned pose +
target and disables free-camera input + TAA jitter + camera-bobbing during
captures.

### 2.7 Hybrid-on baseline (REV 2 — G4 + H3-FLAGGED)

**Hypothesis tested:** Quantifies the "gap to retire" — the RMSE delta MBRC-alone
must close to make hybrid optional by default ([mbrc_quality_plan.md §9 answer 1](mbrc_quality_plan.md)).

**Method:** Capture cascade + hybrid GI (MBRC at frame ≥ 100 + hybrid correction
shader on) per camera, compute luminance-RMSE + per-channel RMSE vs cascade-match
PT reference.

**Hybrid configuration (REV 2 — user override 2026-05-20; critic H3 rejected):**

- **Primary: variance-harness-tuned best arm** (lowest-luminance-RMSE arm from
  `tools/hybrid_validation/run_variance_sweep.ps1` output CSV). Rationale: the
  v2.0 retirement target is "what hybrid is *capable of*" — the upper bound of
  the system being retired — not "what hybrid ships with by default." If MBRC
  matches the best hybrid arm, it trivially beats shipped defaults too.
- **Secondary: hybrid v1.2.4 shipped defaults** (the "what users see"
  comparison). Recorded in the same CSV as a secondary row.
- **Stderr across variance-harness arms recorded alongside primary.** Decision
  rule: MBRC retirement is robust iff
  `MBRC_RMSE < best_arm_RMSE − 2 × stderr_across_arms`. This guards against
  declaring victory on a within-noise improvement (the residual concern from
  critic H3).

**Artifact:** `hybrid_on_baseline.csv` — rows = camera × hybrid-config (shipped,
best-arm), columns = luminance-RMSE + R-RMSE + G-RMSE + B-RMSE + stderr-across-arms
(for best-arm row).

## 3. Instrumentation

**Effort:** ~10h total (REV 2 — see §7.2 for full breakdown). Original rev 1
estimate of ~5.75h superseded (L2); see also §7.2 for the 16h two-day total
including capture and report.

| Change (REV 2) | File(s) | Effort |
|----------------|---------|--------|
| New `uCascadeExclude` uniform + **skip-in-merge with weight renormalization** (H1) | `res/shaders/raymarch.frag` (sampleProbeDir, sampleDirectionalGI; consume-time smoothstep) | 2h |
| Sanity assertion `RMSE(exclude=none) == baseline ± 0.1%` (H1) | analysis script + sweep harness | 0.25h |
| New render mode 20 (Error Decomposition Heatmap) + sub-modes for cascade-dominance / scene-geometry (H4) | `res/shaders/raymarch.frag` | 1.5h |
| New render mode 21 (Cascade Dominance Visualization, dominant-cascade index color-coded) (H4) | `res/shaders/raymarch.frag` | 0.5h |
| GUI: cascade-exclude combo (None/C0/C1/C2/C3) + measurement-camera combo | `src/demo3d.cpp` | 0.5h |
| CLI: `--cascade-exclude=N`, `--measurement-camera=N`, `--noise-seed-offset=N`, `--dump-pt-cache=path`, `--pt-mode={unbiased,cascadeMatch}` | `src/main3d.cpp` | 0.5h |
| PT reference cache mode — write current Mode 16 accumulator to **EXR float32** (L1) | `src/demo3d.cpp` + EXR writer dep (TinyEXR or `stb_image_write` for RGBE fallback) | 0.75h |
| `cascade-config.json` emitter at every capture (M3) | `src/demo3d.cpp` | 0.5h |
| `tools/v20_pre_measurement/cameras.json` — derive cam1/cam2 from scene bounds | bash/python pre-flight | 0.5h |
| Sweep harness PowerShell script (incl. variance-harness reuse per G5) | `tools/v20_pre_measurement/run_v20_pre.ps1` (NEW) | 1.5h |
| Analysis script (per-bin RMSE, per-channel RMSE asymmetry flag, verdict-rule evaluator from §5) | `tools/v20_pre_measurement/analyze.py` (NEW) | 2h |
| Preflight: confirm Mode 16 supports 4k-64k spp progressive accumulation (G1) | smoke-test in `run_v20_pre.ps1` | 0.5h |

**Total instrumentation: ~10.5h.** Capture: ~1.5h (3 cameras × 2 PT modes + escalation +
hybrid baseline + noise-floor 4 runs × 64 frames). Report write: ~3h. **2-day total
~16h** — see §7.2 (rev 2 effort breakdown).

## 4. Artifacts produced (deliverable checklist)

**(SUPERSEDED by §7.3 — see there for the authoritative rev 2 list.) [L3]**

Rev 1 list, kept for diff readability:

1. `tools/v20_pre_measurement/pt_cache/cam{0,1,2}_spp4096.png` (3 files, ~10MB each)
2. `tools/v20_pre_measurement/results/cascade_rmse_attribution.csv`
3. `tools/v20_pre_measurement/results/error_decomposition_cam{0,1,2}.png` + per-bin CSV
4. `tools/v20_pre_measurement/results/pt_convergence.csv`
5. `tools/v20_pre_measurement/results/noise_floor.csv`
6. `doc/7/mbrc_v20_pre_measurement_report.md` — written report

## 5. Sign-off criteria (REV 2 — M4 pre-registered verdict rule)

User sign-off on the report requires all of:

- **C1 (REV 2 — H2 per-bin).** PT reference adequacy demonstrated **per cascade-dominance
  bin**: for each camera and each bin B,
  `pair-RMSE(N_max_in_B, N_max_in_B / 4) < 0.5 × cascade-RMSE-in-B`. If any bin fails
  at the highest spp tier captured, escalate to next tier (16k or 64k) for that camera.
- **C2 (REV 2 — M2).** Noise floor `nf = max(within_run_stdev, between_run_rmse)` measured
  and `nf < 0.5 × baseline cascade RMSE`.
- **C3.** Per-cascade leave-one-out attribution table complete (no NaN, no missing
  camera); sanity assertion `RMSE(exclude=none) == RMSE(baseline) ± 0.1%` passes.
- **C4 (REV 2 — H4).** Cascade-dominance partition produces well-populated bins (every
  bin has > 5% of frame pixels, OR explicitly explained why one bin is empty for a
  particular camera — e.g., cam2 may have no C0-dominated pixels if all near-camera
  surfaces lie outside C0's spatial reach).
- **C5 (REV 2 — M4 pre-registered numeric rule).** Let
  - `nf = max(within_run_stdev, between_run_rmse)`,
  - `R_long = RMSE in (C2 ∪ C3)-dominated bins`,
  - `R_short = RMSE in (C0 ∪ C1)-dominated bins`,
  - `R_baseline = RMSE all pixels`.

  | Condition | Verdict |
  |-----------|---------|
  | `R_long > R_short + 2·nf` | "long-distance dominates → C1 + A2 priority" |
  | `R_short > R_long + 2·nf` | "short-distance dominates → A3 + B2 priority" |
  | `|R_long − R_short| < 2·nf` AND `(R_long + R_short) > 0.5·R_baseline` | "both contribute → A2 + A3 + B2 cluster" |
  | `(R_long + R_short) < 0.5·R_baseline` | "neither cascade-architectural gap dominates; replan mbrc_quality_plan.md §1" |

  This pre-registration prevents motivated-reasoning verdicts (M4).

- **C6 (REV 2 — G4).** Hybrid-on baseline RMSE measured; the "gap MBRC must close
  to retire hybrid" is quantified as `baseline_cascade_RMSE − hybrid_on_RMSE` per
  camera. If hybrid-on RMSE > cascade-only RMSE on any camera, flag in verdict
  (suggests hybrid is hurting on that view — relevant to "hybrid retirement").
- **C7 (REV 2 — M1).** Color-channel asymmetry check: if
  `max(R, G, B) > 1.5 × min(R, G, B)` per any cascade-dominance bin, the report
  MUST identify the cause in the verdict (or document why no cause is identifiable).

If C1 or C2 fails → measurement methodology needs revision before re-running.
If C4 = "all bins underpopulated" → cornell-orig-alcove doesn't exercise the
  cascade architecture as expected; investigate before drawing v2.0 conclusions.
If C5 = "neither dominates" → `mbrc_quality_plan.md §1` is wrong, replan v2.0.

## 6. Self-critique — V / G / F

### Validated ✅

- **V1.** Methodology measures gaps before building levers (anti-bug-212 trap, satisfies
  user answer 5).
- **V2.** Noise floor measurement explicitly defines the meaningfulness threshold.
- **V3.** PT-reference cache (rendered once, reused) avoids re-running PT for every
  comparison — saves capture time and freezes the reference.
- **V4.** Three camera positions reduce happenstance from one viewpoint.
- **V5.** Sign-off criteria are testable (numeric thresholds, not subjective).
- **V6.** New render modes / uniform are additive; nothing existing changes behavior.

### Gaps 🔶 (missing or under-specified)

- **G1.** "Mode 16 PT reference" is brokered in `mbrc_quality_plan.md` but the report
  hasn't confirmed Mode 16 currently supports progressive accumulation to 4096 spp on
  cornell-orig-alcove (some PT implementations cap at lower spp due to RNG sequence
  length). **Action: verify Mode 16 supports 4096-spp accumulation in §3 instrumentation
  before locking the spp sweep range.**
- **G2.** No definition of "RMSE" — luminance-RMSE, RGB-RMSE, perceptual (Lab) RMSE?
  Linear-space or sRGB? Different choices weight different errors. **Action: §2 must
  specify linear-RGB per-channel RMSE then mean over channels; report can also include
  luminance RMSE as secondary metric.**
- **G3.** "linearDepth > 4m" and "sdfDist < 0.3m" thresholds in §2.2 are arbitrary
  given the cornell-orig-alcove geometry (room is ~5m on a side?). **Action: §2.2 must
  derive thresholds from the actual scene bounds — e.g., `farThreshold = 0.4 × sceneDiag`
  and `nearThreshold = 0.1 × sceneDiag`.**
- **G4.** No measurement of per-pixel-correction (hybrid) RMSE as a baseline comparison.
  Without it, the verdict says "MBRC alone has X RMSE" but not "MBRC+hybrid has Y RMSE"
  — and the goal is retiring hybrid, so we need to know what RMSE budget hybrid currently
  delivers. **Action: add §2.7 "hybrid-on baseline" capturing MBRC+hybrid RMSE vs PT
  for the same three cameras. Then the verdict is "MBRC alone needs to close the gap
  from X to Y" — concrete target.**
- **G5.** No A/B against the v1.3.1 variance harness format. If the existing harness
  can already produce some of these CSVs, we should reuse the format instead of inventing
  new schemas. **Action: §3 must check `tools/hybrid_validation/*` for reusable
  scripting before writing `run_v20_pre.ps1` from scratch.**

### Flaws ❌ (design errors that change the plan)

- **F1.** **Per-cascade isolation overstates contribution.** Setting "C0-only" doesn't
  measure C0's contribution — it measures "MBRC with only C0 active," which loses the
  cooperative bracket-merge weighting. The actual per-cascade contribution is closer to
  `RMSE(all-except-Cn) − RMSE(all)` ("leave-one-out") than `RMSE(Cn-only) − RMSE(all)`.
  **Fix: replace §2.1 isolation method with leave-one-out:** uniform becomes
  `uCascadeExclude ∈ {-1, 0, 1, 2, 3}` (−1 = none excluded). Render five frames per
  camera: all-cascades, exclude-C0, exclude-C1, exclude-C2, exclude-C3. RMSE delta
  `RMSE(exclude-Cn) − RMSE(all)` is C_n's contribution to error-reduction. This is the
  correct attribution.

- **F2.** **Error decomposition heatmap (§2.2) uses three orthogonal partitions but
  outputs only one RGB image — partitions overlap (a pixel can be both "far-distant"
  AND "near-grazing" depending on thresholds), and the RGB collapse loses the per-bin
  RMSE numbers.** **Fix: §2.2 outputs FOUR artifacts per camera, not one:** (i) red-only
  far-distant heatmap, (ii) green-only near-grazing heatmap, (iii) blue-only "other"
  heatmap, (iv) a CSV with per-bin pixel-count + per-bin mean-RMSE. The four images are
  composited in the report doc for visual inspection; the CSV is the authoritative
  number.

- **F3.** **Noise floor measurement (§2.4) assumes 16-frame stdev captures all noise,
  but stochastic MC noise + multi-bounce temporal feedback have correlated frame-to-frame
  noise** — stdev of correlated samples understates true variance. **Fix: §2.4 captures
  64 frames not 16, and reports both temporal-stdev (within-run noise floor) and
  inter-run-RMSE across 4 independent runs of 16 frames each (between-run noise floor).
  The larger of the two is the meaningfulness threshold.**

- **F4.** **No defense against camera-determinism bug.** The measurement compares
  cascade RMSE vs PT RMSE — but if camera/scene state isn't bit-identical between the
  two captures, RMSE includes free-camera-drift noise. **Fix: §3 adds CLI flag
  `--measurement-camera=<id>` that sets camera to a hardcoded position+target (no free
  flight), and disables any camera-bobbing / TAA-jitter during measurement captures.**

- **F5.** **One-day estimate is optimistic given F1+F4 corrections.** Leave-one-out + 4
  artifacts per camera + 64-frame noise + camera-pinning adds ~3h. Revised estimate:
  **1.5 days.** Plan should acknowledge.

## 7. Improved plan (post-critique)

Apply fixes F1-F5 and gap-actions G1-G5. The amended plan is:

### 7.1 Methodology amendments

- §2.1 → **leave-one-out cascade exclusion**, not isolation (F1).
- §2.2 → **4 artifacts per camera** (3 separate heatmaps + 1 CSV), thresholds derived
  from `sceneDiag` (F2, G3).
- §2.4 → **64-frame temporal stdev + 4-run inter-run RMSE**, take larger (F3).
- §3 → camera pinning via `--measurement-camera=N`, TAA-jitter / camera-bob disabled
  during measurement (F4).
- §2 → RMSE definition: **luminance-RMSE primary** (per §8 answer 2; matches v1.3.1
  variance harness format); linear-RGB per-channel mean reported as secondary (G2).
- New **§2.7 hybrid-on baseline:** MBRC+hybrid vs PT for same 3 cameras (G4).
- §3 → preflight check that Mode 16 supports 4096-spp progressive accumulation (G1).
- §3 → reuse `tools/hybrid_validation/` scripting where possible (G5).

### 7.2 Revised effort (REV 2 — post-critic-06)

Original rev 1 §7.2 estimate was 12-13h (1.5 days). Critic 06 adds:
- H1 skip-in-merge consume rework: +1h (vs uniform atlas-zero gating)
- H2 per-bin convergence test + alcove-high-spp escalation: +2h
- H4 cascade-dominance partition + mode 21: +1.5h
- M3 cascade-config.json plumbing: +0.5h
- M4 pre-registered verdict rule + report scaffolding: +1h
- M5 frame-≥-100 capture gate: +0.25h
- L1 EXR writer: +0.5h
- L4 dual PT mode capture: +1h capture time
- Critique buffer: +1h

**Total rev 2: ~16h = 2 days.** (Was 1.5 days in rev 1.)

### 7.3 Revised artifact list (REV 2 — AUTHORITATIVE)

1. `tools/v20_pre_measurement/pt_cache/<cam>_unbiased_spp<N>.exr` (3 cameras × N tier — L1, L4)
2. `tools/v20_pre_measurement/pt_cache/<cam>_cascadeMatch_spp<N>.exr` (3 cameras × N tier — L1, L4)
3. `tools/v20_pre_measurement/pt_cache/reference_manifest.json` (per-camera-per-bin chosen spp tier — H2)
4. `tools/v20_pre_measurement/cameras.json` (cam0/cam1/cam2 pinned pose+target — F4)
5. `tools/v20_pre_measurement/results/cascade_loo_rmse.csv` (leave-one-out, NOT isolation — H1; luminance + per-channel R/G/B per row — M1)
6. `tools/v20_pre_measurement/results/error_decomp_<cam>_cascadeDom.png` (× 3 cameras — H4 primary)
7. `tools/v20_pre_measurement/results/error_decomp_<cam>_geomFarDist.png` (× 3 cameras — H4 secondary)
8. `tools/v20_pre_measurement/results/error_decomp_<cam>_geomNearGraze.png` (× 3 cameras — H4 secondary)
9. `tools/v20_pre_measurement/results/error_decomp_<cam>_bins.csv` (× 3 cameras; per-bin pixel-count + lum-RMSE + per-channel RMSE for both partition axes — F2 + M1)
10. `tools/v20_pre_measurement/results/pt_convergence.csv` (per-bin pair-RMSE; rows = camera × bin, columns = spp tier — H2)
11. `tools/v20_pre_measurement/results/noise_floor.csv` (within_run_stdev + between_run_rmse + nf_max per camera — M2)
12. `tools/v20_pre_measurement/results/hybrid_on_baseline.csv` (per camera × hybrid-config; shipped + best-arm with stderr — G4, H3-FLAGGED)
13. `tools/v20_pre_measurement/results/cascade-config.json` (MBRC toggle pinning — M3)
14. `tools/v20_pre_measurement/results/verdict.json` (machine-readable C5 evaluation; verdict-rule output per §5 — M4)
15. `doc/7/mbrc_v20_pre_measurement_report.md` — written report with:
    - Q1-Q5 answered with numbers from artifacts 5-12
    - C1-C7 sign-off pass/fail status
    - C5 verdict per pre-registered rule (one of Rules 1-4)
    - Recommendation: which v2.0a lever bundle proceeds (or "stop here / replan §1")

### 7.4 Revised sign-off criteria (REV 2 — see §5 for authoritative C1-C7 list)

§5 now contains the authoritative criteria. Summary of changes vs rev 1:
- C1 → per-bin PT convergence (H2)
- C2 → noise floor uses `max(within_run, between_run)` (M2)
- C4 → cascade-dominance partition criterion (H4); replaces "heatmap clearly favors"
  language
- C5 → pre-registered numeric rule with 4 named verdicts (M4)
- **NEW C6** → hybrid-on baseline quantifies the "gap to retire" (G4)
- **NEW C7** → color-channel asymmetry flag (M1)

### 7.5 What the improved plan still doesn't do

- Doesn't measure perceptual (SSIM/LPIPS) error — that's deferred to MBRC v2.0a once we
  know which lever to build. RMSE is enough for diagnosis.
- Doesn't measure performance (ms/frame) — irrelevant to a quality-gap diagnosis.
- Doesn't validate Sponza — per `mbrc_quality_plan.md §9` answer 2, cornell-only.

## 8. Open questions — ANSWERED 2026-05-20

User decisions:

1. **1.5-day estimate accepted.** Measurement-only work, no v2.0a code until report
   signed off.
2. **Luminance-RMSE is primary** (matching v1.3.1 variance harness format). Linear-RGB
   per-channel mean is reported as secondary. All §2 RMSE references resolve to
   luminance-RMSE unless explicitly stated.
3. **Hybrid-on baseline (§2.7) uses variance-harness-tuned settings** (USER
   OVERRIDE 2026-05-20 — critic H3 rejected).
   - **User intent:** the v2.0 quality target is "what hybrid is *capable of*"
     (the upper bound of the system being retired), not "what hybrid ships with
     by default." Even if the best-CSV-arm is within noise of shipped, it
     defines the ceiling MBRC must reach to plausibly make hybrid optional.
     Setting the target at the noise-favored arm is the conservative choice:
     if MBRC beats the *best* hybrid arm, it definitely beats shipped defaults
     too.
   - **Critic H3 acknowledged but overridden:** yes, the v1.3.1 #1 finding
     showed a05 vs a00 was a statistical tie. The plan records `stderr across
     variance-harness arms` alongside the primary number so the user can see
     when "improvement" is within noise. Decision rule: if MBRC RMSE beats
     `best_arm_RMSE − 2 × stderr`, the retirement is robust to which arm is
     "best."
   - **Implementation:** §2.7 pulls the best-luminance-RMSE arm from
     `tools/hybrid_validation/run_variance_sweep.ps1` output CSV.
     `cascade-config.json` records the chosen arm + its stderr across arms.
     Shipped defaults reported as a secondary row in
     `hybrid_on_baseline.csv` for the "what users see" comparison.
4. **Three cornell-orig-alcove cameras confirmed** (§2.6). Light-position-variation
   alternative declined. cam0 = current default; cam1 + cam2 to be derived during
   implementation from scene bounds (cam1 = inside-alcove-looking-out, cam2 =
   far-corridor-looking-toward-alcove). Concrete positions captured into
   `tools/v20_pre_measurement/cameras.json` before any measurement capture.

### What this locks in (REV 2 — user override on H3)

- §2 RMSE def updates to **luminance-RMSE primary, per-channel R/G/B always reported
  alongside** (REV 2 — M1 strengthens "secondary" to "co-primary table column").
- §2.7 hybrid baseline: **variance-harness-tuned best arm as primary** (user
  override 2026-05-20; critic H3 rejected). Shipped defaults reported as secondary
  row in the same CSV. Stderr-across-arms recorded; MBRC retirement is robust iff
  it beats `best_arm − 2 × stderr` (guards against within-noise win — addresses
  critic's residual concern).
- §2.6 sticks with **three cameras**; concrete positions are an implementation artifact
  (`cameras.json`), not a doc-level commitment.
- §3 effort breakdown: hybrid-tuned-config loader adds ~0.25h; cameras.json + scene-bound
  derivation adds ~0.5h. Both already inside the 1.5-day envelope from F5.

### Next action

Open implementation. First step is the §3 instrumentation table — `uCascadeExclude`
uniform, render mode 20, GUI/CLI hooks, PT cache dumper, sweep harness, analysis
script. Implementation will produce an impl doc
(`doc/7/mbrc_v20_pre_measurement_impl.md`) per [[feedback_doc_style]] convention.
