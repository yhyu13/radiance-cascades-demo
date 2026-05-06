# Reply to Review 23 — Phase 12a Auto-Capture Implementation Review

**Date:** 2026-05-02

Finding 1 is a real code bug and has been fixed. Findings 2 and 3 are documentation
corrections applied to `phase12a_autocapture_impl.md`.

---

## Finding 1: `statsPathForAnalysis` leaks into plain P-key screenshots — accepted, code fixed

The review is correct. `statsPathForAnalysis` is a member. After the auto-capture writes
the JSON and sets:

```cpp
statsPathForAnalysis = statsPath;
```

a subsequent P-key screenshot calls:

```cpp
launchAnalysis(path, statsPathForAnalysis);
```

with the old, stale path — passing last session's probe stats to an unrelated screenshot.
This silently corrupts the Phase 6a plain-screenshot analysis.

**Root cause:** the member was used as both the "just-written path" signal and the "path
to pass to analysis". These are different lifetimes and should be different variables.

**Fix:** introduce a local `statsToPass` (default `""`) inside `takeScreenshot()`. It is
only set when `pendingStatsDump` is consumed in the same call. The member
`statsPathForAnalysis` is retained for UI display (`lastAnalysisPath` update path) but
no longer passed to `launchAnalysis()`.

```cpp
// Phase 12a: write probe stats JSON alongside the screenshot.
// statsToPass is local — it is empty for plain screenshots and only populated here,
// preventing statsPathForAnalysis (a member) from leaking into future P-key captures.
std::string statsToPass;
if (pendingStatsDump) {
    pendingStatsDump = false;
    // ... build JSON, write file ...
    if (sf) {
        statsToPass          = statsPath;   // used for this call only
        statsPathForAnalysis = statsPath;   // member kept for UI display only
    }
}

if (launchAiAnalysis)
    launchAnalysis(path, statsToPass);  // statsToPass="" for plain P-key screenshots
```

With this fix, plain screenshots always pass `""` to `launchAnalysis()` — identical to
the pre-Phase-12a behaviour. The stale-path leak is closed.

---

## Finding 2: Slider is "startup arm", not a reusable scheduling control — accepted, doc updated

The review is correct that `static bool autoCaptured` inside `render()` means the delay
slider cannot reschedule a second auto-capture during the same run. The doc said the slider
"controls when capture fires" without making this one-shot constraint explicit.

**Doc update:** the slider description and Known Limitations table now state:

> `autoCaptured` is a local-static — resets only on app restart. To re-capture within a
> session, use the Screenshot [P] button or the future Burst Capture button (Phase 12b).
> The slider only affects the first (and only) automatic capture per run.

---

## Finding 3: "Valid stats because cascadeReady" is narrower than stated — accepted, doc softened

The review is correct that the stats are "last completed cascade update" values, not a
formally synchronized capture-state snapshot. The wording implied a stronger contract
than the code enforces.

**Doc update:** the relevant paragraph is reworded from:

> placed after probe readback so the JSON has valid stats

to:

> placed after probe readback so the JSON reflects the most recently completed cascade
> bake. These are not a synchronized snapshot — they are whatever the probe arrays held
> at the time `cascadeReady` was last set to `true`.

---

## Summary

| Finding | Status |
|---|---|
| `statsPathForAnalysis` leaks into plain screenshots | Accepted; **code fixed**: local `statsToPass` used for `launchAnalysis()` call; member kept for UI display only |
| Delay slider is one-shot per run, not a reusable scheduler | Accepted; doc updated to state one-shot constraint explicitly |
| "Valid stats" wording overstates the capture contract | Accepted; doc softened to "most recently completed bake values" |
