# Phase 12b — Multi-Mode Screenshot Burst

**Date:** 2026-05-02
**Depends on:** Phase 12a (auto-capture infrastructure, `pendingStatsDump`, `statsPathForAnalysis`,
`lastAnalysisPath`, `autoCloseAfterCapture`, `captureAndAnalysisDone`) **and the Review 23
stats-path fix** (local `statsToPass` in `takeScreenshot()` — plain P-key screenshots pass `""`
to `launchAnalysis()`, not the stale `statsPathForAnalysis` member).

---

## Goal

Capture render modes 0 (final), 3 (indirect×5), and 6 (GI-only) as three separate PNGs
over three consecutive frames, then send all three images plus probe stats to Claude in
a single multi-image API call for richer triage.

**Visibility:** the three burst frames ARE briefly visible to the user as a 3-frame mode
flicker (~50 ms at 60 fps). The scene renders in mode 0, 3, then 6 before the ImGui
overlay is drawn. The HUD's render-mode label is not shown during those frames (ImGui
renders after `takeScreenshot()`), but the 3D scene itself changes visibly. This is
acceptable for an offline analysis tool.

Phase 12a's single auto-capture screenshot is **upgraded** to a burst trigger. The
plain P-key single screenshot is unchanged.

---

## Architecture

### Why three frames instead of one

`raymarchRenderMode` controls which output the fragment shader writes. Changing it
mid-frame is not possible — the GPU renders the whole frame with one mode. So each
mode requires its own rendered frame, captured separately before the UI is drawn.

### Path collection problem

`takeScreenshot()` is called from `main3d.cpp` **after** `EndMode3D()` and after
`render()` returns. The path of the screenshot just written is not known inside
`render()` during the same frame. The burst state machine therefore collects each
path one frame late, reading `lastScreenshotPath` (a new member set by
`takeScreenshot()` on every successful write).

**Failure handling:** if a write fails, `lastScreenshotPath` must NOT carry a stale
path forward. The implementation must clear `lastScreenshotPath` at the beginning of
each burst frame (before setting `pendingScreenshot`). On the collecting frame, if the
path is empty or does not end with the expected suffix, the burst is aborted:
`savedRenderMode` is restored and `burstState` is reset to `Idle`. This prevents
`launchBurstAnalysis()` from receiving a mismatched triplet silently.

### Timing diagram

```
Frame N   [burstState=CapM0] render() sets mode=0, pendingScreenshot=true, tag="_m0"
           render() returns → takeScreenshot() writes frame_T_m0.png → lastScreenshotPath="…_m0.png"

Frame N+1 [burstState=CapM3] render() reads lastScreenshotPath → burstPaths[0]
           render() sets mode=3, pendingScreenshot=true, tag="_m3"
           render() returns → takeScreenshot() writes frame_T_m3.png → lastScreenshotPath="…_m3.png"

Frame N+2 [burstState=CapM6] render() reads lastScreenshotPath → burstPaths[1]
           render() sets mode=6, pendingScreenshot=true, tag="_m6"
           render() returns → takeScreenshot() writes frame_T_m6.png → lastScreenshotPath="…_m6.png"

Frame N+3 [burstState=Analyze] render() reads lastScreenshotPath → burstPaths[2]
           render() restores savedRenderMode, calls launchBurstAnalysis()
           burstState → Idle
```

Four frames total (three captures + one analysis dispatch). At 60 fps ≈ 67 ms visible
to the user as brief mode-switch flicker if they look at the right moment, but the UI
overlay (which shows the mode label) is rendered after takeScreenshot() so the HUD never
shows the wrong mode.

---

## New members (`src/demo3d.h`)

```cpp
// Phase 12b — burst state machine
enum class BurstState { Idle, CapM0, CapM3, CapM6, Analyze };
BurstState  burstState       = BurstState::Idle;
int         savedRenderMode  = 0;           // restored after burst
std::string burstPaths[3];                  // [0]=_m0, [1]=_m3, [2]=_m6
std::string lastScreenshotPath;             // set by takeScreenshot() on each write
std::string pendingScreenshotTag;           // suffix inserted before ".png" (_m0/_m3/_m6)

void launchBurstAnalysis();                 // private: dispatch multi-image analysis
```

`lastScreenshotPath` replaces the need for `takeScreenshot()` to return a value; it is
written unconditionally on every successful PNG write.

`pendingScreenshotTag` is consumed by `takeScreenshot()` to produce `frame_T_m0.png`
instead of `frame_T.png`; it is cleared to `""` after use.

---

## `render()` changes

### 1. Replace Phase 12a auto-capture trigger with burst trigger

