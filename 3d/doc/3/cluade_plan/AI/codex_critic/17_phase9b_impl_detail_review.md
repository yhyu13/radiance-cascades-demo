# Phase 9b Implementation Detail Review

## Verdict

This note is mostly aligned with the live code. The Phase 9b changes are real:

- `dirRes` now defaults to `8`
- history seeding exists
- Halton jitter exists
- UI readouts exist

The main remaining issue is that the new observability section overstates what the counters mean, and the document still makes the Phase 9b instrumentation sound stronger than it really is.

## Findings

### 1. High: `probeJitterIndex` is not a general rebuild counter in the live code

The note says:

- `Rebuilds:` is the total probe-grid rebuild count since temporal was enabled
- and that it is the same counter as the Halton sequence index

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:35-47`
- `src/demo3d.cpp:1086-1095`
- `src/demo3d.cpp:2620-2621`

In the actual code, `probeJitterIndex` only increments when `useProbeJitter` is true:

```cpp
if (useProbeJitter) {
    ...
    ++probeJitterIndex;
} else {
    ...
    probeJitterIndex = 0;
}
```

So it is **not** a general rebuild counter for temporal accumulation.

Implications:

- if temporal is ON but jitter is OFF, the UI readout stays at `0`
- `Coverage:` also stays at `0%`
- even though history may already be seeded and valid

That makes the current wording in the doc misleading.

### 2. Medium: the `Coverage` formula is mathematically fine, but the implementation only tracks jitter-sample count, not actual EMA history state

The note explains coverage as:

- `1 - (1 - alpha)^N`

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:43-52`
- `src/demo3d.cpp:2620`

That formula is reasonable as an EMA settling heuristic, but in the live code `N` is currently:

- Halton index / jitter sample count

not:

- actual number of temporal blend updates applied to history in all modes

So the readout is better described as:

- "jittered-sample coverage estimate"

not:

- a true history-fill meter.

### 3. Medium: the note presents "debug observability" more strongly than the UI really provides

The doc says Step 1 fixes debug observability.

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:10-17`
- `src/demo3d.cpp:2601-2624`

What Phase 9b actually added is:

- text readouts for rebuild-like count, coverage estimate, and jitter vector

What it still does **not** add is:

- a history texture viewer
- a current-vs-history comparison view
- a residual/error view
- a reset-history button

So the observability improvement is real, but still lightweight. It is UI telemetry, not true history visualization.

### 4. Medium: the D=8 explanation still overcompresses octahedral directional resolution into simple angle-step language

The note says:

- D=4 is `~45°`
- D=8 is `~22.5°`

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:57-76`

That is the same oversimplification seen in earlier notes. `D x D` octahedral bins are not best modeled as uniform angular wedges with one simple degree-per-bin step.

The practical claim "higher D reduces directional discretization" is correct.
The exact geometric explanation is too simplified.

### 5. Low: the Phase 9b note still does not explicitly say there is no new debug visualization mode

The file is much better than the earlier Phase 9 note, but since it emphasizes observability it should say directly:

- no new render mode was added
- no radiance-debug history view was added

That would prevent readers from expecting more than the current text readouts actually provide.

## Answer to the implied UI/debug-vis question

Phase 9b adds:

- GUI readouts
  - `Rebuilds`
  - `Coverage`
  - current `Jitter` vector

Phase 9b does **not** add:

- new debug render modes
- history-atlas or history-grid visualization
- reset-history button

So this is improved observability, but still not true debug visualization.

## Bottom line

This is a mostly accurate implementation note, but I would revise it before treating it as the canonical Phase 9b status record.

The main fixes are:

1. stop calling `probeJitterIndex` a general rebuild counter,
2. relabel `Coverage` as a jittered-EMA heuristic rather than a true history-fill metric,
3. explicitly say that observability improved only through text UI readouts, not through new visual debug modes.
