# Phase 5g Implementation Learnings Review

## Verdict

This is one of the stronger Phase 5 implementation notes. It correctly records the review-driven fixes that mattered most:

- directional GI stayed display-path only,
- texture unit `3` was used instead of clobbering `uAlbedo`,
- and the final implementation preserved spatial trilinear filtering instead of regressing to nearest-probe snapping.

But the document still contains one false safety invariant and one important validation gap.

## Findings

### 1. High: the document claims a safe "atlas not allocated" fallback that the live code does not actually enforce

The correctness table says:

- if the C0 atlas is not allocated, atlas binding is skipped
- `uUseDirectionalGI` is pushed as `0` from `useDirectionalGI=false`

Evidence:

- `doc/cluade_plan/phase5g_impl_learnings.md:185-190`

The first half is true:

- `raymarchPass()` only binds `uDirectionalAtlas` if `cascadeCount > 0 && cascades[0].active && cascades[0].probeAtlasTexture != 0`

Evidence:

- `src/demo3d.cpp:1248-1258`

But the second half is not enforced by code. The shader toggle is always pushed directly from the UI state:

- `glUniform1i(..., "uUseDirectionalGI", useDirectionalGI ? 1 : 0);`

Evidence:

- `src/demo3d.cpp:1259`

So if `useDirectionalGI` were true while the atlas was unavailable, the shader would still enter the directional path and sample `uDirectionalAtlas` without the document's promised forced fallback to isotropic. In normal branch usage the atlas is usually present, so this may not bite often, but the invariant in the writeup is false as written.

### 2. Medium: the implementation note is honest about modes 3 and 6 staying isotropic, but that also means the branch still lacks a clean directional-GI-only validation view

The document explicitly says Phase 5g does not apply to debug modes 3 or 6:

- mode 6 remains on the isotropic average as a comparison reference

Evidence:

- `doc/cluade_plan/phase5g_impl_learnings.md:195-203`
- `res/shaders/raymarch.frag:252-257`
- `res/shaders/raymarch.frag:264-271`

That is an honest description of the code, but it leaves a real testing gap. Once `uUseDirectionalGI` is enabled, there is still no direct “GI only, but directional” view in the final renderer. Validation has to happen indirectly through mode 0, where direct light, indirect light, tone mapping, and shadow-ray state are all mixed together.

That is not a bug in the implementation note, but it is a residual instrumentation gap the note should probably call out more explicitly if it is meant to serve as a canonical status record.

## Where the document is strong

- It correctly captures the important architectural correction from plan review 11: texture unit `3`, not `2`.
- It correctly records the shift from single-probe atlas sampling to 8-probe trilinear spatial blending.
- It properly tones down the penumbra language and says the smoothness comes from probe interpolation, not a true area-light model.

## Bottom line

The implementation note is broadly trustworthy. The main revision I would make is:

1. fix the false "atlas missing => forced isotropic fallback" invariant, and
2. explicitly note that the branch still lacks a directional-GI-only debug mode in `raymarch.frag`.
