# Phase 9 Reinvestigation: ShaderToy, Mode 5, and Temporal History Clamping

## Scope

User concern:

- Phase 9 jitter is unstable
- low `alpha` converges too slowly
- jitter introduces new color bleeding
- banding is still not truly fixed
- maybe we need TAA-style history clamping / validation instead of raw EMA

Local artifact context:

- RenderDoc capture exists: `3d_temporal_fail3.rdc`
- This note is based on code inspection plus the already observed runtime behavior
- I verified the capture file exists locally, but I did **not** extract new GPU-stage evidence from the `.rdc` in this pass

---

## Short answer

Yes, the current Phase 9 temporal method is probably the wrong tool for the specific artifact.

It is not "mathematically invalid," but it is trying to solve probe-grid banding with:

- global sub-cell jitter
- raw EMA accumulation
- no history validation
- no neighborhood clamp

That combination is exactly the kind of setup that produces:

- slow convergence at low alpha
- ghosting / color drag / bleeding
- only partial visual softening instead of true structural cleanup

So the next improvement direction should **not** be "keep lowering alpha."
It should be:

- validate or clamp history against current data
- and only trust history where it is consistent with the current frame

---

## What ShaderToy is actually doing differently

The local ShaderToy `Image.glsl` does reduce visible probe-boundary artifacts, but not by anything like our Phase 9 temporal path.

What it is doing:

1. probe spacing and cascade relationship are tightly structured in a surface-parameterized layout
2. upper-cascade reuse is merged spatially with bilinear weighting
3. there is approximate visibility weighting (`WeightedSample`)
4. the merge is local and geometric, not history-based

Relevant local reference:

- `shader_toy/Image.glsl`
- especially `WeightedSample(...)`
- and the "Merging with weighted bilinear" section

That means the ShaderToy branch is primarily fighting banding through:

- better **same-frame reconstruction**
- and visibility-aware upper-probe reuse

not through temporal stochastic supersampling.

So if ShaderToy looks cleaner at probe boundaries, that does **not** prove our current Phase 9 EMA+jitter should have been enough.
It more likely means we are solving the wrong problem with the wrong lever.

---

## What mode 5 actually shows

Current mode 5 in `raymarch.frag` is:

```glsl
float t5 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
```

So mode 5 is a visualization of:

- integer primary-ray march iteration count

It does **not** directly show:

- probe spatial resolution
- probe interpolation quality
- atlas reconstruction quality
- or probe-boundary sampling error

It shows where the display-path primary ray needed more or fewer SDF steps before finding a surface.

That can correlate with geometry layout, but it is not a probe-debug view.

## Important refinement: mode 5 can still be the right visual proxy

The user's observation is important:

- the contouring in mode 5 is visually the same contour family seen in the GI banding

That does **not** mean:

- mode 5 and GI are the same mechanism

But it likely **does** mean:

- both are responding to the same underlying spatial structure in the scene / SDF / sampling layout

So the safer interpretation is:

- mode 5 is a **strong correlated proxy**
- not the GI output itself

In other words:

- mode 5 contouring can track the same rectangular distance-driven structure
- while mode 0 / mode 6 show how that structure reappears after probe bake and GI reconstruction

This is a better framing than either extreme:

- too weak: "mode 5 is irrelevant"
- too strong: "mode 5 is literally the GI artifact"

The current best interpretation is:

- mode 5 is not the final GI signal,
- but its contour family is still useful evidence for the spatial structure the GI path is struggling to reconstruct cleanly.

## Important consequence

Even if we eliminated visible contouring in mode 5, that would only mean:

- the display-path primary-ray step-count visualization became smoother

It would **not** prove:

- the GI banding is solved

Mode 5 can be useful as a warning sign and as a correlated spatial-structure proxy, but it is still not the right sole success target for probe-boundary banding.

If the real issue is probe-grid / atlas / reconstruction behavior, mode 6 and probe-debug outputs matter far more than mode 5.

---

## Why Phase 9 temporal accumulation is producing bad behavior

Current Phase 9 / 9b temporal logic is:

1. jitter all probe sample positions by one global offset in `[-0.5, 0.5]^3`
2. re-bake
3. blend history and current with
   `history = mix(history, current, alpha)`
4. display the history texture directly

There are four major problems with that design.

### 1. No history validation

The history texel is always trusted.

There is no test like:

- is history close to current?
- is history an outlier relative to local neighborhood?
- did the probe sample a different side of a steep GI edge this frame?

So when jitter crosses a strong irradiance transition, history drags old color into the new sample.

That is the exact mechanism for:

- color bleeding
- laggy smearing
- unstable mixed colors

### 2. Jitter is global, not locally decorrelated

All probes use the same `currentProbeJitter` each rebuild.

That helps inter-cascade consistency, but it also means:

- the whole probe lattice shifts coherently
- large correlated bias can appear
- history errors are spatially structured, not noise-like

So the result is often not pleasant stochastic blur. It can become coherent drifting color.

### 3. EMA is too trusting

Raw EMA assumes history is always useful:

```glsl
mix(history, current, alpha)
```

But TAA-style systems only keep history when it remains compatible with current observations.

Without that, low alpha means:

