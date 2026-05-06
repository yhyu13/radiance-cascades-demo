# Phase 5h Shadow Ray Plan Review

## Verdict

The plan is directionally useful because it targets a real bug in the final renderer: `raymarch.frag` still shades the direct term as fully unshadowed Lambertian light. Adding a shadow ray there would materially improve the final image and give the branch a proper direct-occlusion path.

But the document overstates what that shadow ray proves about Phase 5g. A hard direct-light shadow check is not a ground-truth validator for directional-probe irradiance, and the repeated "hard core + soft penumbra from cascade" framing is conceptually too strong for a point-light + diffuse-GI setup.

## Findings

### 1. High: the plan is right about the missing direct-shadow bug, but wrong to frame it as ground truth for Phase 5g

The live final renderer still does:

- `diff = max(dot(normal, lightDir), 0.0)`
- no occlusion test before applying direct light

Evidence:

- `res/shaders/raymarch.frag:274-292`

So a Phase 5h shadow ray would fix a real omission.

But the plan then says this becomes the "ground-truth reference" that Phase 5g must converge toward, and that the combined result should be a "hard shadow core + soft penumbra from indirect propagation".

Evidence:

- `doc/cluade_plan/phase5h_shadow_ray_plan.md:26-28`
- `doc/cluade_plan/phase5h_shadow_ray_plan.md:154-162`

That is too strong. Phase 5h measures direct-light occlusion from the actual shaded point to the light. Phase 5g, as proposed, is an irradiance estimate from probe-stored directional data. Those are related signals, but they are not the same quantity, and one is not a clean ground truth for the other.

In particular, with the current single direct light setup, a physically correct soft penumbra would come from an area-light model, not from diffuse GI alone. So the plan should sell 5h as "fix the missing direct shadow term" rather than "ground truth for 5g penumbra".

### 2. Medium: the shadow bias strategy is copied from the bake shader, but the final renderer has a better local signal available and should use it

The plan proposes:

- start at `hitPos`
- march toward the light with `t = 0.05`
- reuse the bake shader's `inShadow()` constants

Evidence:

- `doc/cluade_plan/phase5h_shadow_ray_plan.md:45-57`

That will probably work, but `raymarch.frag` already has the surface normal at the hit point:

- `normal = estimateNormal(pos)`

Evidence:

- `res/shaders/raymarch.frag:235-238`

So this is one place where the final renderer can do better than the bake path by offsetting the shadow-ray origin along the normal before marching. Reusing the bake constants is reasonable as a first pass, but the document should not present it as automatically robust.

### 3. Medium: the performance section is too confident for a per-pixel 32-step secondary march

The plan says the cost should be negligible and "same order as the raymarching loop itself".

Evidence:

- `doc/cluade_plan/phase5h_shadow_ray_plan.md:145-150`

This is plausible as a rough estimate, but it is not yet validated. The direct path is adding up to 32 extra 3D SDF reads per shaded pixel on top of:

- the primary march,
- the normal estimate,
- and any cascade sampling already being done.

So this section should be softened to "expected acceptable, verify at runtime" rather than "negligible".

## Where the plan is strong

- It targets a real display-path defect in `raymarch.frag`.
- The zero-regression framing for `uUseShadowRay=0` is sound.
- Applying the same shadow factor to mode 4 direct-only debug is the right validation path.

## Bottom line

This is a worthwhile plan, but it should be described more narrowly:

1. Phase 5h fixes missing direct-light shadowing in the final renderer.
2. It does not by itself validate directional-probe irradiance correctness.
3. The "hard core + soft penumbra" story should be toned down unless the project also changes the light model or defines that softness as a deliberate approximation rather than physical direct-light penumbra.
