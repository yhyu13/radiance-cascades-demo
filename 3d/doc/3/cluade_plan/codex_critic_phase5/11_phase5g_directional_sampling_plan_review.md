# Phase 5g Directional Sampling Plan Review

## Verdict

The plan is aiming at a real weakness in the current final renderer: `raymarch.frag` only samples the isotropic reduced probe texture, so all directional structure in the atlas is thrown away at display time.

But as written, the plan has two concrete implementation bugs and one larger modeling problem. The biggest issue is that its proposed final-render directional path would regress from today's spatially filtered `texture(uRadiance, uvw)` lookup to a nearest-probe `texelFetch` on a single atlas tile, which would likely replace one artifact with another.

## Findings

### 1. High: the proposed texture binding collides with the live albedo binding

The plan says to bind the directional atlas in `raymarchPass()` on texture unit `2`:

- `glActiveTexture(GL_TEXTURE2);`
- `uDirectionalAtlas = 2`

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:185-197`

But the live render path already binds:

- `uSDF` on unit `0`
- `uRadiance` on unit `1`
- `uAlbedo` on unit `2`

Evidence:

- `src/demo3d.cpp:1211-1229`

So the plan, as written, would stomp the current albedo binding unless it also moves `uAlbedo` to another slot and updates the shader wiring accordingly.

### 2. High: the proposed directional GI path throws away today's spatial interpolation and will likely become blockier than the isotropic baseline

The current final renderer samples indirect light with:

- `texture(uRadiance, uvw).rgb`

Evidence:

- `res/shaders/raymarch.frag:167-174`
- `res/shaders/raymarch.frag:289-292`

That is a filtered 3D texture lookup in world-space coordinates.

The plan replaces this with:

- `pc = floor(uvw * uAtlasVolumeSize)`
- a per-bin `texelFetch` from exactly one probe tile

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:139-155`

So even if the directional hemisphere weighting is useful, the proposed path discards the current spatial interpolation and snaps each shaded point to one probe. That is a serious regression risk. The final renderer would be comparing:

- isotropic + spatial filtering

against

- directional + nearest-probe snapping

If the project wants a fair 5g result, it should preserve spatial interpolation in the final lookup too, not only directional weighting.

### 3. High: the plan calls the cosine-weighted bin sum "architecturally correct", but it still ignores octahedral-bin solid-angle distortion

The document says the correct model is:

- cosine-weighted hemisphere integration over the directional atlas

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:38-56`

That is directionally better than isotropic averaging, but the proposed discrete implementation weights bins only by:

- `max(0, dot(bdir, normal))`

and then normalizes by `wsum`.

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:145-154`

For an octahedral parameterization, equal `dx,dy` bins are not equal-solid-angle patches on the sphere. So this is an approximation, not a fully correct irradiance integral. The plan should not oversell it as "the correct model" without acknowledging that discretization bias.

### 4. Medium: the plan depends on a "debug mode 7" that does not exist in the current branch

The header lists as prerequisite:

- "Phase 5h"
- "debug mode 7 (atlas quality confirmed)"

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:6-7`

But the live raymarch renderer has modes `0-6`, and the live radiance debug UI also exposes `0-6`.

Evidence:

- `res/shaders/raymarch.frag:252-315`
- `src/demo3d.cpp:2034-2040`
- `src/demo3d.cpp:2303-2304`

So this prerequisite is stale or refers to a mode that has not been implemented.

### 5. Medium: the Phase 5h + 5g target image is framed too much like direct-light penumbra

The expected-outcome table says:

- 5g only gives a soft shadow from the cascade
- 5h + 5g gives hard shadow core + soft penumbra

Evidence:

- `doc/cluade_plan/phase5g_directional_sampling_plan.md:221-229`

That framing is risky for the same reason as in the 5h plan. With the current point-light direct term, 5g is better understood as a directional diffuse-GI estimate, not as a physically correct direct-shadow penumbra solver.

## Where the plan is strong

- It correctly identifies that `reduction_3d.comp` destroys directional information for the current final renderer.
- It rejects the idea of pre-reducing by normal without a surface normal.
- It keeps the directional-GI toggle as a display-path-only change, which is the right scope for an A/B path.

## Bottom line

This plan should be revised before implementation.

I would fix it by:

1. correcting the texture-unit wiring,
2. preserving spatial interpolation in the final directional lookup instead of snapping to one probe,
3. downgrading the "architecturally correct" claim to "better directional irradiance approximation", and
4. removing or replacing the nonexistent debug-mode-7 prerequisite.
