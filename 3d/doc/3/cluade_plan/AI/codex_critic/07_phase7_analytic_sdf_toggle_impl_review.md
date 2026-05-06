# Phase 7 Analytic SDF Toggle Implementation Review

## Verdict

This note is directionally useful and it tracks a real implementation that now exists in the codebase. The toggle, SSBO bind, and raymarch shader path are real.

The main issue is that the document is too confident about what the toggle proves, and it is already stale on one basic implementation fact: the current default SDF volume is still `64^3`, not `128^3`.

## Findings

### 1. High: the note is stale on the actual texture-path resolution

The document repeatedly says the OFF path reads from a precomputed `128^3` `R32F` volume and derives all of its spacing math from that assumption.

Evidence:

- `doc/cluade_plan/AI/phase7_analytic_sdf_toggle_impl.md:13`
- `doc/cluade_plan/AI/phase7_analytic_sdf_toggle_impl.md:23-31`
- `src/demo3d.h:46`
- `src/demo3d.cpp:101`

But the current default `DEFAULT_VOLUME_RESOLUTION` is still `64`, not `128`.

That means the note's:

- `3.125 cm` spacing claim,
- `128^3` baseline wording,
- and some of its diagnostic framing

are already out of sync with the live code.

### 2. High: the note overstates the toggle as a universally clean diagnostic, but it is only clean for analytic-primitive scenes

The implementation note presents the toggle as if it simply swaps two equivalent SDF evaluation paths for the current scene.

Evidence:

- `doc/cluade_plan/AI/phase7_analytic_sdf_toggle_impl.md:8-44`
- `src/demo3d.cpp:1275-1279`
- `src/demo3d.cpp:1614-1688`
- `src/demo3d.cpp:2707-2721`
- `src/demo3d.cpp:3021`

In the live code, the analytic fragment path evaluates `analyticSDF` primitives from `primitiveSSBO`. That makes it a clean comparison for the built-in analytic scenes, but not for arbitrary geometry paths such as OBJ-loaded scenes.

So the right statement is closer to:

- "clean diagnostic for analytic Cornell-box / analytic primitive scenes"

not:

- "clean diagnostic of the general current scene SDF path."

The note should explicitly scope the experiment, or it will quietly overteach what the toggle means.

### 3. Medium: the "banding stays identical => no resolution increase can fix it" conclusion is too strong

The note says that if mode 5 looks the same with the analytic toggle ON and OFF, then the banding is just natural box iso-contours plus integer `stepCount`, and no resolution increase can fix it.

Evidence:

- `doc/cluade_plan/AI/phase7_analytic_sdf_toggle_impl.md:35-44`
- `res/shaders/raymarch.frag:441-445`
- `res/shaders/raymarch.frag:519-520`

That is too strong.

Mode 5 is an integer-valued debug visualization, so yes, it can create contouring that a continuous-distance debug mode would not show. But an unchanged mode-5 result does not strictly prove that resolution increases are irrelevant. The step pattern still depends on the sampled SDF values and the adaptive stepping behavior that produced those integer counts.

Mode 7 is a useful companion test, but the document should still phrase the conclusion as a strong hypothesis, not as a proof.

### 4. Low: "same `sdfBox`/`sdfSphere` math" is true locally, but the note skips one important asymmetry

The doc says the fragment shader evaluates the same analytic math the compute shader used to bake the texture.

Evidence:

- `doc/cluade_plan/AI/phase7_analytic_sdf_toggle_impl.md:29-31`
- `res/shaders/raymarch.frag:195-221`
- `res/shaders/sdf_analytic.comp:43-83`

That is broadly true for distance evaluation, but the toggle does **not** make the whole display path fully analytic. Albedo still comes from the precomputed albedo volume, and the GI bake still uses the texture SDF path. The note does mention this later, but the "clean diagnostic" section would be clearer if it foregrounded that this only isolates the display-path SDF sample, not the entire rendering pipeline.

## Where the note is strong

- It correctly describes the new `uUseAnalyticSDF` / `uPrimitiveCount` shader wiring.
- It correctly notes that the toggle is display-path only and does not rebuild cascades.
- It correctly connects mode 5 and mode 7 as complementary diagnostics.

## Bottom line

This is a useful implementation note, but it should be revised before being treated as the canonical Phase 7 diagnosis note.

I would fix it by:

1. correcting all `128^3` baseline references to match the live `64^3` default unless the code changes first,
2. explicitly scoping the toggle to analytic-primitive scenes,
3. softening the "no resolution increase can fix it" conclusion to a hypothesis,
4. stating earlier that the toggle isolates only the display-path SDF sample, not albedo or cascade bake behavior.