- history dominates
- convergence is slow
- stale/wrong history contaminates many frames

### 4. The underlying artifact may not be temporal-noise-shaped at all

If the remaining banding is mostly due to:

- probe spacing
- cascade interval structure
- or same-frame reconstruction limits

then temporal averaging can only blur it, not solve it structurally.

That matches the observed behavior:

- banding is reduced visually
- but not really eliminated

---

## So are we "doing the math wrong"?

Not in the narrow sense.

The EMA formula is fine.
The Halton jitter is more reasonable than plain RNG.
The startup seeding fix is good.

The real issue is:

- we are using a **too-naive history filter** for a signal with steep spatial/color discontinuities

So the wrong part is not `mix()` arithmetic itself.
The wrong part is:

- **unconditional trust in history**

---

## What should replace raw EMA

The next improvement should be a TAA-style history validation / clamping pass for probe history.

## Minimum viable version

For each texel in atlas/grid history:

1. read `current`
2. read `history`
3. read a small neighborhood from `current` around the same texel
4. compute neighborhood min/max or mean/variance
5. clamp history into that current neighborhood range
6. blend clamped history with current

Conceptually:

```glsl
vec4 cur = imageLoad(uCurrent, coord);
vec4 his = imageLoad(oHistory, coord);

vec4 nMin = ...;   // neighborhood minimum from current
vec4 nMax = ...;   // neighborhood maximum from current

vec4 hisClamped = clamp(his, nMin, nMax);
vec4 outVal = mix(hisClamped, cur, uAlpha);
imageStore(oHistory, coord, outVal);
```

This is the probe-space analogue of TAA neighborhood clamping.

## Why this helps

If jitter makes a texel sample a very different probe-side color this frame:

- old history from the previous side of the edge will be clipped
- history can no longer drag impossible colors forward indefinitely

That directly targets:

- color bleeding
- slow ghost convergence
- temporal oversmear

---

## Better versions

### 1. Variance clip instead of hard min/max

Instead of simple clamp to neighborhood min/max:

- compute local mean and variance from current neighborhood
- clamp history to `mean +/- k * sigma`

This is usually less aggressive and preserves more detail.

Conceptually:

```glsl
vec4 mu = neighborhoodMean;
vec4 sigma = sqrt(max(neighborhoodVar, 0.0));
vec4 lo = mu - k * sigma;
vec4 hi = mu + k * sigma;
vec4 hisClamped = clamp(his, lo, hi);
```

This is closer to modern TAA history validation.

### 2. Luminance-only clamp + chroma preservation

If the bleeding is mostly colored contamination:

- clamp history more strongly in luminance
- optionally reduce chroma if history/current diverge heavily

This can reduce obvious red/green drag from Cornell-box walls.

### 3. Adaptive alpha

When `history` and `current` disagree strongly:

- increase alpha locally
- trust current more

When they agree:

- use lower alpha
- keep the smoothing benefit

Conceptually:

```glsl
float disagreement = length(cur.rgb - hisClamped.rgb);
float alphaLocal = mix(alphaStable, alphaReactive, saturate(disagreement * scale));
outVal = mix(hisClamped, cur, alphaLocal);
```

This is often more important than just tuning one global alpha.

---

## Why this makes more sense than pushing alpha lower

Lower alpha only means:

- slower replacement of history by current

If history is wrong, lower alpha makes the artifact live longer.

So with no history validation:

- low alpha helps smoothing
- but worsens lag / bleeding

That is exactly the failure mode you described.

TAA-style clamping changes the tradeoff:

- history is kept when it is plausible
- rejected or limited when it is implausible

That is the right mathematical control knob.

---

## What not to conclude from mode 5

Do **not** use:

- "if we can eliminate mode 5 banding, we are good"

as the only criterion.

That is the wrong target for this problem.

A better success ladder is:

1. mode 6 GI-only becomes smoother without new color drag
2. final mode 0 indirect field improves without obvious temporal smearing
3. radiance debug / atlas-derived views show more stable probe output
4. mode 5 contouring weakens too, confirming that the shared underlying spatial structure is being handled better

So mode 5 should not be thrown away conceptually. It should just be treated as:

- a correlated proxy,
- not the definitive output metric.

---

## Proposed concrete Phase 9c direction

### Step 1

Replace raw `temporal_blend.comp` with:

- current-neighborhood min/max clamp
- then EMA blend

No jitter changes yet.

### Step 2

If that reduces bleeding but leaves residual smearing:

- move to mean/variance clipping
- optionally adaptive local alpha

### Step 3

Only after history validation is working, decide whether the current global Halton jitter is still appropriate.

If needed:

- keep Halton
- but reduce amplitude
- or use per-cascade / per-probe decorrelation more carefully

---

## Bottom line

Reinvestigation result:

- ShaderToy’s probe-boundary cleanup is mostly a **same-frame reconstruction** story, not a temporal EMA story.
- Our current mode 5 is **not** the right probe-banding truth source.
- The current Phase 9 temporal path is likely failing because it uses **raw history accumulation without validation**.

So the most defensible improvement is:

- **TAA-style history clamping / validation in probe space**

not:

- just lower `alpha`
- or keep tuning jitter alone.
