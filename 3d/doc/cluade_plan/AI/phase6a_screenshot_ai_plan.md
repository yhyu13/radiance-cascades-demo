# Phase 6a — Screenshot + Claude Vision Analysis

**Date:** 2026-04-30 (revised after Review 01)
**Goal:** Press `P` in the running app → PNG saved → Claude analyzes visual artifacts automatically.

---

## Scope: opt-in local developer tool

This workflow requires:
- `pip install anthropic`
- `ANTHROPIC_API_KEY` in the shell environment
- A live internet connection during analysis

It is **not** part of CI, not part of branch validation, and not run automatically.
The analysis runs only when the developer presses `P` and has configured their local
environment. If the `ANTHROPIC_API_KEY` is absent, the Python script exits with an
error message to the console — the app continues normally.

---

## Problem

Visual artifacts in the radiance cascades renderer (banding, color bleeding, cascade seams,
shadow acne) are hard to describe verbally. Comparing two toggle states requires writing down
observations between runs. There is no automated way to name and locate artifacts in a frame.

## Solution

A one-key workflow:
1. User presses `P`.
2. C++ calls `takeScreenshot(/*launchAiAnalysis=*/true)`.
3. `takeScreenshot()` captures the current frame (3D scene only, without ImGui) via `glReadPixels()`.
4. PNG saved to `doc/cluade_plan/AI/screenshots/frame_<timestamp>.png`.
5. C++ spawns `tools/analyze_screenshot.py` in a background thread (non-blocking).
6. Python calls Claude claude-opus-4-7 with the image and an artifact-focused prompt.
7. Analysis saved to `doc/cluade_plan/AI/analysis/<timestamp>.md`.

The analysis is **heuristic visual triage**: it names and locates artifacts, and where
the artifact geometry is tightly coupled to a known cause (e.g., banding at exactly the
probe cell period), it states that cause. It does not diagnose software-level root causes
(wrong texture unit, wrong uniform) — those require Phase 6b pipeline inspection.

---

## Files to create / modify

| File | Action | Purpose |
|---|---|---|
| `lib/stb/stb_image_write.h` | Create (copy) | PNG write, header-only |
| `CMakeLists.txt` | Modify | Add `lib/stb/` as include dir |
| `src/demo3d.h` | Modify | Extend `takeScreenshot()` signature + add `launchAnalysis()` |
| `src/demo3d.cpp` | Modify | Implement capture + launch inside `takeScreenshot()` |
| `src/main3d.cpp` | Modify | Call `takeScreenshot()` between `EndMode3D()` and `rlImGuiBegin()` |
| `tools/analyze_screenshot.py` | Create | Claude API multimodal call |

---

## C++ changes

### demo3d.h — extend takeScreenshot()

`takeScreenshot()` already exists as a stub (`demo3d.cpp:2842-2848`). Extend its
signature and add the fields it needs:

```cpp
// Phase 6a — screenshot + AI analysis
std::string screenshotDir = "doc/cluade_plan/AI/screenshots";
std::string analysisDir   = "doc/cluade_plan/AI/analysis";

// Replaces the existing stub:
void takeScreenshot(bool launchAiAnalysis = false);

private:
    bool pendingScreenshot = false;
    void launchAnalysis(const std::string& imagePath);
```

### demo3d.cpp — processInput()

```cpp
// Phase 6a: P = screenshot + AI analysis
if (IsKeyPressed(KEY_P)) {
    pendingScreenshot = true;
    std::cout << "[6a] Screenshot queued (captured at next render point)." << std::endl;
}
```

### demo3d.cpp — takeScreenshot()

Replaces the existing stub. Called from main3d.cpp after `EndMode3D()` and before
`rlImGuiBegin()` so ImGui panels are absent from the image:

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <filesystem>
#include <chrono>

