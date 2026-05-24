# The Downstream Symmetrizer Pattern — an architectural finding from MBRC v2.0 h-stage

**Status:** Standalone architectural doc, synthesizing 4 sequential A/Bs from the
v2.0 h-stage. Captures a meta-pattern that the per-stage impl docs each touch but
none individually frame: **every downstream cascade-consumption feature in this
engine is acting as a partial cross-camera symmetrizer**, not a contributor to
the underlying asymmetry. This is a property of the merge architecture, not a
bug, and it has design implications beyond the cam0/cam2 spread question that
motivated the original investigation.
**Date:** 2026-05-24
**Source docs:**
[v20_b_smoothstep_toggle_impl.md](v20_b_smoothstep_toggle_impl.md),
[v20_c_fract_viz_impl.md](v20_c_fract_viz_impl.md),
[v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md),
[v20_cprime2_downstream_knobs_impl.md](v20_cprime2_downstream_knobs_impl.md),
[v20_cprime3_st0_mitigation_impl.md](v20_cprime3_st0_mitigation_impl.md)

## 1. The pattern in one table

All measurements on `cornell-orig-alcove`, MB-OFF, b=2, M0 merge mode,
cam0 + cam2 from `tools/v20_pre_measurement/cameras.json`. Each row is a
feature toggle that flips a single downstream cascade-consumption knob from
its ON state to its OFF state. "Δcam0" / "Δcam2" are absolute changes in
cascade/PT-GI luminance ratio at that camera; "spread" is cam2/cam0.

| feature toggle (ON→OFF)         | Δcam0 ratio | Δcam2 ratio | Δcam0/Δcam2 | spread Δ |
|---------------------------------|------------:|------------:|------------:|---------:|
| useSpatialTrilinear (8-nbr → nearest-parent) | +0.092 (+21%) | +0.025 (+9%)  | 3.75× | −0.067 |
| useDirectionalMerge (per-bin → isotropic avg) | +0.128 (+29%) | +0.025 (+9%)  | 5.10× | −0.103 |
| useDirBilinear (4-bin → nearest-bin)          | +0.017 (+4%)  | +0.002 (+1%)  | 7.50× | −0.021 |
| blendMode (smoothstep → step)                 | +0.002 (+0%)  | −0.001 (−0%)  | ~tied | −0.004 |

**Three signals, identical shape:**
1. cam0 ratio rises 2-7× more in absolute terms than cam2 ratio under any
   downstream-feature disable.
2. The spread cam2/cam0 always *widens* (negative Δspread) when a downstream
   feature is disabled — the spread is at its NARROWEST with all downstream
   features ON.
3. Both cameras move in the **same direction** (both brighten, both dim, or
   both stay put) under any single-feature toggle.

The blendMode row sits at the noise floor — that toggle exposes no leverage at
all on either camera and was the first to be ruled out (h.b). The other three
rows form the symmetrizer family: each feature touches a different part of the
consumption pipeline, but each has the same per-camera effect shape.

## 2. What "symmetrizer" means here

Define a feature as a **symmetrizer** if, when disabled, it reveals a larger
underlying spread between two evaluation points (here: two cameras viewing the
same scene) than the spread observed with the feature enabled. Equivalently:
the feature was *closing* a real gap, but doing so by suppressing the *higher*
side toward the *lower* side rather than by raising the lower side toward the
higher one. The feature looks neutral or beneficial in steady-state operation
because the underlying asymmetry is invisible, but the moment you turn it off
the latent spread re-appears.

Contrast with a **contributor**: a feature that, when disabled, *closes* a gap
that was present with the feature enabled. A contributor is responsible for
the asymmetry; a symmetrizer is masking one.

The distinction matters because:
- A contributor can be **fixed**: identify it, replace its math, the spread
  narrows.
- A symmetrizer can only be **swapped**: leaving it on hides the asymmetry but
  costs absolute quality (see §4); removing it surfaces the asymmetry but
  raises both endpoints. Neither operation eliminates the underlying source.

The pre-v2.0 hypothesis tree implicitly framed every downstream knob as a
candidate contributor: "is feature X the source of the spread?" The data
answers "no" to each one individually but *also* answers a second question
the framing didn't ask: "is feature X masking part of the spread?" — and the
answer to that one is yes for three of the four tested knobs.

## 3. Why every downstream feature ends up a symmetrizer

This isn't coincidence. The cascade consumption pipeline is *designed* to
smooth radiance across spatial and directional neighborhoods, and smoothing
is symmetrization by construction. Each downstream feature averages over a
different neighborhood:

| feature | what it averages over |
|---|---|
| useSpatialTrilinear | 8 spatial neighbor probes per fetch |
| useDirectionalMerge | per-direction bin lookup vs single isotropic value |
| useDirBilinear      | 4 directional bins within a probe |
| smoothstep blend    | continuous transition between cascade levels |

