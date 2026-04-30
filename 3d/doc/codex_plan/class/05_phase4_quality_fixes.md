# 05 Phase 4: Quality Fixes Before the Big Directional Upgrade

Phase 4 is best understood as cleanup before the real Phase 5 architecture shift.

It did not replace the isotropic merge model. It tried to make that model as honest and debuggable as possible.

## Phase 4a: environment fill

Question:

What should happen when a probe ray exits the simulation volume without hitting geometry?

Two options:

- honest black miss
- return a dim sky color

This matters because open boundaries can otherwise make probes near the room boundary unnaturally dark.

What it changed:

- added a toggle for sky/environment fill
- separated direct surface hits from sky exits in debug stats

## Phase 4b: ray-count scaling

Question:

Should higher cascades fire more rays than lower cascades?

The logic was reasonable:

- higher cascades cover larger distances
- larger intervals often need more angular sampling

This phase improved controls and instrumentation, but later Phase 5 changed the direction-sampling model enough that some of this UI became stale and partially retired.

That is why you see historical `baseRaysPerProbe` language mixed with later `D x D` direction-bin language.

## Phase 4c: blend zone near the interval boundary

Problem:

If a surface lies just inside a cascade boundary, a hard switch can create a visible discontinuity.

Fix:

Blend surface hits near `tMax` toward the upper cascade instead of switching abruptly.

Important result:

The A/B outcome was basically a no-op for the main visible artifact.

This was useful. It told the branch that the remaining banding was not mainly caused by a hard distance boundary. It was mainly caused by directional mismatch.

That finding points directly into Phase 5.

## Phase 4d: filter verification

This phase mostly confirmed that some texture/filter assumptions were already correct.

Its value was not a new feature. Its value was removing one possible explanation for later artifacts.

## Phase 4e: debug/stat cleanup

This phase made the branch easier to reason about:

- better packed-count decoding
- better fill/coverage displays
- better mean-luminance views

This matters because once Phase 5 changes storage and merge semantics, stale debug assumptions become dangerous.

## What Phase 4 really accomplished

Phase 4 did not "solve the final GI quality problem."

It did something more useful:

1. separated easy fixes from structural issues
2. improved the branch's honesty about what the probes were doing
3. showed that isotropic merge was now the main remaining limitation

That is why the branch could move into Phase 5 with a clearer target.
