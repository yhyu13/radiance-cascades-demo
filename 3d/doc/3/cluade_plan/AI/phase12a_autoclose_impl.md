# Phase 12a Addendum — `--auto-analyze` CLI Flag (Auto-Close)

**Date:** 2026-05-02
**Trigger:** Auto-capture fires but app stays open; need headless capture-and-exit mode.

---

## Design decision

Normal runs never auto-close — the window stays open for interactive use.
When `--auto-analyze` is passed, the app:
1. Waits for the existing 5s auto-capture trigger
2. Writes PNG + probe stats JSON (same as normal 12a)
3. Runs the Python analysis script **synchronously** (blocking the main thread)
4. Sets `captureAndAnalysisDone = true` → main loop breaks → process exits cleanly

Synchronous analysis (vs detached thread) is correct here because:
- In headless/CI use the caller needs to know analysis is complete before the process exits
- On Windows, `system()` spawns `cmd.exe /c python ...`; when the parent exits before
  `system()` returns, the `cmd.exe` child gets killed and the Python grandchild's fate
  is undefined — file writes may be incomplete
- The window freezes during the API call (~10–15 s), which is acceptable in auto-close mode

---

## Files changed

| File | Change |
|---|---|
| `src/demo3d.h` | 2 private data members + 2 public inline methods |
| `src/demo3d.cpp` — `launchAnalysis()` | Branch on `autoCloseAfterCapture` |
| `src/main3d.cpp` | `argc/argv`, flag parse, loop-break check |

---

## `src/demo3d.h`

### Private data members (alongside other Phase 12a members)

```cpp
// --auto-analyze CLI mode: block on analysis then signal the main loop to exit
bool autoCloseAfterCapture  = false;
bool captureAndAnalysisDone = false;
```

### Public inline methods (in the public Utility section, before `private:`)

```cpp
// Phase 12a — CLI auto-close query / setter (used by main3d.cpp)
bool isReadyToClose() const { return captureAndAnalysisDone; }
void setAutoCloseMode(bool v) { autoCloseAfterCapture = v; }
```

`isReadyToClose()` / `setAutoCloseMode()` must be **public** — called from `main3d.cpp`.
The data members stay private; the inline accessors live in the public section just
before the `private:` label.

---

## `src/demo3d.cpp` — `launchAnalysis()` branch

```cpp
void Demo3D::launchAnalysis(const std::string& imagePath,
                            const std::string& statsPath) {
    namespace fs = std::filesystem;
    std::string stem = fs::path(imagePath).stem().string();
    lastAnalysisPath = analysisDir + "/" + stem + ".md";  // set eagerly, before any thread

    std::string cmd = "python \"" + toolsScript + "\" \""
                    + imagePath + "\" \""
                    + analysisDir + "\"";
    if (!statsPath.empty())
        cmd += " \"" + statsPath + "\"";

    if (autoCloseAfterCapture) {
        // Blocking path: run synchronously so the process stays alive for the full
        // API call, then signal the main loop to exit.
        std::cout << "[12a] Running analysis synchronously (auto-close mode)...\n";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;
    } else {
        // Normal path: detached thread — UI remains responsive, window stays open.
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
        }).detach();
    }
}
```

Note: the detached thread lambda captures `cmd` by value (not `this`) — `toolsScript`
and `analysisDir` were already baked into `cmd` at construction time, so there is no
dangling reference risk if the `Demo3D` object were ever destroyed first.

---

## `src/main3d.cpp` changes

### Signature change

```cpp
// before
int main() {

// after
int main(int argc, char* argv[]) {
```

### Flag parse (after Demo3D construction, before the loop)

```cpp
bool autoAnalyze = false;
for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--auto-analyze") {
        autoAnalyze = true;
        demo->setAutoCloseMode(true);
        std::cout << "[MAIN] --auto-analyze: will capture, analyze, then exit.\n";
    }
}
```

`setAutoCloseMode(true)` sets `autoCloseAfterCapture` before the first frame runs —
the auto-capture trigger in `render()` fires at the usual delay, but `launchAnalysis()`
will block instead of detaching.

### Loop-break check (inside the render loop, after `takeScreenshot`)

```cpp
// Phase 6a: capture after 3D, before ImGui (clean 3D-only frame)
demo->takeScreenshot(/*launchAiAnalysis=*/true);

// --auto-analyze: exit once capture + analysis are done
if (autoAnalyze && demo->isReadyToClose())
    break;

// Render UI overlay
rlImGuiBegin();
    demo->renderUI();
rlImGuiEnd();
```

The break skips the ImGui render for that final frame, then falls through to the
cleanup block (`delete demo; rlImGuiShutdown(); CloseWindow()`), which runs normally.

---

## End-to-end flow with `--auto-analyze`

```
t=0s   app starts, window opens, shaders load
t≈1s   first cascade bake completes → cascadeReady=true
t=5s   auto-capture trigger fires (GetTime() > 5.0 && cascadeReady)
         pendingScreenshot=true, pendingStatsDump=true
       takeScreenshot() called from main3d.cpp after EndMode3D():
         → frame_T.png written
         → probe_stats_T.json written
         → launchAnalysis(imagePath, statsPath) called
           autoCloseAfterCapture=true → system("python analyze_screenshot.py ...") BLOCKS
t≈20s  Python script returns (Anthropic API call complete)
         frame_T.md written by Python
         captureAndAnalysisDone=true
       main loop checks isReadyToClose() → true → break
       delete demo; rlImGuiShutdown(); CloseWindow()
       process exits with code 0
```

---

## Backward compatibility

- No `--auto-analyze` arg: `autoCloseAfterCapture=false`, `captureAndAnalysisDone` never
  set → `isReadyToClose()` always false → loop never breaks. Identical to pre-addendum.
- `autoCloseAfterCapture=false` path in `launchAnalysis()`: detached thread, exactly as
  Phase 6a / Phase 12a original implementation.

---

## Verification

| Check | Result |
|---|---|
| Normal run (no flag) | Window stays open; analysis runs in background thread; `lastAnalysisPath` shows in UI |
| `--auto-analyze` run | App exits with code 0; `frame_T.png`, `probe_stats_T.json`, `frame_T.md` all present with same timestamp T |
| Exit ordering | `.md` file is fully written before process exits (synchronous `system()` call ensures this) |
