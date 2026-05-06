# Reply to Review 21 — Phase 10 Temporal Performance Review

**Date:** 2026-05-02

All three findings accepted. Finding 1 is a genuine code bug and has been fixed in code.
Findings 2 and 3 are documentation corrections applied to `phase10_temporal_perf.md`.

---

## Finding 1: Startup seeding is not universal under staggered updates — accepted, code fixed

The review is correct. `historyNeedsSeed = true` is set when temporal is first enabled, but
the staggered loop in `updateRadianceCascades()` skips cascades based on `renderFrameIndex`.
If temporal is enabled when `renderFrameIndex` is not 0 (e.g., on frame 5 of a session),
cascades C1, C2, C3 may be skipped on that cycle. `historyNeedsSeed` is then cleared
unconditionally after the loop. When C1–C3 finally rebuild on their scheduled frames,
`historyNeedsSeed` is already `false`, so `fusedAlpha = temporalAlpha` (e.g., 0.3) applies
against their stale (zero-initialized) history, producing the dark warmup the seeding was
designed to prevent.

**Code fix applied:**

In `update()`, when `useTemporalAccum` is first enabled:
```cpp
renderFrameIndex = 0;  // align so n=0 on first temporal frame,
                       // guaranteeing all cascades rebuild (n % 2^i == 0 for all i)
                       // before historyNeedsSeed is cleared.
```

With `renderFrameIndex = 0`, the condition `(renderFrameIndex % min(1<<i, staggerMaxInterval)) == 0`
is satisfied for every cascade level on that first frame. All cascades rebuild with
`fusedAlpha = 1.0`, seeding their histories fully before `historyNeedsSeed` is cleared.

**Doc update:** The seeding section in `phase10_temporal_perf.md` is revised to explain
that seeding requires all cascades to rebuild on the same frame, and that resetting
`renderFrameIndex = 0` on temporal enable is what guarantees this.

---

## Finding 2: History-neighborhood clamping is not equivalent to current-bake clamping — accepted, doc softened

The review correctly identifies that the original `temporal_blend.comp` built its AABB from
the **current-bake** neighborhood, while the fused bake builds it from the **history**
neighborhood (the only data available within a single dispatch). These are different
algorithms and quality equivalence is a hypothesis, not a proven invariant.

**Doc update:** The "AABB ghost rejection preserved" framing is replaced with:

> Ghost rejection is still attempted via history-neighborhood AABB clamping, but the
> clamp source changed from current-frame bake neighbors to history neighbors. The
> hypothesis is that stable neighbors in history define a valid expected range — values
> outside it are likely transient — but this is not equivalent to current-bake clamping
> and should be validated visually at representative alpha and jitter settings.

---

## Finding 3: Performance numbers are estimates, not measured results — accepted, doc clarified

The dispatch/thread accounting in the doc is directionally correct and tracks the code
structure, but it is an operation-count model, not a GPU profile. Key caveats the doc
failed to state:

- The larger reduction workgroup benefit is architecture-dependent (driver/occupancy varies).
- The fused bake adds 6×D² extra imageLoads per probe for AABB clamping (~12.5 M/cascade);
  net savings depend on how the GPU overlaps that with the raymarching computation.
- Staggering changes average cost over time, not per-cascade worst-case cost.

**Doc update:** The performance table is re-titled "Expected cost movement (operation-count
model, not measured)" and the caveats above are added as a footnote.

---

## Summary

| Finding | Status |
|---|---|
| Seeding not universal under stagger — some cascades may miss the seed frame | Accepted; **code fixed**: `renderFrameIndex = 0` on temporal enable guarantees full-cascade rebuild on seed frame |
| History-neighborhood clamp ≠ current-bake clamp — quality equivalence unproven | Accepted; doc softened to "hypothesis to validate" |
| Performance table is an estimate, not a measured GPU profile | Accepted; doc retitled and caveats added |
