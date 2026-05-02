# Phase 14a — Multi-Frame Sequence Capture: Implementation

**Date:** 2026-05-02  
**Depends on:** Phase 12b (`pendingScreenshotTag`, `lastScreenshotPath`, `pendingStatsDump`,
`statsPathForAnalysis`, `launchBurstAnalysis()` patterns) + Phase 13a (jitter defaults that
expose the flickering this phase is designed to detect).

---

## Motivation

With `probeJitterScale=0.25` and `jitterHoldFrames=2`, each of the 4 jitter positions holds
for 2 rendered frames, giving an 8-frame cycle. If `temporalAlpha` is set too high, or jitter
amplitude too large, the EMA does not damp inter-position GI differences before the next jitter
step fires. The result is per-frame luminance variation on lit surfaces — flickering visible
when watching the app in real time, but invisible in a single-frame burst snapshot.

**Phase 14a solves this** by capturing N consecutive frames (default N=8 = one full cycle),
sending all frames to Claude in a single API call, and asking for temporal stability analysis.
The same infrastructure is reusable for future multi-frame RDC analysis.

---

## What was implemented

| Component | Change |
|---|---|
| `src/demo3d.h` | `SeqCapState` enum + 4 new members + `launchSequenceAnalysis()` decl |
| `src/demo3d.cpp` — `render()` | Sequence state machine (parallel to burst, gated on `burstState==Idle`) |
| `src/demo3d.cpp` — `renderSettingsPanel()` | "Seq Capture" button + "Seq Frames" slider |
| `src/demo3d.cpp` — `takeScreenshot()` | Guard extended to suppress single-image analysis during sequence |
| `src/demo3d.cpp` — `launchSequenceAnalysis()` | New method: strip `_f0`, build `--sequence` command, respect auto-close |
| `tools/analyze_screenshot.py` | `PROMPT_SEQUENCE`, `analyze_sequence()`, `--sequence` dispatch |

---

## New header members (`src/demo3d.h`)

```cpp
// Phase 14a — multi-frame sequence capture (temporal jitter analysis)
enum class SeqCapState { Idle, Capturing };
SeqCapState              seqCapState   = SeqCapState::Idle;
int                      seqFrameCount = 8;   // frames to capture (one jitter cycle)
int                      seqFrameIndex = 0;   // next frame index to request
std::vector<std::string> seqPaths;            // collected frame paths

void launchSequenceAnalysis();
```

`seqFrameCount` default = 8 = `jitterPatternSize(4) × jitterHoldFrames(2)`.  
`seqFrameIndex` is the *next* frame index to request (0-based); it equals `seqPaths.size()`
after each successful collect.

**Contrast with burst:** burst uses discrete enum states (CapM0/CapM3/CapM6/Analyze) because
it also changes `raymarchRenderMode` between frames. Sequence keeps the mode fixed and uses an
integer index — simpler, and extensible to arbitrary N.

---

## Sequence state machine (`render()`)

Placed after the burst state machine block, before `raymarchPass()`. Runs only when
`burstState == BurstState::Idle` so `lastScreenshotPath` is unambiguous.

```cpp
if (burstState == BurstState::Idle) {
    auto abortSeq = [&](const std::string& reason) {
        std::cerr << "[14a] Sequence aborted: " << reason << "\n";
        seqCapState = SeqCapState::Idle;
        seqPaths.clear();
        seqFrameIndex = 0;
        lastScreenshotPath.clear();
    };

    if (seqCapState == SeqCapState::Capturing) {
        // Step 1: collect the screenshot written after the previous tick.
        if (seqFrameIndex > 0) {
            std::string tag = "_f" + std::to_string(seqFrameIndex - 1) + ".png";
            if (lastScreenshotPath.empty() ||
                    lastScreenshotPath.find(tag) == std::string::npos) {
                abortSeq("_f" + std::to_string(seqFrameIndex - 1) + " write failed");
            } else {
                seqPaths.push_back(lastScreenshotPath);
                lastScreenshotPath.clear();
            }
        }

        // Step 2: request next frame or finish.
        if (seqCapState == SeqCapState::Capturing) {
            if (seqFrameIndex < seqFrameCount) {
                if (seqFrameIndex == 0)
                    pendingStatsDump = true;  // capture stats alongside first frame
                pendingScreenshotTag = "_f" + std::to_string(seqFrameIndex);
                pendingScreenshot    = true;
                seqFrameIndex++;
            } else {
                // All seqFrameCount frames collected — launch analysis.
                seqCapState   = SeqCapState::Idle;
                seqFrameIndex = 0;
                launchSequenceAnalysis();
            }
        }
    }
}
```

