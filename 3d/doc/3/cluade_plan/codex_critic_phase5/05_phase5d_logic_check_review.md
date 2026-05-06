# Phase 5d Logic Check Review

## Verdict

This document is mostly honest about the pre-implementation co-located architecture. Its core claim is correct: under the original all-`32^3` co-located setup, Phase 5d visibility weighting was a no-op.

But as a live project record on 2026-04-28, it is now stale. The biggest issue is not the logic itself; it is that the file still reads like the current status after the codebase has already moved on to an implemented non-co-located toggle and a different shader/uniform structure.

## Findings

### 1. High: the document presents pre-Phase-5d implementation evidence as if it were still the current code state

The file cites:
- identical `probeRes` / `cellSz` initialization for all cascades
- `glUniform1f(..., "uBaseInterval", c.cellSize)`
- shader `probeToWorld()` using `uBaseInterval`

Evidence:
- `doc/cluade_plan/phase5d_logic_check.md:20-49`

That was the correct basis for the original no-op analysis, but it is no longer the current implementation. The live code now has:
- `useColocatedCascades`
- per-cascade `probeRes` / `cellSz` when non-co-located
- split uniforms `uBaseInterval` and `uProbeCellSize`
- `uUpperToCurrentScale`

Evidence:
- `src/demo3d.h:674-677`
- `src/demo3d.cpp:968-977`
- `src/demo3d.cpp:1409-1413`
- `res/shaders/radiance_3d.comp:21-26,69-70,204-206`

So the document should now be framed as a historical logic check for the pre-toggle architecture, not as a current code snapshot.

### 2. High: the "Outcome" and "Decision" sections are outdated now that the runtime toggle already exists

The file says:
- non-co-located cascades "are a planned follow-on feature with a runtime toggle"
- non-co-located cascades "will be added as a runtime-switchable mode"

Evidence:
- `doc/cluade_plan/phase5d_logic_check.md:6`
- `doc/cluade_plan/phase5d_logic_check.md:102-107`

That is no longer future tense. The toggle has already been implemented in the codebase.

Evidence:
- `src/demo3d.cpp:380-387`
- `src/demo3d.cpp:2072`
- `src/demo3d.h:675`
- `doc/cluade_plan/phase5d_impl_learnings.md`

This is the main reason the file now misleads: a reader could think Phase 5d is still only an architecture note, when the project has already crossed into implementation and discovered the new non-co-located visibility path is also inert under the current interval design.

### 3. Medium: the file stops one analytical step too early relative to what the project now knows

The logic check correctly proves:
- co-located Phase 5d is a no-op because upper and current probes share the same world position

Evidence:
- `doc/cluade_plan/phase5d_logic_check.md:64-66`

What the project learned later is stronger:
- even after adding non-co-located cascades, the current Euclidean-distance visibility test still cannot fire under the current interval schedule

Evidence:
- `doc/cluade_plan/phase5d_impl_learnings.md:211-226`
- `doc/cluade_plan/codex_critic_phase5/04_phase5d_impl_learnings_review.md`

That does not make this logic check wrong, but it does make it incomplete as a current Phase 5d status artifact.

### 4. Medium: "ShaderToy-style" is still too loose without a parity disclaimer

The document uses "ShaderToy-style" for the planned non-co-located mode.

Evidence:
- `doc/cluade_plan/phase5d_logic_check.md:6,102`

That is directionally fine, but the repo still does not implement ShaderToy's multi-neighbor spatial interpolation merge, so the label needs calibration if it remains in a current-status document.

Evidence:
- `doc/cluade_plan/codex_critic_phase5/01_phase5_plan_review.md:92-110`

## Where the document is strong

- The original co-located no-op reasoning is technically sound.
- The trade-off section explains why co-location was attractive.
- The document correctly identifies spatial multi-resolution as the condition that makes Phase 5d relevant.

## Bottom line

As a historical reasoning note, this file is good. As a current project-status note, it is stale.

I would revise it to:

1. mark it explicitly as pre-implementation analysis for the original co-located architecture,
2. replace future-tense statements about the runtime toggle with a pointer to the implemented `phase5d_impl_learnings.md`,
3. add a short note that later implementation showed the current non-co-located visibility test is also inert under the present interval design, and
4. keep "ShaderToy-style" only with a clear "not full parity" disclaimer.
