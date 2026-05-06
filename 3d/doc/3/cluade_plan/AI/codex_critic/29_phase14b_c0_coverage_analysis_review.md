# Review: `phase14b_c0_coverage_analysis.md`

## Verdict

This note contains a useful observation: low C0 `surfPct` is real, and the current `tMax = cellSize` design does make C0 a very short-range shell around surfaces. But the document pushes that observation too far. It treats “C0 surfPct is low” as if that alone proves the main temporal-instability root cause and makes `uC0MinRange` the clearly correct structural fix. The current evidence is not strong enough for that conclusion.

## Main findings

### 1. The note overclaims that low C0 `surfPct` is the root cause of the residual shimmer

The analysis shows:

- C0 `surfPct` is low
- C0 misses fall back to C1
- jitter can move some probes across the hit/miss boundary

All of that is plausible and technically grounded.

But the note jumps from there to:

- “this is the root cause”
- “this is why sequence is Stable at best”
- “`uC0MinRange` is the correct structural fix”

That is too strong for the current evidence.

Why:

- `surfPct` is an aggregate probe statistic, not a direct measurement of temporal instability at the affected image pixels
- the sequence captures have not yet isolated “hit/miss boundary toggling” from the other active causes in this branch, such as directional quantization, EMA tuning, GI blur choices, or upper-cascade merge differences
- the document does not show a concrete probe- or pixel-level A/B proving that the unstable regions correspond mainly to probes straddling the C0 boundary

So the low-coverage theory is a credible hypothesis, but not yet a proven root cause.

### 2. The “C1 surfPct is 77% entirely because of reach, not probe count” claim is too absolute

The note says the difference between C0 and C1 `surfPct` is entirely due to ray reach, not probe count.

Ray reach is clearly a major factor. But “entirely” is too strong:

- C1 also samples a different spatial grid
- C1 uses different world-space probe placement
- C1’s interval starts farther from the probe origin

Those differences do not make the reach argument wrong, but they mean the document is collapsing several changed variables into one.

### 3. The performance estimate for `uC0MinRange=0.5` is more confident than the code evidence supports

The note says the cost increase should be about `2–3x for C0 bake only`, and frames that as acceptable.

That may be directionally reasonable, but it is still a speculative estimate. In this branch, bake cost depends on:

- scene geometry
- SDF step counts
- whether rays hit early or miss late
- temporal/stagger settings
- current directional resolution and merge path

So the cost section should be framed as an expectation to test, not as a near-established multiplier.

### 4. The proposed fix changes cascade semantics more than the note admits

The note argues that expanding C0 to `tMax = 0.5` is “cascade-correct” because overlap with C1 is acceptable.

That is not obviously wrong, but it is a bigger design change than the writeup suggests:

- C0 would stop being “very near field”
- the C0/C1 division of labor would be materially rebalanced
- the blend zone width for C0 would become much larger

That may still be a good experiment. It just should be framed as a structural hierarchy redesign, not merely a local stability tweak.

### 5. One factual claim is too strong: increasing resolution does not necessarily prove “worse surfPct” in the practical sense the note implies

The doc argues that going from `32^3` to `64^3` must worsen C0 `surfPct` because `tMax` halves with `cellSize`.

At the metric-definition level, that could happen. But the practical implication for visible output is not settled by that one scalar. Higher probe density changes spatial sampling in other ways, so the note should be careful not to turn “the shell gets thinner in world units” into “resolution increase is definitively worse for the problem overall.”

## Bottom line

The strongest part of this note is the explanation that current C0 is a short-range shell because `tMax = cellSize`.

The weakest part is certainty:

- low C0 `surfPct` is a real and important clue
- it is **not yet proven** to be the main root cause of the observed temporal instability
- `uC0MinRange` is a plausible experiment, but the document presents it too much like a settled fix rather than a structural A/B to validate