### Timing diagram (N=3 for brevity)

```
UI click (frame N-1 ImGui)   seqCapState → Capturing, seqFrameIndex=0, seqPaths.clear()

Frame N    [index=0]  Step1: skip (index==0)
                      Step2: pendingStatsDump=true, pendingScreenshotTag="_f0",
                             pendingScreenshot=true, index→1
           (after render) takeScreenshot(): frame_T_f0.png + probe_stats_T.json
                                             → lastScreenshotPath="...frame_T_f0.png"

Frame N+1  [index=1]  Step1: validate lastScreenshotPath contains "_f0.png"
                             → seqPaths[0]="...frame_T_f0.png", lastScreenshotPath.clear()
                      Step2: pendingScreenshotTag="_f1", pendingScreenshot=true, index→2
           (after render) takeScreenshot(): frame_T_f1.png → lastScreenshotPath

Frame N+2  [index=2]  Step1: validate "_f1.png" → seqPaths[1]
                      Step2: "_f2", index→3

Frame N+3  [index=3=seqFrameCount]
                      Step1: validate "_f2.png" → seqPaths[2]
                      Step2: index >= seqFrameCount → launchSequenceAnalysis()
```

**Total frames to completion: seqFrameCount + 1 render ticks** (one tick to start the pipeline
per the ImGui→render ordering; one extra tick to collect the last screenshot).

**Stats captured once:** `pendingStatsDump=true` only when `seqFrameIndex==0`. This captures
the **initial parameter context** (dirRes, temporalAlpha, probeJitterScale, cascade stats) at
the start of the sequence — not per-frame temporal state. The runtime jitter position and EMA
history intentionally evolve across the sequence; that evolution is the signal being observed,
and it is not reflected in the JSON. The stats are a convenience snapshot passed to
`launchSequenceAnalysis()` via `statsPathForAnalysis` (same member used by burst).

---

## `launchSequenceAnalysis()`

```cpp
void Demo3D::launchSequenceAnalysis() {
    namespace fs = std::filesystem;
    std::string stem = fs::path(seqPaths[0]).stem().string();  // "frame_T_f0"
    if (stem.size() >= 3 && stem.substr(stem.size() - 3) == "_f0")
        stem = stem.substr(0, stem.size() - 3);                // "frame_T"
    lastAnalysisPath = analysisDir + "/" + stem + "_seq.md";   // "frame_T_seq.md"

    // --sequence <output_dir> <f0> <f1> ... [stats.json]
    std::string cmd = "python \"" + toolsScript + "\""
                    + " --sequence"
                    + " \"" + analysisDir + "\"";
    for (const auto& p : seqPaths)
        cmd += " \"" + p + "\"";
    if (!statsPathForAnalysis.empty())
        cmd += " \"" + statsPathForAnalysis + "\"";

    if (autoCloseAfterCapture) {
        int ret = system(cmd.c_str());   // blocking for --auto-analyze mode
        if (ret != 0) std::cerr << "[14a] Sequence analysis failed (exit " << ret << ")\n";
        captureAndAnalysisDone = true;
    } else {
        std::thread([cmd]() { system(cmd.c_str()); }).detach();
    }
}
```

Output file suffix `_seq` distinguishes sequence output (`frame_T_seq.md`) from burst output
(`frame_T.md`) even when triggered on the same timestamp root.

