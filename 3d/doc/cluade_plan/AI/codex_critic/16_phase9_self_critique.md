# Phase 9 Self-Critique

## Question

Phase 9 is implemented, but the GI banding still persists. Even with low `alpha`, the image does not become clean. Is the math wrong? Is the jitter pattern wrong? What threshold would completely eliminate the banding?

## Short answer

Probably not "wrong" in the simple sense, but **overoptimistic** in what EMA + random sub-cell jitter can achieve here.

The current Phase 9 method is:

1. jitter probe positions by a global random offset in `[-0.5, 0.5]^3` cell units
2. re-bake every frame
3. blend history with
   `history = mix(history, current, alpha)`
4. display the history texture

That is a valid stochastic supersampling idea. But it does **not** guarantee that rectangular GI banding disappears, and the current implementation has a few mathematical limitations that make the expected improvement much smaller than the plan language suggested.

---

## Main critique

### 1. EMA alone does not solve deterministic spatial aliasing

This is the biggest conceptual correction.

If probe positions do not move, EMA only denoises repeated samples of the **same** biased probe lattice. It cannot invent missing spatial samples.

The current code already acknowledges this in the UI help:

- without jitter: suppresses stochastic noise only
- deterministic probe positions converge to the same biased result

So the real Phase 9 mechanism is not "temporal accumulation."
It is:

- **jittered spatial supersampling**
- with EMA used as the accumulation operator

That means if jitter coverage is weak, EMA cannot save it.

### 2. The startup math is biased dark by construction

History starts at zero and is updated as:

```glsl
history = mix(history, current, alpha);
```

For a constant signal `C`, after `N` rebuilds:

```text
history_N = C * (1 - (1 - alpha)^N)
```

So the displayed GI is dark during warm-up unless you bias-correct or seed history from the first bake.

Examples:

- `alpha = 0.1`
  - 1 frame: `10%`
  - 10 frames: `65%`
  - 22 frames: `90%`
  - 44 frames: `99%`

- `alpha = 0.05`
  - 10 frames: `40%`
  - 20 frames: `64%`
  - 59 frames: `95%`

- `alpha = 0.01`
  - 10 frames: `9.6%`
  - 50 frames: `39.5%`
  - 299 frames: `95%`

So lower alpha is **not** automatically better in practice. If the user does not wait long enough, lower alpha only makes the image darker and slower to converge.

### 3. The current jitter kernel is only a one-cell box filter

Probe jitter is:

```glsl
uProbeJitter in [-0.5, 0.5]^3 cell units
```

That means each probe samples somewhere inside its own cell-sized cube.

Interpretation:

- Phase 9 is approximating a **box prefilter of width 1 cell**
- not increasing true resolution
- not changing topology of the probe lattice

This can soften probe-cell-scale discontinuities, but it cannot fully remove banding if:

- the GI field changes significantly across more than one cell
- the contour spacing is already larger than the jitter kernel
- or the artifact is partly caused by cascade interval transitions, not just in-cell undersampling

So mathematically, Phase 9 is a **low-pass filter**, not a true reconstruction of missing high-frequency data.

### 4. The jitter pattern is valid, but not especially strong

Current jitter generation is:

```cpp
uniform_real_distribution<float>(-0.5f, 0.5f)
currentProbeJitter = vec3(rand, rand, rand);
```

and the same jitter is broadcast to all cascades for that rebuild.

That is defensible because it preserves inter-cascade alignment. But it also means:

- the sequence is plain random, not low-discrepancy
- convergence is noisy
- coverage of the cell cube is uneven over short frame windows

So the jitter math is not "wrong," but it is also not ideal.

A better sampling pattern would be:

- blue noise per frame, or
- a low-discrepancy sequence such as Halton / R2 / Sobol,

because those fill the jitter domain more evenly than independent RNG in the first 16-64 frames.

### 5. "Completely eliminate banding" probably has no single alpha threshold

This is the key expectation reset.

There is no universal `alpha` threshold that guarantees the banding disappears, because `alpha` only controls:

- how fast history forgets old samples
- how many effective jittered samples contribute

It does **not** change:

- probe spacing
- cascade interval design
- directional atlas layout
- or the underlying GI field shape

Roughly:

- larger `alpha` -> faster response, fewer effective samples, less smoothing
- smaller `alpha` -> slower response, more effective samples, more smoothing

But if the underlying bias is too strong, even infinite EMA over the current jitter kernel converges only to the **box-filtered** GI field, not to a band-free ground truth.

So the right question is not:

- "what alpha completely eliminates banding?"

It is:

