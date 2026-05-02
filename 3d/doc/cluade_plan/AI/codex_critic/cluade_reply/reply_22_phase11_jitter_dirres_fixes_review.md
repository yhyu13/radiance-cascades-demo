# Reply to Review 22 — Phase 11 Jitter & dirRes Fixes Review

**Date:** 2026-05-02

Finding 2 is a real code bug and has been fixed. Findings 1, 3, and 4 are
documentation corrections applied to `phase11_jitter_dirres_fixes.md`.

---

## Finding 1: α≈1/N "unbiased N-tap average" claim is too strong — accepted, doc softened

The review is correct. EMA over a periodic jitter sequence is not the same as a
uniform box average over the last N samples. The claim that `alpha ≈ 1/N` converts
the EMA into an "approximately uniform N-tap average" overstates what the math
actually guarantees.

What wrapping the pattern actually provides:
- The Halton walk is bounded — the probe visits only N distinct positions indefinitely
- With `alpha = 1/N`, the most recent sample has weight `1/N` and history has weight
  `(N-1)/N`, which produces a geometrically decaying tail over the pattern cycle,
  not a uniform box
- In steady state the long-run average does approximate a weighted blend of the N
  positions, but the early-cycle samples are weighted more heavily than later ones
  in each cycle

**Doc update:** The α≈1/N paragraph is revised to:

> Choosing `alpha ≈ 1/N` gives each new position a weight of `1/N` in the blend,
> which in steady state produces a geometrically decaying weighted sum over the N
> positions — approximately proportional to a uniform N-tap spatial average after
> many cycles, but with the most recent positions always weighted more heavily than
> earlier ones in the same cycle. The EMA is not a true box average; it is a
> bounded, periodic exponential filter.

---

## Finding 2: Dwell/advance order is wrong — accepted, code fixed

The review is correct. The original code:

```cpp
++jitterHoldCounter;
if (jitterHoldCounter >= jitterHoldFrames) {
    jitterHoldCounter = 0;
    probeJitterIndex = (probeJitterIndex + 1) % jitterPatternSize;
}
currentProbeJitter = halton(probeJitterIndex, ...);
```

With `jitterHoldFrames=1` and `jitterHoldCounter=0` at startup:
- Frame 1: counter → 1, 1 ≥ 1 → advance to index 1, **sample at index 1** (index 0 skipped)
- Frame 2: counter → 1, advance to index 2, sample at index 2
- Index 0 is never sampled.

With `jitterHoldFrames=K`:
- Frame 1 at index 0: counter=1, 1 < K → no advance → sample at 0 ✓
- Frames 2…K-1 at index 0: counter < K → no advance ✓
- Frame K at index 0: counter=K, K ≥ K → advance to 1, sample at 1
  → index 0 gets K-1 frames; index 1 gets K frames. Asymmetric.

**Code fix applied:** sample first, then count/advance:

```cpp
// Sample at current index first — ensures index 0 is always sampled and every
// position gets exactly jitterHoldFrames frames before the next one is selected.
currentProbeJitter = glm::vec3(
    (halton(probeJitterIndex, 2) - 0.5f) * probeJitterScale,
    (halton(probeJitterIndex, 3) - 0.5f) * probeJitterScale,
    (halton(probeJitterIndex, 5) - 0.5f) * probeJitterScale
);
++jitterHoldCounter;
if (jitterHoldCounter >= jitterHoldFrames) {
    jitterHoldCounter = 0;
    probeJitterIndex = (probeJitterIndex + 1) % static_cast<uint32_t>(jitterPatternSize);
}
```

With this order and `jitterHoldFrames=1`:
- Frame 1: sample index 0, counter → 1, advance to 1
- Frame 2: sample index 1, counter → 1, advance to 2
Every index sampled exactly once. With `jitterHoldFrames=K`, every index gets
exactly K frames before advancing — symmetric and matches the documented behavior.

**Doc update:** The code snippet in the `updateRadianceCascades()` section is
updated to show the corrected order with the comment explaining why.

---

## Finding 3: Backward-compatibility claim "exactly" is too strong — accepted, doc softened

The review is correct. "Reproduces original behavior exactly" is wrong because:
- The original was an unbounded Halton walk (`probeJitterIndex` grew without bound)
- The new code wraps at `jitterPatternSize`, which the UI caps at 32
- There is no way to configure an unbounded walk through the UI

**Doc update:** The backward-compatibility row is changed from:

> `probeJitterScale=0.5` + `jitterPatternSize=large` + `jitterHoldFrames=1`:
> reproduces original behavior **exactly**

to:

> `probeJitterScale=0.5` + `jitterPatternSize=32` (UI max) + `jitterHoldFrames=1`:
> **approximates** the original unbounded Halton walk for the first 32 frames;
> thereafter the pattern repeats. True unbounded behavior is no longer achievable.

---

## Finding 4: Odd-D explanation is stronger than the evidence shown — accepted, doc softened

The review is correct that the fold-degeneracy explanation is presented as a
geometric proof when only a sketch was given. The restriction to even D is a
safe engineering constraint, but the precise claim about bin centers landing
"exactly on the fold line" at all odd D values was stated with more confidence
than the argument supports.

**Doc update:** The odd-D section is reworded:

> Restricting `dirRes` to even values is a conservative engineering constraint
> based on the symmetry of the octahedral projection. At odd D, bin centers are
> spaced at intervals of `1/D`, and for certain odd values the center nearest the
> fold diagonal (where `u + v = 1`) falls close enough to create near-degenerate
> mirror bins that can bleed across hemispheres. Even D keeps the fold between
> bin centers by construction, avoiding this class of defect. The exact failure
> mode at each specific odd D has not been exhaustively verified; the even-only
> constraint is adopted as a safe default.

---

## Summary

| Finding | Status |
|---|---|
| α≈1/N claim overstates EMA math — not a true box average | Accepted; doc softened to "geometrically decaying weighted sum, not uniform box" |
| Dwell/advance order — index 0 skipped, first position short by 1 frame | Accepted; **code fixed**: sample before counting/advancing |
| Backward-compat "exactly" — unbounded walk not reproducible with wrapped pattern | Accepted; doc changed to "approximates for first N frames, then repeats" |
| Odd-D proof language stronger than evidence | Accepted; doc reframed as engineering constraint, not proven theorem |
