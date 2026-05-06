# Phase 4 Codebase vs Original Goal Review

## Question

Does the current Phase 4 codebase still track the original goal of building a 3D radiance-cascades result similar to the local ShaderToy reference in `shader_toy/`?

## Short answer

Yes, but only in the limited sense of "working multi-cascade 3D GI prototype." No, if "similar to ShaderToy" means the same merge model and directional behavior. The current branch has reached a stable isotropic prototype, and `PLAN.md` is only aligned with the original target because it now explicitly pushes the main remaining ShaderToy gap into Phase 5.

## Findings

### 1. The core ShaderToy gap is still unresolved: upper-cascade reuse is isotropic, not directional

Current compute shader behavior:
- samples the upper cascade once at the current probe position: `texture(uUpperCascade, uvwProbe).rgb`
- reuses that same isotropic value for all miss rays and all near-boundary blends

Evidence:
- `res/shaders/radiance_3d.comp:159-184`

ShaderToy reference behavior:
- does a per-direction weighted bilinear merge from neighboring upper probes
- uses `WeightedSample(...)` and four directional upper-probe taps before blending

Evidence:
- `shader_toy/Image.glsl:21-40`
- `shader_toy/Image.glsl:195-219`

Assessment:
- this is still the single biggest reason the current image is not yet "similar to ShaderToy" in the strong sense
- `PLAN.md` is correct to make this the next milestone, but Phase 4 completion should not be read as parity with the reference

### 2. The ray model is still materially different from the reference

Current probe sampling:
- full-sphere Fibonacci directions
- uniform averaging over all rays

Evidence:
- `res/shaders/radiance_3d.comp:69-75`
- `res/shaders/radiance_3d.comp:188-192`

Known gap document:
- ShaderToy uses surface-attached hemispheres
- current implementation uses a volumetric 3D grid and full-sphere rays
- the gap note already classifies missing BRDF / cosine / solid-angle handling as a remaining mismatch

Evidence:
- `doc/cluade_plan/phase3.X_shadertoy_gap_analysis.md:16-18`
- `doc/cluade_plan/phase3.X_shadertoy_gap_analysis.md:29-33`

Assessment:
- this does not make the branch "wrong"; it means the project is pursuing a 3D adaptation, not a close behavioral clone
- if the original goal is visual/algorithmic similarity, Phase 5 still carries most of that burden

### 3. `PLAN.md` is mostly honest now, but its top-level goal text is softer than the original target

Current goal text:
- "Visible Cornell-box raymarched image with a working multi-cascade radiance hierarchy"

Evidence:
- `doc/cluade_plan/PLAN.md:5`

Current Phase 5 wording:
- explicitly says the next step is replacing isotropic `upperSample` with per-direction lookup
- explicitly states remaining banding is directional mismatch

Evidence:
- `doc/cluade_plan/PLAN.md:123-133`

Assessment:
- this is a defensible status goal for the current branch
- but it is narrower than "similar to ShaderToy"
- the plan now sticks to the original goal only because it clearly admits the current model is still isotropic and incomplete

### 4. Some UI wording still overstates what is already achieved

The cascade panel tooltip says:
- "This is the correct RC algorithm and produces global illumination."

Evidence:
- `src/demo3d.cpp:1933-1940`

Assessment:
- that statement is too strong given the known Phase 5 directional-merge gap
- more accurate wording would be: "This is the current multi-cascade merge path" or "This is the current isotropic RC approximation"

### 5. The debug/stat path is improved, but still not fully trustworthy at higher packed values

Current plan and UI now correctly acknowledge the limit:
- packed surf/sky counts live in `RGBA16F`
- decode math is better, but storage precision still caps reliable mixed-hit stats

Evidence:
- `doc/cluade_plan/PLAN.md:125`
- `res/shaders/radiance_3d.comp:189-191`
- `src/demo3d.cpp:1977-1981`

Assessment:
- this is mostly a validation-tooling issue, not the main rendering blocker
- still, it means some Phase 4 debug evidence should be treated as approximate, not decisive

## Verdict

The codebase has not drifted away from the original goal, but it also has not reached the ShaderToy-like target yet. The current state is:

- Phase 4: a working, compile-clean, multi-cascade 3D GI prototype with better diagnostics
- Phase 5: still the point where the project either closes or fails to close the main ShaderToy similarity gap

So the right conclusion is:
- **`PLAN.md` is now directionally aligned with the original goal**
- **the current implementation is not yet behaviorally close enough to claim ShaderToy-like merge quality**

## Verification

- Windows Debug MSBuild succeeded: `0` warnings, `0` errors
- No runtime image A/B validation was run in this review pass
