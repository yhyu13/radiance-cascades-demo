# Phase 8 Debug Mode Insights Review

## Verdict

This is one of the better Phase 8 notes conceptually. It captures a real and useful lesson: mode 5 and mode 7 are not measuring the same thing, and mode 5 can mislead if you treat integer step count as a continuous-field diagnostic.

The main problem is that it still overstates several conclusions, and one core technical description of mode 7 is wrong against the live shader.

## Findings

### 1. High: the mode 7 normalization description is wrong

The note says mode 7 normalizes hit distance against the cascade interval `[tNear, tFar]`.

Evidence:

- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:31-39`
- `res/shaders/raymarch.frag:445-448`

That is not what the shader does. In the live code, mode 7 uses:

```glsl
float tNorm = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.0, 1.0);
```

where `tNear` and `tFar` are the primary-ray volume entry/exit distances, not cascade bake intervals.

So the diagnostic idea is still broadly right, but the document's explanation of what is being normalized is technically incorrect.

### 2. High: "mode 0 GI banding is in the probe atlas data, not the display path" is still too strong

The note says the actual banding lives in the probe atlas data and frames that as the current understanding.

Evidence:

- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:61-99`

That is a plausible leading hypothesis, but it is still stronger than the current experiment set strictly proves. The note itself later acknowledges that:

- E1 has not yet been run,
- E4 has not yet been run,
- `probeGridTexture` is derived from the atlas,
- and bake-path influences are still entangled.

So this section should be phrased as:

- "leading remaining hypothesis"

not as a settled statement of ownership.

### 3. Medium: the "integer vs float" rule is useful, but overstated as a universal diagnostic law

The note says:

- integer output always produces discrete bands regardless of geometry smoothness
- float output is smooth unless the underlying field has discontinuities

Evidence:

- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:47-57`

That is a helpful heuristic, not a strict law. Integer-valued diagnostics can still be useful, and float-valued diagnostics can still produce visually stepped patterns depending on normalization, transfer function, and field shape.

The core lesson should be kept, but the wording should be softened so it does not read like a proof rule.

### 4. Medium: the "Nyquist bottleneck" framing is an interesting interpretation, but it is still a hypothesis dressed as a conclusion

The note says:

- the probe atlas is the Nyquist bottleneck
- the GI field frequency exceeds half the probe sampling rate

Evidence:

- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:98-100`
- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:183-190`

This is a plausible mental model, but the current branch has not actually measured the field frequency or shown the alias condition directly. E4 is still pending in the surrounding Phase 8 workflow, which means the note is jumping from circumstantial evidence to a named signal-processing diagnosis too early.

### 5. Low: the solution ranking drifts from diagnosis into roadmap speculation

The final sections rank solutions like temporal accumulation, jitter, DDGI-style visibility weighting, SH probes, and screen-space blur.

Evidence:

- `doc/cluade_plan/AI/phase8_debug_mode_insights.md:136-197`

Those are reasonable ideas, but by this point the note has shifted from "debug mode insights" into a design-roadmap document. That is not inherently bad, but it blurs:

- what the debug modes actually proved,
- what the current branch merely suspects,
- and what longer-term architectural options might be worth exploring.

It would be cleaner either to split this into two docs or mark the solutions section as speculative follow-up guidance.

## Where the note is strong

- It correctly emphasizes that mode 5 is about convergence/iteration count, not direct GI quality.
- It correctly notes that `probeGridTexture` is derived from atlas reduction rather than a separate isotropic bake.
- It correctly notes that `useDirBilinear` affects bake-side upper-cascade merge, not final `sampleDirectionalGI()`.

## Bottom line

This is a useful note, but it should be revised before being treated as the canonical Phase 8 diagnostic methodology.

I would fix it by:

1. correcting the mode 7 explanation to say it normalizes against the primary-ray volume segment, not a cascade interval,
2. downgrading "banding lives in the probe atlas data" to a leading hypothesis,
3. softening the integer-vs-float section from universal rule to practical heuristic,
4. labeling the Nyquist and solution-ranking sections as interpretive/speculative rather than proven findings.
