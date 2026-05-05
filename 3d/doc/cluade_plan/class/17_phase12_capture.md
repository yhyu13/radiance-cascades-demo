# 17 — Capture Pipeline (Phases 6a / 6b / 12a / 12b / 14a)

**Purpose:** Understand the screenshot, RenderDoc capture, and auto-analyze CLI modes.

---

## Overview

The capture pipeline provides three independent but composable capture systems:

1. **Screenshot + AI analysis** (Phase 6a) — single PNG or burst of 3 modes → `analyze_screenshot.py`
2. **RenderDoc GPU frame capture** (Phase 6b) — full pipeline GPU trace → `rdoc_extract.py`
3. **Multi-frame sequence capture** (Phase 14a) — 8 frames × jitter positions → temporal analysis

All three are exposed via:
- Manual keyboard/button triggers in the UI
- CLI flags: `--auto-analyze`, `--auto-sequence`, `--auto-rdoc`

---

## Phase 6a — Screenshot + AI analysis

### Path resolution

At construction, `initToolsPaths()` resolves absolute paths from the executable location:

```
screenshotDir = <exe_dir>/tools/          # PNG output
analysisDir   = <exe_dir>/tools/          # matches screenshotDir
toolsScript   = <exe_dir>/tools/analyze_screenshot.py
```

This is independent of the working directory, so paths are correct regardless of how
the exe is launched.

### takeScreenshot(launchAiAnalysis)

Called in the main loop between `EndMode3D()` and `rlImGuiBegin()`:
- Writes a PNG to `screenshotDir` using `stb_image_write`.
- Filename includes a high-resolution timestamp + optional tag (e.g. `_m0`, `_m3`, `_m6`).
- If `launchAiAnalysis=true` and `pendingStatsDump=true`, also writes a JSON probe stats
  file alongside the PNG, then calls `launchAnalysis(imagePath, statsPath)`.

### launchAnalysis

Spawns `analyze_screenshot.py` in a detached background thread:
```
python tools/analyze_screenshot.py <imagePath> [<statsPath>]
```
The tool sends the image (and optional stats JSON) to the Anthropic API and writes
a `.md` analysis file next to the PNG.

---

## Phase 6b — RenderDoc In-Process GPU Capture

### DLL loading order

RenderDoc **must** hook into OpenGL at context creation time.
`main3d.cpp` calls `rdoc_load_api()` before `InitWindow()`:

```cpp
// BEFORE InitWindow():
RENDERDOC_API_1_6_0* rdoc_preload = nullptr;
rdoc_load_api(&rdoc_preload);

// AFTER InitWindow() — Demo3D constructor:
initRenderDoc();  // acquires the same API pointer
```

If `renderdoc.dll` is not installed, both calls silently return false — no effect.

### beginRdocFrameIfPending / endRdocFrameIfPending

Called in the main loop around `update()` and `render()`:

```cpp
demo->beginRdocFrameIfPending();   // BEFORE update (before cascade dispatches)
demo->update();
BeginDrawing();
    demo->render();
EndMode3D();
demo->endRdocFrameIfPending();     // AFTER render
```

Bracketing the cascade dispatches inside the RenderDoc frame ensures the GPU trace
includes all compute shader work (SDF bake, cascade bake, temporal blend, GI blur).

### autoRdocDelaySeconds

Set by `--auto-rdoc` CLI flag to 8.0 seconds. After that delay, one frame is captured.
The `autoRdocFired` latch prevents subsequent captures.
The RenderDoc `.rdc` file is saved to `tools/captures/`.

---

## Phase 12a — Auto-capture + stats dump

### autoCaptureDelaySeconds

After this many seconds from launch (default 5.0, 0=disabled), a capture is triggered
automatically. Used by `--auto-analyze` to ensure the scene has converged before capture.

### pendingStatsDump

When true, the next `takeScreenshot()` call also writes a JSON file containing
probe readback stats (per-cascade: nonZero, surfaceHit, skyHit, maxLum, meanLum,
variance, histogram). The JSON path is passed to `launchBurstAnalysis()`.

### autoCloseAfterCapture / captureAndAnalysisDone

Set by `--auto-analyze`. After the burst completes and analysis is launched,
`captureAndAnalysisDone` is set true. The main loop checks `isReadyToClose()`
and breaks — the process exits.

---

## Phase 12b — Burst state machine

Captures 3 screenshots at different render modes then launches multi-image analysis.

```
BurstState::Idle
    → user triggers burst (or auto-capture fires)
BurstState::CapM0  — set renderMode=0 (final image), takeScreenshot tag="_m0"
    → next frame
BurstState::CapM3  — set renderMode=3 (indirect×5), takeScreenshot tag="_m3"
    → next frame
BurstState::CapM6  — set renderMode=6 (GI only), takeScreenshot tag="_m6"
    → next frame
BurstState::Analyze — restore original renderMode, launchBurstAnalysis()
    → Idle
```

`burstPaths[3]` collects the three PNG paths. `launchBurstAnalysis()` spawns
`analyze_screenshot.py` with all three paths and the stats JSON.

`savedRenderMode` stores the pre-burst mode so it can be restored in `Analyze`.

---

## Phase 14a — Multi-frame sequence capture

Captures `seqFrameCount = 8` consecutive frames — one per jitter position in the
Halton cycle — to analyze temporal convergence and jitter artifacts.

```
SeqCapState::Idle
    → trigger
SeqCapState::Capturing
    each frame: takeScreenshot(tag="_f<n>"), push to seqPaths
    after seqFrameCount frames: launchSequenceAnalysis()
    → Idle
```

`launchSequenceAnalysis()` spawns `analyze_screenshot.py` with all 8 frame paths.
The AI tool can then compare frames to assess jitter smoothness and convergence speed.

---

## CLI flags summary

| Flag | Effect |
|---|---|
| `--auto-analyze` | Sets `autoCloseAfterCapture=true`; triggers burst capture + analysis after 5s warm-up; exits on completion |
| `--auto-sequence` | Sets `autoCloseAfterCapture=true`; triggers 8-frame sequence capture + analysis after 5s; exits on completion |
| `--auto-rdoc` | Calls `setAutoRdocMode(8.0f)`; triggers one RenderDoc frame capture 8s after launch; stays open |
