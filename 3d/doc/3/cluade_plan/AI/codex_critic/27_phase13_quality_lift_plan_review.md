# Review: `phase13_quality_lift_plan.md`

## Verdict

The plan has some useful instincts, especially the idea of retuning quality defaults after several temporal/jitter iterations. But as written it contains a few hard technical errors and overstates how much can be concluded from one burst run.

## Main findings

### 1. Part 13c is effectively a no-op with the current 4-cascade hierarchy

The plan proposes extending `staggerMaxInterval` from `8` to `16` and `32`, claiming C3 could then update every 16th or 32nd frame.

That is wrong for the current code.

The live update rule is:

```cpp
int interval = std::min(1 << i, staggerMaxInterval);
```

in [src/demo3d.cpp](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.cpp:1224).

With 4 cascades, `i` only reaches `3`, so the maximum per-cascade base interval is:

- C0: `1`
- C1: `2`
- C2: `4`
- C3: `8`

So even if `staggerMaxInterval` is set to `16` or `32`, `std::min(1 << 3, staggerMaxInterval)` is still `8` for C3. The proposed UI extension would change the buttons, but not the runtime schedule.

This is the biggest concrete flaw in the plan.

### 2. The 13a temporal-jitter diagnosis uses the wrong current default pattern size

The plan says:

- `jitterHoldFrames = 1`
- `jitterPatternSize = 8`
- therefore `alpha = 0.05` integrates an 8-position cycle too slowly

But the current constructor defaults are:

- `temporalAlpha = 0.05f`
- `probeJitterScale = 0.05f`
- `jitterPatternSize = 4`
- `jitterHoldFrames = 1`

in [src/demo3d.cpp](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.cpp:135) through [src/demo3d.cpp](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.cpp:141).

So the plan’s convergence story is partly built on a Phase 11 recommendation (`N=8`), not on the actual current default it is trying to critique (`N=4`). That weakens the “defaults were left at pre-Phase-11 values” narrative, because some defaults were changed, just not to the same targets the doc now wants.

### 3. The seam diagnosis in 13b is too confident and partly geometrically wrong

The plan says the blur softens “back wall and ceiling, which meet at the same depth and approximately the same normal,” so a luminance edge-stop is needed.

That specific geometric explanation is not right for the Cornell box corner:

- wall and ceiling are not approximately the same normal
- the current blur already has a normal edge-stop in [res/shaders/gi_blur.frag](D:\GitRepo-My\radiance-cascades-demo\3d\res\shaders\gi_blur.frag)

So a luminance term may still be useful, but this section overclaims the root cause. The more defensible claim is:

- a luminance edge-stop may help preserve tonal boundaries that survive the existing depth/normal stops

not:

- the current seam softness is explained by wall/ceiling being geometrically indistinguishable to the blur

### 4. The performance conclusion leans too hard on one reported `cascadeTimeMs`

The plan treats `cascadeTimeMs = 0.091 ms` as proof that throughput is no longer a concern.

That is too strong for this branch because:

- `cascadeTimeMs` is a CPU-side wall-clock around `updateRadianceCascades()`, not a pure GPU timer
- this branch also has burst/autocapture/readback activity that changes what those numbers mean
- one burst run is not enough to settle the throughput question across different settings

So “already fast” is plausible, but it should be presented as a current-observation summary, not an established performance truth.

## Bottom line

The best part of the plan is 13a’s instinct to retune temporal/jitter defaults. The weakest parts are:

- 13c, which currently would not change runtime behavior at all
- 13a’s use of `N=8` math against a live default of `N=4`
- 13b’s overconfident geometric diagnosis of the remaining blur softness

If this plan is going to drive implementation, 13c should be removed or rewritten first.
