# Reply to Review 25 — Phase 12b Burst Capture Plan Review

**Date:** 2026-05-02

All four findings are accepted. Phase 12b has no code yet, so all corrections are
applied to `phase12b_burst_capture_plan.md`.

---

## Finding 1: "Invisible to the user" claim is too strong — accepted, doc corrected

The review is correct. The proposed design changes `raymarchRenderMode` before
`raymarchPass()`, so the 3D scene itself renders in mode 0, 3, then 6 across the three
burst frames. The user sees the scene flicker through those modes. The only thing
hidden from the HUD is the render-mode label, because ImGui runs after
`takeScreenshot()` — but the 3D viewport content is visible.

**Doc update:** the "invisible to the user" language is removed from the Goal section.
The replacement is explicit about what is and is not hidden:

> **Visibility:** the three burst frames ARE briefly visible to the user as a 3-frame
> mode flicker (~50 ms at 60 fps). The scene renders in mode 0, 3, then 6 before the
> ImGui overlay is drawn. The HUD's render-mode label is not shown during those frames,
> but the 3D scene itself changes visibly. This is acceptable for an offline analysis tool.

The verification checklist item "No UI flicker during burst" is corrected to:

> "HUD mode label not shown during burst" — watch that the ImGui overlay's render-mode
> radio buttons are not visible to be checked; the 3D viewport will visibly cycle modes.

---

## Finding 2: Path collection fragile on write failure — accepted, plan hardened

The review is correct. If `stbi_write_png` fails for the _m0 frame, `lastScreenshotPath`
is not updated (it is only set on success). On the following frame (CapM3), the stale
or empty `lastScreenshotPath` would be silently stored as `burstPaths[0]`, resulting in
a mismatched triplet passed to `launchBurstAnalysis()`.

**Three hardening measures added to the plan:**

1. **Clear before each capture:** each burst state handler clears `lastScreenshotPath`
   before setting `pendingScreenshot`. A failed write leaves the member empty; a
   successful write sets it to the new path.

2. **Suffix validation on collection:** each collecting state checks that
   `lastScreenshotPath` is non-empty and contains the expected suffix (`_m0.png`,
   `_m3.png`, `_m6.png`) before accepting it into `burstPaths[i]`.

3. **`abortBurst()` helper:** if validation fails, a lambda restores `savedRenderMode`,
   resets `burstState = Idle`, and logs the failure. No analysis is launched.

Updated state machine (abbreviated):
```cpp
auto abortBurst = [&](const char* reason) {
    std::cerr << "[12b] Burst aborted: " << reason << "\n";
    raymarchRenderMode = savedRenderMode;
    burstState         = BurstState::Idle;
    lastScreenshotPath.clear();
};

// CapM3: validate _m0 before advancing
if (lastScreenshotPath.empty() ||
        lastScreenshotPath.find("_m0.png") == std::string::npos)
    { abortBurst("_m0 write failed"); }
else {
    burstPaths[0] = lastScreenshotPath;
    lastScreenshotPath.clear();
    // ... set mode 3, pendingScreenshot, advance state ...
}
// CapM6 and Analyze: same pattern for _m3 and _m6
```

**`--auto-analyze` interaction:** an aborted burst does NOT set `captureAndAnalysisDone`
— the app stays open rather than exiting with an incomplete analysis. This is also
noted in the interaction table.

---

## Finding 3: "P-key unchanged" implicitly depends on post-review fix — accepted, doc explicit

The review is correct that the interaction table's "not reintroduced" claim for the
`statsPathForAnalysis` sticky bug is already relying on the Review 23 fix (local
`statsToPass` in `takeScreenshot()`). The Phase 12b plan listed only "Phase 12a" as
its dependency.

**Doc update:** the dependency line in the header is updated to:

> **Depends on:** Phase 12a infrastructure **and the Review 23 stats-path fix**
> (local `statsToPass` in `takeScreenshot()` — plain P-key screenshots pass `""`
> to `launchAnalysis()`, not the stale `statsPathForAnalysis` member).

The interaction table entry is also updated to reference this explicitly.

---

## Finding 4: `savedRenderMode` restoration under-specified for failure/interruption — accepted, doc updated

The review is correct that the plan only described the happy-path restore (in the
`Analyze` state) and left failure and mid-burst window-close unaddressed.

**Doc update:** the interaction table now specifies three additional cases:

| Case | Policy |
|---|---|
| Burst write failure | `abortBurst()` restores `savedRenderMode` and resets `burstState=Idle` — mode is always restored on abort |
| Mid-burst window close | Loop breaks on `WindowShouldClose()`; mode is left at the burst value since the app is exiting. If needed, the destructor can check `burstState != Idle` and restore. |
| Re-trigger while active | `burstState == Idle` guard in both the auto-capture block and the Burst Capture button prevents re-entry — no double-burst possible. |

The `burstState == Idle` guard was already in the plan; the failure and close cases
were not addressed. They are now.

---

## Summary

| Finding | Status |
|---|---|
| "Invisible" claim overstates visibility — 3D scene flickers across modes | Accepted; doc corrected to "brief 3-frame mode flicker, HUD label hidden, scene visible" |
| `lastScreenshotPath` fragile on write failure | Accepted; plan hardened with clear-before-capture + suffix validation + `abortBurst()` abort helper |
| "P-key unchanged" implicitly depends on Review 23 fix | Accepted; dependency header updated to name the fix explicitly |
| `savedRenderMode` restoration under-specified | Accepted; failure/close/re-entry policies added to interaction table |
