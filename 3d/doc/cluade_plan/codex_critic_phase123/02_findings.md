# Findings

## High

### 1. Cornell-box material colors are not actually used in either the final shader or the cascade shader

Refs:
- `res/shaders/sdf_analytic.comp:34`
- `res/shaders/raymarch.frag:237`
- `res/shaders/radiance_3d.comp:87`

The analytic scene defines primitive colors, and the Cornell box uses red and green walls in `src/analytic_sdf.cpp:97`, but the SDF pass explicitly says color is not used, and both shading paths use only `diff * uLightColor + ambient`.

Impact:
- the rendered result is not a true Cornell-box material result
- red/green wall color bleeding is not physically represented by the current GI path
- any visible Phase 2 change is more likely a generic brightness bias than convincing colored indirect transport

### 2. There is no shadow or visibility test from hit point to light in either shading path

Refs:
- `res/shaders/raymarch.frag:235`
- `res/shaders/radiance_3d.comp:85`

Both shaders compute Lambertian `max(dot(n,l), 0)` but do not cast a shadow ray toward the light.

Impact:
- direct lighting is not occlusion-aware
- interior boxes and walls can receive light through blockers
- the cascade stores unshadowed direct illumination, which weakens the value of the visual validation

### 3. Phase 2 is not really a radiance cascade yet, only a single static probe volume

Refs:
- `src/demo3d.cpp:1071`
- `res/shaders/radiance_3d.comp:101`
- `src/demo3d.cpp:714`

The code now implements one 32^3 probe field. There is no hierarchy, no merge logic, no coarse fallback, no temporal accumulation, and no cascade-to-cascade transport.

Impact:
- the implementation is useful as a prototype
- the naming in some comments and docs overstates what is currently working
- this should be described as `single probe-grid GI prototype`, not a completed RC solution

## Medium

### 4. The UI still reports SDF generation and full raymarching as placeholders

Refs:
- `src/demo3d.cpp:1718`
- `src/demo3d.cpp:1719`

The debug UI contradicts the plan docs by still listing SDF generation and full raymarching as placeholders.

Impact:
- makes the project status harder to trust
- increases the chance of future confusion when debugging or handing off work

### 5. `injectDirectLighting()` was disabled by early return but the old implementation remains as unreachable code

Refs:
- `src/demo3d.cpp:736`
- MSBuild warning `C4702` during rebuild

This is not a functional bug by itself, but it leaves dead code in an important lighting path.

Impact:
- status of the direct-light injection path is ambiguous
- dead code increases maintenance friction

### 6. Resource cleanup is still only partially resolved

Refs:
- `src/demo3d.cpp:216`
- `src/demo3d.cpp:1058`

There are still TODO markers around destructor/buffer cleanup even though cascade texture cleanup itself now exists.

Impact:
- okay for short-lived runs
- still not a clean “finished phase” implementation standard

## Low

### 7. The probe GI quality is intentionally minimal and will likely be noisy or bland

Refs:
- `src/demo3d.cpp:1077`
- `res/shaders/radiance_3d.comp:111`

A 32^3 grid with 4 rays per probe is a good prototype setting, but it is extremely low for convincing 3D GI.

Impact:
- acceptable for a first visible result
- not enough to use visual subtlety as proof of correctness without careful debug views

### 8. The plan wording is stronger than the actual evidence

Refs:
- `doc/cluade_plan/PLAN.md`
- `doc/cluade_plan/phase2_changes.md`

The docs correctly keep the final visual toggle as pending, but some surrounding language reads closer to “done” than the implementation evidence supports.

Impact:
- mostly a documentation precision issue
- easy to fix
