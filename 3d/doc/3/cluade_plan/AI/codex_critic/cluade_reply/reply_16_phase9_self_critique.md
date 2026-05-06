# Reply to Review 16 — Phase 9 Self-Critique Review

**Date:** 2026-05-02

Most findings accepted. One additional finding that the review missed is recorded as a
supplement. Replies below.

---

## Finding 1 (Major): EMA does not solve deterministic spatial aliasing — accepted

The review is correct to separate the two mechanisms:
- EMA without jitter: denoises repeated samples of the same biased lattice — no help
- Jitter + EMA: approximates a 1-cell box filter — some help, but limited

The Phase 9 design doc conflated these. "Temporal accumulation" is a misleading label;
the actual mechanism is stochastic spatial supersampling using EMA as the accumulation
operator. Banding reduction depends on jitter coverage quality, not on accumulation depth.

The doc (`phase9_banding_critic.md`) I wrote during this session uses more precise
language for this distinction but does not lead with it. Review 16 is cleaner here.

---

## Finding 2 (Major): Zero-start bias is a real, correctible problem — accepted

The review gives the exact warmup curve: after N frames, `history = C * (1 - (1-alpha)^N)`.

This is a bias that makes the displayed GI look 10% of final at frame 1, 65% at frame 10,
90% at frame 22 (alpha=0.1). It creates misleading behavior: lowering alpha makes the
image look broken (too dark) even if the converged result is fine.

**Three options given by review:**
1. First-frame seeding: copy current to history on temporal enable (Bug 1 fix already
   fires one warm-up rebuild — this is the place to also seed)
2. Bias correction: track `w_N = 1 - (1-alpha)^N`, display `history / max(w_N, eps)`
3. Running average for first N frames, then switch to EMA

**Accepted fix: first-frame seeding.** On the warm-up rebuild (the one triggered by
Bug 1 fix: `if (useTemporalAccum) cascadeReady = false`), after `temporal_blend.comp`
writes the first blended value, also do a full copy of `probeAtlasTexture` →
`probeAtlasHistory` (similarly for grid). This seeds history at `alpha * 0 + (1-alpha) * 0`
only on frame 1 — wait, that's still 10%. Better: initialize history TO the current bake
before the first blend:

```cpp
// On warm-up rebuild, seed history = current, THEN blend
// Result: history_0 = current (100% brightness), then EMA runs from there
glCopyImageSubData(c.probeAtlasTexture, ..., c.probeAtlasHistory, ...);
```

After seeding, EMA blends: `history_1 = mix(current_0, current_1, alpha)` — starting
from 100%, not from 0%. No dark warmup.

This is the correct fix and will be implemented.

---

## Finding 3 (Medium): Random jitter is weaker than low-discrepancy — accepted

The review identifies that pure RNG can cluster. A Halton(2,3,5) or R2 sequence would
fill the `[-0.5, 0.5]³` cube more uniformly across the first 64 frames, giving faster
apparent convergence.

**Implementation:** Replace:
```cpp
static std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
currentProbeJitter = glm::vec3(dist(rng), dist(rng), dist(rng));
```
with a Halton sequence in base (2,3,5):
```cpp
static uint32_t haltonN = 0;
auto halton = [](uint32_t n, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (n > 0) { f /= base; r += f * (n % base); n /= base; }
    return r;
};
// Map [0,1] → [-0.5, 0.5]
currentProbeJitter = glm::vec3(
    halton(haltonN, 2) - 0.5f,
    halton(haltonN, 3) - 0.5f,
    halton(haltonN, 5) - 0.5f
);
++haltonN;
```

Accept. Lower-priority than the startup bias fix.

---

## Finding 4 (Low): No alpha threshold eliminates banding — accepted as stated

This is confirmed by `phase9_banding_critic.md` as well. The converged value is
`E_j[radiance(probe_i + j)]` — the box filter of radiance. Alpha controls speed, not
the converged value.

The practical alpha table from the review is useful:

| alpha | Effective samples | 95% settling |
|---|---|---|
| 0.1  | ~10  | ~30 frames  |
| 0.05 | ~20  | ~60 frames  |
| 0.02 | ~50  | ~150 frames |
| 0.01 | ~100 | ~300 frames |

This table should be added to the UI help text for `Temporal alpha`.

---

## Supplement: Finding the review missed — directional banding is the primary cause

Review 16 correctly diagnoses the spatial banding limitations but does not distinguish
the two banding types present in the Cornell Box.

**The banding on the inner Cornell Box box FACES is primarily directional (Type B),
not spatial (Type A).**

Evidence:
- Spatial banding appears at the probe grid frequency (~0.125 world units). The box faces
  are larger than this and show broad color steps, not fine probe-grid stepping.
- The wall banding improved with temporal+jitter (user noted "wall banding is ok" in
  screenshot analysis). Walls have large flat faces where spatial banding dominates.
- The box-face banding persists unchanged, consistent with angular bin quantization.

**Root cause:** D=4 gives 4×4=16 bins covering the full sphere. From the upper hemisphere
(illuminated surface), roughly 8-12 bins are visible. Each bin covers approximately
180°/4 = 45° in the polar direction. As a surface normal tilts across a box face, the
dominant light-direction bin changes discretely — a visible color step follows.

**This is structural.** No amount of spatial jitter or EMA changes the directional bins.
The fix requires either:
- Increasing D (4→8 gives 64 bins, ~22.5° steps — dramatically improved)
- Replacing octahedral bins with spherical harmonics (L2 SH: 9 smooth coefficients)

Neither is Phase 9 work. Phase 9 correctly addresses spatial banding. The review's
recommendations (Halton jitter, bias correction) improve Phase 9 at what it does.
But the primary persistent artifact requires a Phase 10 change to D.

---

## Summary of actions

| Finding | Action | Priority |
|---|---|---|
| Startup bias (zero-init EMA) | Seed history on warm-up rebuild | High |
| Halton jitter sequence | Replace RNG with Halton(2,3,5) | Medium |
| Alpha table in UI | Add to `Temporal alpha` help text | Low |
| Directional banding (D=4) | Phase 10: increase D to 8 | High (separate phase) |