void Demo3D::takeScreenshot(bool launchAiAnalysis) {
    if (!pendingScreenshot) return;
    pendingScreenshot = false;

    std::filesystem::create_directories(screenshotDir);
    std::filesystem::create_directories(analysisDir);

    int w = GetScreenWidth(), h = GetScreenHeight();
    std::vector<uint8_t> pixels(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    stbi_flip_vertically_on_write(1);  // OpenGL origin is bottom-left

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = "frame_" + std::to_string(now) + ".png";
    std::string path     = screenshotDir + "/" + filename;

    if (stbi_write_png(path.c_str(), w, h, 3, pixels.data(), w * 3)) {
        std::cout << "[6a] Screenshot saved: " << path << std::endl;
        if (launchAiAnalysis)
            launchAnalysis(path);
    } else {
        std::cerr << "[6a] Screenshot write failed: " << path << std::endl;
    }
}
```

### demo3d.cpp — launchAnalysis()

```cpp
#include <thread>

void Demo3D::launchAnalysis(const std::string& imagePath) {
    std::thread([imagePath, this]() {
        std::string cmd = "python tools/analyze_screenshot.py \""
                        + imagePath + "\" \""
                        + analysisDir + "\"";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
    }).detach();
}
```

### main3d.cpp — capture point

```cpp
BeginMode3D(demo->getRaylibCamera());
    demo->render();
EndMode3D();

demo->takeScreenshot(/*launchAiAnalysis=*/true);  // ← add here: after 3D, before ImGui

rlImGuiBegin();
    demo->renderUI();
rlImGuiEnd();
```

This captures only the 3D scene — ImGui panels are absent from the screenshot, giving
the AI a clean view of the rendered output without UI clutter.

---

## Python analysis script — `tools/analyze_screenshot.py`

```python
#!/usr/bin/env python3
"""
Phase 6a: Visual artifact triage via Claude.
Usage: python analyze_screenshot.py <image.png> <output_dir>
Requires: ANTHROPIC_API_KEY env var, pip install anthropic
Opt-in local developer tool — not part of CI or branch validation.
"""
import sys, base64, pathlib, textwrap, anthropic, datetime

PROMPT = textwrap.dedent("""
    You are performing heuristic visual triage on a real-time 3D Cornell Box rendered
    with a radiance cascades global illumination system. The system stores per-direction
    probe radiance in a directional atlas and merges it across 4 cascade levels (C0 near
    to C3 far).

    This is triage — your goal is to name and locate artifacts. Where the artifact
    geometry is tightly coupled to a known physical cause, state it:
    - Regular banding at exactly the probe cell spacing (~12.5 cm for C0) → likely
      the isotropic reduction texture is being sampled instead of the directional atlas
    - Hard angular color steps at ~36° intervals → directional bin banding (D=4, 8 bins)
    - Ring-shaped discontinuity at a fixed distance → cascade boundary seam

    Do NOT infer software-level causes (wrong texture unit, wrong uniform, bad shader
    branch) from visual appearance alone — those require pipeline inspection.

    Known artifact types:
    - Probe-grid banding: regular grid-shaped patterns in indirect light
    - Cascade boundary seams: ring-shaped discontinuities where cascade levels meet
    - Color bleeding errors: wrong wall color (red/green) tinting surfaces incorrectly
    - Shadow acne / self-shadowing: dark speckles on directly lit surfaces
    - Directional bin banding: hard angular color steps in indirect (~36° steps at D=4)
    - Missing shadows: surfaces fully lit despite geometry blocking the light

    Instructions:
    1. Describe what the image shows overall (1 sentence).
    2. List each visible artifact: name it, locate it, and (only if geometry-coupled)
       the likely pipeline cause (1 sentence each).
    3. If the image looks clean with no artifacts, say so explicitly.
    4. Rate overall visual quality: Poor / Fair / Good / Excellent.

    Be specific about location (e.g., "floor near the left red wall", "ceiling center").
    Do not speculate about artifacts you cannot see.
""")

def analyze(image_path: str, output_dir: str) -> None:
    img_bytes = pathlib.Path(image_path).read_bytes()
    b64 = base64.standard_b64encode(img_bytes).decode("utf-8")

    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1024,
        messages=[{"role": "user", "content": [
            {"type": "image",
             "source": {"type": "base64", "media_type": "image/png", "data": b64}},
            {"type": "text", "text": PROMPT},
        ]}],
    )
    text = msg.content[0].text

    stem = pathlib.Path(image_path).stem
    out  = pathlib.Path(output_dir) / (stem + ".md")
    ts   = datetime.datetime.now().isoformat(timespec="seconds")
    out.write_text(
        f"# AI Visual Triage\n\n"
        f"**Image:** `{image_path}`  \n"
        f"**Analyzed:** {ts}  \n"
        f"**Model:** claude-opus-4-7\n"
        f"**Note:** Heuristic triage only — geometry-coupled causes stated; "
        f"software-level root causes require Phase 6b pipeline inspection.\n\n"
        f"---\n\n{text}\n",
        encoding="utf-8",
    )
    print(f"[6a] Analysis saved: {out}")
    print(text)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: analyze_screenshot.py <image.png> <output_dir>")
        sys.exit(1)
    analyze(sys.argv[1], sys.argv[2])
```

---

## CMakeLists.txt change

Add one line after existing include_directories:

```cmake
include_directories(${CMAKE_SOURCE_DIR}/lib/stb)
```

---

## Setup requirements

```bash
pip install anthropic
export ANTHROPIC_API_KEY=sk-ant-...
```

The `stb_image_write.h` header can be obtained from:
- https://github.com/nothings/stb/blob/master/stb_image_write.h
- Or copied from Raylib's include directory (Raylib ships it).

---

## Verification

| Step | Expected |
|---|---|
| Build with new include | Zero new errors |
| Press `P` in app | Console: `[6a] Screenshot saved: ...` |
| PNG file exists | `doc/cluade_plan/AI/screenshots/frame_<ts>.png` |
| Python script runs (background) | Console: `[6a] Analysis saved: ...` within ~10s |
| Analysis `.md` header | Contains "Heuristic triage only" scope note |
| Analysis `.md` content | Lists artifacts by name/location; geometry-coupled causes stated; no software-level guesses |
| App does not freeze | Main loop continues while Python runs in background thread |
| No API key set | Python exits with Anthropic auth error; app unaffected |