Old (Phase 12a):
```cpp
// Phase 12a: auto-capture on startup after delay
{
    static bool autoCaptured = false;
    if (!autoCaptured
            && autoCaptureDelaySeconds > 0.0f
            && GetTime() > autoCaptureDelaySeconds
            && cascadeReady) {
        autoCaptured      = true;
        pendingScreenshot = true;
        pendingStatsDump  = true;
        std::cout << "[12a] Auto-capture triggered at t=" << GetTime() << "s\n";
    }
}
```

New (Phase 12b):
```cpp
// Phase 12b: auto-capture delay now triggers a burst instead of a single screenshot
{
    static bool autoCaptured = false;
    if (!autoCaptured
            && autoCaptureDelaySeconds > 0.0f
            && GetTime() > autoCaptureDelaySeconds
            && cascadeReady
            && burstState == BurstState::Idle) {
        autoCaptured = true;
        burstState   = BurstState::CapM0;
        std::cout << "[12b] Auto-burst triggered at t=" << GetTime() << "s\n";
    }
}
```

The trigger only arms `burstState` — the state machine below handles the rest.

### 2. Burst state machine block (immediately after trigger, before Pass 4)

```cpp
// Phase 12b: burst state machine — runs before raymarchPass() so mode is live this frame
// Abort helper: restore mode and return to Idle without launching analysis
auto abortBurst = [&](const char* reason) {
    std::cerr << "[12b] Burst aborted: " << reason << "\n";
    raymarchRenderMode = savedRenderMode;
    burstState         = BurstState::Idle;
    lastScreenshotPath.clear();
};

if (burstState == BurstState::CapM0) {
    savedRenderMode      = raymarchRenderMode;
    lastScreenshotPath.clear();              // clear so a failed write leaves it empty
    pendingStatsDump     = true;             // write stats JSON alongside _m0 screenshot
    pendingScreenshotTag = "_m0";
    pendingScreenshot    = true;
    raymarchRenderMode   = 0;
    burstState           = BurstState::CapM3;

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
        raymarchRenderMode = savedRenderMode;
        burstState         = BurstState::Idle;
        launchBurstAnalysis();
    }
}
```

**Ordering invariant:** the trigger block runs first (so `burstState=CapM0` is set this
frame), then the state machine runs immediately — the first burst frame's mode-0 render
happens on the same frame as the trigger with no extra delay.

**Probe stats:** `pendingStatsDump=true` is set only in CapM0. `takeScreenshot()` writes
the JSON and sets `statsPathForAnalysis` alongside `frame_T_m0.png`. The Analyze state
reads `statsPathForAnalysis` via `launchBurstAnalysis()`.

---

## `takeScreenshot()` changes

Two additions:

```cpp
// 1. Apply tag to filename
std::string tag  = pendingScreenshotTag;
pendingScreenshotTag.clear();              // consumed immediately
std::string stem     = "frame_" + std::to_string(now) + tag;  // e.g. frame_T_m0
std::string filename = stem + ".png";
std::string path     = screenshotDir + "/" + filename;

// ... (existing PNG write + stats JSON write unchanged) ...

// 2. Record written path for burst collection (read on next frame)
if (stbi_write_png(...))
    lastScreenshotPath = path;
```

The tag is inserted between the timestamp and `.png`. When `pendingScreenshotTag` is
empty (plain P-key or 12a single-shot), the filename is unchanged: `frame_T.png`.

`lastScreenshotPath` is set on every successful write regardless of burst state — it is
safe to read even when no burst is active (it just holds the last screenshot path).

---

## New method: `launchBurstAnalysis()`

```cpp
void Demo3D::launchBurstAnalysis() {
    // Derive base stem from _m0 path (strip "_m0" suffix)
    namespace fs = std::filesystem;
    std::string stem = fs::path(burstPaths[0]).stem().string();   // "frame_T_m0"
    if (stem.size() >= 3 && stem.substr(stem.size() - 3) == "_m0")
        stem = stem.substr(0, stem.size() - 3);                   // "frame_T"
    lastAnalysisPath = analysisDir + "/" + stem + ".md";

    std::string cmd = "python \"" + toolsScript + "\""
                    + " --burst"
                    + " \"" + burstPaths[0] + "\""
                    + " \"" + burstPaths[1] + "\""
                    + " \"" + burstPaths[2] + "\""
                    + " \"" + analysisDir   + "\"";
    if (!statsPathForAnalysis.empty())
        cmd += " \"" + statsPathForAnalysis + "\"";

    std::cout << "[12b] Burst analysis: " << burstPaths[0]
              << " + m3 + m6 → " << lastAnalysisPath << "\n";

    if (autoCloseAfterCapture) {
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;   // signals main loop to break (--auto-analyze)
    } else {
        std::thread([cmd]() {
            int ret = system(cmd.c_str());
            if (ret != 0)
                std::cerr << "[12b] Burst analysis failed (exit " << ret << ")\n";
        }).detach();
    }
}
```

