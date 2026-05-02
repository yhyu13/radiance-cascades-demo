# Phase 9 Temporal Dark Bugfix Review

## Verdict

This note is mostly accurate against the live code. The key diagnosis is real: when Phase 9 first switched the display path to history textures, there was no rebuild trigger on `useTemporalAccum`, so the renderer could read zero-initialized history and show dark indirect GI.

The main correction is scope: the document's "Bug 2" is not really the same bug as the "fully dark" symptom. It is a separate effectiveness/design issue about whether temporal+jitter ever gets enough rebuilds to converge.

## Findings

### 1. Medium: Bug 1 explains the dark-screen symptom; Bug 2 is a different class of problem

The note presents two bugs under one root-cause heading:

- no rebuild on temporal toggle
- no continuous rebuild for jitter accumulation

Evidence:

- `doc/cluade_plan/AI/phase9_temporal_dark_bugfix.md:16-44`
- `src/demo3d.cpp:507-524`

Those are related, but they are not the same failure mode.

- **Bug 1** explains why GI could stay black: the display switched to history before history had ever been populated.
- **Bug 2** explains why temporal+jitter would fail to improve banding over time in a static scene: not enough rebuilds would occur.

So the document is strongest if it says:

- Bug 1 = dark-history read bug
- Bug 2 = temporal-convergence/control-flow bug

rather than framing both as the direct root cause of the same symptom.

### 2. Medium: the "What was NOT the bug" table overstates one eliminated candidate

The table says the `GL_READ_ONLY` / `readonly` mismatch is eliminated because drivers allow it in practice.

Evidence:

- `doc/cluade_plan/AI/phase9_temporal_dark_bugfix.md:95-102`
- `res/shaders/temporal_blend.comp:24-36`

That is too strong. It may not be the current dark-GI cause, but it is not really "eliminated" in a rigorous sense if the justification is "drivers allow it." That is better described as:

- probably not the observed bug in this branch,
- but still worth cleaning up for correctness/spec hygiene.

### 3. Low: the note is accurate about the fix, but it should more clearly separate symptom verification from code reasoning

The document combines:

- RenderDoc/image-based symptom confirmation,
- control-flow inspection,
- and implementation patch text.

That is fine, but it would be clearer if it explicitly labeled:

- "observed runtime symptom"
- "code-proven cause"
- "follow-on design consequence"

The current version mostly does this already; it just blurs them a bit around Bug 2.

## Answer to the UI / debug-vis question

For Phase 9 in the live code:

- **GUI controls exist**
  - `Temporal accumulation`
  - `Temporal alpha`
  - `Probe jitter`

- **No new Phase 9-specific debug visualization exists**
  - no new render mode
  - no history-buffer viewer
  - no dedicated temporal debug panel

So this bugfix is about control flow and history-population, not about new debug visualization.

## Bottom line

This is a solid implementation note overall.

The only revision I would make is to split the diagnosis more cleanly:

1. Bug 1 caused the dark indirect-GI symptom.
2. Bug 2 is the separate reason temporal+jitter would not accumulate meaningfully in a static scene.
3. The `READ_ONLY` / `readonly` mismatch should be downgraded from "eliminated" to "not the current leading cause."