**`launchSequenceAnalysis()` trusts `seqPaths` after per-frame tag validation.** Each frame's
path is validated against its expected `_fN.png` tag during collection; any mismatch aborts
the sequence. However there is no cross-vector ordering or timestamp check on the completed
vector. This is sufficient for the current static-scene single-process use case where frames
are collected CPU-sequentially; a stricter check (e.g., monotonic timestamp validation) would
be needed if sequence capture were extended to async or multi-process scenarios.

---

## `takeScreenshot()` guard update

The existing guard suppressed single-image AI analysis during burst. Extended to also cover
sequence:

```cpp
// Old:
if (launchAiAnalysis && burstState == BurstState::Idle)
    launchAnalysis(path, statsToPass);

// New:
if (launchAiAnalysis && burstState == BurstState::Idle && seqCapState == SeqCapState::Idle)
    launchAnalysis(path, statsToPass);
```

Without this, each of the N sequence frames would fire `launchAnalysis()` (N separate API
calls), because `burstState` IS `Idle` during sequence capture.

---

## UI additions (`renderSettingsPanel()`)

```cpp
ImGui::SameLine();
if (ImGui::Button("Seq Capture##seq")) {
    if (burstState == BurstState::Idle && seqCapState == SeqCapState::Idle) {
        seqCapState   = SeqCapState::Capturing;
        seqFrameIndex = 0;
        seqPaths.clear();
        lastScreenshotPath.clear();   // prevent stale path from previous capture
    }
}
// tooltip: describes Phase 14a purpose and output filename

ImGui::SliderInt("Seq Frames##seq", &seqFrameCount, 2, 32);
// tooltip: "8 = one full jitter cycle at jitterPatternSize=4, holdFrames=2"
```

`lastScreenshotPath.clear()` on button click is defensive: if a plain P-key screenshot
happened in the same or immediately preceding frame, that path would still be in the member
when `seqFrameIndex=1` runs its Step 1 check. The clear forces the check to wait for the
actual `_f0.png` path.

---

## Python script (`tools/analyze_screenshot.py`)

### Dispatch

```python
if sys.argv[1] == "--sequence":
    # --sequence <output_dir> <f0.png> <f1.png> ... [stats.json]
    output_dir = sys.argv[2]
    if sys.argv[-1].endswith(".json") and len(sys.argv) > 4:
        stats      = sys.argv[-1]
        frame_paths = sys.argv[3:-1]
    else:
        stats      = None
        frame_paths = sys.argv[3:]
    analyze_sequence(frame_paths, output_dir, stats)
```

Stats detection: if the last argument ends in `.json` AND there are at least 2 frame paths
(to avoid treating a single-frame stats-less call as stats-bearing), it is treated as the
stats path. Otherwise all trailing args are frame paths.

### `analyze_sequence()`

```python
def analyze_sequence(frame_paths, output_dir, stats_path=None):
    content = []
    for i, path in enumerate(frame_paths):
        content.append({"type": "text",  "text": f"### Frame f{i}"})
        content.append({"type": "image", "source": {"type": "base64",
                        "media_type": "image/png",
                        "data": base64.standard_b64encode(
                            pathlib.Path(path).read_bytes()).decode()}})
    # ... append PROMPT_SEQUENCE (and PROMPT_STATS_SUFFIX if stats present) ...
    msg = client.messages.create(model="claude-opus-4-7", max_tokens=2000,
                                 messages=[{"role": "user", "content": content}])
    # output: strip "_f0" → "frame_T" → "frame_T_seq.md"
```

All N frames plus the prompt are sent in a single API call. Claude sees the full temporal
sequence and can compare frame-to-frame differences directly.

### `PROMPT_SEQUENCE`

Four instructions:

1. Describe what is **stable** across all frames (1 sentence)
2. Identify regions that **change** between frames: where, magnitude, and temporal pattern
   - Random per-frame noise → alpha too high or jitter amplitude too large
   - Periodic every 2 or 4 frames → jitter cycle not damped by EMA at current alpha
   - Slow monotonic drift → EMA converging (acceptable)
