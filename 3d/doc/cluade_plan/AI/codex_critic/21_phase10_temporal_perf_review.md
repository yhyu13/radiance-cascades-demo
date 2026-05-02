# Review: `phase10_temporal_perf.md`

## Verdict

This is mostly a strong implementation note. The major Phase 10 changes it describes are real in the current code and `git diff`:

- staggered cascade updates
- fused in-bake atlas EMA
- atlas/grid handle swaps instead of post-bake temporal dispatches
- larger reduction workgroup size

The main issue is that the document overstates the safety of the new startup/seeding path once staggering is active. There is also some overconfident language around the fused history clamp preserving old quality characteristics.

## What matches the code

- `src/demo3d.h` / `src/demo3d.cpp` really add `renderFrameIndex` and `staggerMaxInterval`.
- `updateRadianceCascades()` really skips cascades by `renderFrameIndex % min(1 << i, staggerMaxInterval)`.
- `radiance_3d.comp` really adds `uAtlasHistory`, `uTemporalAlpha`, `uTemporalActive`, and `uClampHistory`, and performs the fused EMA inside the atlas write path.
- `updateSingleCascade()` really swaps `probeAtlasTexture <-> probeAtlasHistory` and `probeGridTexture <-> probeGridHistory` in the fused path.
- `reduction_3d.comp` really changed from `4x4x4` to `8x8x4`.

## Main findings

### 1. The startup/seeding section is too strong under staggered updates

The note says the old copy-based seed is replaced by:

- `historyNeedsSeed = true`
- `fusedAlpha = 1.0`
- therefore first-frame temporal startup is clean

That is only fully true if the cascades that matter actually rebuild on that frame.

But with Phase 10 staggering, `updateRadianceCascades()` skips cascades based on the current `renderFrameIndex`, and `historyNeedsSeed` is cleared after that loop unconditionally. If temporal is enabled when `renderFrameIndex` is not aligned to all cascade intervals, some cascades can be skipped on the first enabled frame and therefore never get the "alpha=1 overwrite" seed on that cycle.

Why this matters:

- the document overclaims "frame 1" startup safety
- upper-cascade data used by lower cascades can remain stale until that upper cascade's scheduled update
- the old dark-warmup bug may be fixed for the cascades that rebuild, but the note should not imply a universal same-frame seed guarantee

This is the biggest concrete documentation issue.

### 2. The note treats history-neighborhood clamping as if it preserves the old clamp semantics

The document says ghost rejection is preserved by building the AABB from the history neighborhood instead of the current-bake neighborhood. That is a plausible design trade, and the code does implement it, but it is not the same algorithm anymore.

The safer wording would be:

- ghost rejection is still attempted
- but the clamp source changed from current-neighborhood to history-neighborhood
- so quality equivalence is a hypothesis to validate, not a proven invariant

### 3. The performance numbers should be read as estimates, not measured results

The dispatch/thread accounting in the note is directionally reasonable, and it tracks the code structure. But it is still an operation-count estimate, not a measured GPU profile.

That matters because:

- the larger reduction workgroup benefit is architecture-dependent
- the fused bake now pays extra image loads inside `radiance_3d.comp`
- staggering changes average cost over time, not worst-case cost for an updated cascade

So the performance table is useful, but it should be framed as expected cost movement rather than established measured speedup.

## Bottom line

The note is broadly accurate about what changed in Phase 10. The main correction is:

- the fused seeding story is not universally "frame 1 clean" once staggered updates are active

Secondary correction:

- the new history clamp is a modified strategy, not a proven quality-equivalent carryover of the old one

If this doc is meant to be canonical, revise the startup section first.
