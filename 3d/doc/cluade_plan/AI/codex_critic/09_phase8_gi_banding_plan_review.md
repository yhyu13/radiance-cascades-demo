# Phase 8 GI Banding Plan Review

## Verdict

The high-level direction is sensible: if the remaining banding is mostly probe-side, the next step is to separate angular from spatial cascade artifacts.

The problem is that several concrete implementation steps in this plan do not match the live code. As written, it would send the next phase after the right question but through the wrong controls.

## Findings

### 1. High: E4 proposes changing `volumeResolution` to increase probe count, but probe density is controlled by `cascadeC0Res`

The plan says that if spatial probe spacing is dominant, the implementation path is to change `volumeResolution` or add a separate `probeResolution` parameter.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:42-53`
- `src/demo3d.h:658-662`
- `src/demo3d.cpp:125`
- `src/demo3d.cpp:1543-1550`
- `src/demo3d.cpp:2264-2282`

In the current branch, the cascade probe layout is driven by `cascadeC0Res`, not by `volumeResolution`.

- `volumeResolution` controls the SDF/albedo/etc. 3D texture resolution
- `cascadeC0Res` controls C0 probe resolution and therefore probe spacing

So E4 is currently wired to the wrong knob. The plan needs to make `cascadeC0Res` the primary spatial-density experiment, not `volumeResolution`.

### 2. High: E2 says `dirRes=8` is a no-code UI experiment, but there is no active `dirRes` UI control today

The plan treats higher directional resolution as a slider or default tweak with no shader work needed.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:25-36`
- `src/demo3d.cpp:114`
- `src/demo3d.cpp:2445-2449`

The branch does have a `dirRes` variable, but the visible UI in this area only shows:

- a disabled, retired `Base rays/probe` slider
- a text readout derived from `dirRes`

There is no active runtime `dirRes` slider in the current panel. So E2 is not "no code change needed" in the current build. At minimum it is:

- a constructor/default edit, or
- a new live UI control plus cascade rebuild path.

### 3. High: E5 asks whether bilinear directional merge should be enabled, but it is already ON by default

The plan says to verify whether `uUseDirBilinear` is already ON and enable it if not.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:55-61`
- `src/demo3d.cpp:118`
- `src/demo3d.cpp:2228-2241`
- `res/shaders/radiance_3d.comp:53,122,337`

That check has effectively already been answered by the code:

- `useDirBilinear` defaults to `true`
- the UI toggle already exists
- the shader already uses that path

So E5 is stale as written. The meaningful experiment now is not "turn bilinear on?" but:

- A/B `useDirBilinear` ON vs OFF to measure how much of the remaining artifact class is still bin-boundary smoothing versus deeper angular undersampling.

### 4. Medium: E2 understates the cost by framing it only as cascade bake time

The plan says `D8` means 4x cascade bake time because there are 64 vs 16 directions per probe.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:25-36`
- `res/shaders/raymarch.frag:294-356`

That is only part of the cost story in this branch. With directional GI enabled, the final renderer's `sampleDirectionalGI()` also scales strongly with `D^2`, because it integrates across the atlas bins during display. So the experiment is not just "slower rebuilds." It also risks a heavier per-pixel render path.

### 5. Medium: the whole plan inherits a slightly overstrong dependency from the Phase 7 summary

The plan depends on `phase7_findings_summary.md`, which already overstates that the remaining root cause has been narrowed to cascade GI data.

Evidence:

- `doc/cluade_plan/AI/phase8_gi_banding_plan.md:2-9`
- `doc/cluade_plan/AI/phase7_findings_summary.md:3,46-67`

That does not make the Phase 8 direction wrong. But it means the opening context should be softened to:

- "leading remaining hypothesis"

rather than presenting probe-side ownership as fully settled.

## Where the plan is strong

- E1 is a good first discriminator and really is available in the live UI.
- The overall sequencing idea, "separate angular from spatial before changing algorithms," is sound.
- E6 is correctly treated as a last resort rather than a first fix.

## Bottom line

This should be revised before implementation.

I would fix it by:

1. replacing `volumeResolution` with `cascadeC0Res` as the spatial-density control,
2. rewriting E2 to reflect that `dirRes` is not currently a live UI experiment,
3. rewriting E5 as an ON/OFF A/B on the already-enabled bilinear path,
4. adding display-path cost to the `D8` tradeoff,
5. softening the opening dependency language from "established" to "current leading hypothesis."
