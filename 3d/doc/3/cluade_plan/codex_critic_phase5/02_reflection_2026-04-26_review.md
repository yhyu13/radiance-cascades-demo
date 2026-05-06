# Reflection 2026-04-26 Authenticity Review

## Verdict

The reflection is mostly authentic. It correctly names the current Phase 5a boundary, acknowledges that the merge is still isotropic, and explicitly admits some real debt.

But it is not fully honest in a few places. The main issue is not fabrication; it is overstatement. Several claims are stronger than the current evidence supports.

## Findings

### 1. High: "the only gap that prevents visual similarity to ShaderToy is the isotropic merge" is too strong

The reflection makes this claim twice:
- "After Phase 5a, the only gap that prevents visual similarity to ShaderToy is the isotropic merge."
- "the isotropic `upperSample` is the last major blocker."

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:107`
- `doc/cluade_plan/reflection_2026-04-26.md:189`

That is not fully honest relative to the current code state. Phase 5a replaced per-cascade ray scaling with a fixed `D^2` loop:
- compute shader now traces `uDirRes * uDirRes` rays for every cascade
- current default is `D=4`, so all cascades run 16 rays

Evidence:
- `res/shaders/radiance_3d.comp:188-211`
- `src/demo3d.cpp:923`
- `src/demo3d.h:662`

This means upper-cascade angular budget has already been reduced relative to Phase 4's `C3=64`, and there is still no runtime A/B recorded proving that this regression is visually harmless. The earlier Codex Phase 5 review explicitly called this out as a high-risk tradeoff, and the reflection itself admits runtime validation was not run.

Evidence:
- `doc/cluade_plan/codex_critic_phase5/01_phase5_plan_review.md`
- `doc/cluade_plan/reflection_2026-04-26.md:136`

More honest wording would be:
- isotropic merge is the main remaining correctness gap
- fixed `D=4` is still an unresolved quality-risk until validated

### 2. Medium: "Everything committed is working" overstates the state of the UI/debug path

The reflection says:
- "Everything committed is working."

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:47`

But the same document also admits:
- the `baseRaysPerProbe` slider is cosmetically non-functional

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:73`
- `doc/cluade_plan/reflection_2026-04-26.md:85`

And the live UI still presents those stale ray counts as if they are active:
- the slider still shows `C0..C3` counts derived from `baseRaysPerProbe`
- probe-fill UI still says `r=N shows actual raysPerProbe for that cascade level`
- the radiance debug shader still receives `uRaysPerProbe` from the C++ side

Evidence:
- `src/demo3d.cpp:1985-1991`
- `src/demo3d.cpp:2065-2079`
- `src/demo3d.cpp:694`

So this is not a "broken build" issue, but it is more than harmless cosmetic drift. The debug/UI path is currently misleading. "Everything committed builds and the core path runs" would be honest. "Everything committed is working" is too broad.

### 3. Medium: the reflection marks Phase 5a "Done" while also admitting runtime validation was never performed

The summary table marks:
- `5a ... Done`
- with `0 errors, 0 new warnings`

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:41`

Later, the same reflection says:
- "Phase 5a visual validation not run."
- no result was recorded for image equivalence vs Phase 4 baseline

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:136`

That is a real honesty mismatch. Compile-clean is true. "Done" is stronger than the evidence currently supports if the intended meaning is "validated implementation complete." The accurate label is closer to:
- implemented and compile-verified
- runtime-equivalence still pending

### 4. Low: the recorded "Head commit" is stale relative to the file that contains it

The reflection says:
- `Head commit: ccb2934`

Evidence:
- `doc/cluade_plan/reflection_2026-04-26.md:5`

But the current history shows the reflection itself was committed later in:
- `f184afa [Claude] Phase 5a reflection: project status review + PLAN.md updated`

Evidence:
- local git log at review time

This is not a technical honesty problem about the renderer, but it is imprecise metadata. If the file is meant to be a snapshot "as of ccb2934", it should say that explicitly instead of "Head commit".

## Where the reflection is genuinely honest

These parts are candid and accurate:
- it plainly states the merge is still isotropic
- it correctly identifies `raymarch.frag` as still consuming `probeGridTexture`
- it names the non-functional slider as tech debt instead of pretending it is active
- it admits Phase 5a visual validation was not run
- it correctly elevates atlas sampling/filter semantics and barrier ordering as load-bearing for Phase 5b

## Bottom line

The document is mostly authentic, but it is not fully honest in its calibration language.

The main places to tighten are:
- replace "only gap" / "last major blocker" with "main remaining blocker"
- replace "Everything committed is working" with a narrower claim
- replace `5a Done` with `implemented, compile-verified, runtime validation pending`
- fix or clarify the stale `Head commit` field
