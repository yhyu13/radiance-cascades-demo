# Phase 7 Implementation Status Review

## Verdict

This status note is partly accurate on what has changed in code, but it is not reliable as a current technical status summary because it inherits the same overconfident SDF diagnosis as the companion analysis doc.

The strongest section is the implementation record for the defaults and the `smoothstep` blend change. The weakest section is the root-cause update, which presents a speculative diagnosis as settled fact.

## Findings

### 1. High: the root-cause section treats the companion SDF analysis as established fact, but that analysis is not implementation-accurate

The status note says the dominant source of banding has been revised to SDF voxel quantization at `64^3` and points to the analysis doc for details.

Evidence:

- `doc/cluade_plan/AI/phase7_impl_status.md:46-60`
- `doc/cluade_plan/AI/phase7_sdf_quantization_analysis.md:1-99`
- `src/demo3d.cpp:985-1023`

That is too strong for a status document because the linked analysis itself is built on a stale JFA assumption. The current branch uses the analytic SDF path for these scenes, so this status note should not promote that diagnosis to "revised root cause" without qualification.

### 2. Medium: "these match the configuration under which the visual triage screenshots were captured" is still a provenance claim, not a code-proven fact

The defaults table is correct against the current constructor diff:

- `useColocatedCascades = false`
- `useScaledDirRes = true`
- `useDirectionalGI = true`

Evidence:

- `doc/cluade_plan/AI/phase7_impl_status.md:10-20`
- `src/demo3d.cpp:116-121`

But the extra sentence claiming that these match the screenshot capture configuration is not something the code proves. That should be phrased as an assumed baseline unless there is explicit capture metadata.

### 3. Medium: the status note says Experiment 1 is only a partial improvement, but it still implies stronger validation than the current evidence shows

The implementation description for the `smoothstep` change is technically correct and matches the live shader.

Evidence:

- `doc/cluade_plan/AI/phase7_impl_status.md:24-42`
- `res/shaders/radiance_3d.comp:350-354`

The weaker point is the explanatory sentence that says this softens the cascade-boundary contribution to the contour banding but not the dominant SDF cause. The first half is a plausible intent statement; the second half again leans on an unsettled diagnosis.

### 4. Low: "not yet planned" is slightly misleading for the SDF-resolution fix

The root-cause table says the `64^3` SDF issue is addressed by increasing SDF resolution and labels that as "not yet planned."

Evidence:

- `doc/cluade_plan/AI/phase7_impl_status.md:54-60`

That wording is awkward because the same Phase 7 analysis doc is already proposing exactly that as the next action. A clearer phrasing would be:

- "not yet implemented"
- or "proposed next experiment"

This is a status-hygiene issue, not a core technical flaw.

## Where the note is solid

- The constructor-default changes are accurately recorded.
- The `smoothstep` shader change is accurately recorded and preserves the live guard conditions.
- The pending sections correctly say `blendFraction` and `dirRes(8)` have not yet been applied.

## Bottom line

This is usable as a narrow changelog for what was implemented, but not yet as a trustworthy Phase 7 status document.

I would revise it by:

1. keeping the defaults and `smoothstep` sections mostly as-is,
2. softening the SDF root-cause section to "current leading hypothesis,"
3. removing or qualifying the screenshot-provenance claim,
4. changing "not yet planned" to "proposed next experiment" or "not yet implemented."