Reuses the same `autoCloseAfterCapture` / `captureAndAnalysisDone` mechanism from Phase
12a addendum — `--auto-analyze` works transparently with burst.

---

## UI changes (`renderSettingsPanel()`)

Add a "Burst Capture" button next to "Screenshot [P]":

```cpp
if (ImGui::Button("Screenshot [P]"))
    pendingScreenshot = true;
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

The button is grayed-out implicitly when `burstState != Idle` (the press has no effect
because of the `== Idle` guard). Optionally replace with `ImGui::BeginDisabled()` if
explicit visual feedback is desired.

---

## Python script changes (`tools/analyze_screenshot.py`)

### New `--burst` invocation

```
python analyze_screenshot.py --burst <m0.png> <m3.png> <m6.png> <output_dir> [stats.json]
```

Existing invocation unchanged:
```
python analyze_screenshot.py <image.png> <output_dir> [stats.json]
```

### Implementation

```python
def analyze_burst(m0: str, m3: str, m6: str,
                  output_dir: str, stats_path: str | None = None) -> None:
    labels = [
        ("Final render (mode 0)",   m0),
        ("Indirect × 5 (mode 3)",   m3),
        ("GI only (mode 6)",        m6),
    ]
    content = []
    for label, path in labels:
        img_b64 = base64.standard_b64encode(
            pathlib.Path(path).read_bytes()).decode("utf-8")
        content.append({"type": "text", "text": f"### {label}"})
        content.append({"type": "image",
                        "source": {"type": "base64",
                                   "media_type": "image/png",
                                   "data": img_b64}})

    stats_text = ""
    if stats_path:
        try:
            stats_text = pathlib.Path(stats_path).read_text(encoding="utf-8")
        except OSError as e:
            print(f"[12b] Warning: could not read stats: {e}", file=sys.stderr)

    prompt = PROMPT_BURST
    if stats_text:
        prompt += PROMPT_STATS_SUFFIX.format(stats=stats_text)
    content.append({"type": "text", "text": prompt})

    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1500,
        messages=[{"role": "user", "content": content}],
    )

    # Output filename: strip _m0 suffix → frame_T.md
    stem = pathlib.Path(m0).stem               # "frame_T_m0"
    if stem.endswith("_m0"):
        stem = stem[:-3]                       # "frame_T"
    out = pathlib.Path(output_dir) / (stem + ".md")
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    out.write_text(
        f"# AI Visual Triage — Burst (Modes 0 / 3 / 6)\n\n"
        f"**Mode 0:** `{m0}`  \n"
        f"**Mode 3:** `{m3}`  \n"
        f"**Mode 6:** `{m6}`  \n"
        f"**Stats:** `{stats_path or 'none'}`  \n"
        f"**Analyzed:** {ts}  \n"
        f"**Model:** claude-opus-4-7\n\n---\n\n{msg.content[0].text}\n",
        encoding="utf-8",
    )
    print(f"[12b] Burst analysis saved: {out}")
```

### Burst prompt (`PROMPT_BURST`)

```python
PROMPT_BURST = textwrap.dedent("""
    You are performing visual triage on three render-mode views of the same 3D Cornell
    Box frame, captured consecutively (same scene, same cascade state):

    - Mode 0: Final composite (direct + indirect GI, bilateral blur applied if enabled)
    - Mode 3: Indirect GI × 5 (indirect term only, amplified 5× for visibility)
    - Mode 6: GI only (raw indirect GI without amplification)

    Instructions:
    1. Compare the three images to isolate GI contribution: where is indirect light
       bright vs absent relative to the direct-only expectation?
    2. Locate banding artifacts in mode 6 (GI only) — describe their shape and spacing
       to help distinguish probe-grid vs directional-bin vs cascade-boundary sources.
    3. Assess whether the indirect term in mode 3 looks spatially smooth or noisy,
       and whether the bilateral blur in mode 0 has over-smoothed any edges.
    4. Rate each mode: Poor / Fair / Good / Excellent.
    5. Suggest one concrete parameter change based on all three views.

    Be specific about location. Do not speculate about artifacts you cannot see.
""")
```

### `__main__` dispatch

```python
if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--burst":
        # --burst m0 m3 m6 output_dir [stats]
        if len(sys.argv) < 6:
            print("Usage: analyze_screenshot.py --burst <m0> <m3> <m6> <output_dir> [stats]")
            sys.exit(1)
        stats = sys.argv[6] if len(sys.argv) > 6 else None
        analyze_burst(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], stats)
    else:
        # legacy / Phase 6a / 12a single-image mode
        if len(sys.argv) < 3:
            print("Usage: analyze_screenshot.py <image.png> <output_dir> [stats]")
            sys.exit(1)
        stats = sys.argv[3] if len(sys.argv) > 3 else None
        analyze(sys.argv[1], sys.argv[2], stats)
