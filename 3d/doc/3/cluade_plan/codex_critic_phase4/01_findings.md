# Findings

## High

### 1. `4a` improves the probe statistics by injecting a new environment term, not by fixing the existing cascade transport

Refs:
- `doc/cluade_plan/phase4_plan.md:45`
- `doc/cluade_plan/phase4_plan.md:100`
- `doc/cluade_plan/phase4_plan.md:330`
- `res/shaders/radiance_3d.comp:57`
- `res/shaders/radiance_3d.comp:115`
- `src/analytic_sdf.cpp:105`

The plan treats `out of SDF volume` as equivalent to `should receive sky ambient`. In the current shader, leaving the volume only means `sampleSDF()` went out of bounds and returned `INF`; it does not prove that the ray escaped through a real opening in the scene. For the current Cornell-box setup, the front is open, but the finite simulation box is still not a sky visibility test.

Impact:
- `C3 non-zero% -> ~100%` becomes a metric win by construction
- darker far-field probes stop meaning `missing transport` and start meaning `ambient policy`
- this weakens the same image-path honesty standard established in the earlier critic

### 2. The proposed `baseRaysPerProbe` UI path does not actually update the live cascades as written

Refs:
- `doc/cluade_plan/phase4_plan.md:148`
- `doc/cluade_plan/phase4_plan.md:305`
- `doc/cluade_plan/phase4_plan.md:316`
- `src/demo3d.cpp:813`
- `src/demo3d.cpp:832`
- `src/demo3d.cpp:1213`
- `src/demo3d.cpp:1227`

`uRaysPerProbe` is sourced from `c.raysPerProbe`, and that field is populated in `initCascades()`. The plan adds a slider and a dirty flag, but it never updates `cascades[i].raysPerProbe` or rebuilds the cascade objects after the slider changes. As written, the UI would claim `C0=8 C1=16 C2=32 C3=64` while the shader still receives the old values.

Impact:
- the Phase 4 UI can drift away from the actual shader configuration
- performance and quality measurements become untrustworthy
- the implementation work is understated; this is not just a render-time sentinel change

## Medium

### 3. `4d` is already present in the shared texture creation path, so it should be documented as verified rather than planned as a fix

Refs:
- `doc/cluade_plan/phase4_plan.md:248`
- `doc/cluade_plan/phase4_plan.md:252`
- `src/demo3d.cpp:63`
- `src/gl_helpers.cpp:45`
- `src/gl_helpers.cpp:75`
- `include/gl_helpers.h:87`

`RadianceCascade3D::initialize()` creates probe textures through `gl::createTexture3D()`, and that helper already calls `setTexture3DParameters()` with `GL_LINEAR` filtering and `GL_CLAMP_TO_EDGE` defaults.

Impact:
- `4d` is not a missing correctness fix in the current branch
- leaving it in the phase plan as implementation work inflates the remaining scope
- the acceptance criteria should treat this as a verification note, not a user-visible milestone

### 4. The `4c` benefit is likely real but oversold while upper-cascade sampling remains isotropic and same-position only

Refs:
- `doc/cluade_plan/phase4_plan.md:172`
- `doc/cluade_plan/phase4_plan.md:237`
- `res/shaders/radiance_3d.comp:149`
- `res/shaders/radiance_3d.comp:152`

The current merge path samples the upper cascade at the same probe position and uses one isotropic RGB value for all missed directions. Distance-based blending can soften the binary handoff, but it cannot address the larger representational mismatch in the current merge model. The claim that the transition becomes "visually imperceptible" is stronger than the available evidence.

Impact:
- `4c` should be measured as a targeted smoothness tweak, not presented as a near-complete boundary fix
- if the visual result barely changes, that does not mean the implementation failed
- Phase 5 remains the real fix for directionally wrong upper-cascade reuse