When two evaluation points (cam0, cam2) sample from systematically *different*
neighborhoods (because their surface geometry hits different probes / probe
regions / probe-cell positions), averaging within each point's neighborhood
pulls each point toward its local mean. If point A's local mean happens to be
higher than point A's true single-sample value, and point B's local mean
happens to be lower than point B's true single-sample value, the spread
between A and B narrows — even when no single neighborhood overlaps the
other.

This is the **opposite of the textbook intuition for smoothing**. In a
uniform field, smoothing reduces noise without bias. In a field with
spatially-correlated structure — which is exactly what a probe-based GI
scheme produces, because the probe-content distribution is itself spatially
structured — smoothing introduces a bias proportional to the local gradient
of the underlying structure.

For the alcove scene specifically, cam2 samples surfaces near probes whose
direction bins are systematically dim (most directions encounter SDF
occluders within the cascade's `tMin..tMax` window). Smoothing within cam2's
neighborhood pulls those samples *up* toward neighbors that have more
open-direction bins. Smoothing within cam0's neighborhood pulls those samples
*down* toward the same kind of neighbor probes — but cam0 was sampling from
high-bin-population probes to begin with, so the pull is downward. The
spread narrows.

## 4. What symmetrizers cost (and when)

The symmetrizer pattern is not free. (h.c)' and (h.c)''' measured the
absolute-quality cost of one specific symmetrizer (`useSpatialTrilinear`)
on three scenes:

| scene | baseline ratio (ST=1) | symmetrizer-off ratio (ST=0) | Δratio | ΔRMSE vs PT-GI |
|---|---:|---:|---:|---:|
| alcove cam0 | 0.435 | 0.527 | +0.092 (+21%) | — |
| alcove cam2 | 0.283 | 0.307 | +0.025 (+9%)  | — |
| cornell_default (auto-fit) | 0.301 | 0.342 | +0.041 (+14%) | −0.005 (−4%) |
| cornell_orig (auto-fit)    | 0.275 | 0.315 | +0.039 (+14%) | −0.002 (−3%) |

On every measured cam-scene pair, turning the symmetrizer OFF moves
cascade-vs-PT ratio toward 1.0 (less under-integration). On the two scenes
where RMSE-vs-PT was also computed, that metric also improves. The
symmetrizer is paying for the cross-camera spread by giving up absolute
match to ground truth.

This is the rule the engine-default flip (this commit) takes seriously: when
a symmetrizer's only purpose is to mask an asymmetry that lives upstream,
and that masking costs absolute quality, the symmetrizer should default OFF
and the asymmetry should be addressed at its source.

The corollary: a symmetrizer is *worth keeping* if (a) the asymmetry it
masks is acceptable as a residual, (b) the masking is cheap (no quality
cost), or (c) addressing the upstream source is infeasible. Engine
defaults should reflect the trade for the workload. This engine targets
single-frame GI integration quality on small scenes, so absolute match
wins; an engine targeting cross-view temporal stability on a moving camera
might value the symmetrizer's masking property more highly.

## 5. Generalization — the rule for any probe-based GI scheme

The pattern is not unique to this engine. Any system that (i) gathers
radiance at discrete spatial probes, (ii) interpolates probe values when
evaluating off-probe positions, AND (iii) averages over neighborhoods at
any consumption-time stage, is structurally susceptible to the same
symmetrizer-contributor confusion. Specifically: every consumption-time
smoothing layer is at risk of acting as a symmetrizer whenever the
underlying probe-content distribution has spatial structure that
correlates with viewer geometry.

The diagnostic posture:

1. **Don't validate spatial smoothing reduces RMSE vs ground truth on a
   single scene.** Validate it across scenes with varying probe-content
   variance. A smoothing layer that reduces RMSE on a uniform-content
   scene may *increase* RMSE on a high-variance scene by symmetrizing
   structure that should remain visible.

2. **When an A/B flips a "smoothing" feature, measure BOTH endpoints
   of the asymmetry, not just the headline.** If both arms move in the
   same direction (both brighter or both dimmer) but unequally, the
   feature is acting as a symmetrizer, not a contributor. The
   underlying asymmetry source is BEFORE the smoothing step.

3. **Pre-committed verdict bands on single-endpoint metrics will
   silently mislabel symmetrizers as innocent.** Always include a
   per-endpoint absolute-delta sub-test alongside the headline (here:
   the spread-Δ band that the analyzer pre-committed got the verdict
   numerically correct, but the per-cam Δ table was what surfaced the
   symmetrizer interpretation).

4. **A pipeline stage is locked-in innocent only when every feature in
   it has been individually toggled.** The (h.c)'' chain demonstrates
   that ruling out a stage requires testing each downstream component;
   a single feature toggle never proves the stage as a whole.

## 6. What this leaves open

This doc closes the architectural framing of the v2.0 h-stage but does not
fix the underlying asymmetry. The bake-side per-direction-bin atlas content
at cam2-visible probes is the remaining live suspect, established by
elimination across 4 downstream-stage A/Bs. The next direct measurement is
P2 — per-pixel dominant-direction-bin viz shader — which converts the
inferential bake-side framing to direct measurement by surfacing WHICH atlas
direction bins each camera samples dominantly. Once P2 localizes the bin
asymmetry, targeted bake-side fixes (bin-coverage hardening, direction-aware
probe placement, per-direction-bin firefly clamps at bake) become
well-scoped.

The cam0/cam2 spread on this scene will persist until the bake-side source
is addressed. Engine defaults flipped this session prioritize absolute
quality over spread masking; users who need spread masking on alcove-like
scenes can opt-in to `--use-spatial-trilinear=1` or `--use-directional-merge=1`
as documented in the per-feature impl docs.

## 7. A note on naming

The (h.c)'' analyzer's verdict label `DOWNSTREAM_KNOB_INNOCENT` is
mechanically correct (the knob does not close the cam0/cam2 spread) but
semantically incomplete (the knob is *affecting* both cameras, just
symmetrically toward symmetry). A future analyzer should sub-classify
`INNOCENT_QUIET` (both cameras move <5% in absolute terms — true neutrality)
versus `INNOCENT_SYMMETRIZER` (one camera moves ≥2× the other in absolute
terms — the symmetrizer pattern). Three of the four h-stage features land
in the latter sub-class; only blendMode lands in the former. This
distinction would have surfaced the symmetrizer interpretation directly
from the verdict line rather than requiring a separate read of the per-cam
delta table.

## 8. Self-critique

**Strengths:**
- The symmetrizer-vs-contributor distinction is a transferable concept,
  not a one-scene observation. The §5 generalization gives the rule a
  shape that applies to any probe-based GI scheme — the project's
  internal vocabulary now includes a useful frame that didn't exist
  before this session.
- The §4 trade table makes the engine-default flip decision auditable.
  Future readers can see the specific magnitudes the decision was based
  on rather than re-deriving from per-doc tables.
- §7 names the analyzer-label gap directly. Future v2.0 work will benefit
  from analyzers that distinguish quiet-innocent from
  symmetrizer-innocent automatically.

**Weaknesses:**
- The pattern is established on ONE scene's underlying asymmetry. Without
  testing on a scene that's *symmetric* at the probe level (e.g.,
  featureless sphere room), the strength of the generalization to "every
  downstream feature in any probe-based GI" is inferential. A counter-
  example test — find a downstream feature on a different scene that
  acts as a contributor not symmetrizer — would meaningfully harden §5.
