# Review: `phase9d_gi_blur_mode_fixes.md`

## Verdict

This note is mostly aligned with the live Phase 9d worktree, and the core implementation it describes is real. I double-checked it against the current `git diff` plus the new untracked blur shader files in `res/shaders/`.

The main problem is that the document repeatedly frames the feature as a **GI blur**, but the code currently blurs the **full raymarched scene color buffer**, not a separated indirect-light buffer. That is a meaningful difference in both behavior and interpretation.

## What the code does match

- `raymarch.frag` really restores **mode 5** to post-loop step-count heatmap and moves probe-cell visualization to **mode 8**.
- `src/demo3d.cpp` really adds the **ProbeCell (8)** radio button and expands C0 options to **8, 16, 24, 32, 48, 64**.
- `raymarch.frag` really adds a second output `fragGBuffer` for normal/depth.
- `src/demo3d.h` / `src/demo3d.cpp` really add the GI-blur FBO state and the methods `initGIBlur()`, `destroyGIBlur()`, and `giBlurPass()`.
- `res/shaders/gi_blur.frag` really exists and implements a bilateral screen-space filter using full-frame color plus a normal/depth GBuffer.

## Main findings

### 1. The blur is not GI-only in the implementation

The note calls this a "GI bilateral blur" and says it suppresses probe-grid noise, which is directionally true as a goal. But the live code binds:

- `giColorTex` = full raymarch color output
- `giGBufferTex` = normal/depth

Then `giBlurPass()` filters `uColorTex` directly. That means the filter operates on the whole shaded image, not on an isolated indirect-GI term. The tooltip in `demo3d.cpp` is actually more accurate here: it says "applied to the full frame."

Why this matters:

- the pass can soften any screen-space color variation that survives the bilateral weights, not just GI banding
- the document overstates the architectural cleanliness of the fix
- this is closer to a screen-space postfilter than a true GI-only reconstruction pass

### 2. The mode-8 diagnosis table is too confident

The note says:

- aligned mode 6 / mode 8 banding = Type A
- misaligned = Type B

That is a useful heuristic, but still too strong as written. Mode 8 is a correlated probe-grid visualization, not a formal proof of root cause. The same caution from earlier Phase 8/9 reviews still applies: alignment is evidence, not a complete classifier.

### 3. `git diff` alone does not fully verify the new shader-file claims

The note lists `res/shaders/gi_blur.frag` and `res/shaders/gi_blur.vert` as new files. That is true in the worktree, but they are currently **untracked** rather than part of the tracked diff. So:

- the implementation claim is still supported by the live files
- but a reviewer using only `git diff` would miss part of the Phase 9d change unless they also check `git status`

That is worth stating explicitly because the user asked for verification against `git diff`.

## Bottom line

This is one of the stronger recent implementation notes: most of its file-level claims match the code. The main correction is conceptual, not factual:

- **mode 5 / mode 8 swap:** correct
- **new probe-resolution options:** correct
- **new bilateral blur pass exists:** correct
- **"GI-only blur" framing:** inaccurate; the live code currently blurs the full frame

## Recommended doc correction

Revise the note to say something like:

> "Phase 9d adds a depth+normal aware full-frame bilateral postfilter intended to reduce visible GI banding, rather than a blur applied only to the isolated indirect-light term."

That wording matches the actual implementation much better.
