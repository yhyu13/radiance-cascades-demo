# Critic Review 01 — sponza_gi_root_cause_hypothesis_test_plan.md

Reviewed: 2026-05-12

Target: [doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test_plan.md](../sponza_gi_root_cause_hypothesis_test_plan.md)

## Verdict

The plan is structurally sound — a measurement-only A/B sweep that re-uses
already-landed CLI infrastructure (Step 12 `--cascade-c0-res`/`--window-size`
+ lighting-controls follow-up `--light-direction`/`--light-intensity`/
`--ambient-bake-strength`/`--ambient-composite-strength`), with no code
changes and a clear ranking of 4 ranked hypotheses. The decision to zero
both ambient floors so that "what mode 0 shows is purely cascade GI" is
the right experimental design — without it, the user-perceived "GI" is
dominated by the constant `vec3(0.05)` floor that codex 09 already
identified as the dominant signal at the cam.md viewpoint.

However, the plan has several factual errors and unaddressed dependencies
that will hurt the experiment's legibility:

1. The "8× probe-count range" claim for {16, 32, 48, 64} is wrong by 8×
   (the actual cube-volume range is **64×**, from 4096 → 262144 probes).
2. The "~5× cascade bake time at C0=64" cost estimate undershoots the
   measured Step 12 scaling data by ~2× (32→64 measured **9.5×**, not 5×).
3. The Cornell anyPct ≈ 80% claim is fabricated — codex 09 only measured
   Sponza (3.5%) and stated 50–80% as a *hypothetical expectation* for
   "what a healthy bake should look like." Cornell was never measured.
4. The "spot-check anyPct from probe stats JSON" verification path
   silently assumes Phase 12b auto-burst capture is firing — the plan's
   command does NOT pass any of the auto-capture flags, so no
   `probe_stats_*.json` will be written. Outcome A/B/C will rely entirely
   on visual inspection + meanLum trend.
5. The plan keeps GI blur radius at default (8) — the bilateral
   postfilter will smear away exactly the kind of probe-density-driven
   detail differences the experiment is trying to measure. To isolate
   the density signal, blur should be reduced (radius 1) or disabled.
