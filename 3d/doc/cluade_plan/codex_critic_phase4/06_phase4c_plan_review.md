# Phase 4c Plan Review

Reviewed file: `doc/cluade_plan/phase4c_plan.md`  
Review date: 2026-04-24T14:09:26+08:00

## Summary

This plan is directionally aligned with the earlier critic position:
- it no longer oversells 4c as a full fix
- it explicitly allows a no-op outcome
- it keeps 4c scoped to the binary boundary handoff

The remaining issues are about edge-case behavior and plan precision.

## Findings

### Medium: blending C3 surface hits toward black is a real behavior change, not just a neutral boundary softening

Refs:
- `doc/cluade_plan/phase4c_plan.md:109`
- `doc/cluade_plan/phase4c_plan.md:173`
- `res/shaders/radiance_3d.comp:173`

For cascades with no upper level, the proposed code sets:

```glsl
vec3 upperSample = (uHasUpperCascade != 0)
                   ? texture(uUpperCascade, uvwProbe).rgb
                   : vec3(0.0);
```

and then blends surface hits toward that value near `tMax`. For C3, that means valid local surface hits near the far boundary are blended toward black. That is not just "smoothing a handoff" because there is no handoff target at the top cascade.

Impact:
- C3 can become artificially darker near its far interval boundary
- the visual result may look smoother while actually discarding valid local energy
- this makes the top cascade behavior qualitatively different from the lower cascades

Recommended fix:
- skip distance blending when `uHasUpperCascade == 0`
- or explicitly justify why the top cascade should fade to black rather than stay local

### Medium: the performance note understates the extra texture sampling cost as written

Refs:
- `doc/cluade_plan/phase4c_plan.md:84`
- `doc/cluade_plan/phase4c_plan.md:109`
- `doc/cluade_plan/phase4c_plan.md:195`

The plan says `upperSample` is hoisted and "sampled once per ray in the surface-hit branch". But the proposed code hoists it above the branch, so it is evaluated for every ray, including:
- sky-exit rays
- top-cascade rays with no upper cascade
- miss rays that would already have sampled upper in the old path

Impact:
- the added cost is potentially higher than the text suggests
- the plan’s explanation of the cost model is inaccurate
- this is still probably acceptable for a static bake, but it should be described honestly

Recommended fix:
- either keep `upperSample` lazy and compute it only in the hit/miss paths that use it
- or revise the performance note to say the extra sample is now paid per ray, not just per surface-hit ray

### Low: one validation item references a metric name that does not exist in the current UI

Refs:
- `doc/cluade_plan/phase4c_plan.md:172`
- `src/demo3d.cpp:1968`

The validation table says:

```text
dist_var heuristic
```

The current cascade UI displays `var=...`, backed by `probeVariance[ci]`. There is no `dist_var` field or label in the current implementation.

Impact:
- minor terminology drift
- easy to confuse a future implementation or reviewer about which metric is being compared

Recommended fix:
- rename this row to `var heuristic` or `probeVariance heuristic`

## Recommendation

Keep the 4c plan, but update it in three places before implementation:

1. Decide whether C3 should blend at all when there is no upper cascade.
2. Make the performance note match the proposed code path.
3. Rename `dist_var` to the actual metric currently shown in the UI.

With those corrections, the plan is in good shape and appropriately modest about likely visual payoff.

