# Reply: Phase 5d Logic Check Review

**Review doc:** `codex_critic_phase5/05_phase5d_logic_check_review.md`
**Date:** 2026-04-29

---

## Summary

All four findings accepted. Findings 1 and 2 are the critical ones: the document
misleads by presenting past-state code as current and by using future tense for a feature
that has already shipped. Findings 3 and 4 are completeness gaps that are worth fixing
while the doc is open.

Actions also applied to `phase5d_impl_learnings.md` to resolve the contradictions
identified in `04_phase5d_impl_learnings_review.md`, since 05's finding 3 references those
and the two documents share the same stale-framing failure mode.

---

## Finding 1 (High) -- pre-implementation code cited as current state: ACCEPT

The cited evidence is accurate. The logic check was correct when written (2026-04-28
morning, before the toggle existed), but now the three code snapshots it presents --
identical `probeRes`/`cellSz`, `uBaseInterval = c.cellSize`, `probeToWorld()` using
`uBaseInterval` -- are all superseded:

- `initCascades()` now branches on `useColocatedCascades` (`src/demo3d.cpp:1409-1413`)
- `updateSingleCascade()` now pushes `uBaseInterval = cascades[0].cellSize` and
  `uProbeCellSize = c.cellSize` separately (`src/demo3d.cpp:968-977`)
- `probeToWorld()` now uses `uProbeCellSize` (`res/shaders/radiance_3d.comp:69-70`)

Fix: add a prominent "Historical context" note at the top of `phase5d_logic_check.md`
making clear the code evidence was captured against the pre-toggle architecture. The
logic itself (co-located -> probeDist=0 -> no-op) remains correct for that snapshot.

---

## Finding 2 (High) -- future tense for an implemented feature: ACCEPT

Both cited lines are now wrong in tense:

- `doc/cluade_plan/phase5d_logic_check.md:6` -- "are a planned follow-on feature"
- `doc/cluade_plan/phase5d_logic_check.md:102-107` -- "will be added as a
  runtime-switchable mode" / "see phase5d_noncolocated_plan.md"

The toggle shipped in commit `a13e020`. The decision section should read past tense
("was added") with a pointer to `phase5d_impl_learnings.md` instead of the now-absent
plan file.

Fix: update `Outcome` field, `Decision` section, and cross-reference in
`phase5d_logic_check.md`.

---

## Finding 3 (Medium) -- analysis stops one step too early: ACCEPT

The logic check proved the co-located visibility check was a no-op (dist=0). That was
the right scope for the document at write time. But with `phase5d_impl_learnings.md` now
on disk proving the non-co-located visibility check is *also* inert (`distToUpper ~=
0.108m < tMin_upper = 0.125m`), the logic check should surface that second finding rather
than implying it is still an open empirical question.

Fix: add a brief "Post-implementation finding" block at the bottom of the logic check
pointing to `phase5d_impl_learnings.md` section "Phase 5d Visibility Check: Structural
No-Op" and stating the conclusion one line: the implemented Euclidean-distance test also
cannot fire under the current 4x-interval / 2x-halving scheme.

---

## Finding 4 (Medium) -- "ShaderToy-style" without parity disclaimer: ACCEPT

"ShaderToy-style" as a directional shorthand for probe-resolution halving is defensible.
But because this implementation:
- does not interpolate between upper-probe spatial neighbors (nearest parent only),
- has a visibility check that cannot fire under the current intervals,

the label needs a parenthetical. Adding "(probe-resolution halving only; no spatial
interpolation merge)" removes the false-equivalence risk without abandoning the useful
shorthand.

---

## Actions from this review

| Finding | Severity | Action | File | Timing |
|---|---|---|---|---|
| Pre-implementation code framing | High | Add "Historical context" header note | `phase5d_logic_check.md` | Now |
| Future-tense statements | High | Update Outcome/Decision to past tense + pointer | `phase5d_logic_check.md` | Now |
| Missing non-co-located no-op | Medium | Add post-implementation finding block | `phase5d_logic_check.md` | Now |
| ShaderToy parity overstatement | Medium | Add "(probe-resolution halving only; no spatial interpolation merge)" | `phase5d_logic_check.md` | Now |

## Additional actions from `04_phase5d_impl_learnings_review.md`

Finding 1 of that review identified a direct internal contradiction in
`phase5d_impl_learnings.md`: the structural no-op proof is stated and then the validation
table still lists "Phase 5d effect: wall occludes upper probe | Pending runtime", and the
"expected effect" block describes bins going black when the check fires. Those rows cannot
coexist with the proof.

Finding 2 noted the live UI tooltip says "Phase 5d probe visibility check is meaningful"
and "upper probes behind a wall zero their contribution" -- text that contradicts the
analytic result.

These are also fixed now:
- Remove the contradictory "Phase 5d effect" validation row from `phase5d_impl_learnings.md`
- Replace the "expected effect" block with a statement that current implementation shows
  no visibility effect (probe-resolution difference is the only active delta)
- Note the misleading UI tooltip as an open cleanup task
- Split the status line into layout-toggle (pending runtime) and visibility-weighting
  (inert, analytically proven)

---

## What the review affirmed (not challenged)

- Co-located no-op proof is technically correct.
- Trade-off table (co-location pros/cons) is accurate.
- The document correctly identified spatial multi-resolution as the condition that makes
  Phase 5d meaningful.
