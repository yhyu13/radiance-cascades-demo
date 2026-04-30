# Phase 5d Implementation Learnings Review

## Verdict

This document is partially honest, but it is internally inconsistent.

Its strongest section is the structural proof that the implemented Phase 5d visibility check cannot fire under the current interval scheme. That part is useful and technically important. But the document then keeps talking about the same effect as if runtime validation could still confirm it, which is too contradictory for a file that calls itself implementation learnings.

## Findings

### 1. High: the document proves the Phase 5d visibility effect is impossible, then still lists that effect as a pending runtime validation

The file explicitly says:
- "The Phase 5d visibility check cannot fire with our current interval scheme."
- "Phase 5d is implemented but has no effect."

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:193-218`

But the validation table still lists:
- `Phase 5d effect: wall occludes upper probe | Pending runtime`

And the "expected effect" section still describes bins going black when visibility weighting fires.

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:231`
- `doc/cluade_plan/phase5d_impl_learnings.md:234-239`

Those statements cannot all be true at once. If the proof is correct, runtime testing cannot validate the intended 5d visibility effect because that effect is structurally unreachable in the current implementation.

### 2. High: the document does not call out that the live UI still contradicts its own no-op proof

The learnings file says the non-co-located visibility check is meaningful in the invariants section, then later proves it cannot fire.

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:187`
- `doc/cluade_plan/phase5d_impl_learnings.md:193-218`

The live UI help text is even stronger:
- "Phase 5d probe visibility check is meaningful"
- "upper probes behind a wall zero their contribution"

Evidence:
- `src/demo3d.cpp:2077-2081`

That active UI contradiction is exactly the kind of thing an implementation-learnings note should record, because otherwise the document overstates how settled the feature really is.

### 3. Medium: `Status: Implemented ... Runtime visual validation pending` is too vague for a feature whose core effect is already proven unreachable

The status line is not false, but it blurs together two different things:
- the non-co-located cascade layout toggle does need runtime validation
- the specific Phase 5d visibility-weighting effect does not remain an open visual question if the structural proof is correct

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:5`
- `doc/cluade_plan/phase5d_impl_learnings.md:193-218`

The more accurate framing would be:
- layout toggle implemented, compile-verified, runtime validation pending
- visibility-weighting path implemented but currently inert under this interval design

### 4. Medium: calling the non-co-located mode "ShaderToy-style" still overstates parity

I agree that halving the spatial resolution per cascade is a ShaderToy-like move. But this implementation note also proves that the intended Phase 5d visibility behavior is inert, and the broader codebase still does not implement ShaderToy's multi-neighbor spatial interpolation merge.

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:182-218`
- `doc/cluade_plan/codex_critic_phase5/01_phase5_plan_review.md:92-110`

So "ShaderToy-style" is acceptable as a loose directional label, but not as a claim of functional equivalence. The document should calibrate that more carefully.

## Where the document is strong

- The `uBaseInterval` / `uProbeCellSize` split is correctly identified as load-bearing.
- The atlas-address split between current write and upper read is explained clearly.
- The per-cascade readback sizing fix is concrete and matches the code.
- The structural no-op proof is the most valuable part of the file.

## Bottom line

The best part of this document is the proof that current Phase 5d visibility weighting does nothing. But once that proof is accepted, the rest of the file should be rewritten to match it.

I would revise it to:

1. remove the "Phase 5d effect" runtime-validation row,
2. replace the "expected effect" section with a statement that the current implementation should not show that effect,
3. explicitly note that the live UI text is currently misleading, and
4. narrow "runtime validation pending" to the layout-toggle/rebuild path rather than the visibility-weighting effect itself.
