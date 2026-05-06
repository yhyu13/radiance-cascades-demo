# Phase 7b Anti-Banding Revised Plan Review

## Verdict

This revision is better than the first Phase 7 note. It fixes the stale shader path, acknowledges the current `D4/D8/D16/D16` baseline, and treats `blendFraction` as a UI A/B before changing defaults.

The main remaining problem is that it still overstates diagnosis. It upgrades a plausible explanation into a "confirmed hypothesis" without enough evidence, and one of its proposed code snippets is no longer mechanically safe against the live shader.

## Findings

### 1. High: "confirmed hypothesis" is still too strong for the current evidence

The revision now says the root cause is a confirmed hypothesis built around the C0->C1 directional-resolution jump plus linear blend weighting.

Evidence:

- `doc/cluade_plan/AI/phase7b_anti_banding_revised_plan.md:20-43`

This is better than the previous plan's tone, but it is still too assertive. From the current branch state, the screenshot can still plausibly reflect a mixture of:

- low C0 directional resolution,
- general probe-field quantization,
- soft-shadow bake shaping,
- and cascade-hand-off behavior.

So the smoother weighting experiment is reasonable, but the document should still present this as a leading hypothesis rather than as something already confirmed.

### 2. High: the Experiment 1 replacement snippet drops the current safety/logic guard

The live shader does not compute the blend factor unconditionally. It currently guards the expression with:

- `uHasUpperCascade != 0`
- `blendWidth > 0.0`

Evidence:

- `doc/cluade_plan/AI/phase7b_anti_banding_revised_plan.md:49-58`
- `res/shaders/radiance_3d.comp:350-351`

But the proposed replacement snippet is:

```glsl
float t = clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
float l = 1.0 - smoothstep(0.0, 1.0, t);
```

If copied literally, that loses the existing top-cascade/no-upper guard and it also reintroduces a `blendWidth == 0.0` divide path. The plan needs to show the safe replacement inside the existing conditional structure, not a simplified standalone formula.

### 3. Medium: the screenshot provenance is still assumed rather than demonstrated

The revised note says the screenshot was captured with all three quality options already enabled and treats that as confirmed baseline.

Evidence:

- `doc/cluade_plan/AI/phase7b_anti_banding_revised_plan.md:8-18`

The constructor diff does confirm those are now the defaults in `src/demo3d.cpp`, so the branch baseline is real. But that does not by itself prove this specific PNG was captured from a run that actually used those toggles. If the document wants to call that confirmed, it should cite a runtime capture note or screenshot metadata, not just current defaults.

### 4. Medium: Experiment 3 is framed more carefully now, but it still understates the design tradeoff

The revised plan is much better about cost. It now correctly calls out the jump from 128 to 512 display-side atlas fetches per pixel at C0 when moving from `D4` to `D8`.

Evidence:

- `doc/cluade_plan/AI/phase7b_anti_banding_revised_plan.md:87-118`
- `res/shaders/raymarch.frag:252-314`

What it still does not fully say is that this is not only a performance trade. It also cuts against the current branch's Phase 5e budget choice:

- keep dense near-field spatial sampling at C0,
- push extra angular budget upward instead.

So Experiment 3 is not just "expensive but maybe cleaner." It is also a strategy change that partially unwinds the current spatial-vs-angular allocation decision.

### 5. Low: the "4x angular resolution jump" wording is imprecise

The document says the C0->C1 transition crosses a "4x angular resolution jump."

Evidence:

- `doc/cluade_plan/AI/phase7b_anti_banding_revised_plan.md:22-27`

What is actually quadrupling here is total bin count:

- `D4` = 16 bins
- `D8` = 64 bins

That shorthand is understandable, but technically it is cleaner to say:

- 4x total directional bins, or
- 2x resolution per angular axis.

This is a wording issue, not a plan-breaker.

## Where the revision improved

- It corrected the shader path to `res/shaders/radiance_3d.comp`.
- It correctly recognizes that `useScaledDirRes(true)` is already baseline, so the earlier "turn it on" experiment was moot.
- It reframed `blendFraction` as a no-code A/B before changing defaults.
- It is much more honest than the first draft about the real cost of `dirRes(8)`.
- It demoted dither to a last-resort masking step, which is the right framing.

## Bottom line

This revised plan is closer to implementation-ready, but I would still revise it once more before treating it as the working plan.

The needed fixes are:

1. change "confirmed hypothesis" back to "leading hypothesis",
2. correct Experiment 1 so the `smoothstep` version preserves the current `uHasUpperCascade` / `blendWidth > 0.0` guard,
3. avoid treating the specific screenshot baseline as proven unless there is capture provenance,
4. describe `dirRes(8)` as a strategy change as well as a performance cost.