6. The known NaN/Inf first-frame contamination (codex 09 P0 blocker,
   reproduced in every step10/step11 log file: `[4c A/B] meanLum: C0=-nan
   C1=-nan ...`) is not acknowledged. Any meanLum trend reading must skip
   the first-frame NaN/Inf line; if a capture happens to land on a
   transient-NaN frame, the meanLum readout is garbage. The zero-init
   plan that would have fixed this (`doc/5/claude_plan/zero_init_cascade_textures_step11_followup_plan.md`)
   has not been implemented (no `glClearTexImage` on cascade textures
   anywhere in `src/demo3d.cpp` — only on `sdfTexture` at
   [demo3d.cpp:2009](../../../src/demo3d.cpp#L2009)).
7. H1 vs H4 are presented as mutually exclusive but the codebase has a
   known "Type A banding (C1-discontinuity at probe centers)" issue
   (cerebrum, "Open" section). Type-A banding *will* shift in
   world-space scale as probe density changes — so a density sweep can
   simultaneously confirm H1 (more energy, brighter shadows) and exhibit
   the H4 signature (banding pattern shifts but persists). The mutually-
   exclusive framing will force a forced choice on a confounded result.

The experiment is still worth running — H1 dominance is genuinely the
most-likely answer and the A/B will rank it usefully. But the cost
estimate, probe-count math, anyPct anti-prediction, and JSON capture path
all need fixes before the report-template (Outcome A/B/C/D) maps cleanly
to what the data will actually look like.

## Evidence Checked

- [doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test_plan.md](../sponza_gi_root_cause_hypothesis_test_plan.md) — full read.
- [src/main3d.cpp](../../../src/main3d.cpp) lines 184–319: confirms all
  CLI flags exist as the plan describes — `--load-obj=sponza-master`
  (line 366), `--gpu-voxelize` (240), `--gpu-sdf` (referenced),
  `--cascade-c0-res=N` (281), `--exit-frames=N` (215),
  `--screenshot=path` (218), `--ambient-bake-strength` (248),
  `--ambient-composite-strength` (253), `--light-intensity` (258),
  `--light-direction` (263, normalizes vector in setter, implies
  `useDirectionalLight=true`), and `--camera-pos` (sscanf %f,%f,%f).
- [src/demo3d.cpp:225](../../../src/demo3d.cpp#L225): `raymarchSteps(256)` confirms plan's
  "default raymarch steps (256)".
- [src/demo3d.cpp:279](../../../src/demo3d.cpp#L279): `giBlurRadius(8)` confirms plan's
  "default GI blur radius (8)".
- [src/demo3d.cpp:175](../../../src/demo3d.cpp#L175): `useCascadeGI(true)` — cascade on by
  default, no need to pass an enable flag.
- [doc/5/claude_plan/codex_critic/09_step11_heatmap_verify_report.md](../../../doc/5/claude_plan/codex_critic/09_step11_heatmap_verify_report.md):
  Sponza C0 anyPct = 3.5% (line 119), Cornell NOT measured. The ~50–80%
  quoted figure is the *expected* value for a healthy bake, not a Cornell
  measurement.
- [doc/5/claude_plan/perf/gi_pass_scaling_experiment.md](../../../doc/5/claude_plan/perf/gi_pass_scaling_experiment.md) lines 16, 63:
  measured C0 bake at 32³ = 4.7 ms, at 64³ = 44.6 ms — that's 9.5×, not
  5×. Note also the 48³ = 56.9 ms anomaly (higher than 64³), which is
  the kind of single-shot-variance artifact codex 13 flagged. Whole-cascade
  reallocation per data point is documented at codex 12 as ~1–2 s.
- [src/demo3d.cpp:2009](../../../src/demo3d.cpp#L2009): only `glClearTexImage` site is
  the SDF texture. Cascade textures (probeGrid, probeAtlas, history) are
  never zero-initialized — the codex 10 zero-init plan was approved but
  never landed. NaN/Inf first-frame contamination is a known live bug.
- [tools/app_run_step10_sponza_mode4.log:160](../../../tools/app_run_step10_sponza_mode4.log#L160),
  [tools/app_run_step10_sponza_mode10.log:160](../../../tools/app_run_step10_sponza_mode10.log#L160),
  [tools/app_run_step10_sponza_mode0.log:160](../../../tools/app_run_step10_sponza_mode0.log#L160): every
  log's first `[4c A/B]` line shows `nan`/`-nan`/large-negative values
  on at least one cascade. Trend-reading must skip line 0.
- [src/demo3d.cpp:1011](../../../src/demo3d.cpp#L1011): Phase 12b auto-burst is gated by
  the auto-capture warm-up delay path (`burstState = CapM0` triggered
  inside the `--auto-rdoc`/`--auto-sequence`/`--auto-analyze` branch).
  The plan's command does not pass any of these, so no `probe_stats_*.json`
  will be produced.
- [res/shaders/radiance_3d.comp:387–393](../../../res/shaders/radiance_3d.comp#L387-L393): the
  smoothstep blend cited as a possible H4 culprit is at lines 387–393
  (not 388–389 — a small reference offset).
- [res/shaders/radiance_3d.comp:180](../../../res/shaders/radiance_3d.comp#L180):
  `sampleUpperDirTrilinear` exists; reference is correct.

## What Looks Good

- **Strip ambient floors before measuring**: setting both
  `--ambient-bake-strength=0` and `--ambient-composite-strength=0` is the
  correct experimental control. Codex 09 P0/P1 already established that
  the `vec3(0.05)` floor swamps the real cascade signal at the cam.md
  viewpoint (mode 0 looks "GI-strong" because the floor is amplified
  through bake; turn it off and the right wall goes near-black). Without
  this strip, an A/B at varying density would just show "the floor
  amplifies more / less depending on probe count," which is not the GI
  question being asked.
- **Anti-confirm point at C0=16**: including a below-default density
  data point is good design — if H1 is real, C0=16 should look visibly
  *worse* than C0=32, not the same. Without the lower bound, the
  experiment can only confirm "more density helps," not "less density
  hurts proportionally."
- **Sponza-master scene + cam.md viewpoint**: this is the same scene +
  camera that codex 09 measured and that all step10/step11 logs use.
  Continuity with prior measurements is preserved — meanLum baselines
  from the existing logs (C0 ≈ 0.017, C1 ≈ 0.016, C2 ≈ 0.014, C3 ≈ 0.008
  at C0=32 with floor on; will drop substantially with floor at 0) can
  be diff'd against the new captures.
- **Hypothesis ranking with predicted A/B outcomes per hypothesis**:
  the 4-row prediction table is the strongest part of the plan —
  H1/H2/H3/H4 each have a *distinct, falsifiable* visual prediction
  before the experiment runs. This is genuine pre-registration.
- **Honest expectation note at the bottom**: explicitly stating "expect
  H1 helps significantly; H2 still matters; H3 still matters" rather than
  promising a single silver bullet matches the codex 09 verdict and
  protects the report from over-claiming on whichever outcome lands.
- **Lighting-direction implies-useDirectionalLight is correct**: the
  setter `--light-direction=` at [main3d.cpp:268](../../../src/main3d.cpp#L268) auto-flips
  `useDirectionalLight=true`, so the plan does NOT need a separate
  `--use-directional-light` flag. The plan's note "Sponza auto-enables
  directional light" matches the code.
- **Out-of-scope section is well-curated**: deferring H2 (volume-res
  sweep) until H1 outcome known, deferring multi-bounce, and deferring
  per-cascade D-resolution sweep are all correct sequencing — these are
  expensive and only worth doing if H1 fails to dominate.

## Findings

### 1. Probe-count range stated as 8×; actual is 64×

Severity: Medium

Plan text (line 49):

> `--cascade-c0-res` ∈ **{16, 32, 48, 64}** (4 data points spanning 8× probe-count range)

Probe count scales as `cascadeC0Res³`. The 4 data points produce:

| C0 res | probes | ratio vs 16 |
|---|---|---|
| 16 | 4,096 | 1× |
| 32 | 32,768 | 8× |
| 48 | 110,592 | 27× |
| 64 | 262,144 | **64×** |

The 16→64 sweep spans **64×**, not 8×. The 8× figure is the 16→32
or 32→64 sub-range. This matters because the bake-time cost scaling
(also cubic per Step 12) means the C0=64 capture will be ~64× more
expensive *per cascade frame* than C0=16, not 8×. The runtime estimate
"~2 minutes total" assumes the smaller cost; at 300 frames per capture
× 4 captures × cubic scaling, the real total is closer to 4–6 minutes
on the cited RTX 2080 SUPER (Step 12 measured C0=64 single-frame bake at
44.6 ms; 300 frames = ~13 s of cascade work alone, and that's *without*
the 1–2 s reallocation cost per data point).

Fix: rewrite as "4 data points spanning **64× probe-count range** (16³ →
64³ = 4K → 262K probes)." Update the runtime estimate accordingly.

### 2. C0=64 cost cited as "~5× cascade bake time"; measured is ~10×

Severity: Medium

Plan text (Outcome A, line 95):

> Cost: ~5× cascade bake time at C0=64 (matches Step 12 scaling experiment data).

Step 12 scaling data ([gi_pass_scaling_experiment.md:16](../../../doc/5/claude_plan/perf/gi_pass_scaling_experiment.md#L16)):

> probe-res sweep at fixed 320×180 window shows C0 bake going from
> 167 µs (8³) → 4.7 ms (32³) → 56.9 ms (48³) → 44.6 ms (64³)

C0 bake at 32³ = 4.7 ms, at 64³ = 44.6 ms → **9.5×**, not 5×. (The 48³
data point at 56.9 ms is anomalously higher than 64³ — the kind of
single-shot variance codex 13 flagged at ±2-5×, but even the smoothed
trend gives ~10× from 32→64.)

Additionally, that's *just C0*. C1 = C0/2 = 32 at default vs 16 at
C0=32 baseline, also cubic-scaling — so the *total* 4-cascade bake cost
at C0=64 vs C0=32 is closer to 8–10× across the whole frame. The
"5× cascade bake time" claim understates this by 2×.

Fix: either re-cite as "~10× cascade bake time at C0=64 vs C0=32 (Step 12
measured 4.7 ms → 44.6 ms for C0; whole-frame cascade work scales
similarly)" or, if the intent was "5× of frame budget," say so explicitly.

### 3. Cornell anyPct ≈ 80%+ claim is unsubstantiated

Severity: Medium

Plan text (line 67):

> Cornell should show much smaller relative change because its anyPct is
> already ~80%+ at C0=32.

Codex 09 ([09_step11_heatmap_verify_report.md:119](../../../doc/5/claude_plan/codex_critic/09_step11_heatmap_verify_report.md#L119)) only measured
Sponza (C0 anyPct ≈ 3.5%). The ~50–80% figure cited there is a
*hypothetical expectation* for what a healthy bake should look like, NOT
a Cornell measurement. Cornell's actual anyPct at C0=32 is unknown.

The plan uses this fabricated baseline to predict that Cornell will be
the "control" (showing little change with density), and then derives an
inference-rule from it (small Cornell change + large Sponza change = H1
confirmed). If Cornell actually has anyPct ≈ 30% (plausible — it's a
small box where the cascade volume includes lots of empty interior
space outside the box walls), then it will show large changes too, and
the "control" interpretation collapses.

Fix: either (a) drop the Cornell-as-control claim and present the
optional Cornell captures as data only, not as a falsification anchor,
or (b) measure Cornell anyPct first (one extra capture at C0=32 with
auto-burst enabled to write the JSON) and use the measured value.

### 4. Probe-stats JSON capture path requires auto-burst flag not in command

Severity: Medium

Plan text (lines 79-82):

> spot-check `anyPct` from the probe stats JSON (Phase 12a infrastructure
> produces `tools/probe_stats_*.json` when burst capture fires...

Auto-burst is gated at [demo3d.cpp:1001-1011](../../../src/demo3d.cpp#L1001-L1011) on the
auto-capture warm-up state machine (`autoCaptureArmed && burstState ==
Idle && t > warmupDelay`). The state machine only arms when one of
`--auto-rdoc`, `--auto-sequence`, or `--auto-analyze` is passed. The
plan's command passes none of them, so the burst will never fire and no
`probe_stats_*.json` will be written.

Concretely, the Outcome A/B/C distinction is supposed to lean on
"meanLum trend + spot-check anyPct from JSON." Without JSON, anyPct
cannot be measured at all, and the meanLum trend alone (already noisy
because of the first-frame NaN — see Finding 6) becomes the sole
quantitative signal. That works for H1-vs-H3 (both move meanLum
differently) but is weak for H1-vs-H4 (both can leave meanLum monotone
while disagreeing on anyPct).

Fix: add `--auto-rdoc` (or whichever flag actually triggers burst
capture without launching RenderDoc) to the command, OR drop the
"spot-check anyPct" path and explicitly state that Outcome D
falsification rests on visual inspection alone.

### 5. GI blur radius default (8) will smear out density signal

Severity: Medium

Plan text (line 58):

> Default raymarch steps (256), default GI blur radius (8)

Default `giBlurRadius = 8` ([demo3d.cpp:279](../../../src/demo3d.cpp#L279)) means the
two-pass bilateral postfilter has an 8-tap radius per direction. This
is by design a smoothing pass that hides probe-density-driven detail
differences (that's what bilateral blur exists to do — exchange spatial
resolution for variance reduction).

For an experiment that's specifically asking "is the cascade signal
coarse because we don't have enough probes," running it with the blur
that *exists to hide probe coarseness* will partially mask the very
signal being measured. C0=16 might look much better than it really is
because blur = 8 averages over many probes per pixel; C0=64 might look
only modestly better than C0=32 because the blur was already doing some
of the work.

Fix: add `--gi-blur-radius=1` to the command (using the Step 12 setter)
or run a parallel pair (radius=8 vs radius=1) at one density value to
calibrate how much the blur is masking. The Step 12 setter is in place
([demo3d.cpp:581](../../../src/demo3d.cpp#L581)), so this is a one-flag change.

### 6. NaN/Inf first-frame contamination not acknowledged

Severity: Medium

Every step10/step11 log shows first-frame `[4c A/B] meanLum` containing
NaN/Inf or large-negative values:

```
[tools/app_run_step10_sponza_mode4.log:160]
[4c A/B] blend=0.5 (blended)  meanLum: C0=nan  C1=-nan  C2=0.00000  C3=0.02140
[4c A/B] blend=0.5 (blended)  meanLum: C0=0.01709  C1=0.01605  C2=0.01379  C3=0.00770   ← real values from frame 2 onward
```

This is the codex 09 P0 blocker (cascade textures created with
`data=nullptr` in `glTexImage3D`, leaving uninitialized GPU memory
that produces NaN until the first full bake completes). The zero-init
fix plan ([zero_init_cascade_textures_step11_followup_plan.md](../../../doc/5/claude_plan/zero_init_cascade_textures_step11_followup_plan.md))
was reviewed at codex 10 but **has not landed** — `glClearTexImage` is
only called on the SDF texture at [demo3d.cpp:2009](../../../src/demo3d.cpp#L2009),
not on cascade textures.

Implications for the plan:

- "every frame logs `[4c A/B] meanLum`" is true, but trend-reading must
  skip line 0 (NaN-contaminated).
- If the `--exit-frames=300` capture happens to land on a frame where
  one cascade has just been re-initialized (e.g. after `--cascade-c0-res`
  triggers `destroyCascades + initCascades`), the meanLum readout for
  that frame may itself be NaN.
- The `setCascadeC0Res` setter ([demo3d.cpp:5216](../../../src/demo3d.cpp#L5216)) does
  `destroyCascades + initCascades + cascadeReady=false`. After that, it
  takes 4 staggered frames for all 4 cascades to bake fresh. If any
  cascade still contains uninitialized memory at the meanLum readout
  frame, the trend table will be polluted.

Fix: explicitly call out the first-frame NaN issue and either (a) require
the report to use a meanLum value taken at frame ≥2, (b) land the
zero-init plan first, or (c) capture meanLum across the entire 300-frame
window and report a stable median/mode rather than any single frame.

### 7. H1 vs H4 are not mutually exclusive (Type A banding is density-coupled)

Severity: Low

Plan text (Outcome D, line 117):

> Even if magnitude grows with density, the same world-space banding/leak
> pattern repeats at all densities — meaning the artifact is algorithmic,
> not sampling-density.

Cerebrum "Open" entry: *"Type A banding (C1-discontinuity at probe
centers) — suppressed by blur, not structurally fixed."*

Type-A banding is, by construction, **density-coupled**: the band
spacing equals the world-space probe spacing, which scales with
`1/cascadeC0Res`. So at C0=16 the banding will be at one world-space
scale; at C0=64 it will be at a 4× finer scale. Outcome D's "same
world-space scale" criterion will *correctly* fire for some algorithmic
bugs (e.g. a fixed-pixel-size shader bug) but will *miss* exactly the
banding class the codebase is known to suffer from.

Conversely, Outcome A's "fewer checkerboard leaking artifacts" can fire
for two completely different reasons:
- H1 dominant: more probes → more surface hits → less leak.
- H4 dominant + density-coupled: more probes → finer-scale banding →
  perceptually less leak (the eye smooths fine bands into a uniform
  tint, but the algorithmic kink is still there).

These are mutually confounding. The report template should be widened so
"H1 confirmed" requires both a meanLum increase AND a non-density-coupled
banding pattern (i.e. blur-radius A/B at fixed density should improve
the artifact independently). Otherwise the plan can record "H1 confirmed"
when the truth is "H1 + H4 simultaneously, density helps cosmetically."

Fix: add a third axis to the report template — at the chosen winning
density, also vary GI blur radius {1, 4, 8} to disentangle whether the
remaining artifact is density-bound (changes with C0) or filter-bound
(changes with blur). Two extra captures, well within the experiment's
scope.

### 8. Smoothstep / sampleUpperDirTrilinear line references off

Severity: Low

Plan text (Outcome D, lines 121-123):

> Likely culprits: `sampleUpperDirTrilinear` (Phase 5d), the smoothstep
> blend at `radiance_3d.comp:388-389`, or directional-bin merge edge cases.

Actual locations in current source:

- `sampleUpperDirTrilinear`: defined at [radiance_3d.comp:180](../../../res/shaders/radiance_3d.comp#L180); called at line 371. ✓ correct function name.
- The smoothstep blend spans lines 387–393, not 388–389:
  ```
  [387]  // Phase 7: smoothstep removes the derivative kink ...
  [389]      float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
  [390]          ? 1.0 - smoothstep(0.0, 1.0,
  [391]                clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0))
  [392]          : 1.0;
  [393]      rad = hit.rgb * l + upperDir * (1.0 - l);
  ```

  Lines 388–389 land on the comment second-line and the `float l =`
  declaration. The actual `smoothstep` call is at line 390.

Fix: cite as `radiance_3d.comp:387-393` (or `:390` for the smoothstep
call specifically).

### 9. Runtime estimate "~2 min" is optimistic given cubic scaling

Severity: Low

Plan text (line 62):

> Total captures: 4 (one per probe-res value). Runtime ~2 minutes
> including cascade reallocation overhead.

Per-capture work at default 60 fps with `--exit-frames=300`:

| C0 res | C0 bake/frame | 300 frames × C0 alone | + 3 other cascades + raymarch + reallocation |
|---|---|---|---|
| 16 | <1 ms | <1 s | ~5 s |
| 32 | 4.7 ms | 1.4 s | ~10 s |
| 48 | ~50 ms | 15 s | ~30 s |
| 64 | 44.6 ms | 13 s | ~30 s |

Total: ~75 s of cascade work + 4 × (1–2 s reallocation) + 4 × app
startup/teardown (~3 s each = 12 s) + GPU command buffer flush
overhead. Realistic total: **3–5 minutes**, not 2.

This is not a blocker — it just means an unattended run is fine, but
quoting "2 minutes" risks a user thinking the run hung at the 3-minute
mark when it's actually progressing normally.

Fix: revise to "~3-5 minutes" or drop the estimate (the user will see
when it finishes).

### 10. Output filename in Verification step 4 doesn't match plan filename

Severity: Low

Plan text (line 139):

> 4. Report dumped to `doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test.md`

The current plan file is `sponza_gi_root_cause_hypothesis_test_plan.md`
(with `_plan` suffix). The convention used in `doc/5/claude_plan/`
appears to be `<topic>_plan.md` for plan, `<topic>_impl.md` for
implementation note (e.g. `gpu_sdf_step8_plan.md` /
`gpu_sdf_step8_impl.md`, `load_path_step9_plan.md` /
`load_path_step9_impl.md`).

The plan should output the report under
`sponza_gi_root_cause_hypothesis_test_impl.md` (or `_report.md`) to
match — otherwise the report will sit beside the plan with a name
that's confusingly close but not following convention.

Fix: rename the verification output path to
`doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test_impl.md`.

## Verification Gaps To Add

- Correct the probe-count range from "8×" to "64×" (Finding 1) and
  update the runtime estimate accordingly.
- Replace the "~5× cascade bake time at C0=64" cost with the measured
  ~10× from Step 12 data (Finding 2), and clarify whether the figure
  refers to single-pass or whole-frame cost.
- Either drop the Cornell-as-control claim or measure Cornell anyPct
  first (Finding 3). If kept, the report template's "control = small
  Cornell change" inference should be stripped until the baseline is
  observed, not assumed.
- Add `--auto-rdoc` (or equivalent burst-trigger flag) to the command
  so `probe_stats_*.json` is actually written (Finding 4), or remove
  the "spot-check anyPct from JSON" verification path.
- Add `--gi-blur-radius=1` to the command (Finding 5), or run a
  blur-radius A/B at one density to calibrate how much the default
  radius=8 is masking density-driven detail.
- Acknowledge the NaN/Inf first-frame issue (Finding 6) and require
  meanLum readings be taken at frame ≥2, OR land the zero-init plan
  before running the experiment.
- Widen the Outcome A criterion: "H1 confirmed" should require both
  meanLum increase AND a non-density-coupled banding pattern (Finding 7).
  Add a blur-radius axis at the winning density to separate density-bound
  from filter-bound artifacts.
- Fix line references: `radiance_3d.comp:387-393` (or `:390` for
  smoothstep specifically) instead of `:388-389` (Finding 8).
- Revise runtime estimate to 3-5 minutes or drop it (Finding 9).
- Rename the report output path to match the `_plan.md` / `_impl.md`
  convention from `doc/5/claude_plan/` (Finding 10).

## Things The Plan Got Right (Worth Preserving)

- Pre-registration of falsifiable predictions per hypothesis.
- Strip-both-ambient-floors as the experimental control — codex 09
  established this is the only way to see the real cascade signal.
- Anti-confirm point at C0=16.
- Continuity with cam.md viewpoint and prior step10/step11 measurements.
- Honest expectation note ruling out the "single silver bullet"
  narrative before the data lands.
- No code changes — measurement-only is correctly scoped.
- Out-of-scope curation defers the expensive H2/H3 follow-up tests
  until H1 outcome is known.
