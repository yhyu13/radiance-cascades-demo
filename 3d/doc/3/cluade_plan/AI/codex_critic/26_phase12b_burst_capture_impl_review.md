# Review: `phase12b_burst_capture_impl.md`

## Verdict

This implementation note is mostly aligned with the live code. The burst state machine, filename tagging, `lastScreenshotPath` validation, `launchBurstAnalysis()`, and Python `--burst` mode are all real.

The biggest remaining issue is that the implementation fixed the stale screenshot-path problem but not the stale stats-path problem for burst mode. If the `_m0` JSON write fails, the later burst analysis can still reuse an old `statsPathForAnalysis` from a previous burst.

## What matches the code

- `BurstState`, `burstPaths`, `lastScreenshotPath`, `pendingScreenshotTag`, and `launchBurstAnalysis()` exist in [src/demo3d.h](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.h).
- `render()` really now:
  - replaces the old single-shot auto-capture with a burst trigger
  - runs a multi-frame state machine before `raymarchPass()`
  - restores `raymarchRenderMode` on the happy path and on `abortBurst()`
- `takeScreenshot()` really now:
  - consumes `pendingScreenshotTag`
  - clears `lastScreenshotPath` before write
  - records `lastScreenshotPath` on success
- `tools/analyze_screenshot.py` really now supports `--burst` and multi-image analysis.

## Main findings

### 1. Burst mode still has a stale-stats-path bug

The document says the burst stats path is safe because `_m0` writes the JSON for the burst and `launchBurstAnalysis()` reads that member later.

That is only safe on successful JSON write.

In the current code:

- `_m0` screenshot success does **not** imply stats JSON success
- on stats write success, `statsPathForAnalysis = statsPath`
- on stats write failure, the code logs an error but does **not** clear `statsPathForAnalysis`
- `launchBurstAnalysis()` later does:

```cpp
if (!statsPathForAnalysis.empty())
    cmd += " \"" + statsPathForAnalysis + "\"";
```

So a later burst can reuse stale stats from a previous burst if the current `_m0` JSON write fails. This is the strongest concrete issue in the live implementation and the note does not call it out.

### 2. One UI string is already stale against the new burst behavior

The implementation doc is mostly up to date, but the live settings tooltip for `Auto-capture delay` still says:

- “captures a screenshot + probe_stats JSON and triggers AI analysis automatically”

That no longer describes the actual runtime behavior after Phase 12b, because the auto-trigger now starts a burst, not a single screenshot.

This is a smaller issue, but it is a real code/doc/UI mismatch.

### 3. The “Burst Capture button appears enabled but does nothing” description is accurate but still a UX risk

The note correctly says the button silently ignores clicks while `burstState != Idle`. That matches the code.

I would still count it as a residual implementation weakness:

- behavior is safe
- feedback is weak

So the doc is not wrong here, but the implementation is less polished than the writeup sounds.

## Bottom line

This is a much stronger implementation than the earlier Phase 12 plan. The main correction is:

- the burst path still needs the same kind of stale-state hygiene for `statsPathForAnalysis` that it already added for `lastScreenshotPath`

Secondary mismatch:

- the auto-capture delay tooltip in the live UI still describes the old single-shot behavior instead of the new burst behavior.
