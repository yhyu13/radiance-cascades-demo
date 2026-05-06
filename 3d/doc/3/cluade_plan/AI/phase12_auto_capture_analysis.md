# Phase 12 — Automatic Render Capture & AI Analysis

**Date:** 2026-05-02
**Trigger:** User request — auto-capture frame after startup, auto-analyze with AI.

---

## Overview

Three incremental phases, each independently usable:

| Phase | What it captures | AI input | New dependencies |
|---|---|---|---|
| 12a | Single screenshot + probe stats JSON | 1 PNG + JSON text | none |
| 12b | 3 render-mode screenshots (0, 3, 6) | 3 PNGs + JSON text | none |
| 12c | RenderDoc in-process .rdc + cascade atlas PNG | 2+ PNGs + JSON text | renderdoc.h |

All phases share the same output pipeline:
- App writes PNG(s) + `probe_stats.json` to `tools/`
- Detached Python script (`tools/analyze_screenshot.py`) reads them, calls Claude API, writes `tools/analysis_TIMESTAMP.md`
- App can optionally display the analysis path in the UI

---

## Phase 12a — Auto Screenshot + Probe Stats JSON

### Goal
After N seconds of startup (default 5s), automatically take a screenshot and dump
the current probe statistics to JSON, then launch the existing AI analysis script
with the new stats file as additional context.

### Trigger (C++ side, `update()`)

```cpp
// Phase 12a: auto-capture on startup
static bool autoCaptured = false;
if (!autoCaptured && GetTime() > autoCapturDelaySeconds && cascadeReady) {
    autoCaptured    = true;
    pendingScreenshot   = true;
    pendingStatsDump    = true;  // new flag — triggers JSON write in takeScreenshot()
}
```

New members (`demo3d.h`):
```cpp
float autoCaptureDelaySeconds;  // default 5.0; 0.0 = disabled
bool  pendingStatsDump;         // written alongside the next screenshot
```

### Probe stats JSON (`takeScreenshot()` extension)

```cpp
if (pendingStatsDump) {
    pendingStatsDump = false;
    nlohmann::json j;  // or hand-rolled JSON string — no extra dep needed
    j["dirRes"]           = dirRes;
    j["cascadeCount"]     = cascadeCount;
    j["temporalAlpha"]    = temporalAlpha;
    j["probeJitterScale"] = probeJitterScale;
    j["cascadeTimeMs"]    = cascadeTimeMs;
    j["raymarchTimeMs"]   = raymarchTimeMs;
    for (int ci = 0; ci < cascadeCount; ++ci) {
        j["cascades"][ci]["anyPct"]    = 100.f * probeNonZero[ci]    / probeTotalPerCascade[ci];
        j["cascades"][ci]["surfPct"]   = 100.f * probeSurfaceHit[ci] / probeTotalPerCascade[ci];
        j["cascades"][ci]["skyPct"]    = 100.f * probeSkyHit[ci]     / probeTotalPerCascade[ci];
        j["cascades"][ci]["meanLum"]   = probeMeanLum[ci];
        j["cascades"][ci]["maxLum"]    = probeMaxLum[ci];
        j["cascades"][ci]["variance"]  = probeVariance[ci];
    }
    std::string statsPath = screenshotDir + "/probe_stats_" + timestamp + ".json";
    writeJsonFile(statsPath, j.dump(2));
    statsPathForAnalysis = statsPath;  // passed to launchAnalysis()
}
```

### Python script extension (`tools/analyze_screenshot.py`)

```python
import anthropic, base64, json, sys, pathlib

img_path   = sys.argv[1]
stats_path = sys.argv[2] if len(sys.argv) > 2 else None

img_b64 = base64.b64encode(pathlib.Path(img_path).read_bytes()).decode()
stats_text = pathlib.Path(stats_path).read_text() if stats_path else ""

client = anthropic.Anthropic()   # reads ANTHROPIC_API_KEY from env / .env
msg = client.messages.create(
    model="claude-sonnet-4-6",
    max_tokens=1024,
    messages=[{"role": "user", "content": [
        {"type": "image", "source": {"type": "base64",
                                     "media_type": "image/png",
                                     "data": img_b64}},
        {"type": "text", "text":
            "This is a frame from a 3D Radiance Cascades GI demo.\n\n"
            f"Probe statistics:\n{stats_text}\n\n"
            "Identify visible artifacts (banding, ghosting, dark regions), "
            "assess cascade convergence from the stats, and suggest one concrete "
            "parameter change to improve quality."}
    ]}]
)

out_path = pathlib.Path(img_path).with_suffix(".analysis.md")
out_path.write_text(msg.content[0].text)
print(f"[analyze] written: {out_path}")
```

