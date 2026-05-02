# Reply to Review 15 — Phase 9 Temporal Dark Bugfix Review

**Date:** 2026-05-02

All three findings accepted. Replies below.

---

## Finding 1 (Medium): Bug 1 and Bug 2 are different failure classes — accepted

The review is correct. The document conflates two bugs under one "root cause" heading
when they should be separated by symptom:

- **Bug 1 (dark-history read bug):** the direct cause of "fully dark GI." Display
  immediately reads zero-initialized history on toggle. No rebuild fires. History never
  populated. Effect is instant and permanent in a static scene regardless of jitter.

- **Bug 2 (temporal-convergence / control-flow bug):** a separate effectiveness issue.
  Even after Bug 1 is fixed (one warm-up rebuild fires), a static scene with no further
  parameter changes never triggers subsequent rebuilds. With jitter ON, this means the
  EMA only ever holds one sample (`alpha * current`) and never accumulates the ~22
  distinct positions needed to soften banding.

The correct framing: Bug 1 is the cause of the reported symptom. Bug 2 is the reason
temporal+jitter would fail to improve banding over time even after Bug 1 is fixed. Both
are real bugs requiring the same code changes (the three-condition block), but they
should be described as distinct failure modes with distinct observable consequences.

Doc will be amended to split them clearly:
- Bug 1: "dark-screen symptom — history never populated"
- Bug 2: "temporal convergence failure — not enough rebuilds in static scene"

---

## Finding 2 (Medium): GL_READ_ONLY / readonly mismatch overstated as eliminated — accepted

The review is correct. "Drivers allow it in practice" is too strong as an elimination
rationale. The correct characterization:

- **Not the current leading cause** — the dark-screen bug is fully explained by Bug 1
  (no rebuild trigger). The `readonly` mismatch does not need to fire for the symptom
  to occur.

- **Still a spec violation** — the OpenGL spec says using imageLoad on a uniform not
  declared `readonly` when the image is bound as `GL_READ_ONLY` is undefined behavior.
  On some drivers it may silently produce zero reads, which would compound the Bug 1
  symptom and make debugging harder.

- **Worth fixing for correctness** — add `readonly` to `uCurrent` in
  `temporal_blend.comp`:
  ```glsl
  layout(rgba16f, binding = 1) readonly uniform image3D uCurrent;
  ```
  This is a one-line fix that eliminates the UB with zero runtime cost.

The eliminated-candidates table will be updated to say "not the current leading cause —
but spec UB, fix separately" rather than "eliminated."

---

## Finding 3 (Low): Separation of symptom / code cause / design consequence — accepted

The review is right that the document blurs the three levels. The cleaner structure:

1. **Observed runtime symptom** — GI fully dark after toggle; direct lighting
   unaffected; thumbnail from RDC confirms.

2. **Code-proven cause** — `cascadeReady` never set to `false` on `useTemporalAccum`
   toggle; traced in `update()` change-detector block.

3. **Follow-on design consequence** — even with Bug 1 fixed, no continuous rebuild
   for jitter accumulation (Bug 2); requires the `if (useTemporalAccum && useProbeJitter)`
   every-frame trigger.

The document will be restructured to label these three levels explicitly so the reader
can distinguish what was observed at runtime, what was proven from code, and what was
inferred as a design gap.

---

## Code change: apply the readonly fix now

The `readonly` qualifier addition to `temporal_blend.comp` is unambiguously correct
and has no tradeoffs. Applied immediately as a follow-on to this review.
