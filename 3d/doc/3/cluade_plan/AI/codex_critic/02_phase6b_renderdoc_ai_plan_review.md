# Phase 6b RenderDoc AI Plan Review

## Verdict

The high-level direction is right: if the problem is "the final frame looks wrong but we do not know which pass is wrong," then a GPU capture is the right class of tool.

But the current plan dramatically overstates how stage-aware its analysis would be. The proposed Python script is not actually analyzing pipeline stages in a reliable RenderDoc sense; it is mostly scanning for resource names and grabbing one texture slice. That is a much weaker tool than the plan title suggests.

## Findings

### 1. High: the proposed analysis is not truly per-stage or per-pass, because it does not walk the frame events or bound outputs

The plan headline promises:

- “extract each pipeline stage texture”
- “automated per-stage analysis”

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:4-5`
- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:17-20`

But the script mostly does:

- `controller.GetResources()`
- name-fragment matching
- `controller.GetTexture(res.resourceId)`
- save one middle Z slice

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:248-253`
- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:275-289`

That is not the same as identifying:

- which dispatch wrote a texture,
- what the resource looked like immediately after that dispatch,
- or which pass actually consumed the wrong input.

Without traversing draw/dispatch events and reading the bound outputs at the relevant event, this is not really “pipeline stage analysis.” It is “resource snapshot analysis.”

### 2. High: the resource-name strategy is likely to fail unless the app explicitly labels GL objects, which the plan does not include

The analysis script expects resources named like:

- `sdfTexture`
- `albedoTexture`
- `probeAtlasTexture`
- `probeGridTexture`

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:184-203`

But the current codebase does not label OpenGL objects for RenderDoc with those names. In the live branch, textures are created and bound, but there is no corresponding object-labeling plan here.

So the likely real-world outcome is that RenderDoc exposes generic texture names, and:

- `find_last_use(controller, name_fragment)` returns nothing,
- or returns an arbitrary wrong match.

This is the single biggest technical gap in the proposal after the event-model issue.

### 3. Medium: the “final frame” extraction path is unsafe and not actually anchored to the swapchain output

The script uses:

- `controller.SetFrameEvent(controller.GetDrawcalls()[-1].eventId, True)`
- `controller.GetTextures()[0].resourceId`

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:291-297`

That is a weak assumption. Texture index 0 is not a trustworthy way to identify the backbuffer or the final presented image. Even if it works on one capture, it is not a robust basis for automation.

### 4. Medium: the C++ plan leaks internal capture state into `main3d.cpp`

The main-loop snippet directly reads:

- `demo->pendingRdocCapture`
- `demo->rdoc`

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:131-159`

That may compile only if those fields are made public. As a design shape, it is weaker than giving `Demo3D` one explicit method like:

- `beginPendingGpuCapture()`
- `endPendingGpuCaptureAndAnalyze()`

This is smaller than the resource-analysis issues above, but it is still a sign that the plan has not been fully tightened into this codebase's structure.

### 5. Medium: like 6a, this plan also assumes host tools, host Python environment, and network/API access

The plan requires:

- RenderDoc installed at a hard-coded host path
- RenderDoc's Python environment
- Anthropic API credentials

Evidence:

- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:25-29`
- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:333-340`
- `doc/cluade_plan/AI/phase6b_renderdoc_ai_plan.md:175-179`

That makes it useful as a local expert workflow, but not a robust default branch feature.

## Where the plan is strong

- Using RenderDoc at all is the right escalation from screenshot-only diagnosis.
- The in-process hotkey capture idea is practical.
- Sharing the same output area and markdown-report convention as 6a is sensible.

## Bottom line

This plan should be revised before implementation.

I would fix it by:

1. downgrading the claim from “per-stage pipeline analysis” to “resource/capture-assisted analysis” unless event-walking is added,
2. adding an explicit GL object-labeling plan so RenderDoc can find the intended resources,
3. replacing the backbuffer shortcut with a real final-output selection method, and
4. keeping the whole workflow clearly optional and host-local rather than presenting it as core validation infrastructure.