### UI additions (`renderSettingsPanel()`)

```cpp
ImGui::SliderFloat("Auto-capture delay (s)", &autoCaptureDelaySeconds, 0.0f, 30.0f, "%.1f s");
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("0 = disabled. Captures screenshot + probe stats JSON and\n"
                      "launches AI analysis N seconds after startup.");
if (!lastAnalysisPath.empty())
    ImGui::TextDisabled("Last analysis: %s", lastAnalysisPath.c_str());
```

---

## Phase 12b — Multi-Mode Screenshot Burst (3 Render Modes)

### Goal
Capture render modes 0 (final), 3 (indirect×5), and 6 (GI-only) as separate PNGs
in three consecutive frames without the user seeing mode switches, then send all
three to Claude in a single API call for a richer multi-image analysis.

### Approach: frame-delayed burst

New state machine (3 states over 3 frames):

```cpp
enum class BurstState { Idle, Mode0, Mode3, Mode6, Analyze };
BurstState burstState = BurstState::Idle;
int        savedRenderMode = 0;
std::vector<std::string> burstPaths;
```

In `update()`:
```cpp
if (burstState == BurstState::Mode0) {
    savedRenderMode = raymarchRenderMode;
    raymarchRenderMode = 0;
    pendingScreenshot = true;
    burstState = BurstState::Mode3;
} else if (burstState == BurstState::Mode3) {
    raymarchRenderMode = 3;
    pendingScreenshot = true;
    burstState = BurstState::Mode6;
} else if (burstState == BurstState::Mode6) {
    raymarchRenderMode = 6;
    pendingScreenshot = true;
    burstState = BurstState::Analyze;
} else if (burstState == BurstState::Analyze) {
    raymarchRenderMode = savedRenderMode;
    burstState = BurstState::Idle;
    launchBurstAnalysis(burstPaths, statsPathForAnalysis);
}
```

`takeScreenshot()` appends each path to `burstPaths` when burst is active.

### Python script extension (multi-image)

```python
# called with: analyze_screenshot.py --burst mode0.png mode3.png mode6.png stats.json
content = []
labels = ["Final render (mode 0)", "Indirect × 5 (mode 3)", "GI only (mode 6)"]
for path, label in zip(img_paths, labels):
    content.append({"type": "text", "text": f"### {label}"})
    content.append({"type": "image", "source": {
        "type": "base64", "media_type": "image/png",
        "data": base64.b64encode(pathlib.Path(path).read_bytes()).decode()
    }})
content.append({"type": "text", "text":
    f"Probe stats:\n{stats_text}\n\n"
    "Compare the three render modes. Identify where indirect GI is bright vs dark "
    "relative to the direct-only result, locate banding in the GI-only view, and "
    "assess whether temporal accumulation has converged."})
```

### Notes
- Mode switches happen off-screen (before `ImGui::NewFrame()`) so the UI never flickers
- Each screenshot is tagged `_m0`, `_m3`, `_m6` in the filename
- The burst is triggered by the same auto-capture delay OR a new "Burst Capture" button

---

## Phase 12c — RenderDoc In-Process Capture + Atlas Extraction

### Goal
Programmatically trigger a RenderDoc .rdc capture from inside the app (no manual
RenderDoc launch needed), then use the RenderDoc Python replay API to extract the
cascade atlas texture as a PNG and feed it to Claude alongside the screenshot burst.

### Part 1: renderdoc.h integration (C++ side)

`renderdoc.h` is header-only and ships with every RenderDoc install. No linking
against a static library — the API is obtained by loading the DLL at runtime:

```cpp
// demo3d.h
#include "renderdoc_app.h"  // copy from %RENDERDOC_INSTALL%/include/

RENDERDOC_API_1_6_0 *rdocAPI = nullptr;

// demo3d.cpp — init()
HMODULE rdocMod = GetModuleHandleA("renderdoc.dll");
if (rdocMod) {
    pRENDERDOC_GetAPI fn = (pRENDERDOC_GetAPI)GetProcAddress(rdocMod, "RENDERDOC_GetAPI");
    fn(eRENDERDOC_API_Version_1_6_0, (void**)&rdocAPI);
    rdocAPI->SetCaptureFilePathTemplate((screenshotDir + "/rc_capture").c_str());
}
```

