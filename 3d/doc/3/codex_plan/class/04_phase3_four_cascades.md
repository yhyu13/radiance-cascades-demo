# 04 Phase 3: Four Cascades

Phase 3 is where the branch becomes a radiance-cascades renderer instead of just "GI from one probe grid."

## The problem Phase 3 solved

A single probe level has a bad tradeoff:

- short rays are good for nearby detail
- long rays are needed for far lighting
- using one setting for both is inefficient and noisy

So the project split the work into 4 distance bands.

## The four levels

The base distance unit is `d = 0.125 m`.

The cascade intervals are:

- C0: `0.02 m` to `0.125 m`
- C1: `0.125 m` to `0.5 m`
- C2: `0.5 m` to `2.0 m`
- C3: `2.0 m` to `8.0 m`

Interpretation:

- C0 handles immediate local bounce
- C1 handles short-range bounce
- C2 handles mid-range light transport
- C3 handles far-field contribution

## The merge chain

Each level only raymarchs inside its own interval.

So when C0 misses, it does not know the answer yet. It asks C1.
When C1 misses, it asks C2.
When C2 misses, it asks C3.

That means the bake order has to be:

1. bake C3 first
2. bake C2 using C3 as fallback
3. bake C1 using C2 as fallback
4. bake C0 using C1 as fallback

After that, C0 effectively contains the full near-to-far answer and becomes the texture the final renderer samples.

## Why this was a big step

Phase 2 had one GI texture.
Phase 3 had a hierarchy with information flowing downward.

This introduces most of the later terminology:

- cascade index
- upper cascade
- miss
- merge
- interval
- bake order

## The major limitation that remained

Phase 3 still merged isotropically.

That means when C0 missed in some direction, it asked C1 for one averaged probe value, not "what is the radiance for this exact direction?"

This is the key reason Phase 3 can work yet still show boundary artifacts and wrong color bleed.

That problem does not become dominant until the hierarchy itself is stable. Phase 3 made it visible.

## Why the debug work mattered

Once there are 4 levels, visual bugs become hard to reason about from the final image alone.

So the branch added:

- radiance debug views
- per-cascade stats
- debug render modes

That is why the docs start getting jargon-heavy around Phase 3. The system became deep enough that instrumentation was necessary.

## The dot that connects Phase 3 to Phase 4

Phase 3 answered:

"Can a 4-level hierarchy work at all?"

Phase 4 then asked:

"Now that it works, which visible problems are just simple cleanup, and which ones are structural?"
