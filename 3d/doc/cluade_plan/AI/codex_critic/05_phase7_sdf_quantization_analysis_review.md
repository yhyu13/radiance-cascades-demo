# Phase 7 SDF Quantization Analysis Review

## Verdict

This note identifies a real class of artifact worth investigating, but as a technical diagnosis of the current branch it is not reliable enough to drive implementation.

The main problem is that it argues from the wrong SDF-generation model. The live branch is currently using the analytic SDF path, not voxel-based jump flooding, so the document's root-cause chain overstates confidence and misattributes where the error must be coming from.

## Findings

### 1. High: the document diagnoses current artifacts as jump-flood quantization, but the live branch is using the analytic SDF path

The analysis says the current SDF values were computed by jump flooding at `64^3` and builds most of its explanation on JFA approximation error.

Evidence:

- `doc/cluade_plan/AI/phase7_sdf_quantization_analysis.md:34-50`
- `src/demo3d.cpp:985-1023`
- `res/shaders/sdf_analytic.comp:1-83`

In the current code, `sdfGenerationPass()` uses `sdf_analytic.comp` when `analyticSDFEnabled` is true, and the source comment explicitly says JFA is future work. So for the Cornell-box-style analytic scene, the live SDF is not coming from the 3D JFA shader path at all.

That means the document's claims about:

- jump-flood approximation overshoot/undershoot,
- JFA-induced non-Lipschitz error,
- and JFA as the demonstrated dominant source

are not established against the current branch.

### 2. High: "none — this is directly observable" is too strong because the step heatmap does not isolate SDF storage quantization from other raymarch effects

The comparison table says the SDF-quantization hypothesis has no evidence against it because mode 5 directly shows the issue.

Evidence:

- `doc/cluade_plan/AI/phase7_sdf_quantization_analysis.md:67-75`

Mode 5 is useful, but it is still an indirect diagnostic. It visualizes step count, not raw SDF error. Step count can be shaped by several things besides "the SDF is too coarse," including:

- the room geometry itself,
- adaptive-step dynamics near surfaces,
- hit threshold / termination behavior,
- and storage/sampling precision choices.

So mode 5 is evidence that the banding exists in the raymarch path without cascades. It is not by itself proof that the dominant cause is specifically JFA-generated voxel quantization.

### 3. Medium: the document collapses "analytic continuous SDF sampled on a 64^3 texture" into "too coarse to represent the Cornell box smoothly"

The note argues that a `64^3` SDF is too coarse and therefore the Cornell-box geometry cannot be represented smoothly.

Evidence:

- `doc/cluade_plan/AI/phase7_sdf_quantization_analysis.md:34-63`
- `res/shaders/sdf_analytic.comp:61-83`

That is too blunt. In the current implementation, the shader writes an analytic distance value at each voxel center into an `R32F` 3D texture. There can still be discretization artifacts from sampling a finite grid, but that is not the same thing as "the geometry itself is only represented approximately by jump flooding."

The distinction matters because the likely next experiments differ:

- if the problem were JFA approximation, changing the generator would matter;
- if the problem is texture resolution / sampling / threshold behavior on an analytic field, the right experiment is narrower.

### 4. Medium: the proposed fix jumps straight to raising `DEFAULT_VOLUME_RESOLUTION` without first separating display-path and bake-path sensitivity

The note recommends moving from `64` to `128` as the next action.

Evidence:

- `doc/cluade_plan/AI/phase7_sdf_quantization_analysis.md:77-99`
- `src/demo3d.h:46`

That may still be a reasonable experiment, but the document presents it as if the diagnosis already justifies it. It would be stronger if it first said:

1. mode 5 suggests the banding exists before cascade reconstruction,
2. therefore the issue is at least partly in the shared SDF/raymarch substrate,
3. next isolate whether resolution increase changes mode 5 alone before calling it the dominant final-render fix.

As written, it skips too quickly from observation to a confident root-cause ranking.

### 5. Low: some source framing is stale or imprecise even where the broad intuition is useful

- `src/demo3d.h` still describes the architecture in older JFA-oriented terms, but the implementation comment in `sdfGenerationPass()` says analytic SDF is the active path and JFA is future work.
- The line reference for mode 5 in `raymarch.frag` is brittle; line numbers will drift.

These are smaller issues, but they reinforce that the note is mixing historical architecture language with current runtime behavior.

## Where the note is useful

- It correctly recognizes that mode 5 is an important clue because it removes cascade GI from the picture.
- It is plausible that `64^3` SDF sampling contributes to the visible rectangular contouring.
- The recommendation to test a higher SDF resolution is directionally reasonable as an experiment.

## Bottom line

This should be revised before it is treated as the working root-cause document.

I would revise it by:

1. replacing the jump-flood-specific diagnosis with implementation-accurate wording about the current analytic-SDF-on-a-64^3-grid path,
2. downgrading "dominant source" to a hypothesis supported by mode-5 evidence,
3. separating "artifact exists before cascades" from "therefore SDF resolution is definitely the main cause,"
4. framing `64 -> 128` as a targeted experiment rather than as an already-justified fix.
