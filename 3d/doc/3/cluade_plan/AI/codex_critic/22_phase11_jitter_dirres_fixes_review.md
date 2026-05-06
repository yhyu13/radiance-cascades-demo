# Review: `phase11_jitter_dirres_fixes.md`

## Verdict

This note is mostly grounded in real in-flight code. The even-only `dirRes` UI and the new jitter controls (`probeJitterScale`, `jitterPatternSize`, `jitterHoldFrames`) do exist in the current diff.

The main problems are:

- the document overstates the math behind the new jitter cycle as if `alpha ≈ 1/N` gives a clean unbiased N-tap average
- the documented dwell behavior is cleaner than the actual update order in code

## What matches the code

- `dirRes` really changed from a free slider to radio buttons `{2,4,6,8}` in `renderCascadePanel()`.
- `probeJitterScale`, `jitterPatternSize`, `jitterHoldFrames`, and `jitterHoldCounter` were really added to `src/demo3d.h` / `src/demo3d.cpp`.
- `updateRadianceCascades()` really now wraps `probeJitterIndex` and scales the Halton offset.
- the tutorial panel really now prints live jitter settings.

## Main findings

### 1. The “alpha ≈ 1/N gives an unbiased N-tap average” claim is too strong

The note says that with repeating N-position jitter, choosing `alpha ≈ 1/N` gives an approximately uniform N-tap spatial average.

That is not a safe mathematical description of EMA. The accumulation is still an exponential filter over a periodic sequence, not a box average over the last `N` samples. Repeating the pattern helps make the long-run signal periodic and bounded, but it does not convert EMA into a uniform N-tap average.

So the right framing is:

- wrapping the pattern prevents unbounded walk
- smaller `alpha` relative to the cycle length reduces bias toward the most recent positions
- but there is still no exact “unbiased N-tap average” guarantee from this EMA alone

This is the biggest conceptual issue in the note.

### 2. The dwell description is cleaner than the actual code order

The document says:

- hold each jitter position for `K` frames
- then advance to the next one

But the live code does:

```cpp
++jitterHoldCounter;
if (jitterHoldCounter >= jitterHoldFrames) {
    jitterHoldCounter = 0;
    probeJitterIndex = (probeJitterIndex + 1) % jitterPatternSize;
}
currentProbeJitter = ... halton(probeJitterIndex) ...
```

That means the advance happens **before** sampling the current frame’s jitter position. In particular:

- with `jitterHoldFrames = 1`, the first sampled position is index `1`, not `0`
- the startup dwell timing is slightly asymmetric relative to the simple “hold K frames, then move” story

The implementation may still be acceptable, but the note currently describes a simpler model than the code actually uses.

### 3. The backward-compatibility claim is not fully true as written

The note says:

- `probeJitterScale=0.5` + `jitterPatternSize=large` + `jitterHoldFrames=1` reproduces original behavior exactly

That is too strong.

Why:

- the old behavior was an unbounded Halton walk
- the new behavior always wraps at `jitterPatternSize`
- the UI slider only exposes a bounded range for `Pattern N`

So this can approximate the old behavior for a while with a large cycle, but it does not reproduce it exactly.

### 4. The odd-`D` explanation is plausible, but more confident than the evidence shown

Restricting `dirRes` to even values is a practical safety improvement, and the code now enforces it. But the document’s fold-degeneracy explanation reads like a fully proven geometric result while providing only a sketch.

The implementation change is good. The proof language in the note is just stronger than necessary.

## Bottom line

The implementation changes are real and mostly sensible. The document’s main weakness is not file mismatch, but mathematical overstatement:

- repeating jitter patterns help
- reduced amplitude helps
- dwell helps

But the current EMA is still not a true uniform N-tap average, and the code’s dwell/update order is slightly different from how the note narrates it.
