# Recommendation

## Keep

Keep the Phase 4 focus on small, isolated quality improvements before Phase 5.

That still fits the earlier critic position:
- finish the current image path honestly
- avoid claiming correctness wins that are really policy changes
- prefer changes that can be verified against the existing debug views and probe stats

## Update

### 1. Reframe `4a`

Do not describe sky ambient as an energy-correctness fix.

Better wording:
- `environment fallback for out-of-volume rays`
- default OFF for Cornell-box validation
- acceptable as a debug toggle or stylistic fill term

If it ships in Phase 4, the acceptance criteria should say:
- brighter GI when enabled
- expected change in C3 occupancy
- explicit tradeoff: less physically honest for box-style validation scenes

### 2. Keep `4b`, but include the real wiring work

Recommended implementation shape:
- add `baseRaysPerProbe` to `Demo3D`
- when it changes, either rerun `initCascades()` safely or directly rewrite each `cascades[i].raysPerProbe`
- then invalidate `cascadeReady`

That keeps the UI, shader uniforms, and performance measurements aligned.

### 3. Keep `4c`, but narrow the claim

Recommended wording:
- smooths the local-to-upper handoff
- may reduce boundary banding
- does not solve directional mismatch in the current isotropic merge

Use a visual A/B check for validation instead of promising an imperceptible transition.

### 4. Demote `4d` to a verification note

Recommended wording:
- confirm probe textures already use linear filtering and clamp-to-edge through `gl::createTexture3D()`
- remove it from the main milestone list unless the verification finds a real gap

## Suggested Phase 4 order

1. `4b` with correct runtime wiring
2. `4c` with modest acceptance criteria
3. optional `4a` as an explicit environment-fill toggle, default OFF
4. `4d` documented as already verified unless evidence changes

## Final position

Phase 4 should proceed, but not exactly as written.

The plan is strongest where it improves sampling quality without changing scene semantics. It is weakest where it treats a new ambient policy as if it were a transport correction, or where it counts already-present GL state setup as unfinished work.

