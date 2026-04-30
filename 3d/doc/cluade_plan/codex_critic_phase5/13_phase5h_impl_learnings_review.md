# Phase 5h Implementation Learnings Review

## Verdict

This writeup is broadly solid. It describes a real implementation that now exists in the branch, and it improved on the original plan in the right way by using a normal-offset shadow-ray origin instead of blindly copying the bake shader's fixed-bias start.

The main remaining weakness is not the shader logic itself. It is that the document reads more precise than the live UI text, and one runtime-validation claim is stronger than the evidence available in code or build outputs alone.

## Findings

### 1. Medium: the document's description of the shadow ray is more accurate than the live UI text, and the mismatch should be acknowledged

The implementation note correctly says the final renderer uses:

- `shadowRay(pos, normal, uLightPos)`
- a normal-offset origin `normal * 0.02 + ldir * 0.01`
- a method that is strictly better than the bake shader's fixed-bias `t = 0.05`

Evidence:

- `doc/cluade_plan/phase5h_impl_learnings.md:33-57`
- `res/shaders/raymarch.frag:280-293`

But the live UI help still says:

- "Uses the same SDF-march as the cascade bake inShadow()."

Evidence:

- `src/demo3d.cpp:2248-2250`

That is directionally true at a high level, but technically incomplete now that the final path has a better origin bias strategy than the bake path. The document should mention this UI drift if it is meant to be the authoritative implementation record.

### 2. Medium: the document includes a runtime-quality claim that is not established by code inspection or compile evidence alone

The note says the 32-step setup was:

- "Verified at runtime — no false-positive shadow or acne visible."

Evidence:

- `doc/cluade_plan/phase5h_impl_learnings.md:63-66`

That may be true from the original implementation session, but it is not something that can be inferred from the code or from the current build. I verified the branch compiles, but compile success does not prove acne-free runtime behavior.

So this line is acceptable as a session note only if the project wants to preserve manual observation claims. If the file is meant to be a stricter status artifact, it should label this as empirical/manual validation rather than presenting it with the same confidence level as the structural code facts.

## Where the document is strong

- It correctly records the normal-offset improvement over the original Phase 5h plan.
- It correctly keeps the scope narrow: display path only, no cascade rebuild, no bake-path changes.
- It avoids the stronger "Phase 5h is ground truth for 5g" framing that was a problem in the earlier plan review.

## Bottom line

This is a good implementation note. I would only tighten two things:

1. mention that the live help text still understates the difference from bake `inShadow()`, and
2. label the "no acne visible" statement as manual runtime observation rather than a code-proven fact.
