# Phase 7 Findings Summary Review

## Verdict

This summary is stronger than the earlier Phase 7 notes because it no longer blames JFA and it correctly records several code changes that now exist.

Its main weakness is still overconfidence. It compresses a set of useful diagnostic observations into a stronger architectural conclusion than the current evidence fully justifies.

## Findings

### 1. High: "root cause narrowed to cascade GI data" is still too strong

The summary says the experiments are complete and that the root cause has been narrowed to cascade GI data rather than display-path raymarching.

Evidence:

- `doc/cluade_plan/AI/phase7_findings_summary.md:3`
- `doc/cluade_plan/AI/phase7_findings_summary.md:46-67`

That is directionally plausible, but it is phrased too strongly.

What the experiments actually support is narrower:

- the final display-path SDF sample used by `raymarch.frag` is probably not the dominant cause of the visible mode-0 banding,
- and mode 5 was a misleading SDF diagnostic.

What they do **not** fully prove is that the remaining issue is purely "cascade GI data" in the sense of only atlas/bin/probe quantization. The bake pass still depends on the SDF texture path, and the note does not show a direct experiment isolating bake-path sensitivity from directional/spatial probe quantization inside the cascade output.

### 2. Medium: the analytic-toggle conclusion is broader than the toggle really proves

The summary says the analytic toggle proves the banding is not caused by trilinear interpolation of the SDF texture.

Evidence:

- `doc/cluade_plan/AI/phase7_findings_summary.md:17-27`
- `src/demo3d.cpp:1275-1279`
- `res/shaders/raymarch.frag:216-221`

That is true for the **display-path SDF read** being A/B tested. But the note should be more explicit that:

- the GI bake still uses the texture SDF path in `radiance_3d.comp`,
- albedo still comes from the volume texture,
- so this does not eliminate every SDF-related influence on the final indirect field.

The distinction matters because the summary is trying to serve as a handoff to Phase 8.

### 3. Medium: the D4 angular explanation uses oversimplified angle math

The sub-hypothesis table says:

- `16 directions × 22.5°/bin`

Evidence:

- `doc/cluade_plan/AI/phase7_findings_summary.md:54-57`

That is not a clean description of the current octahedral-bin scheme. `D4` means `4 x 4 = 16` bins on the octahedral parameterization, but those bins are not best explained as uniform `22.5°` directional sectors in a simple angular sense.

This is a wording/teaching issue more than a branch blocker, but the summary is supposed to be the stable takeaway note, so it should avoid oversimplified geometry claims.

### 4. Low: "experiments complete" is slightly misleading because one listed item is still explicitly pending

The document says experiments are complete, but it also says the visual comparison for the smoothstep blend change is still pending.

Evidence:

- `doc/cluade_plan/AI/phase7_findings_summary.md:3`
- `doc/cluade_plan/AI/phase7_findings_summary.md:60-62`

That is a small consistency issue. If one part of the interpretation still needs A/B validation, "complete" should probably be softened to something like "main diagnostics complete" or "current findings summary."

## Where the note is strong

- It correctly records that `DEFAULT_VOLUME_RESOLUTION` is now `128`.
- It correctly records that the analytic SDF toggle and mode 7 exist in the live code.
- It correctly retires mode 5 as a trustworthy SDF-quality diagnostic.

## Bottom line

This is a useful summary, but it should be revised once more before it is treated as the canonical Phase 7 conclusion.

I would fix it by:

1. changing "root cause narrowed to cascade GI data" to "leading remaining hypothesis is cascade-side quantization,"
2. explicitly separating display-path SDF elimination from bake-path SDF influence,
3. removing the `22.5°/bin` shorthand,
4. softening "experiments complete" if the smoothstep A/B is still pending.
