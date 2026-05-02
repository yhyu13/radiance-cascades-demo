# Reply to Review 26 — Phase 12b Burst Capture Impl Review

**Date:** 2026-05-02

Findings 1 and 2 are code bugs — both fixed in `demo3d.cpp`. Finding 3 is acknowledged
as a cosmetic gap; no code change for now.

---

## Finding 1: Stale `statsPathForAnalysis` on failed JSON write — accepted, code fixed

The review is correct. `statsPathForAnalysis` is only written on JSON success; a failed
write leaves the member holding a path from a previous burst. `launchBurstAnalysis()` then
passes that stale path to the Python script with no indication it belongs to a different run.

The fix mirrors the pattern already used for `lastScreenshotPath`: clear the member at the
**start** of the `pendingStatsDump` block, before the write attempt. A failed write leaves
it empty; a successful write sets it to the new path.

**Before:**
```cpp
if (pendingStatsDump) {
    pendingStatsDump = false;
    // ...
    std::ofstream sf(statsPath);
    if (sf) {
        sf << j.str();
        statsToPass          = statsPath;
        statsPathForAnalysis = statsPath;
    } else {
        std::cerr << "[12a] Failed to write stats: " << statsPath << "\n";
        // statsPathForAnalysis retains previous value — BUG
    }
}
```

**After:**
```cpp
if (pendingStatsDump) {
    pendingStatsDump = false;
    statsPathForAnalysis.clear();   // clear before write; failed write leaves it empty
    // ...
    std::ofstream sf(statsPath);
    if (sf) {
        sf << j.str();
        statsToPass          = statsPath;
        statsPathForAnalysis = statsPath;
    } else {
        std::cerr << "[12a] Failed to write stats: " << statsPath << "\n";
        // statsPathForAnalysis is now empty — launchBurstAnalysis() omits --stats arg
    }
}
```

`launchBurstAnalysis()` already gates on `if (!statsPathForAnalysis.empty())`, so the
omission of the stale path is automatic.

**Interaction table update:** the "Burst write failure" row in the Phase 12b plan already
covers screenshot failure via `abortBurst()`. The stats-path failure is a softer failure
(the burst continues without stats); the fix ensures `launchBurstAnalysis()` gets `""`
rather than a mismatched path, producing a valid (but stats-free) analysis.

---

## Finding 2: Auto-capture delay tooltip describes old single-screenshot behavior — accepted, code fixed

The tooltip for `Auto-capture delay (s)` still says:

> "After N seconds AND cascade is ready, captures a screenshot + probe_stats JSON and
> triggers AI analysis automatically."

After Phase 12b, the auto-trigger starts a burst (modes 0/3/6 over 4 frames) not a single
screenshot. The tooltip is updated:

**New tooltip:**
```cpp
ImGui::SetTooltip("Phase 12b: 0 = disabled.\n"
                  "After N seconds AND cascade is ready, triggers a burst capture\n"
                  "(modes 0, 3, 6 over 4 frames) + probe_stats JSON, then sends\n"
                  "all three images to Claude for multi-mode analysis.");
```

---

## Finding 3: Burst Capture button gives no visual feedback during active burst — acknowledged

The review correctly identifies this as a UX weakness. The button silently ignores clicks
while `burstState != Idle`; the user gets no confirmation that a burst is in progress.

**No code change for now.** Adding `ImGui::BeginDisabled()` around the button when
`burstState != Idle` would provide clear visual feedback (grayed-out button). This is
the right fix but is cosmetic — the burst proceeds correctly regardless. Deferred to a
future cleanup pass.

---

## Summary

| Finding | Status | Action |
|---|---|---|
| Stale `statsPathForAnalysis` on failed JSON write | Accepted — real bug | `statsPathForAnalysis.clear()` added at start of `pendingStatsDump` block in `takeScreenshot()` |
| Auto-capture delay tooltip describes old single-screenshot behavior | Accepted — real UI/code mismatch | Tooltip updated to describe burst in `renderSettingsPanel()` |
| Burst button gives no visual feedback during active burst | Acknowledged — UX weakness | Deferred; `ImGui::BeginDisabled()` is the right fix but cosmetic |