```

---

## Output file layout

```
tools/
  frame_T_m0.png          ← mode 0 screenshot (burst frame 1)
  frame_T_m3.png          ← mode 3 screenshot (burst frame 2)
  frame_T_m6.png          ← mode 6 screenshot (burst frame 3)
  probe_stats_T.json      ← probe stats (same T, written alongside _m0)
  frame_T.md              ← Claude burst analysis
```

All four files share the same timestamp `T` from the _m0 capture.

---

## Interaction with existing features

| Feature | Interaction |
|---|---|
| P-key plain screenshot | Unchanged: `burstState=Idle`, `pendingScreenshotTag=""`, no stats dump |
| `--auto-analyze` | Works: `launchBurstAnalysis()` checks `autoCloseAfterCapture` and runs synchronously, then sets `captureAndAnalysisDone=true`. Aborted burst does NOT set `captureAndAnalysisDone` — app stays open. |
| `statsPathForAnalysis` sticky bug | Not reintroduced: burst stats are written in CapM0 and read in Analyze via the member; plain screenshots pass `statsToPass` (local, empty) as fixed in Review 23. Dependency on that fix is explicit in the header. |
| `autoCaptured` static | Still one-shot per process — the auto-trigger only fires once; the Burst Capture button provides re-trigger capability |
| `useGIBlur` | All three burst modes (0, 3, 6) pass the GI blur condition — screenshots include the bilateral blur if enabled, matching what the user sees |
| Burst write failure | `abortBurst()` restores `savedRenderMode` and resets `burstState=Idle`; mode restored even if window is closed after abort |
| Mid-burst window close | On `WindowShouldClose()` the loop breaks; `delete demo` runs, cleaning GPU resources. Mode is left at whatever the burst had set it to — this is acceptable since the app is exiting. If a restore-on-destruct is needed, the destructor can check `burstState != Idle`. |
| Re-trigger while active | `burstState == Idle` guard in both the auto-capture block and the Burst Capture button prevents re-entry. |

---

## `--auto-analyze` end-to-end with burst

```
t=0s    app starts
t≈1s    cascadeReady=true
t=5s    auto-burst trigger: burstState=CapM0
t=5s    same frame: CapM0 handler: mode→0, pendingScreenshot, pendingStatsDump
t=5s    takeScreenshot(): frame_T_m0.png + probe_stats_T.json → lastScreenshotPath
t≈5.1s  CapM3: burstPaths[0]=_m0, mode→3, pendingScreenshot
t≈5.1s  takeScreenshot(): frame_T_m3.png → lastScreenshotPath
t≈5.2s  CapM6: burstPaths[1]=_m3, mode→6, pendingScreenshot
t≈5.2s  takeScreenshot(): frame_T_m6.png → lastScreenshotPath
t≈5.3s  Analyze: burstPaths[2]=_m6, mode restored, launchBurstAnalysis() [BLOCKING]
t≈20s   Python returns, captureAndAnalysisDone=true → main loop breaks → exit 0
```

---

## Implementation order

1. Add new members to `demo3d.h`
2. Update `takeScreenshot()`: tag + `lastScreenshotPath`
3. Update `render()`: replace Phase 12a trigger with burst trigger; add burst state machine
4. Add `launchBurstAnalysis()` to `demo3d.cpp`
5. Add Burst Capture button to `renderSettingsPanel()`
6. Extend `analyze_screenshot.py` with `--burst` mode

---

## Verification checklist

| Check | Procedure |
|---|---|
| No UI flicker during burst | Watch the HUD — render mode label should not change during 3-frame burst |
| Three tagged PNGs written | `ls -t tools/` shows `frame_T_m0.png`, `_m3.png`, `_m6.png` with same T |
| Stats JSON paired with _m0 | `probe_stats_T.json` timestamp matches `frame_T_m0.png` timestamp |
| `frame_T.md` output | MD filename = `frame_T.md` (no `_m0` suffix) |
| Burst button grayed during burst | Clicking "Burst Capture" mid-burst has no effect |
| Mode restored after burst | After analysis launches, `raymarchRenderMode` returns to pre-burst value |
| P-key still works | Plain screenshot: no tag, no stats dump, no burst interference |
| `--auto-analyze` exits | App exits cleanly after burst analysis script returns |
| Single-image path unchanged | P-key + AI analysis: script called without `--burst`, output same as Phase 12a |
