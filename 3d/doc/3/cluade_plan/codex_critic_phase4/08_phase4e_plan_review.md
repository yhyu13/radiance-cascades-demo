# Phase 4e Plan Review

Reviewed file: `doc/cluade_plan/phase4e_plan.md`  
Review date: 2026-04-24T15:12:50+08:00

## Summary

This is a reasonable cleanup/debug-polish phase, but two parts of the plan are technically weaker than they look:
- the packed-alpha decode fix is not sufficient on its own because the source value still lives in an `RGBA16F` texture
- the proposed hit-type mini-bars mix overlapping probe categories and label them like exclusive fractions

## Findings

### High: integer decode alone does not make the packed alpha channel trustworthy at `base=16`, because the packed value is still stored in `GL_RGBA16F`

Refs:
- `doc/cluade_plan/phase4e_plan.md:14`
- `doc/cluade_plan/phase4e_plan.md:36`
- `doc/cluade_plan/phase4e_plan.md:55`
- `src/demo3d.cpp:63`
- `res/shaders/radiance_3d.comp:181`
- `res/shaders/radiance_3d.comp:182`
- `src/demo3d.cpp:398`

The plan correctly identifies that the current float decode is wrong. But it assumes the rounded integer recovered from:

```cpp
int packed_int = static_cast<int>(packed + 0.5f);
```

is itself reliable at `base=16`.

That is not guaranteed here, because `packed` comes from a probe texture created as `GL_RGBA16F`, and the shader stores:

```glsl
float packedHits = float(surfaceHits) + float(skyHits) * 255.0;
```

Half-float storage does not preserve arbitrary integers exactly at this range. Once the packed value gets large enough, low-order bits are quantized away before the CPU readback ever sees them. So the modulo/division decode may be logically correct while still operating on an already-rounded packed value.

Impact:
- `base=16` is not proven safe just by switching to integer arithmetic on the CPU
- mixed `skyHits` / `surfaceHits` cases are the risky ones, because the lower-byte-like `surfHits` payload is exactly what half precision will lose first
- the current plan overstates the confidence of the new ceiling

Recommended fix:
- either keep the slider capped unless you redesign the storage format for hit metadata
- or explicitly frame `base=16` as an experiment that still needs runtime validation under `RGBA16F`
- if exact packed counts matter, store them in an actually exact representation instead of piggybacking on half-float probe alpha

### Medium: the hit-type mini-bars are described as ray fractions, but the inputs are overlapping probe fractions

Refs:
- `doc/cluade_plan/phase4e_plan.md:143`
- `doc/cluade_plan/phase4e_plan.md:148`
- `doc/cluade_plan/phase4e_plan.md:149`
- `doc/cluade_plan/phase4e_plan.md:150`
- `src/demo3d.h:573`
- `src/demo3d.h:574`
- `src/demo3d.h:575`

The proposed code computes:

```cpp
float surfF = float(probeSurfaceHit[ci]) / float(probeTotal);
float skyF  = float(probeSkyHit[ci])     / float(probeTotal);
float missF = std::max(0.0f, 1.0f - surfF - skyF);
```

But `probeSurfaceHit` and `probeSkyHit` are counts of probes with at least one hit of that type. Those sets overlap: a single probe can have both a surface-hit ray and a sky-exit ray. So:
- these are not ray fractions
- they are not mutually exclusive probe fractions
- `1 - surfF - skyF` is not a valid miss fraction in the set-theoretic sense

Impact:
- the mini-bars can visually imply a partition that does not exist
- `missF` can be understated whenever `surf` and `sky` overlap
- the legend `surf sky miss` suggests cleaner semantics than the data actually supports

Recommended fix:
- label them explicitly as `probe coverage` bars, not ray fractions
- avoid presenting them as a single partition unless you derive disjoint categories
- if you want true hit-type fractions, derive them from per-ray counts rather than per-probe any-hit flags

### Low: the mean-luminance chart range is anchored to `probeMaxLum[0]`, which can clip or distort cross-cascade comparison

Refs:
- `doc/cluade_plan/phase4e_plan.md:135`

The proposed plot range uses:

```cpp
probeMaxLum[0] * 1.5f
```

That assumes cascade 0 is a good scale reference for all cascades. If another cascade has a higher mean or higher representative range, the visualization can clip or compress comparisons in a misleading way.

Impact:
- low severity
- the chart may still render, but it can bias interpretation

Recommended fix:
- use the maximum across active cascades for the plot ceiling
- or let ImGui auto-scale via textual values only

## Recommendation

Keep 4e as a debug-polish phase, but tighten two parts before implementation:

1. Do not claim `base=16` is safe solely because of integer CPU decode while the packed value still comes from `GL_RGBA16F`.
2. Reframe the proposed mini-bars so they do not pretend overlapping probe categories are exclusive fractions.

The blend-zone annotation and the general idea of better in-panel validation are both good.