- The cross-scene cost table (§4) only spans 4 cam-scene pairs. A larger
  cross-scene matrix would let us state the absolute-quality cost in
  ranges with confidence intervals rather than point estimates.
- The doc takes for granted that "spread narrowing = symmetrization" is
  the right frame. A more skeptical read: maybe the downstream features
  ARE contributors to a *different* asymmetry source than the bake-side
  one we've localized — perhaps they were correctly addressing a separate
  asymmetry that we've now re-introduced by flipping the defaults. The
  P2 per-pixel dominant-bin viz will help discriminate: if cam0 and cam2
  sample radically different bin populations, the bake-side framing
  holds; if their bin populations are similar, the symmetrizer-only
  framing is incomplete and the engine defaults may need re-evaluation.
- Does not address the *interaction* between symmetrizers. Each feature
  was tested with the others at their default state; whether disabling
  multiple symmetrizers simultaneously produces additive, sub-additive,
  or super-additive spread widening is unmeasured. The (h.c)' + (h.c)''
  data suggests roughly additive but a 2×2×2 factorial sweep would be
  the clean answer (12 cells, ~3 min — cheap follow-up).

## 9. Artifacts cross-reference

Source measurement docs (chronological):
- (h.b) blend-zone 3-mode A/B: [v20_b_smoothstep_toggle_impl.md](v20_b_smoothstep_toggle_impl.md)
- (h.c) probe-cell fract viz: [v20_c_fract_viz_impl.md](v20_c_fract_viz_impl.md)
- (h.c)' spatial-trilinear A/B: [v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md)
- (h.c)'' downstream-knobs final rule-out: [v20_cprime2_downstream_knobs_impl.md](v20_cprime2_downstream_knobs_impl.md)
- (h.c)''' ST=0 cross-scene mitigation: [v20_cprime3_st0_mitigation_impl.md](v20_cprime3_st0_mitigation_impl.md)

Engine state at time of writing:
- `useSpatialTrilinear` default flipped 1→0 (this commit), revert via
  `--use-spatial-trilinear=1`
- `useDirectionalMerge`, `useDirBilinear` already default OFF (engine_default_validation_impl.md)
- `useMultiBounce` default ON, g=1.0 (engine_default_validation_impl.md)
- blendMode default 0 (smoothstep) — measured noise-floor, no rationale to change

Cerebrum DNRs relevant:
- 2026-05-24 same-sign-unequal-arms-indicates-symmetrizer rule (the rule §5.2 distills)
- 2026-05-24 relative-magnitude-thresholds-on-ratio-metrics (the §4 RMSE table interpretation issue)
- 2026-05-24 downstream-path-locked-in-innocent (the cumulative 4-A/B elimination)
- 2026-05-23 colormap-normalized-pitfall (precursor — same family of pre-committed-verdict failure modes)
