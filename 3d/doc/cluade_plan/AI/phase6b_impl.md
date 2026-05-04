# Phase 6b — Implementation Record

**Date:** 2026-05-03  
**Status:** IMPLEMENTED — awaiting first live capture validation  
**Depends on:** Phase 6a (screenshot + AI), Phase 14c (C1 coverage validated)

---

## What was built

Phase 6b is a two-component GPU analysis tool triggered by pressing **G** in the app,
or automatically after a configurable delay (see §Auto-capture). It is opt-in and
silently disables itself if RenderDoc is not installed.

### Component A — Pipeline texture snapshot

At end-of-frame, named 3D textures are extracted as PNG slices and sent to Claude
for visual analysis. Textures labeled:

| Label | Content |
|---|---|
| `sdfTexture` | Signed distance field volume |
| `albedoTexture` | Surface color volume |
| `cascade0_probeAtlas` | C0 directional probe atlas (D×D tile per probe) |
| `cascade1_probeAtlas` | C1 directional probe atlas (Phase 14c: tMax=1.0wu) |
| `cascade0_probeGrid` | C0 isotropic probe grid (reduction output) |

### Component B — GPU performance timing

Walks all dispatches and draws in the captured frame via `controller.GetRootActions()`.
Each event's `duration` (nanoseconds, driver-provided) is collected, keyword-matched
against `PASS_KEYWORDS`, and reported as a markdown µs table. Unrecognized dispatches
print `[6b WARNING]` so labeling drift is visible.

Expected output for a full frame (no stagger):
```
| Cascade bake (C3)      | dispatch | ~Xµs |
| Cascade reduction (C3) | dispatch | ~Xµs |
...
| Cascade bake (C0)      | dispatch | ~Xµs |  ← elevated by c0MinRange=1.0
| Cascade bake (C1)      | dispatch | ~Xµs |  ← elevated by c1MinRange=1.0
| Raymarching            | draw     | ~Xµs |
```

---

## Files changed

| File | Role |
|---|---|
| `lib/renderdoc/renderdoc_app.h` | Public-domain RenderDoc capture API (CC0, copied from install) |
| `src/rdoc_helper.h` | Thin shim: declares `rdoc_load_api()` |
| `src/rdoc_helper.cpp` | Isolates `<windows.h>` from raylib.h TUs (CloseWindow/ShowCursor clash) |
| `src/demo3d.h` | `#include "rdoc_helper.h"`; private fields; public `begin/endRdocFrameIfPending()` |
| `src/demo3d.cpp` | `initRenderDoc()`, G-key handler, capture lifecycle, `launchRdocAnalysis()`, `glObjectLabel` calls |
| `src/main3d.cpp` | `beginRdocFrameIfPending()` before `BeginDrawing()`; `endRdocFrameIfPending()` after `EndDrawing()` |
| `CMakeLists.txt` | `lib/renderdoc` include dir; `rdoc_helper.cpp` added to sources |
| `tools/analyze_renderdoc.py` | Full two-component analysis script |

---

## Key design decisions

### rdoc_helper.cpp isolation

`<windows.h>` → `winuser.h` declares `CloseWindow(HWND)` and `ShowCursor(BOOL)`.
Raylib's `raylib.h` declares `CloseWindow(void)` and `ShowCursor(bool)` with `extern "C"`.
These cannot coexist in the same translation unit (MSVC C2733: cannot overload extern-C
function). Solution: `rdoc_helper.cpp` includes `<windows.h>` and `renderdoc_app.h` but
NOT `raylib.h`. It exposes only `rdoc_load_api(RENDERDOC_API_1_6_0**)` — a single
boolean call that `demo3d.cpp` uses safely.

### Capture path resolution

`rdocCaptureDir` is resolved at init time by walking up from the exe directory until a
`doc/` sibling is found — the same pattern as `initToolsPaths()`. RenderDoc appends
`_<n>.rdc` to the template; `GetCapture()` retrieves the actual path after each capture.

### glObjectLabel placement

- Volume textures: labeled immediately after creation in `createVolumeBuffers()`
- Cascade textures (atlas, grid, histories): labeled inside `initCascades()` for loop,
  after all `glTexParameteri` calls, using `"cascade{i}_probeAtlas"` etc.
- Programs: labeled inside `loadShader()` after successful link/load, using the shader
  filename as the label (e.g. `"radiance_3d.comp"`) — matches `PASS_KEYWORDS` keys

### Analysis script invocation

`launchRdocAnalysis()` spawns a detached `std::thread` running:
```
"C:/Program Files/RenderDoc/renderdoccmd.exe" python tools/analyze_renderdoc.py
    <capture.rdc> <analysis_dir>
```
`renderdoccmd.exe python` provides the `renderdoc` module. The analysis runs asynchronously
so the app frame loop is not blocked.

---

## Auto-capture (added after initial implementation)

A `--auto-rdoc` CLI flag (or the `autoRdocDelaySeconds` member) queues a G capture
automatically after a warm-up delay. This enables headless CI-style capture without
requiring a human to press G. Manual G press is still available alongside.

Members added to `demo3d.h`:
```cpp
float    autoRdocDelaySeconds = 0.0f;  // 0 = disabled; set via --auto-rdoc or UI
bool     autoRdocFired        = false; // latch: only fire once per session
```

Logic in `update()`:
```cpp
if (autoRdocDelaySeconds > 0.0f && !autoRdocFired && rdoc && time >= autoRdocDelaySeconds) {
    pendingRdocCapture = true;
    autoRdocFired = true;
    std::cout << "[6b] Auto-capture triggered (t=" << time << "s)\n";
}
```

`main3d.cpp` sets the flag from `--auto-rdoc`:
```cpp
demo->setAutoRdocMode(8.0f);  // 8-second warm-up
```

---

## Verification checklist

| Step | Expected |
|---|---|
| App starts without RenderDoc | `[6b] RenderDoc DLL not found — GPU capture disabled.` |
| App starts with RenderDoc | `[6b] RenderDoc in-process API loaded OK. Press G to capture.` |
| Press G (or auto-capture fires) | `[6b] RenderDoc capture queued for next frame.` |
| After frame ends | `[6b] Capture saved: .../captures/rdoc_frame_1.rdc` |
| Python script runs | `[6b] Analysis saved: .../tools/rdoc_frame_1_pipeline.md` |
| GPU timing table | Non-zero µs values; C0/C1 bake rows visible and elevated vs C2/C3 |
| Texture analysis | No "Resource not found" errors; atlas analysis shows populated tiles |
| Phase 14c validation | C0 and C1 bake costs both elevated; confirms extended tMax has measurable cost |
| Can open .rdc manually | RenderDoc GUI opens and shows all passes with labels |