- "what is the asymptotic image if we average infinitely many jittered samples from this kernel?"

And the answer is:

- a one-cell box-filtered version of the probe-sampled GI field
- which may still show broad contouring if the field variation is larger-scale than the kernel

### 6. The likely remaining limitation is not alpha, but sampling support

If banding still persists after enough rebuilds, the likely reasons are:

1. the jitter kernel is too small
2. the probe spacing is still too coarse
3. the remaining artifact is partly from cascade interval structure
4. the GI field itself has broad rectangular gradients that a one-cell prefilter cannot hide

That means the current Phase 9 result is not strong evidence that EMA is broken.
It is stronger evidence that:

- **the chosen supersampling support is insufficient**

---

## What is mathematically weak in the current implementation

### A. Zero-start EMA without bias correction

This causes dark warm-up and makes small alpha look worse than it really is.

Better options:

1. first-frame seeding
   - if history is invalid, copy current directly
2. bias correction
   - track accumulated weight `w_N = 1 - (1 - alpha)^N`
   - display `history / max(w_N, eps)`
3. explicit sample count
   - use running average for first `N` frames, then switch to EMA

This fixes darkening, but not the core banding.

### B. Pure RNG jitter instead of structured coverage

Random jitter can cluster.
A low-discrepancy or blue-noise sequence would give more even early convergence.

This is likely a real quality improvement.

### C. No direct visibility into current vs history vs residual

There is still no Phase 9-specific debug vis for:

- current atlas
- history atlas
- `abs(history - current)`
- current jitter vector
- effective accumulated sample count

So it is hard to tell whether:

- history is converging correctly
- jitter is exploring the cell
- or the method has already converged to its best possible filtered result

---

## Practical interpretation of alpha

For EMA, a useful approximation is:

```text
effective sample count ~ 1 / alpha
95% settling time ~ 3 / alpha
```

So:

- `alpha = 0.1`
  - effective samples: ~10
  - 95% settling: ~30 frames

- `alpha = 0.05`
  - effective samples: ~20
  - 95% settling: ~60 frames

- `alpha = 0.02`
  - effective samples: ~50
  - 95% settling: ~150 frames

- `alpha = 0.01`
  - effective samples: ~100
  - 95% settling: ~300 frames

If the user only watches 10-30 rebuilds, `alpha=0.01` is too small to judge.

---

## What would make more sense next

### 1. Add Phase 9 debug observability

Before changing more math, add:

- a checkbox to view `probeAtlasHistory` instead of `probeAtlasTexture` in radiance debug
- a checkbox to view `probeGridHistory`
- a text readout of current `uProbeJitter`
- a frame counter / effective history weight
- optionally a residual view: `abs(history - current)`

Without this, Phase 9 is hard to reason about.

### 2. Fix startup bias

Either:

- seed history from first bake when temporal turns on

or:

- add bias correction

This will not remove banding, but it will remove the misleading dark-warmup behavior.

### 3. Replace RNG jitter with a deterministic low-discrepancy sequence

Examples:

- Halton(2,3,5)
- Sobol
- blue-noise frame table

This should improve short-horizon coverage substantially.

### 4. Re-evaluate jitter amplitude

Current support is `[-0.5, 0.5]` cell.
That is safe, but maybe too conservative.

Possible experiments:

- `±0.5` cell (current)
- `±0.75` cell
- anisotropic jitter only in wall tangent directions

Tradeoff:

- more support = stronger smoothing
- but also more bias / more probe leakage / less faithful reconstruction

### 5. Accept that full removal may require a different lever

If the asymptotic Phase 9 image is still banded, then the correct fix is probably one of:

- more probes (`cascadeC0Res`)
- different cascade interval structure
- stronger reconstruction/filtering
- or a more explicit irradiance basis / visibility model

That is not a Phase 9 math bug. It means Phase 9 is only a partial filter.

---

## Bottom line

My self-critique is:

- Phase 9 was **not mathematically absurd**
- but it was **oversold**
- EMA + random sub-cell jitter is a weak stochastic box filter, not a guaranteed banding eliminator

The most likely issues are:

1. startup bias makes low alpha look darker/slower than expected
2. random jitter coverage is inefficient
3. the jitter kernel is too small to erase the remaining contour scale
4. there is no single alpha threshold that can guarantee full removal

So the right mental model is:

- `alpha` sets convergence speed, not magic quality
- jitter supplies sample diversity
- the asymptotic result is limited by the jitter kernel and probe spacing

If banding still persists after many rebuilds, the current Phase 9 method is probably reaching its natural limit rather than simply "using the wrong alpha."
