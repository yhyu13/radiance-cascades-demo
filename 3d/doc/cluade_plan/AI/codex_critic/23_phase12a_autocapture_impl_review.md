# Review: `phase12a_autocapture_impl.md`

## Verdict

This note is mostly aligned with the live implementation. The auto-capture trigger, probe-stats JSON dump, new `launchAnalysis()` signature, and settings-panel controls are all real in the current diff.

The main problem is that the document overstates backward compatibility for normal P-key screenshots. There is a real sticky-state bug in the implementation: `statsPathForAnalysis` can survive from an earlier auto-capture and get reused by a later plain screenshot analysis.

## What matches the code

- `src/demo3d.h` really adds:
  - `autoCaptureDelaySeconds`
  - `pendingStatsDump`
  - `statsPathForAnalysis`
  - `lastAnalysisPath`
- `render()` really has a one-shot delayed auto-capture block before the raymarch pass.
- `takeScreenshot()` really now writes a PNG first, optionally writes `probe_stats_<T>.json`, and then launches analysis with an optional stats path.
- `launchAnalysis()` really now accepts `statsPath` and sets `lastAnalysisPath` eagerly.
- `tools/analyze_screenshot.py` really accepts an optional third argument for probe stats and appends that content to the prompt.

## Main findings

### 1. The backward-compatibility claim for P-key screenshots is too strong

The note says:

- P-key screenshot keeps `pendingStatsDump == false`
- therefore the old no-stats analysis path is unchanged

That is not fully true in the current code.

In [src/demo3d.cpp](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.cpp:3515), when a stats JSON is successfully written, the code stores:

```cpp
statsPathForAnalysis = statsPath;
```

But on a later plain screenshot, if `pendingStatsDump` is false, nothing clears `statsPathForAnalysis` before:

```cpp
launchAnalysis(path, statsPathForAnalysis);
```

So a normal P-key screenshot can accidentally pass the previous auto-capture JSON path into analysis. That means the "old Phase 6a behaviour exactly" claim is wrong as written.

This is the biggest concrete issue in the implementation note.

### 2. The auto-capture is one-shot per process, not really slider-driven after startup

The document mostly admits this, but it is worth being blunt: the delay slider is not a general scheduling control. The live trigger uses:

```cpp
static bool autoCaptured = false;
```

inside `render()`. So after the first trigger, changing the slider only affects the displayed value, not future automatic captures during the same run.

That is more “startup auto-capture arm” than a reusable autocapture system.

### 3. The “valid stats because `cascadeReady`” explanation is mostly right, but narrower than it sounds

The note says the trigger is placed after probe readback so the JSON has valid stats. That is basically correct for the current render flow.

But those stats are “last completed cascade update” stats, not some independently synchronized capture-state object. In practice that is probably fine here, but the wording should not imply a stronger formal capture contract than the code really enforces.

## Bottom line

This is one of the better recent implementation notes in terms of matching the live code. The main correction is important, though:

- plain screenshots are **not** fully unchanged after Phase 12a, because `statsPathForAnalysis` can leak across captures

If this note is meant to be canonical, fix that claim first.
