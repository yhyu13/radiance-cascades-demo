# Phase 8 GI Banding Plan Review 2

## Verdict

This revision is much better than the previous Phase 8 plan. It fixed the earlier control-mapping mistakes around `cascadeC0Res`, the live `dirRes` slider, and the already-enabled bilinear path.

The remaining issue is more subtle: a few experiments still claim to isolate causes more cleanly than they really do in the live pipeline.

## Findings

### 1. High: E1 does not cleanly isolate "angular vs spatial" because the isotropic path is still derived from the directional bake output

The plan says turning Directional GI OFF isolates angular vs spatial by switching from the directional atlas to the isotropic probe grid.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:20-31`
- `src/demo3d.cpp:1144-1156`
- `res/shaders/raymarch.frag:458-462,494-495`

The problem is that `probeGridTexture` is not an independent non-directional bake. It is produced by reducing the directional atlas after the bake pass. So E1 can tell you:

- how much the **final display lookup** depends on directional atlas sampling versus isotropic reduction

but not cleanly:

- whether the underlying bake artifact is fundamentally angular or spatial.

That makes E1 a useful discriminator, but a weaker one than the plan currently implies.

### 2. High: E5 overstates what `useDirBilinear` proves, because that toggle affects upper-cascade merge in the bake shader, not final C0 directional lookup in `raymarch.frag`

The plan frames E5 as separating "too few bins" from "hard bin boundaries" for the angular artifact source.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:65-76`
- `res/shaders/radiance_3d.comp:53,122-145,327-342`
- `res/shaders/raymarch.frag:294-356`

In the live code, `useDirBilinear` is consumed in `radiance_3d.comp` when reading the **upper cascade** during bake/merge. It does **not** control the final `sampleDirectionalGI()` integration path in `raymarch.frag`.

So E5 is not a general "angular smoothness" A/B. It is specifically an A/B on upper-cascade directional merge smoothness. That may still matter, but the interpretation table is currently too broad.

### 3. Medium: E2 still compresses several different costs into one simple 4x table

The revised plan improved this a lot by including bake, display, and atlas-memory costs.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:35-46`

The remaining nuance is that with D-scaling ON, the cost change is not perfectly summarized by a single "D4 -> D8 everywhere" mental model. The effect depends on which cascades hit the `min(16, dirRes << i)` cap.

That does not make the table wrong as a high-level warning. It just means the doc should present it as an approximate first-order cost model rather than exact per-frame accounting.

### 4. Low: the Phase 8 "Files that changed" section is narrower than the live branch state

The plan says Phase 8 changed:

- `src/demo3d.cpp` for the live `dirRes` slider

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:100-107`
- `res/shaders/radiance_3d.comp:185,203-205,231-256`

But Phase 8 already also changed `radiance_3d.comp` for the minimum-step and epsilon experiments, as reflected in the findings log. If this plan is meant to be a live phase-status note, that section is now underspecified.

## Where the plan is strong

- It fixed the earlier `volumeResolution` vs `cascadeC0Res` mistake.
- It correctly recognizes that `useDirBilinear` is already ON and should be A/B tested, not newly enabled.
- It correctly records that the `dirRes` slider now exists.
- The zero-code-first sequencing is pragmatic.

## Bottom line

This revised plan is close to workable, but I would still revise it once more before treating it as the canonical Phase 8 execution plan.

I would fix it by:

1. reframing E1 as "display lookup directional vs isotropic reduction" rather than a clean angular-vs-spatial separator,
2. narrowing E5 to "upper-cascade merge bilinear A/B" instead of general angular smoothing,
3. labeling the E2 cost table as approximate under current D-scaling,
4. updating the phase-change inventory if the document is also serving as a live status note.