3. Rate temporal stability: **Unstable / Marginal / Stable / Excellent**
4. Suggest one concrete parameter change (`temporalAlpha`, `probeJitterScale`,
   `jitterHoldFrames`, `jitterPatternSize`)

---

## Output file layout

```
tools/
  frame_T_f0.png          ← frame 0 (jitter position A, hold frame 0)
  frame_T_f1.png          ← frame 1 (jitter position A, hold frame 1)
  frame_T_f2.png          ← frame 2 (jitter position B, hold frame 0)
  ...
  frame_T_f7.png          ← frame 7 (jitter position D, hold frame 1)
  probe_stats_T.json      ← initial parameter snapshot (jitterScale, alpha, cascade stats at f0)
  frame_T_seq.md          ← Claude temporal stability report
```

All files share timestamp `T` from the `_f0` capture. The `_seq.md` suffix distinguishes
from burst output (`frame_T.md`) even if both are triggered with the same timestamp root.

---

## Key design decisions

| Decision | Rationale |
|---|---|
| Two-state enum (Idle/Capturing) vs. N discrete states | Integer index scales to arbitrary N without code changes; burst needs mode switching so discrete states were warranted |
| N+1 ticks to complete N frames | UI click happens in ImGui (end of frame N-1's render); state machine sees Capturing at start of frame N and requests _f0. One extra tick collects the last screenshot |
| `pendingStatsDump` only on `seqFrameIndex==0` | Captures the initial parameter snapshot (dirRes, alpha, jitterScale). The runtime jitter/EMA state intentionally evolves across frames — that evolution is the signal. Per-frame stats would be identical on a static scene and add no information |
| `lastScreenshotPath.clear()` on button click | Prevents a stale path from a prior P-key or burst frame polluting the `_f0` collection check on tick N+1 |
| Output suffix `_seq` not `_seq0` | The analysis is a single document covering all N frames; the zero index would imply multiple per-frame outputs |
| Guard for `launchAiAnalysis` in `takeScreenshot()` | Without `seqCapState==Idle` check, each sequence frame fires `launchAnalysis()` — N single-image API calls instead of 1 sequence call |
| Stats detection by `.json` suffix | Avoids a required `--stats` flag while keeping CLI readable; stats is always the last argument when present |

---

## Interaction with existing features

| Feature | Interaction |
|---|---|
| Burst Capture | `burstState==Idle` required before sequence starts; they cannot interleave |
| P-key plain screenshot | Unchanged: both state machines idle, no tag, normal `launchAnalysis()` call |
| Phase 12a auto-capture trigger | Not modified: still starts a burst; sequence has no auto-trigger |
| `statsPathForAnalysis` | Sequence writes stats on `_f0`, reads it in `launchSequenceAnalysis()` — same member as burst; safe because burst and sequence cannot overlap |
| `--auto-analyze` CLI mode | `launchSequenceAnalysis()` runs synchronously and sets `captureAndAnalysisDone=true`; aborted sequence does NOT set it |

---

## Verification checklist

| Check | Procedure |
|---|---|
| N tagged PNGs written | `ls -t tools/` shows `frame_T_f0.png` … `frame_T_f7.png` |
| Stats JSON paired with f0 | `probe_stats_T.json` timestamp matches `frame_T_f0.png` |
| `frame_T_seq.md` output | Suffix `_seq`, not `_f0` |
| Mode unchanged across sequence | All frames look the same render mode as at click time |
| Re-entry blocked during capture | Clicking "Seq Capture" mid-sequence has no effect |
| Burst + Seq mutually exclusive | Click "Burst Capture" then immediately "Seq Capture": sequence ignored |
| P-key still works normally | No `_f0` tag, no sequence interference |
| `--auto-analyze` exits cleanly | App exits after sequence analysis script returns |
| Temporal instability visible | At `probeJitterScale=0.25, temporalAlpha=0.20`, Claude should report Marginal/Unstable and name the jitter-driven pattern |
