# Phase 9b Implementation Detail Review 2

## Verdict

This note is materially better than the earlier Phase 9b writeup. The biggest prior issue around the "rebuild counter" semantics has already been corrected in the live code: there is now a distinct `temporalRebuildCount`, and the UI text now says `EMA fill` rather than pretending to be a precise history state.

The remaining issues are smaller. The document still overstates a few interpretations and has one stale reference path.

## Findings

### 1. Medium: the motivation cites a stale/nonexistent source path

The note says it was motivated by:

- `phase9_banding_critic.md`

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:4`

That is not the current canonical local filename from this review trail. The relevant Codex self-critique is:

- `doc/cluade_plan/AI/codex_critic/16_phase9_self_critique.md`

This is a documentation-hygiene issue, but the file should point at the actual source if it is meant to serve as a durable implementation record.

### 2. Medium: the D=8 explanation still overcompresses octahedral bins into simple degree steps

The note says:

- D=8 halves directional bin angular step from ~45° to ~22.5°

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:57-76`

That is still the same oversimplified geometry language seen in earlier notes. The practical conclusion is fine:

- raising `D` reduces directional discretization

But octahedral bins are not well described as uniform angular wedges with one fixed degree step.

### 3. Medium: the startup-bias section is accurate about brightness, but "converging downward toward the spatially-averaged asymptote" is stronger than the current method really proves

The note says that after seeding, EMA accumulates from full brightness and converges downward toward the spatially averaged asymptote.

Evidence:

- `doc/cluade_plan/AI/phase9b_impl_detail.md:93-123`

The seeding part is accurate. The stronger issue is the phrase "spatially-averaged asymptote," which quietly assumes the current jittered EMA converges to the right filtered field in the useful sense.

Given the Phase 9 self-critique, that should probably be softened to:

- "the jittered EMA asymptote for the current kernel"

because that asymptote may still preserve broad contouring and is not equivalent to a ground-truth spatial average.

### 4. Low: the observability section is now much more honest, but it is still telemetry rather than direct visualization

The live code now shows:

- `Rebuilds`
- `EMA fill`
- `Jitter`

Evidence:

- `src/demo3d.cpp:2625-2633`

That is useful, and the note is mostly fair about it. The only remaining caveat is that this is still not true debug visualization of:

- history textures
- current-vs-history residual
- or accumulation quality

So if the file is trying to sell Step 1 as "debug observability fixed," that claim should stay modest.

## Where the note is strong

- It matches the live code on `dirRes(8)`.
- It matches the live code on history seeding via `glCopyImageSubData`.
- It matches the live code on Halton jitter and the split between `probeJitterIndex` and `temporalRebuildCount`.
- It matches the live UI wording much better than the earlier draft.

## Bottom line

This is mostly accurate now.

I would only revise:

1. the stale `phase9_banding_critic.md` reference,
2. the oversimplified D=4 vs D=8 angle wording,
3. the asymptote wording in the startup-bias section so it does not overclaim what the current jitter kernel converges to.
