# Phase 12b — Multi-Mode Screenshot Burst: Implementation

**Date:** 2026-05-02
**Plan reference:** `phase12b_burst_capture_plan.md`
**Depends on:** Phase 12a (`pendingStatsDump`, `statsPathForAnalysis`, `lastAnalysisPath`,
`autoCloseAfterCapture`, `captureAndAnalysisDone`) + Review 23 stats-path fix (`statsToPass`
local in `takeScreenshot()`).

---

## What was implemented

| Component | Change |
|---|---|
| `src/demo3d.h` | `BurstState` enum + 5 new members + `launchBurstAnalysis()` decl |
| `src/demo3d.cpp` — `takeScreenshot()` | Consume `pendingScreenshotTag`, clear+set `lastScreenshotPath` |
| `src/demo3d.cpp` — `render()` | Replace 12a single-shot trigger with burst trigger; add burst state machine |
| `src/demo3d.cpp` — `launchBurstAnalysis()` | New method: strip `_m0`, build `--burst` command, respect auto-close |
| `src/demo3d.cpp` — `renderSettingsPanel()` | "Burst Capture" button with `Idle` guard and tooltip |
| `tools/analyze_screenshot.py` | `--burst` dispatch, `analyze_burst()`, `PROMPT_BURST`, multi-image content |

---

## New header members (`src/demo3d.h`)

```cpp
// Phase 12b — burst state machine
enum class BurstState { Idle, CapM0, CapM3, CapM6, Analyze };
BurstState  burstState          = BurstState::Idle;
int         savedRenderMode     = 0;        // restored after burst (and on abort)
std::string burstPaths[3];                  // [0]=_m0  [1]=_m3  [2]=_m6
std::string lastScreenshotPath;             // set by takeScreenshot() on each successful write
std::string pendingScreenshotTag;           // suffix inserted before ".png" ("_m0"/"_m3"/"_m6"/"")

void launchBurstAnalysis();                 // private
```

**`lastScreenshotPath` is separate from `pendingScreenshotTag`:** the tag is consumed
synchronously inside `takeScreenshot()` to build the filename. `lastScreenshotPath` is
written after the PNG write succeeds, and read by the burst state machine one frame later.
These are write-ordered across two different call sites (render ↔ takeScreenshot) so they
cannot be the same variable.

---

## `takeScreenshot()` changes

Two additions around the existing PNG write:

```cpp
// 1. Consume tag (cleared to "" after use; empty tag = no suffix = plain frame_T.png)
std::string tag = pendingScreenshotTag;
pendingScreenshotTag.clear();
lastScreenshotPath.clear();               // clear before write: failed write leaves it empty

auto now   = std::chrono::system_clock::now().time_since_epoch().count();
std::string stem     = "frame_" + std::to_string(now) + tag;  // e.g. "frame_T_m0"
std::string filename = stem + ".png";
std::string path     = screenshotDir + "/" + filename;

// ... existing PNG write ...

// 2. Record on success — burst state machine reads this next frame
if (stbi_write_png(...))
    lastScreenshotPath = path;
```

**Why clear before write:** if `stbi_write_png` fails, `lastScreenshotPath` must be empty
so the collecting burst frame sees the failure and calls `abortBurst()`. Without the pre-clear,
a failed write would leave `lastScreenshotPath` holding the previous burst frame's path,
causing a silent triplet mismatch passed to `launchBurstAnalysis()`.

**Backward compat:** when `pendingScreenshotTag` is `""` (plain P-key or Phase 12a), the
filename is unchanged: `frame_T.png`. `lastScreenshotPath` is also set, but the burst state
machine is `Idle` so no one reads it.

---

## Auto-capture trigger change (`render()`)

Phase 12a single-shot:
```cpp
autoCaptured      = true;
pendingScreenshot = true;
pendingStatsDump  = true;
```

Phase 12b burst trigger (same guard conditions + `burstState == Idle`):
```cpp
autoCaptured = true;
burstState   = BurstState::CapM0;
std::cout << "[12b] Auto-burst triggered at t=" << GetTime() << "s\n";
```

The trigger only arms `burstState`; the state machine immediately processes `CapM0` in the
same frame (the state machine block runs right after the trigger block).

---

## Burst state machine (`render()`)

Placed immediately after the trigger block, before `raymarchPass()`:

```cpp
auto abortBurst = [&](const char* reason) {
    std::cerr << "[12b] Burst aborted: " << reason << "\n";
    raymarchRenderMode = savedRenderMode;
    burstState         = BurstState::Idle;
    lastScreenshotPath.clear();
};

if (burstState == BurstState::CapM0) {
    savedRenderMode      = raymarchRenderMode;   // save before first mode change
    lastScreenshotPath.clear();
    pendingStatsDump     = true;                 // stats JSON written alongside _m0
    pendingScreenshotTag = "_m0";
    pendingScreenshot    = true;
    raymarchRenderMode   = 0;
    burstState           = BurstState::CapM3;    // advance immediately

} else if (burstState == BurstState::CapM3) {
    if (lastScreenshotPath.empty() ||
            lastScreenshotPath.find("_m0.png") == std::string::npos)
        { abortBurst("_m0 write failed"); }
    else {
        burstPaths[0]        = lastScreenshotPath;
        lastScreenshotPath.clear();
        pendingScreenshotTag = "_m3";
        pendingScreenshot    = true;
        raymarchRenderMode   = 3;
        burstState           = BurstState::CapM6;
    }

} else if (burstState == BurstState::CapM6) {
    if (lastScreenshotPath.empty() ||
            lastScreenshotPath.find("_m3.png") == std::string::npos)
        { abortBurst("_m3 write failed"); }
    else {
        burstPaths[1]        = lastScreenshotPath;
        lastScreenshotPath.clear();
        pendingScreenshotTag = "_m6";
        pendingScreenshot    = true;
        raymarchRenderMode   = 6;
        burstState           = BurstState::Analyze;
    }

} else if (burstState == BurstState::Analyze) {
    if (lastScreenshotPath.empty() ||
            lastScreenshotPath.find("_m6.png") == std::string::npos)
        { abortBurst("_m6 write failed"); }
    else {
        burstPaths[2]      = lastScreenshotPath;
        raymarchRenderMode = savedRenderMode;     // restore before analysis dispatch
        burstState         = BurstState::Idle;
        launchBurstAnalysis();
    }
}
```

### Timing diagram (4 frames)

```
Frame N    [CapM0]   mode=0, pendingScreenshot+Tag="_m0", pendingStatsDump → advance to CapM3
           (after render returns) takeScreenshot(): frame_T_m0.png + probe_stats_T.json → lastScreenshotPath

Frame N+1  [CapM3]   validate lastScreenshotPath contains "_m0.png" → burstPaths[0]
           mode=3, pendingScreenshot+Tag="_m3" → advance to CapM6
           (after render) takeScreenshot(): frame_T_m3.png → lastScreenshotPath

Frame N+2  [CapM6]   validate _m3.png → burstPaths[1]; mode=6, Tag="_m6" → Analyze
           (after render) takeScreenshot(): frame_T_m6.png → lastScreenshotPath

Frame N+3  [Analyze] validate _m6.png → burstPaths[2]; restore mode; launchBurstAnalysis()
```

**Ordering invariant:** trigger sets `CapM0` → state machine immediately processes `CapM0`
in the same frame → mode 0 render happens the first burst frame with zero extra delay.

