# Phase 6a Screenshot AI Plan Review

## Verdict

The workflow idea is useful: one hotkey, save a frame, attach an artifact-focused analysis note. That could genuinely reduce the friction of comparing toggle states.

But as written, the plan has one major environment/process problem and two smaller integration problems. The biggest issue is that it assumes host Python, host package installation, and host API credentials in a repo that already tries to avoid host-tool drift and unreproducible local setup.

## Findings

### 1. High: the plan depends on host Python tooling and host API secrets, which makes the workflow brittle and violates the repo's own working agreement

The plan requires:

- `pip install anthropic`
- `ANTHROPIC_API_KEY`
- background `system("python tools/analyze_screenshot.py ...")`

Evidence:

- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:20-22`
- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:104-107`
- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:219-226`

But the repo's AGENTS guidance explicitly says:

- do not install system packages on host
- prefer containerized tooling

So this plan is not just "needs setup"; it is proposing an analysis path that is tied to a specific developer machine state and a live external API secret. That makes it poor as a project-integrated validation feature unless it is clearly framed as an optional local helper, not part of the normal branch workflow.

### 2. Medium: the plan duplicates an existing screenshot entry point instead of extending it

The current codebase already has:

- `Demo3D::takeScreenshot()`

Evidence:

- `src/demo3d.h:437`
- `src/demo3d.cpp:2842-2847`

The plan introduces a parallel path:

- `pendingScreenshot`
- `captureIfPending()`
- `launchAnalysis()`

Evidence:

- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:45-50`
- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:71-111`

That is not automatically wrong, but it is architectural drift. The cleaner shape would be to decide whether `takeScreenshot()` becomes the canonical capture path and then hang AI analysis off that, instead of adding a second screenshot mechanism with overlapping responsibility.

### 3. Medium: the screenshot capture point is good for “3D scene only,” but the plan overstates what the AI can infer from a single image

Capturing between `EndMode3D()` and `rlImGuiBegin()` is a reasonable way to exclude ImGui from the image.

Evidence:

- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:114-129`
- current main loop structure in `src/main3d.cpp:148-171`

But the plan's language slides from "artifact naming/localization" into "likely cause" diagnosis from one PNG.

Evidence:

- `doc/cluade_plan/AI/phase6a_screenshot_ai_plan.md:160-166`

For some artifact classes that is fine. For others, likely cause is underdetermined from the final frame alone. A screenshot can say "there is grid banding on the floor"; it generally cannot reliably distinguish "wrong texture unit" from "bad atlas write" from "final shader chose the isotropic path" without extra context. The prompt should be framed as heuristic visual triage, not root-cause analysis.

## Where the plan is strong

- The capture point avoids UI clutter cleanly.
- Saving both the PNG and the analysis markdown is a good audit trail.
- Running the analysis off the render thread is directionally right.

## Bottom line

This is a reasonable optional helper plan, not a trustworthy project-default validation plan.

I would revise it by:

1. framing it as an opt-in local workflow rather than core repo automation,
2. reusing `takeScreenshot()` instead of adding a second screenshot path, and
3. rewriting the analysis goal as “artifact detection and description” rather than reliable cause diagnosis.
