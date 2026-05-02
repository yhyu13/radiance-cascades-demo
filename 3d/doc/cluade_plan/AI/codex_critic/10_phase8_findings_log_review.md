# Phase 8 Findings Log Review

## Verdict

This log is better grounded in the live code than the earlier Phase 7 notes. The Phase 8 `dirRes` slider is real, and the bake-step / normal-epsilon shader edits it mentions are also real.

The main remaining issue is that the document upgrades runtime observations into a stronger root-cause story than the evidence supports, and one of its interval explanations is simply wrong against the current cascade math.

## Findings

### 1. High: the C0/C1 interval explanation uses incorrect numbers

The log says:

- C0: `0–12.5 cm`
- C1: `12.5–50 cm`

but the actual rendered text in the file is corrupted and internally inconsistent, and the underlying explanation needs to match the live cascade math exactly.

Evidence:

- `doc/cluade_plan/AI/phase8_findings_log.md:49-51`
- `src/demo3d.cpp:1543-1550`

For the current `cascadeC0Res = 32` and `volumeSize.x = 4`, the code uses:

- C0: `[0, 0.125]` meters = `0–12.5 cm`
- C1: `[0.125, 0.5]` meters = `12.5–50 cm`

So the intended values are fine, but the file as written is damaged enough that this section is not trustworthy as a status note unless it is cleaned up.

### 2. High: the "pattern geometry matches Cornell Box SDF exactly" conclusion is too strong

The log says the rectangular contour pattern matches Cornell Box SDF iso-distance geometry exactly, and then builds much of the revised explanation on that match.

Evidence:

- `doc/cluade_plan/AI/phase8_findings_log.md:18-24`
- `doc/cluade_plan/AI/phase8_findings_log.md:41-47`

That is an overreach. The scene is lit by a point light and the visible banding is in reconstructed indirect GI, not in a direct visualization of the box SDF itself. Rectangular symmetry is a plausible clue, but "matches the SDF exactly" is stronger than the available evidence justifies.

### 3. Medium: the log presents the new leading explanation as if it were already established, but its own table still says E4 is needed to confirm it

The document says the banding is intrinsic to the GI radiance distribution sampled at current probe density, then later ranks that as the leading hypothesis and explicitly says E4 is still needed.

Evidence:

- `doc/cluade_plan/AI/phase8_findings_log.md:37-52`
- `doc/cluade_plan/AI/phase8_findings_log.md:59-64`

Those two levels of confidence do not match. The safer wording is:

- "leading current hypothesis"

not:

- "revised root cause understanding"

as if the cause were already confirmed.

### 4. Medium: the file correctly says the bake-step hypothesis was eliminated, but the live shader comments still claim that hypothesis as fact

The log says the reduced minimum-step experiment did not fix the banding and therefore the step-snapping hypothesis was wrong.

Evidence:

- `doc/cluade_plan/AI/phase8_findings_log.md:26-35`
- `res/shaders/radiance_3d.comp:252-255`

The problem is that the current shader comments still say the opposite: they describe the `0.01 -> 0.001` change as if the earlier minimum step had caused the banding. That means the log and code comments now disagree.

This is partly a code-hygiene issue, but it matters for the document review because the note presents itself as the new canonical finding.

### 5. Low: the visual findings are runtime observations, not code-verifiable facts

Claims like:

- "C0 and C1 show the worst banding"
- "bands are densest near the ceiling light"
- "pattern unchanged"

may all be true, but they are screenshot observations rather than code-proven invariants.

That does not make them invalid. It just means the note should label them as runtime evidence more explicitly.

## Where the log is strong

- It correctly records that Phase 8 added a live `dirRes` rebuild path.
- It correctly records that the minimum-step and epsilon edits were really made in `radiance_3d.comp`.
- It correctly keeps E4 as the next needed confirmation step rather than claiming everything is already settled.

## Bottom line

This is a useful working log, but it should be revised before being treated as the canonical Phase 8 conclusion.

I would fix it by:

1. cleaning up the corrupted C0/C1 interval text and matching it to `initCascades()` exactly,
2. downgrading "matches SDF exactly" to a visual correlation or leading interpretation,
3. softening the root-cause section from "understanding" to "current leading hypothesis",
4. updating the stale Phase 8 comments in `radiance_3d.comp` so the code no longer contradicts the log.
