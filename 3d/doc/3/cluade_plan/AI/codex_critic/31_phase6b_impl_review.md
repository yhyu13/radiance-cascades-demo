# Review: `phase6b_impl.md`

## Verdict

This implementation note is substantially more accurate than the earlier Phase 6b plan notes. The RenderDoc integration is now real in the worktree: the app has `G` capture, `--auto-rdoc`, in-process API loading, shader/texture labels, `begin/endRdocFrameIfPending()`, and a Python RenderDoc analysis script.

The remaining problems are mostly about status wording and scope precision.

## What matches the code

- `src/demo3d.h` really includes `rdoc_helper.h` and declares:
  - `beginRdocFrameIfPending()`
  - `endRdocFrameIfPending()`
  - `setAutoRdocMode(float)`
- `src/demo3d.cpp` really has:
  - `initRenderDoc()`
  - `G` hotkey queuing
  - auto-RDOC delay logic in `update()`
  - `launchRdocAnalysis()`
  - `glObjectLabel()` calls for programs, volumes, and cascade textures
- `src/main3d.cpp` really brackets the frame with:
  - `beginRdocFrameIfPending()`
  - `endRdocFrameIfPending()`
- `tools/analyze_renderdoc.py` really exists and includes:
  - resource snapshot extraction
  - GPU timing collection
  - Claude analysis

## Main findings

### 1. “IMPLEMENTED” is true for the worktree, but still a bit too strong for repo-integrated status

The note says:

- `Status: IMPLEMENTED — awaiting first live capture validation`

That is close, but it still slightly overstates maturity because at least part of the implementation is currently untracked in git status, notably `tools/analyze_renderdoc.py`.

So the better status would be something like:

- implemented in the current worktree
- not yet fully validated
- not yet cleanly landed as a tracked, proven subsystem

This is the biggest status-level correction.

### 2. The “configurable delay” wording is stronger than the live UI exposure

The note says the tool can trigger automatically after a configurable delay.

The live code does support a delay member and a CLI path:

- `autoRdocDelaySeconds`
- `setAutoRdocMode(8.0f)` from `--auto-rdoc`

But there is no visible UI control for that delay in the current branch. So “configurable” is true in a code/CLI sense, not in the same runtime-UI sense as the screenshot tools.

That should be made explicit.

### 3. “Silently disables itself” is not how the code behaves

The note says the tool silently disables itself if RenderDoc is not installed.

The live code does print explicit console messages:

- DLL/API load failure
- capture unavailable

So this is not silent disablement; it is graceful disablement with logging. Small issue, but still inaccurate wording.

### 4. The dependency on “Phase 14c validated” is too project-specific for an implementation record

The note says it depends on:

- Phase 6a
- Phase 14c (C1 coverage validated)

That second dependency is not really an implementation dependency for Phase 6b itself. It is more of a specific analysis use case baked into the current stage labels and expected timing interpretation.

As an implementation record, the document should separate:

- what the subsystem requires to function
- what later experiment it was intended to help analyze

### 5. The note still reads a bit cleaner than the actual operational risk

The implementation is real, but it still has the normal fragility of local developer tooling:

- RenderDoc install path assumptions
- Windows-only active path
- external Python environment via `renderdoccmd.exe`
- labeling discipline needed for useful output

The doc mentions these, but the overall tone still leans a little more “built feature” than “advanced local tooling with several environmental assumptions.”

## Bottom line

This note is broadly accurate against the current code and much better than the old Phase 6b plan docs.

The main corrections are:

- treat the feature as implemented in the current worktree, not fully proven/landed
- soften “configurable delay” to CLI/config-level rather than UI-level
- replace “silently disables itself” with “gracefully disables itself with console logging”