**Stats are in CapM0 only:** `pendingStatsDump=true` set only in CapM0. `takeScreenshot()`
writes the JSON and sets `statsPathForAnalysis` alongside `frame_T_m0.png`. The Analyze
state reads `statsPathForAnalysis` (the burst's own stats) via `launchBurstAnalysis()`.
This is safe because `statsPathForAnalysis` is only written here, not during plain P-key
screenshots (which use local `statsToPass=""` per Review 23 fix).

---

## `launchBurstAnalysis()`

```cpp
void Demo3D::launchBurstAnalysis() {
    namespace fs = std::filesystem;
    // Strip "_m0" suffix to get shared stem "frame_T"
    std::string stem = fs::path(burstPaths[0]).stem().string();
    if (stem.size() >= 3 && stem.substr(stem.size() - 3) == "_m0")
        stem = stem.substr(0, stem.size() - 3);
    lastAnalysisPath = analysisDir + "/" + stem + ".md";

    std::string cmd = "python \"" + toolsScript + "\""
                    + " --burst"
                    + " \"" + burstPaths[0] + "\""
                    + " \"" + burstPaths[1] + "\""
                    + " \"" + burstPaths[2] + "\""
                    + " \"" + analysisDir   + "\"";
    if (!statsPathForAnalysis.empty())
        cmd += " \"" + statsPathForAnalysis + "\"";

    if (autoCloseAfterCapture) {
        int ret = system(cmd.c_str());   // blocking — app stays alive for full API call
        if (ret != 0)
            std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;   // signals main loop to exit
    } else {
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        }).detach();
    }
}
```

`lastAnalysisPath` is set synchronously (before thread or `system()`) so the UI shows the
expected output path immediately. The `--auto-analyze` path is identical to Phase 12a:
blocking `system()` + `captureAndAnalysisDone=true` → main loop breaks → clean exit.

**Aborted burst:** `abortBurst()` does NOT set `captureAndAnalysisDone`, so `--auto-analyze`
stays open on failure rather than exiting with no analysis.

---

## UI additions (`renderSettingsPanel()`)

```cpp
if (ImGui::Button("Screenshot [P]"))
    pendingScreenshot = true;
// ... existing tooltip ...
ImGui::SameLine();
if (ImGui::Button("Burst Capture##burst")) {
    if (burstState == BurstState::Idle)
        burstState = BurstState::CapM0;
}
if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Phase 12b: capture modes 0/3/6 over 3 frames, then\n"
                      "send all three images + probe stats to Claude in one call.\n"
                      "Produces frame_T.md in tools/.");
```

The `burstState == Idle` guard silently ignores presses during an active burst (re-entry
prevention). No `ImGui::BeginDisabled()` needed — the button appears enabled but does nothing.

---

## Python script (`tools/analyze_screenshot.py`)

### Dispatch

```python
if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--burst":
        # --burst m0 m3 m6 output_dir [stats]
        stats = sys.argv[6] if len(sys.argv) > 6 else None
        analyze_burst(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], stats)
    else:
        # legacy single-image (Phase 6a / 12a)
        stats = sys.argv[3] if len(sys.argv) > 3 else None
        analyze(sys.argv[1], sys.argv[2], stats)
```

### `analyze_burst()`

Builds a multi-image `content` list (alternating text labels and base64 PNG blocks for
all three images), appends `PROMPT_BURST` (and optionally `PROMPT_STATS_SUFFIX`), then
makes a single `client.messages.create()` call with `max_tokens=1500`.

Output filename: strips `_m0` from the m0 stem → `frame_T.md` (no mode suffix).

### `PROMPT_BURST`

Five instructions:
1. Compare the three images to isolate GI contribution
2. Locate banding in mode 6 (GI only) — shape + spacing helps distinguish probe-grid vs
   directional-bin vs cascade-boundary sources
3. Assess mode 3 (indirect×5) smoothness and mode 0 bilateral-blur edge preservation
4. Rate each mode: Poor / Fair / Good / Excellent
5. Suggest one concrete parameter change based on all three views

---

## Output file layout

```
tools/
  frame_T_m0.png          ← mode 0 screenshot
  frame_T_m3.png          ← mode 3 screenshot
  frame_T_m6.png          ← mode 6 screenshot
  probe_stats_T.json      ← probe stats (same T, written alongside _m0)
  frame_T.md              ← Claude burst analysis (stem has no _m0 suffix)
```

All five files share the same timestamp `T` from the `_m0` capture.

---

## Key design decisions

| Decision | Rationale |
|---|---|
| One frame per mode (4 frames total) | GPU renders the whole frame with one `raymarchRenderMode`; mid-frame mode change is impossible |
| `lastScreenshotPath` member (not return value) | `takeScreenshot()` is called from `main3d.cpp` after `render()` returns; the path cannot be known inside `render()` on the same frame |
| Clear `lastScreenshotPath` before each burst capture | Failed PNG write leaves the member empty; collecting frame sees empty → `abortBurst()`. Without pre-clear, stale path from a previous successful write would be silently accepted |
| Suffix validation (`_m0.png`/`_m3.png`/`_m6.png`) | Defends against timestamp collision or stale member across app re-use; if validation fails, `abortBurst()` restores `savedRenderMode` and resets `burstState=Idle` |
| `pendingStatsDump=true` in CapM0 only | All three modes share the same cascade state; one JSON covers the burst |
| Mode flicker IS visible | The 3D scene cycles modes 0→3→6 across 4 frames; the HUD mode label is hidden (ImGui renders after `takeScreenshot()`) but the viewport is visible. Acceptable for offline analysis |

---

## Interaction with existing features

| Feature | Interaction |
|---|---|
| P-key plain screenshot | Unchanged: `burstState=Idle`, `pendingScreenshotTag=""`, no stats dump |
| Phase 12a auto-capture trigger | Replaced: trigger now sets `burstState=CapM0` instead of `pendingScreenshot=true` directly |
| `statsPathForAnalysis` sticky bug (Review 23) | Not reintroduced: burst writes stats in CapM0 and reads via the member in Analyze (same burst's stats); plain P-key uses local `statsToPass=""` |
| `--auto-analyze` | Works: `launchBurstAnalysis()` runs synchronously, sets `captureAndAnalysisDone=true`; aborted burst does NOT set it |
| Re-trigger while active | `burstState==Idle` guard in auto-trigger block and Burst Capture button prevents re-entry |
| Mid-burst window close | Loop breaks; mode left at burst value (app exiting — no need to restore) |

---

## Verification checklist

| Check | Procedure |
|---|---|
| Three tagged PNGs written | `ls -t tools/` shows `frame_T_m0.png`, `_m3.png`, `_m6.png` with same T |
| Stats JSON paired with _m0 | `probe_stats_T.json` timestamp matches `frame_T_m0.png` |
| `frame_T.md` output | MD filename has no `_m0` suffix |
| Mode restored after burst | After analysis launches, `raymarchRenderMode` returns to pre-burst value |
| Burst button re-entry blocked | Clicking "Burst Capture" mid-burst has no effect |
| P-key still works | Plain screenshot: no tag, no stats dump, no burst interference |
| `--auto-analyze` exits | App exits cleanly after burst analysis script returns |
| Single-image path unchanged | P-key + AI analysis: script called without `--burst`, output same as Phase 12a |
| Abort path | Delete a burst PNG mid-flight (or force `stbi_write_png` to fail): app logs abort, restores mode, stays open |
