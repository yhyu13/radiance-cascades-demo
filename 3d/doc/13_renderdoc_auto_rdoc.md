# `--auto-rdoc` — OBSOLETE (2026-08-22)

**Do not run `--auto-rdoc`.** The flag is rejected at process entry (exit 2).
The replacement is `rdc-cli` (skill `renderdoc-gpu-debug`):

```powershell
rdc capture --frame 480 --timeout 180 --json -- `
  .\build\RadianceCascades3D.exe --runtime-shell=legacy --exit-frames=600
rdc open tools\captures\rdoc_frame_frame480.rdc
rdc counters --name "GPU Duration" --json
```

Do not pass `--wait-for-exit`. G-key still saves a `.rdc` (no auto-extract).
Forensic replay of this old pipeline: `RDOC_LEGACY=1`. See `doc/journey.md` Era 12.

---

# Historical: how the in-process RenderDoc capture worked

A learning note on the project's (retired) headless GPU-capture flag:

```powershell
cd 3d
# REJECTED — kept only as the historical command:
# .\build\RadianceCascades3D.exe --runtime-shell=legacy --auto-rdoc --exit-frames=3000
```

One flag: captures a single GPU frame in-process after an 8s warm-up, then
auto-extracts stage textures + GPU timing via qrenderdoc's embedded Python. It runs
inside the app (no external RenderDoc UI), so it works headless in CI/agent loops.

## Outputs (per capture)

| Path | What |
|---|---|
| `tools/captures/rdoc_frame_frame<F>.rdc` | the RenderDoc capture |
| `tools/analysis/rdoc_frame_frame<F>_manifest.json` | GPU timing table |
| `tools/analysis/rdoc_frame_frame<F>_extract.log` | action-tree walk |
| `tools/analysis/rdoc_frame_frame<F>_<sdfTexture\|albedoTexture\|cascade0_probeAtlas\|…>.png` | exported stage slices |
| `tools/analysis/rdoc_frame_frame<F>_pipeline.md` | only if `analyze_renderdoc.py` succeeds (it usually doesn't, see below) |

`<F>` is the frame number at capture time (`renderFrameIndex`). The final-frame thumbnail
is extracted separately: `renderdoccmd.exe thumb --out <x>.png --format png <x>.rdc`.

## The flow, step by step

1. **CLI parse** (`main3d.cpp:464-466`): `--auto-rdoc` → `demo->setAutoRdocMode(8.0f)`.

2. **Pre-load RenderDoc DLL *before* the GL context** (`main3d.cpp:359-371`).
   `rdoc_load_api()` loads `C:/Program Files/RenderDoc/renderdoc.dll` and calls
   `RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, &api)`. This **must** happen before
   `InitWindow()`: RenderDoc hooks OpenGL at context-creation time, so loading after
   makes captures silent no-ops. Missing DLL → returns false, capture disabled, no crash.
   The returned pointer here is deliberately discarded (`rdoc_preload`); the real fetch
   happens later in `initRenderDoc()`.

   The loader lives in its own TU (`rdoc_helper.cpp`) because `<windows.h>` clashes with
   raylib's `winuser.h` (CloseWindow / ShowCursor overload).

3. **Init** (`demo3d.cpp:6523-6548`, `initRenderDoc()`, called at demo construction):
   walks up from the exe to the project root (the dir containing `doc/`), then sets
   `rdocCaptureDir = <root>/tools/captures`, `rdocAnalysisDir = <root>/tools/analysis`,
   `SetCaptureFilePathTemplate("<captures>/rdoc_frame")` (RenderDoc appends `_frame<F>.rdc`),
   and `MaskOverlayBits(eRENDERDOC_Overlay_None, …)` so the capture has no overlay.

4. **Warm-up timer** (`demo3d.cpp:754-766`, in `update()`): once `time >= 8s` it
   (a) sets `skipUIRendering = true` at `8s − 0.1s` — hides ImGui one frame early so the
   captured frame is a clean 3D-only frame, and (b) sets `pendingRdocCapture = true`
   (latched by `autoRdocFired`, fires once).

5. **Queue the capture** (`demo3d.cpp:6550-6567`, `beginRdocFrameIfPending()`, called at
   the TOP of the main loop *before* `update()`):
   - `rdocCaptureCountBefore = GetNumCaptures()` (baseline for polling),
   - `forceCascadeRebuild = true` + `rdocForceRebuildCount = 2` + `renderFrameIndex = 0`
     — forces ALL cascades to dispatch in the captured frame (see subtleties),
   - `rdoc->TriggerCapture()` — captures the **next presented frame** (next SwapBuffers).

6. **Frame loop** (`main3d.cpp:1034-1178`): `beginRdocFrameIfPending()` → `update()`
   (cascade compute dispatches run here) → `render()` → UI (skipped if
   `skipUIRendering`) → `EndDrawing()` (SwapBuffers — RenderDoc actually captures here)
   → `++frameCounter` + `--exit-frames` check → `endRdocFrameIfPending()`.

7. **Collect** (`demo3d.cpp:6569-6584`, `endRdocFrameIfPending()`): polls
   `GetNumCaptures()` until it increments (the capture appears after the SwapBuffers; may
   take 1–2 frames). Then `GetCapture(n-1, path, &len, &ts)` → `launchRdocAnalysis(path)`.

8. **Auto-analyze** (`demo3d.cpp:6586-6628`, detached `std::thread`):
   - sets env `RDOC_CAPTURE=<path>` and `RDOC_OUTDIR=<tools/analysis>`,
   - step 1: `qrenderdoc.exe --py rdoc_extract.py` (blocks) — opens the .rdc via the
     embedded renderdoc Python API, walks the action tree, fetches the `GPU Duration`
     counter, saves textures + manifest + extract.log. The script ends with `os._exit(0)`
     so qrenderdoc's Qt GUI never opens.
   - step 2: `python analyze_renderdoc.py` — reads the manifest, calls Claude, writes
     `_pipeline.md`.

## Why the two-step Python split

`qrenderdoc.exe` embeds **Python 3.6 + the `renderdoc` module** (no `anthropic`).
System `python` has `anthropic` (no `renderdoc` module). So extraction must run inside
qrenderdoc, and the Claude analysis must run under system Python — two separate
subprocesses sharing the env vars.

## Subtleties that bite

- **`TriggerCapture()` is +1 frame.** It captures the *next* SwapBuffers, not the current
  frame. The code compensates by forcing the cascade rebuild for 2 frames
  (`rdocForceRebuildCount = 2`) so the dispatched state survives into the captured frame.
- **Cascade stagger reset.** The bake staggers which cascade rebuilds each frame
  (`renderFrameIndex % cascadeCount`). A capture needs all 4 cascades in ONE frame, so
  `renderFrameIndex = 0` + `forceCascadeRebuild = true` forces the full pipeline.
- **Window association.** `TriggerCapture()` is used instead of
  `Start/EndFrameCapture(null,null)`, which fails when called before `BeginDrawing()`
  (RenderDoc needs a window association to hook the swapchain).
- **`--exit-frames` must outlast the 8s warm-up.** At ~60fps, 400 frames ≈ 6s < 8s → the
  app exits before the trigger fires. Use `--exit-frames=3000` (≈ 50s). This is the most
  common reason a run silently produces no `.rdc`.
- **`analyze_renderdoc.py` (step 2) fails under Python 2.7** — non-ASCII source, no
  `# -*- coding -*-` declaration. Step 1 (`rdoc_extract.py`) is unaffected (qrenderdoc's
  Python 3.6). `[PHASE0] OpenGL error 0x501` at demo init is pre-existing and does not
  affect the captured frame.
- **Auto output goes to `tools/analysis/`**, not `tools/captures/`. A manual
  `qrenderdoc.exe --py rdoc_extract.py` with `RDOC_OUTDIR=<anywhere>` can target any dir;
  the in-app auto path always uses `tools/analysis`.

## Manual re-run (same extraction, no app)

```powershell
$env:RDOC_CAPTURE = "D:\…\tools\captures\rdoc_frame_frame420.rdc"
$env:RDOC_OUTDIR  = "D:\…\tools\captures"
& "C:\Program Files\RenderDoc\qrenderdoc.exe" --py "D:\…\tools\rdoc_extract.py"
& "C:\Program Files\RenderDoc\renderdoccmd.exe" thumb --out "…\frame420_thumb.png" --format png "…\rdoc_frame_frame420.rdc"
```
