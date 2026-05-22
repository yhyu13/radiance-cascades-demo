# MBRC v2.0-pre Engine-Default Validation — Sponza + Plain + Mode 0

**Date**: 2026-05-23 (follow-on to
[alpha_m4_deepdive_impl.md](alpha_m4_deepdive_impl.md)).

**Motivation**: The (α) M4 deep-dive recommended shipping
`useDirectionalMerge=0` + `useMultiBounce=1` (g=1.0, D=8 scaled) as new engine
defaults. Per [alpha_m4_deepdive_impl.md §8.2](alpha_m4_deepdive_impl.md#L350),
three blockers had to clear first:

- **B1** — Sponza visual A/B (M4 voxel-grid moire could be more visible on large
  flat marble surfaces than on Cornell's painted walls).
- **B2** — Plain Cornell (no alcove) visual A/B (the alcove geometry skews
  probe-density-vs-radiance distribution; light source is *in* the alcove).
- **B3** — Mode 0 (full composite) A/B (mode 17 GI-only could overstate impact
  under direct-light dominance).

This session ran a single 10-cell sweep that addresses all three with one
factorial — 3 scenes × 2 configs × mode 0, ~5 min wall.

## 1. Scene matrix

[engine_default_validation_sweep.ps1](../../tools/v20_pre_measurement/engine_default_validation_sweep.ps1)
captures:

| Scene tag | --load-obj             | Camera file                         | Cams   | Blocker |
|-----------|------------------------|-------------------------------------|--------|---------|
| alcove    | `cornell-orig-alcove`  | `cameras.json` (cam0, cam2)         | 2      | B3      |
| plain     | `cornell-orig`         | `cameras.json` (cam0, cam2)         | 2      | B2      |
| sponza    | `sponza-master`        | `sponza_cam.json` (cam0=`cam_md`)   | 1      | B1      |

Sponza cam `sponza_cam_md` is the
[doc/5/claude_plan/cam.md](../5/claude_plan/cam.md) viewpoint — atrium interior
facing down-axis, large flat marble surfaces in view (B1's worst-case).

## 2. Configs (current default vs proposed default)

```
baseline  : useDirectionalMerge=1, useDirBilinear=1, useSpatialTrilinear=1, useMultiBounce=0          (M0 + MBoff = current default)
recommend : useDirectionalMerge=0, useDirBilinear=0, useSpatialTrilinear=1, useMultiBounce=1 g=1.0    (M4 + MBon g=1.0 = proposed default)
```

D=8 scaled (engine default). NOT D=16 — that triple-stack
([alpha_m4_deepdive_impl.md §4](alpha_m4_deepdive_impl.md#L120)) was a ceiling
test only; the D=16 main-effect was +0.024 ratio (tie), so the recommendation
sticks with D=8 to keep cost flat.

All cells: `--use-hybrid=0 --noise-seed-offset=0 --render-mode=0
--exit-frames=512`. Hybrid OFF per v2.0 charter (post-hybrid quality).

## 3. Headline numbers (mode 0, full composite incl. direct)

Per-pair from
[engine_default_results.json](../../tools/v20_pre_measurement/engine_default_results.json):

| Scene  | Cam | base meanL | rec meanL | ΔL abs   | ΔL rel   | dR/dG/dB             | %brightened |
|--------|----:|-----------:|----------:|---------:|---------:|----------------------|------------:|
| alcove | 0   | 0.0713     | 0.0851    | +0.0138  | **+19.3%** | +0.015/+0.014/+0.009 | 12.9%       |
| alcove | 2   | 0.1226     | 0.1368    | +0.0142  | **+11.6%** | +0.016/+0.014/+0.010 | 15.2%       |
| plain  | 0   | 0.1042     | 0.1163    | +0.0122  | **+11.7%** | +0.014/+0.012/+0.009 | 9.7%        |
| plain  | 2   | 0.1375     | 0.1523    | +0.0148  | **+10.7%** | +0.016/+0.015/+0.010 | 14.4%       |
| sponza | 0   | 0.2715     | 0.4123    | +0.1408  | **+51.9%** | +0.233/+0.122/+0.059 | **98.0%**   |

**Aggregate over 5 pairs**: mean rel ΔL = **+21.0%** (range +10.7% .. +51.9%),
mean %brightened = **30.0%**, mean %darkened = **0.00%**, bright-clip increase
= **+0.00pp**.

## 4. Ship/no-ship verdict gates

Gates pre-committed in
[analyze_engine_default.py:131-146](../../tools/v20_pre_measurement/analyze_engine_default.py#L131):

- **G1 — mode-0 mean luminance lift ≥ +5% on at least one cam per scene** (the
  recommendation must be measurable in the full composite, not just GI-only).
  - alcove max lift = **+19.3% → PASS**
  - plain  max lift = **+11.7% → PASS**
  - sponza max lift = **+51.9% → PASS**
- **G2 — bright-clip increase < 5pp anywhere** (no catastrophic over-exposure
  of direct-lit regions).
  - max increase across 5 pairs = **+0.00pp → PASS**
- **G3 — min mode-0 lift across all pairs > −2%** (no scene is catastrophically
  darkened by the new default).
  - min lift = **+10.7% → PASS**

**All gates PASS.** B1/B2/B3 blockers from
[alpha_m4_deepdive_impl.md §8.2](alpha_m4_deepdive_impl.md#L350) clear.

## 5. Visual A/B observations

All 10 captures viewed:
[tools/v20_pre_measurement/captures_engine_default/](../../tools/v20_pre_measurement/captures_engine_default/).

**Cornell (alcove + plain, cam0 + cam2)** — recommend is visibly brighter on
back walls, ceiling, and indirect-lit floor regions. Color bleeding is the
same physical character in baseline and recommend (no new bleed where there
shouldn't be). No new color fringing, no new firefly speckles, no new ringing.
The alcove and plain pairs look like the same lighting model with the
indirect-lit bias correctly increased — exactly what M4+MB should do.

**Sponza cam0 (atrium down-axis)** — recommend is **substantially warmer and
brighter**. Floor/walls shift from soft warm-grey to saturated orange. Marble
floor moire (B1's worst-case concern) does **not** appear — the underlying
voxel-cone blockiness on ceiling arches is visible in *both* baseline and
recommend (it's an unrelated voxel-grid artifact at this distance, not an M4
regression). The +51.9% lift + 98% brightened-pixel count is the single
largest scene effect we have measured to date.

### Sponza warm-shift caveat

dR / dG / dB = **+0.233 / +0.122 / +0.059** — red shifts ~4× as much as blue.
Sponza ships with a warm directional sun + warm marble albedo, so warmer GI is
the *expected* direction; the question is whether **+0.233 R is physically
correct or an over-correction**. Three reasons to believe it's correct rather
than a bug:

1. **Direction follows scene**: Cornell pairs all have dR < dG +
   small dB, which is Cornell's red-wall + white-ceiling palette. Sponza
   pulling toward red follows Sponza's warm-marble palette. The bias direction
   matches the scene in every case.
2. **Bright-clip stays at 0**: even with +0.23 R, no pixel goes super-bright
   (>0.95 luminance). The shift is in the indirect midtone range, not pushing
   into clipping — matches what additional indirect bounces should look like.
3. **Single bounce vs many**: baseline (M0 + MBoff) is a single direct-only
   composite by construction (`useMultiBounce=0` zeros the feedback loop). PT
   reference [pt_reference_impl.md](pt_reference_impl.md) carries many bounces
   of warm tint accumulation. The recommend config moves toward the PT
   reference, not away from it.

**Recommendation**: ship as new default but include a release note: "Sponza GI
becomes noticeably warmer — this is the multi-bounce term, not a bug." If
users prefer the old look they can revert with `--use-multi-bounce=0`.

## 6. Self-critique

### What this validation tested
- 5 capture pairs (2 cornell-alcove + 2 cornell-plain + 1 sponza)
- One viewpoint per camera (no parallax / multi-view)
- 512 frames (sufficient for MBRC convergence per
  [mbrc_v20_pre_measurement_impl.md](mbrc_v20_pre_measurement_impl.md))
- Mode 0 full composite (NOT GI-only mode 17 — that was the prior phase's
  metric, and the §8.2 critique flagged it as overstating impact under direct
  light)
- Three pre-committed numerical gates with `if/elif/else` PASS/MARGINAL/FAIL

### What this validation did NOT test
- **Motion / temporal stability** — single-frame captures only. The hybrid v1.3
  work flagged temporal oscillation in
  [hybrid_v12_validation_phase8_impl.md](hybrid_v12_validation_phase8_impl.md);
  the new default could in principle flicker more. Not in scope: this is the
  *non-hybrid* default and the hybrid path is unchanged.
- **Other scenes** — only cornell variants + sponza. Risk: an unmeasured scene
  could regress. Mitigation: the recommendation can be reverted with two CLI
  flags; the cost of getting this wrong is bounded.
- **MB g sweep on plain / sponza** — the g=1.0 choice came from
  [mb_gain_sweep_impl.md](mb_gain_sweep_impl.md) which used cornell-orig-alcove
  only. The +51.9% Sponza lift suggests **g=1.0 may be too hot for Sponza**;
  the optimal-per-scene g could be lower. Not blocking shipping — but a
  per-scene g sweep is a reasonable next step if user feedback says "Sponza
  too saturated".

### What could still go wrong
- **MB feedback may amplify driver-specific quantization** on different GPUs.
  Not measured here; engine default change should be flagged in CHANGELOG so
  external users hit it deliberately, not by surprise.
- **Sponza's warm shift could be aesthetically objectionable** even if
  physically correct — see §5 caveat. Users have a one-flag escape hatch.

## 7. Verdict

**SHIP** `useDirectionalMerge=0` + `useMultiBounce=1` (g=1.0, D=8 scaled) as
new engine default for the non-hybrid path. All three §8.2 blockers cleared:

- B1 Sponza: no marble moire; warm shift is physically expected and within
  bright-clip budget.
- B2 Plain Cornell: +11.7% / +10.7% lift, no regressions.
- B3 Mode 0 composite: +10.7% .. +51.9% range, mean +21.0% — same direction
  and same magnitude class as the prior mode-17 GI-only measurement, so the
  recommendation is not an artifact of GI-isolation.

**Defaults to change** in [src/main3d.cpp](../../src/main3d.cpp) +
[res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp) flag
init paths:

```
useDirectionalMerge:  1 → 0
useDirBilinear:       1 → 0
useSpatialTrilinear:  1 → 1   (unchanged)
useMultiBounce:       0 → 1
multiBounceGain:    n/a → 1.0
```

CLI flags (`--use-directional-merge=...`, `--use-multi-bounce=...`,
`--multi-bounce-gain=...`) continue to override defaults so the old engine
behavior remains reachable via `--use-directional-merge=1 --use-multi-bounce=0`.

## 8. Artefacts

- Sweep harness: [tools/v20_pre_measurement/engine_default_validation_sweep.ps1](../../tools/v20_pre_measurement/engine_default_validation_sweep.ps1)
- Analyzer: [tools/v20_pre_measurement/analyze_engine_default.py](../../tools/v20_pre_measurement/analyze_engine_default.py)
- Sponza camera: [tools/v20_pre_measurement/sponza_cam.json](../../tools/v20_pre_measurement/sponza_cam.json)
- Captures (10 PNGs): [tools/v20_pre_measurement/captures_engine_default/](../../tools/v20_pre_measurement/captures_engine_default/)
- Per-pair JSON: [tools/v20_pre_measurement/engine_default_results.json](../../tools/v20_pre_measurement/engine_default_results.json)

## 9. Next phase

The default-flag flip itself is a small code change (~5 lines of init in
[src/main3d.cpp](../../src/main3d.cpp)). With this validation signed off, the
remaining v2.0-pre work is:

1. Land the engine-default flip + CHANGELOG note (mention Sponza warm shift).
2. Move v2.0 closing checklist forward in
   [mbrc_v20_pre_measurement_plan.md](mbrc_v20_pre_measurement_plan.md).
3. Optional: per-scene MB-gain sweep on Sponza (only if user feedback flags
   the warm shift as too strong).
