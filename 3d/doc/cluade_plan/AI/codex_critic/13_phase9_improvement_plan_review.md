# Phase 9 Improvement Plan Review

## Verdict

The plan asks the right broad question: which changes are real banding fixes versus code-quality cleanup. It is also correct that fixing the dormant 3D JFA path is not the main answer for the current analytic Cornell-box branch.

The main weakness is that it overstates how effective temporal accumulation would be by itself. Several later sections quietly admit that deterministic probe placement is the actual problem, which makes the ranking for Improvement B too strong as written.

## Findings

### 1. High: Improvement B overclaims that temporal accumulation alone directly fixes the spatial aliasing

The plan says temporal probe accumulation is the most impactful low-risk banding fix and that it directly addresses the probe spatial aliasing.

Evidence:

- `doc/cluade_plan/AI/phase9_improvement_plan.md:76-127`
- `doc/cluade_plan/AI/phase9_improvement_plan.md:132-156`

But the plan's own Improvement C explains the real limitation:

- without jitter, the probe positions are the same every bake
- the banding is deterministic
- the average converges to the same biased sample pattern

That means Improvement B alone is much better described as:

- temporal denoising / stabilization of bake noise

not:

- a direct fix for deterministic probe-grid spatial aliasing

If the remaining artifact is truly dominated by stable spatial undersampling, accumulation alone will not remove it in the strong way the plan claims.

### 2. High: the recommendation order underrates the pending E4 confirmation even though the whole plan depends on it

The plan correctly says E4 (`cascadeC0Res 32 -> 64`) should be run first. But it then gives Improvement B a blanket "High" priority and calls it the most impactful fix before E4 has actually confirmed the spatial-aliasing diagnosis.

Evidence:

- `doc/cluade_plan/AI/phase9_improvement_plan.md:26-33`
- `doc/cluade_plan/AI/phase9_improvement_plan.md:158-173`
- `doc/cluade_plan/AI/phase9_improvement_plan.md:196-206`

That sequence is internally inconsistent. If E4 is still the key falsification test, then B/C should remain contingent fixes rather than already-elevated priorities.

### 3. Medium: Improvement E is described as display-side trilinear weighting, but the current branch still lacks the supporting per-probe data model entirely

The DDGI-style visibility section proposes adding mean/variance depth moments and weighting the 8 trilinear probe corners in `sampleDirectionalGI()`.

Evidence:

- `doc/cluade_plan/AI/phase9_improvement_plan.md:175-193`
- `res/shaders/raymarch.frag:294-356`
- `src/demo3d.h:96-99`

This is directionally reasonable, but the document makes it sound closer to a local patch than it really is. In the current branch there is no existing per-probe depth-moment volume, no display-side access path for such data, and no current separation between "atlas used for directional radiance" and "auxiliary visibility moments used during interpolation."

So the priority being low is fine, but the implementation section understates how architectural this change would be.

### 4. Medium: Improvement A is right about current user-facing value, but it is slightly too categorical about "no banding benefit"

The JFA section says fixing `sdf_3d.comp` would not reduce banding in the current scene.

Evidence:

- `doc/cluade_plan/AI/phase9_improvement_plan.md:12-73`

For the current analytic Cornell-box scene, that is a fair practical conclusion. But because the bake path still uses texture SDF sampling and the branch is actively mixing analytic and non-analytic paths in its diagnostics, the stronger "no benefit" framing should be softened to:

- "no expected benefit for the current analytic test scene"

rather than sounding like a universal statement about the whole branch.

### 5. Low: the E4 section frames "bands halve in spacing => hypothesis confirmed" too strongly

The probe-density experiment is the right next test, but the interpretation is still a bit too absolute.

Evidence:

- `doc/cluade_plan/AI/phase9_improvement_plan.md:158-173`

If the bands narrow when `cascadeC0Res` increases, that is strong evidence for a spatial component. It still does not prove that probe density is the only meaningful cause; it just makes it a much stronger contributor than the alternatives currently tested.

## Where the plan is strong

- It correctly separates JFA cleanup from current-scene banding fixes.
- It correctly treats probe-density E4 as the first no-code discriminator.
- It correctly notes that jitter without accumulation would just add noise.
- It is appropriately skeptical that DDGI visibility weighting would remove the broad Cornell-box contouring by itself.

## Bottom line

This should be revised before being treated as the working Phase 9 plan.

I would fix it by:

1. downgrading Improvement B from "direct spatial-aliasing fix" to "noise/stability helper unless paired with jitter",
2. making B/C explicitly contingent on E4 instead of pre-ranking them as the answer,
3. describing Improvement E as a larger data-model change rather than a near-local interpolation tweak,
4. softening the JFA section to "no expected benefit for the current analytic scene",
5. softening the E4 interpretation from proof to strong evidence.
