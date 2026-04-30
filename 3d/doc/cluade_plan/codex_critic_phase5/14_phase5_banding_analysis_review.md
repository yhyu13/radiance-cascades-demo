# Phase 5 Banding Analysis Review

## Verdict

This is a useful analysis note overall. Its strongest point is that it correctly separates several different artifact sources instead of pretending all Phase 5 banding comes from one bug.

But it still has three important problems:

1. one section is already stale with respect to the implemented Phase 5e scaling,
2. it overstates the "zero-cost pure upgrade" case for soft shadows in the direct term, and
3. it sometimes slides from "reduce visible banding" into "physically correct shadow improvement" without keeping those two goals separate.

## Findings

### 1. High: the Phase 5e section is stale and contradicts the current implementation history

The file says:

- "Phase 5e (already in the plan) adds per-cascade D scaling: C0=D2, C1=D4, C2=D8, C3=D16"

Evidence:

- `doc/cluade_plan/phase5_banding_analysis.md:205-213`

That is no longer the live branch state. Current code already implements the revised safe scaling:

- `C0=D4`
- `C1=D8`
- `C2=D16`
- `C3=D16`

Evidence:

- `src/demo3d.cpp:1466-1470`
- `src/demo3d.cpp:2277-2287`

And the branch documentation already records why `D2` was rejected as degenerate.

So this section should not describe the abandoned `D2/D4/D8/D16` path as if it were still the active Phase 5e plan.

### 2. Medium: Strategy A is oversold as a "pure upgrade" and "zero cost increase"

The note recommends replacing the binary direct shadow with IQ-style SDF cone soft shadow and says:

- same 32 march iterations
- zero extra texture reads
- pure upgrade

Evidence:

- `doc/cluade_plan/phase5_banding_analysis.md:77-105`

It is fair to say this is similar computational shape, but "pure upgrade" is too strong.

Reasons:

- it is not physically the same as the current point-light shadow model,
- it intentionally introduces a soft edge without changing the light into an area light,
- and its visual tuning depends on an artistic parameter `k`, not only scene geometry.

So this is a potentially useful approximation for hiding banding, but the document should present it as that, not as a strict quality upgrade with no tradeoff.

### 3. Medium: the analysis sometimes conflates "fix direct shadow appearance" with "fix probe-baked indirect banding"

The early sections do a good job of separating:

- direct shadow in the final renderer,
- indirect GI banding from the probes,
- binary shadow inside the bake,
- directional resolution limits

Evidence:

- `doc/cluade_plan/phase5_banding_analysis.md:26-72`

But the priority order then makes Strategy A the top global recommendation:

- soft shadow in the direct term

Evidence:

- `doc/cluade_plan/phase5_banding_analysis.md:217-230`

That definitely improves the final image, but it does not actually solve the central RC-specific issue documented in Sources 2 and 3:

- the probe-baked signal near shadow boundaries is still discrete and binary

So Strategy A is better described as "best immediate visual improvement" than "best next step for Phase 5 banding" in general. If the document wants to stay technically sharp, Strategies B and C are more directly tied to the RC-side artifact sources it spent most of the analysis establishing.

### 4. Low: the opening context assumes 5g + 5h are active in mode 0, but that is not the default branch state

The file opens with:

- "After Phase 5g + 5h are active in mode 0, the main remaining visual defect is banding"

Evidence:

- `doc/cluade_plan/phase5_banding_analysis.md:5-7`

That is a reasonable analysis frame, but it is not the out-of-the-box runtime default. Current constructor defaults are:

- `useShadowRay(true)`
- `useDirectionalGI(false)`
- `useCascadeGI(false)`

Evidence:

- `src/demo3d.cpp:100-113`

So this should read as a conditional evaluation setup, not as the general project state.

## Where the document is strong

- The four-source breakdown is much better than blaming everything on one knob.
- The note is right that direct lighting inside the probes is not inherently a semantic bug; it is consistent with one-bounce transport.
- It correctly identifies that spatial interpolation cannot recover detail below the C0 probe spacing.

## Bottom line

This is a worthwhile analysis note, but I would revise it before treating it as the current Phase 5 quality roadmap.

I would fix it by:

1. updating the Phase 5e section to match the implemented `D4/D8/D16/D16` path,
2. reframing Strategy A as an approximation for direct-shadow appearance rather than a pure no-tradeoff upgrade, and
3. separating "best immediate visual win" from "most direct fix for RC-side banding sources."
