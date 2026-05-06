# Phase 5i Implementation Learnings Review

## Verdict

This is broadly a solid implementation note. It matches the live code closely on the important structural points:

- display soft shadow is separate from bake soft shadow,
- bake soft shadow correctly triggers a cascade rebuild,
- and the mode-6 directional-GI fix is real in the shader and UI wiring.

The main remaining issue is not a code mismatch but a wording overclaim around the shared `k` parameter.

## Findings

### 1. Medium: the document overstates what the shared `k` parameter guarantees

The note says:

- one shared `k` "makes bake and display shadows consistent when both are enabled"
- both represent the same physical concept

Evidence:

- `doc/cluade_plan/phase5i_impl_learnings.md:24-25`
- `doc/cluade_plan/phase5i_impl_learnings.md:153-160`

That is too strong.

The live code does share one slider:

- `uSoftShadowK` in `raymarch.frag`
- `uSoftShadowK` in `radiance_3d.comp`

Evidence:

- `res/shaders/raymarch.frag:94-97`
- `res/shaders/radiance_3d.comp:56-58`
- `src/demo3d.cpp:1271-1273`
- `src/demo3d.cpp:1076-1077`

But the two paths are still not the same phenomenon:

- display soft shadow applies to the final direct-light term with a normal-offset origin
- bake soft shadow applies to probe-hit shading with a fixed `t=0.05` bias and then gets filtered through the cascade structure

Evidence:

- `res/shaders/raymarch.frag:307-322`
- `res/shaders/radiance_3d.comp:190-207`

So sharing `k` improves UI simplicity and rough visual alignment, but it does **not** make the two paths truly consistent in a strong technical sense. The later "Physical Accuracy Note" already admits this is an appearance approximation, so the earlier wording should be softened to match that.

## Where the document is strong

- The display-path and bake-path sections both match the live shader code.
- The rebuild-trigger description for `useSoftShadowBake`/`softShadowK` matches the current `render()` logic.
- The mode-6 directional-GI fix is accurately documented and is present in the live shader and help text.

## Bottom line

No major technical drift here. I would revise one sentence cluster only:

1. describe shared `k` as a UI/authoring convenience that keeps the two approximations roughly aligned,
2. not as something that makes bake and display shadows genuinely consistent or physically tied.