Capture trigger (in `update()`):
```cpp
if (pendingRDCCapture && rdocAPI) {
    pendingRDCCapture = false;
    rdocAPI->StartFrameCapture(nullptr, nullptr);
    rdocCaptureActive = true;
}
// ... one frame renders with all passes ...
if (rdocCaptureActive && rdocAPI) {
    rdocCaptureActive = false;
    rdocAPI->EndFrameCapture(nullptr, nullptr);
    // RenderDoc writes rc_capture_NNNN.rdc
    // launch replay script
    launchRDCAnalysis(lastRDCPath);
}
```

RenderDoc only intercepts the frame if it's already loaded (launched with RenderDoc
or RenderDoc attached). If not running under RenderDoc, `rdocMod` is null and the
path degrades gracefully to Phase 12b.

### Part 2: Python replay script (`tools/analyze_rdc.py`)

```python
import renderdoc as rd   # ships with RenderDoc Python installation
import qrenderdoc as qrd # optional: GUI integration
import PIL.Image, io

cap = rd.OpenCaptureFile()
res = cap.OpenFile(rdc_path, "", None)
controller = cap.OpenCapture(0, None)

# Find the reduction pass output (probeGridTexture slot)
# Draw calls are named by the shader; search for "reduction_3d"
for draw in controller.GetDrawcalls():
    if "reduction_3d" in draw.name:
        controller.SetFrameEvent(draw.eventId, True)
        break

# Extract texture by binding slot
textures  = controller.GetTextures()
grid_tex  = next(t for t in textures if t.width == 32 and t.depth == 32)
tex_data  = controller.GetTextureData(grid_tex.resourceId, rd.Subresource())
# save as PNG, feed to Claude alongside burst PNGs

controller.Shutdown()
cap.Shutdown()
```

Key textures to extract:
| Texture | What it shows AI |
|---|---|
| `probeGridTexture` (C0 slice) | Isotropic GI per probe — spatial banding visible |
| `probeAtlasTexture` (C0 slice) | Per-direction bins — directional quality, D resolution |
| Final framebuffer | Same as mode-0 screenshot but guaranteed pre-UI |

### Part 3: Combined analysis call

```python
# tools/analyze_rdc.py (final Claude call)
content = [
    # burst PNGs (modes 0, 3, 6)
    *burst_image_blocks,
    # atlas slice
    {"type": "text",  "text": "### Cascade C0 atlas (direction bins, slice z=16)"},
    {"type": "image", "source": {"type": "base64", ..., "data": atlas_b64}},
    # probe grid slice
    {"type": "text",  "text": "### Cascade C0 isotropic grid (slice z=16)"},
    {"type": "image", "source": {"type": "base64", ..., "data": grid_b64}},
    # stats
    {"type": "text",  "text": f"Probe stats:\n{stats_text}\n\n"
        "You have the final render (3 modes), the raw directional atlas, "
        "and the isotropic grid for cascade C0. "
        "Diagnose: (1) banding sources in the atlas vs grid, "
        "(2) convergence state from the stats, "
        "(3) one actionable parameter recommendation."}
]
```

### Graceful degradation

```
RenderDoc present → Phase 12c (full atlas + burst)
RenderDoc absent  → Phase 12b (burst only)
Burst fails       → Phase 12a (single screenshot + stats)
```

---

## Shared Infrastructure

### Output directory layout

```
tools/
  probe_stats_1234567890.json       ← probe stats dump
  frame_1234567890_m0.png           ← mode 0 screenshot
  frame_1234567890_m3.png           ← mode 3 screenshot
  frame_1234567890_m6.png           ← mode 6 screenshot
  rc_capture_0001.rdc               ← RenderDoc capture (Phase 12c)
  atlas_c0_z16_1234567890.png       ← extracted atlas slice (Phase 12c)
  grid_c0_z16_1234567890.png        ← extracted grid slice (Phase 12c)
  analysis_1234567890.md            ← Claude response
```

### Environment / API key

`tools/.env` (already exists from Phase 6a):
```
ANTHROPIC_API_KEY=sk-ant-...
```

Python scripts load with `python-dotenv`:
```python
from dotenv import load_dotenv
load_dotenv(pathlib.Path(__file__).parent / ".env")
```

### UI additions summary

```
Settings panel:
  [Auto-capture delay]  [____5.0__] s
  [Burst Capture Now]   [RDC Capture Now]   (Phase 12b / 12c buttons)
  Last analysis: tools/analysis_1234567890.md
```

---

## Implementation Order

1. **Phase 12a** — extend `takeScreenshot()` + JSON dump + script update + startup timer
2. **Phase 12b** — burst state machine + multi-image script + "Burst Capture" button
3. **Phase 12c** — `renderdoc_app.h` copy + runtime DLL load + Python replay script

Each phase is independently shippable and leaves the previous working as fallback.
