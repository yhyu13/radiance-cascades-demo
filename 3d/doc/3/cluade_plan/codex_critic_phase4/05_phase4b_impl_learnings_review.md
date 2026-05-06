# Phase 4b Implementation Learnings Review

Reviewed file: `doc/cluade_plan/phase4b_impl_learnings.md`  
Review date: 2026-04-24T08:24:13+08:00

## Summary

The underlying 4b implementation is real:
- `baseRaysPerProbe` exists and is wired at init and at runtime
- the capped slider range `4..8` is in place
- the settings panel, cascade panel, and tutorial panel were updated
- extra debug visualization support was added

The main problems are in how the learnings doc interprets those debug metrics. It currently presents spatial probe-distribution measurements as if they were direct noise/convergence measurements, which is too strong.

## Findings

### High: the histogram and variance are described as noise/convergence metrics, but the implementation measures spatial probe-luminance distribution across the whole cascade

Refs:
- `doc/cluade_plan/phase4b_impl_learnings.md:84`
- `doc/cluade_plan/phase4b_impl_learnings.md:85`
- `doc/cluade_plan/phase4b_impl_learnings.md:86`
- `doc/cluade_plan/phase4b_impl_learnings.md:87`
- `doc/cluade_plan/phase4b_impl_learnings.md:89`
- `doc/cluade_plan/phase4b_impl_learnings.md:95`
- `src/demo3d.cpp:396`
- `src/demo3d.cpp:416`
- `src/demo3d.cpp:419`
- `src/demo3d.cpp:421`
- `src/demo3d.cpp:436`

The code computes:
- `probeVariance[ci]` from the luminance of all probes in a cascade
- a histogram of probe luminance values across the full cascade texture

That is a spatial distribution over probes, not a per-probe Monte Carlo variance estimate. A wide histogram or large variance can come from real scene structure, strong light gradients, wall colors, and sky coverage differences, not just sampling noise.

Impact:
- "Narrow spike = converged" is too strong
- "Wide spread = noisy" is too strong
- "Directly quantifies noise level" is inaccurate
- comparing `base=4` to `base=8` can still be useful, but only as a heuristic visual trend, not as a direct noise proof

Recommended fix:
- reword these as `probe-luminance distribution` and `cascade-wide luminance variance`
- describe them as heuristic indicators that may correlate with noise, not as direct convergence metrics
- if true sampling variance is wanted later, store additional per-probe moments or repeated-sample comparisons

### Medium: “No shader changes were needed” is misleading in this learnings doc

Refs:
- `doc/cluade_plan/phase4b_impl_learnings.md:36`
- `doc/cluade_plan/phase4b_impl_learnings.md:38`
- `doc/cluade_plan/phase4b_impl_learnings.md:97`
- `doc/cluade_plan/phase4b_impl_learnings.md:99`
- `src/demo3d.cpp:673`
- `res/shaders\\radiance_debug.frag:36`
- `res/shaders\\radiance_debug.frag:177`

The statement is only true for radiance integration in `radiance_3d.comp`. The implementation also added shader-facing debug support:
- `radiance_debug.frag` gained `uRaysPerProbe`
- mode 4 decodes hit types from packed alpha
- `renderRadianceDebug()` now pushes `uRaysPerProbe`

Impact:
- the doc understates the actual implementation scope
- a reader could incorrectly conclude that 4b touched only C++ code

Recommended fix:
- change the wording to `No shader changes were needed for radiance integration`
- then separately note that debug visualization shader support was added

### Medium: the learnings doc mixes implemented facts with expected runtime outcomes without clearly separating them

Refs:
- `doc/cluade_plan/phase4b_impl_learnings.md:5`
- `doc/cluade_plan/phase4b_impl_learnings.md:87`
- `doc/cluade_plan/phase4b_impl_learnings.md:129`
- `doc/cluade_plan/phase4b_impl_learnings.md:143`

Examples:
- `C3 histogram should visibly tighten when moving slider from base=4 to base=8`
- `Python-verified` decode claims are asserted in prose but not preserved as repo evidence
- the document status is `Implemented + debug vis added`, but it does not explicitly separate code-complete from runtime-observed behavior

Today’s Windows Debug build succeeded, but it still emitted 30 warnings, and the learnings doc does not report any captured runtime screenshots, measured bake times, or before/after histogram examples.

Impact:
- the note reads partly like an implementation log and partly like a validation report
- readers may overread expected outcomes as observed outcomes

Recommended fix:
- split the document into `Implemented`, `Observed at runtime`, and `Open questions`
- keep the current visual claims under `Expected / how to interpret`
- if runtime validation exists, attach concrete observations or screenshots

## Non-findings

- The capped `SliderInt("Base rays/probe", ..., 4, 8)` matches the earlier plan critique.
- The settings panel now correctly reports per-level ray counts rather than only `cascades[0].raysPerProbe`.
- The tutorial panel does show 4b as a live green status entry.
- `BASE_RAY_COUNT = 4` is gone from `demo3d.h`.

## Verification

I rebuilt the Windows Debug target during this review:
- build succeeded
- 0 errors
- 30 warnings remain

That means 4b is compile-verified, but this learnings doc still should not imply stronger runtime proof than it actually records.

## Recommendation

Keep the implementation. Revise the learnings doc so it does not equate cascade-wide luminance distribution with sampling noise, and so it cleanly distinguishes:
- implemented code changes
- debug tooling additions
- actual observed runtime outcomes

