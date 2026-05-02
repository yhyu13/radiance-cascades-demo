# Review: `phase12b_burst_capture_plan.md`

## Verdict

The plan is directionally reasonable and fits the current Phase 12a architecture better than trying to switch modes inside one frame. The proposed `lastScreenshotPath` / multi-frame state-machine approach is coherent with the existing `takeScreenshot()` call site in `main3d.cpp`.

The main problems are:

- it overclaims that the burst is effectively invisible to the user
- it assumes every screenshot write succeeds, even though the whole plan depends on a single sticky `lastScreenshotPath`

## Main findings

### 1. The “no UI flicker / invisible to the user” claim is too strong

The plan says the three-frame burst is effectively invisible and that the HUD never shows the wrong mode because `takeScreenshot()` runs before ImGui.

That is not the right distinction.

What actually happens in the proposed design:

- `raymarchRenderMode` is changed before `raymarchPass()`
- the whole 3D scene for that frame is rendered in mode 0, then mode 3, then mode 6
- only after that does `takeScreenshot()` run
- then the UI is rendered for the same frame

So the user would still see the scene itself flicker across those modes, and the UI would also be rendering during those burst frames with the temporary mode value unless extra suppression logic is added.

The plan is still viable, but it is not truly “invisible to the user” in the strong sense it claims.

### 2. The path-collection design is fragile if any screenshot write fails

The plan depends on:

- `lastScreenshotPath` being updated after each successful write
- the next frame reading that value into `burstPaths[i]`

But if one burst screenshot fails to write, `lastScreenshotPath` would still hold the previous successful path unless the implementation explicitly clears or validates it. That means:

- `_m3` or `_m6` collection could silently reuse the wrong earlier path
- `launchBurstAnalysis()` could receive a mismatched triplet without noticing

The plan should require at least one of:

- clear `lastScreenshotPath` before each expected capture
- validate suffixes (`_m0`, `_m3`, `_m6`) before accepting each path
- abort burst analysis if any expected capture is missing

Right now it assumes the happy path too aggressively.

### 3. The “P-key unchanged” section is slightly stale against the current codebase wording

The interaction table says the sticky `statsPathForAnalysis` bug is “not reintroduced” because plain screenshots now pass a local `statsToPass`.

That is directionally correct against the current worktree, but it means this plan is already partially coupled to a post-review code fix rather than just to “Phase 12a infrastructure” as stated at the top. The dependency section should be more explicit about that.

### 4. `savedRenderMode` restoration is under-specified for interruptions or re-entry

The plan restores the mode only in the `Analyze` state. That is fine in the happy path, but it does not spell out what should happen if:

- one capture fails
- the user closes the window mid-burst
- the burst is manually retriggered while not idle

The `burstState == Idle` guard helps, but the recovery path still needs a clearer failure policy.

## Bottom line

This is a workable plan shape, but it is too optimistic about two things:

- user invisibility
- capture-path reliability

If implemented, the first thing to harden should be path validation and failure handling around `lastScreenshotPath`.
