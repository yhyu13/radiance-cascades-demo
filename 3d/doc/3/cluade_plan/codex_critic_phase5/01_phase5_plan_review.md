# Phase 5 Plan Review

## Verdict

I agree with the core direction of Phase 5: the current codebase will not get ShaderToy-like merge quality until it replaces isotropic upper-cascade reuse with per-direction storage and per-direction merge.

But the current `phase5_plan.md` should be revised before implementation. As written, it has a few high-risk design gaps that could either regress quality or make the new atlas path ineffective.

## Findings

### 1. High: the plan does not explain how the final image shader still gets indirect lighting after the atlas conversion

Current final rendering still samples a per-probe 3D radiance grid:
- `raymarch.frag` reads `texture(uRadiance, uvw).rgb`
- `raymarchPass()` binds `cascades[selC].probeGridTexture` to `uRadiance`

Evidence:
- `res/shaders/raymarch.frag:83,173,255,269,291`
- `src/demo3d.cpp:1078-1085`

The plan introduces a directional atlas for the compute pass, but only says to "keep old `probeGridTexture[ci]` for debug readback (averaged isotropic fallback, or retire in a later pass)."

Evidence:
- `doc/cluade_plan/phase5_plan.md:112-118`

That is not enough. The plan needs an explicit ownership model for:
- how the atlas is reduced back into the isotropic grid for the final image, or
- how the final image shader switches to using the atlas directly

Without that, the plan changes the bake path but leaves the display path underspecified.

### 2. High: the proposed atlas lookup uses filtered `texture()` sampling, but this codebase creates 3D textures with linear filtering by default

The plan's merge examples use normalized `texture(uUpperCascadeAtlas, vec3(atlasUV, atlasW))`.

Evidence:
- `doc/cluade_plan/phase5_plan.md:166-182`

Current 3D texture helper defaults:
- `GL_LINEAR` min filter
- `GL_LINEAR` mag filter

Evidence:
- `include/gl_helpers.h:92-100`
- `src/gl_helpers.cpp:75-85`

That is a real problem for a direction-bin atlas. Linear filtering will blend across adjacent direction bins and probe tiles, which defeats the whole point of per-direction storage unless the plan explicitly switches the atlas to `GL_NEAREST` or uses `texelFetch()`.

This should be fixed in the plan before implementation. Right now the plan assumes "atlas lookup at probe tile + direction bin" but specifies a sampling method that will smear bins together.

### 3. High: fixing all cascades to `D^2 = 16` rays is an unjustified quality regression for upper cascades

The plan replaces current per-cascade ray scaling with:
- `D = 4`
- fixed `16` directional bins for all cascades

Evidence:
- `doc/cluade_plan/phase5_plan.md:25`
- `doc/cluade_plan/phase5_plan.md:36`
- `doc/cluade_plan/phase5_plan.md:239`

That directly cuts current C3 sampling from `64` rays to `16`. It also contradicts the earlier local gap analysis, which correctly identified scaled rays as a major requirement for upper-cascade quality.

Evidence:
- `doc/cluade_plan/phase3.X_shadertoy_gap_analysis.md:29-33`
- `doc/cluade_plan/phase3.X_shadertoy_gap_analysis.md:169`

Directional correctness matters, but it does not make angular under-sampling free. The more defensible plan is:
- keep directional storage
- keep per-cascade scaling in directional resolution or samples-per-bin
- only collapse to fixed 16 if runtime evidence shows no visible regression

As written, this is the riskiest quality tradeoff in the document.

### 4. Medium: the "each bin covers 1/16 of the sphere solid angle" stop condition is false for octahedral bins

The plan says:
- "Verify direction coverage: D=4 gives 16 bins; each bin covers 1/16 of the sphere solid angle"

Evidence:
- `doc/cluade_plan/phase5_plan.md:79-82`

Octahedral parameterization is invertible, but equal-size bins in octahedral UV space are not equal-solid-angle bins on the sphere. So this validation condition is not technically correct.

This does not break the direction choice, but it is the wrong success criterion. The plan should validate:
- full-sphere directional coverage
- stable bin indexing
- acceptable visual anisotropy at D=4

not equal solid-angle coverage.

### 5. Medium: the optional Phase 5d section correctly says visibility weighting is a no-op for co-located probes, which means it should be removed from the critical path entirely

The plan itself states:
- all cascades share the same 32^3 grid
- `probeToWorld(probePos) == worldPos`
- the visibility check therefore trivially passes

Evidence:
- `doc/cluade_plan/phase5_plan.md:226-227`

That means 5d is not just optional; it is irrelevant for the current architecture. Keeping it in the plan risks distracting implementation effort away from the real issues:
- directional atlas storage
- correct atlas sampling
- final-image integration
- validation against the current Phase 4 baseline

### 6. Medium: the plan’s "no spatial interpolation needed" claim is reasonable, but it should be framed as a project-specific simplification, not parity with ShaderToy

I agree with the core claim that co-located 32^3 cascades remove the need for ShaderToy's 4-neighbor probe interpolation.

Evidence:
- `doc/cluade_plan/phase5_plan.md:26-28`
- `doc/cluade_plan/phase3.X_shadertoy_gap_analysis.md:30`

But the plan should state explicitly that this is a deliberate 3D adaptation, not a faithful port. Otherwise it reads closer to "we no longer need the spatial part of ShaderToy's merge" when the more accurate statement is "our architecture avoids that part by construction."

## Recommendation

I agree with Phase 5's goal and main move: directional upper-cascade merge is the right next step.

Before implementation, I would revise the plan in these ways:

1. Add an explicit final-image integration step.
   Decide whether the bake produces:
   - both atlas data and a reduced isotropic `probeGridTexture`, or
   - atlas-only data plus a new raymarch shading path

2. Specify discrete atlas sampling.
   Use either:
   - `texelFetch()` with integer atlas coordinates, or
   - a dedicated atlas texture configured with `GL_NEAREST`

3. Do not regress upper-cascade angular budget by default.
   Keep some form of per-cascade scaling:
   - larger `dirRes` for upper cascades, or
   - multiple samples per directional bin on upper cascades, or
   - prove with runtime A/B that fixed 16 is visually sufficient before locking it in

4. Remove the equal-solid-angle claim from validation.

5. Drop Phase 5d from the implementation plan for now.
   Keep it as an architecture note only.

## Bottom line

The plan is strategically correct but tactically incomplete. I agree with the destination, not with the current implementation recipe. If revised on the points above, it becomes a much stronger Phase 5 plan for this codebase.
