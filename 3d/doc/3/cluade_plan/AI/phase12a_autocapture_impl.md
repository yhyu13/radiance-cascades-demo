# Phase 12a — Auto-Capture + Probe Stats JSON: Implementation

**Date:** 2026-05-02
**Plan reference:** `phase12_auto_capture_analysis.md` (Phase 12a section)

---

## What was implemented

| Component | Change |
|---|---|
| `src/demo3d.h` | 4 new members + updated `launchAnalysis` signature |
| `src/demo3d.cpp` — `render()` | Auto-capture trigger block |
| `src/demo3d.cpp` — `takeScreenshot()` | Hand-rolled JSON dump + new `launchAnalysis` call |
| `src/demo3d.cpp` — `launchAnalysis()` | Optional `statsPath` arg, `lastAnalysisPath` set eagerly |
| `src/demo3d.cpp` — `renderSettingsPanel()` | Delay slider + last-analysis path display |
| `tools/analyze_screenshot.py` | Optional 4th arg for stats JSON; richer prompt |

---

## New header members (`src/demo3d.h`)

```cpp
// Phase 12a — Auto-capture + probe stats JSON
float       autoCaptureDelaySeconds = 5.0f;  // 0.0 = disabled
bool        pendingStatsDump        = false;  // write JSON alongside next screenshot
std::string statsPathForAnalysis;             // path passed to launchAnalysis()
std::string lastAnalysisPath;                 // shown in settings panel
```

`launchAnalysis` updated from:
```cpp
void launchAnalysis(const std::string& imagePath);
```
to:
```cpp
void launchAnalysis(const std::string& imagePath,
                    const std::string& statsPath = "");
```
Default `""` means the P-key screenshot path (no stats) is unchanged.

---

## Auto-capture trigger (`render()`)

Inserted after probe readback, before Pass 4 (Raymarching):

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

**Why here:** `cascadeReady` is local-static inside `render()`. After the probe readback block,
all stats arrays (`probeNonZero`, `probeMeanLum`, etc.) are populated — the JSON can be written
with valid data. `pendingScreenshot=true` is consumed by `takeScreenshot()` called from
`main3d.cpp` after `EndMode3D()`, so the captured frame is the rendered 3D scene (no UI).

**One-shot:** `autoCaptured` is a local-static — resets only on app restart. To re-capture,
use the Screenshot [P] button or reduce the delay slider below the current elapsed time
(the trigger won't re-fire because `autoCaptured` is static).

---

## Probe stats JSON (`takeScreenshot()` extension)

```cpp
// Phase 12a: write probe stats JSON alongside the screenshot
if (pendingStatsDump) {
    pendingStatsDump = false;
    std::ostringstream j;
    j << "{\n";
    j << "  \"dirRes\": "          << dirRes         << ",\n";
    j << "  \"cascadeCount\": "    << cascadeCount   << ",\n";
    j << "  \"temporalAlpha\": "   << temporalAlpha  << ",\n";
    j << "  \"probeJitterScale\": "<< probeJitterScale<< ",\n";
    j << "  \"cascadeTimeMs\": "   << cascadeTimeMs  << ",\n";
    j << "  \"raymarchTimeMs\": "  << raymarchTimeMs << ",\n";
    j << "  \"cascades\": [\n";
    for (int ci = 0; ci < cascadeCount; ++ci) {
        int tot = probeTotalPerCascade[ci]; if (tot < 1) tot = 1;
        j << "    {\n";
        j << "      \"anyPct\": "  << (100.f * probeNonZero[ci]    / tot) << ",\n";
        j << "      \"surfPct\": " << (100.f * probeSurfaceHit[ci] / tot) << ",\n";
        j << "      \"skyPct\": "  << (100.f * probeSkyHit[ci]     / tot) << ",\n";
        j << "      \"meanLum\": " << probeMeanLum[ci]  << ",\n";
        j << "      \"maxLum\": "  << probeMaxLum[ci]   << ",\n";
        j << "      \"variance\": "<< probeVariance[ci] << "\n";
        j << "    }";
        if (ci < cascadeCount - 1) j << ",";
        j << "\n";
    }
    j << "  ]\n}\n";

    std::string statsPath = screenshotDir + "/probe_stats_" + std::to_string(now) + ".json";
    std::ofstream sf(statsPath);
    if (sf) {
        sf << j.str();
        statsPathForAnalysis = statsPath;
    }
}
```

**No nlohmann dependency** — hand-rolled `ostringstream` with the same field set specified
in the plan. `now` is the same `chrono` timestamp used for the PNG filename, so
`frame_T.png` ↔ `probe_stats_T.json` share the same `T`.

**PNG fail guard:** if `stbi_write_png` fails, `pendingStatsDump` is cleared early and
`takeScreenshot()` returns — no orphaned JSON without a corresponding PNG.

---

## `launchAnalysis()` — stats path + eager `lastAnalysisPath`

```cpp
void Demo3D::launchAnalysis(const std::string& imagePath,
                            const std::string& statsPath) {
    // Expected output path known before thread starts
    std::string stem = fs::path(imagePath).stem().string();
    lastAnalysisPath = analysisDir + "/" + stem + ".md";

    std::thread([imagePath, statsPath, this]() {
        std::string cmd = "python \"" + toolsScript + "\" \""
                        + imagePath + "\" \""
                        + analysisDir + "\"";
        if (!statsPath.empty())
            cmd += " \"" + statsPath + "\"";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6a] Analysis script failed (exit " << ret << ")\n";
    }).detach();
}
```

`lastAnalysisPath` is set synchronously before the thread starts — the UI shows the expected
output path immediately rather than waiting for the script to finish (which can take several
seconds for the Anthropic API call).

---

## UI additions (`renderSettingsPanel()`)

```cpp
ImGui::SliderFloat("Auto-capture delay (s)##ac", &autoCaptureDelaySeconds, 0.0f, 30.0f, "%.1f s");
// tooltip: 0 = disabled; after N seconds AND cascade ready → screenshot + JSON + AI analysis
if (!lastAnalysisPath.empty())
    ImGui::TextDisabled("Last analysis: %s", lastAnalysisPath.c_str());
```

Placed immediately after the existing "Screenshot [P]" button row, before the first separator.

---

## Python script (`tools/analyze_screenshot.py`)

**Call signature unchanged for existing usage:**
```
python analyze_screenshot.py <image.png> <output_dir>
```

**New optional arg for Phase 12a:**
```
python analyze_screenshot.py <image.png> <output_dir> <probe_stats.json>
```

Stats content is appended to the prompt:
```python
PROMPT_STATS_SUFFIX = textwrap.dedent("""
    Additional context — current probe statistics:
    {stats}

    Use the stats to:
    - Assess cascade convergence (surfPct, anyPct — low means under-sampled probes)
    - Cross-check luminance variance with visible banding (high variance = spatial structure)
    - Suggest one concrete parameter change (e.g. temporalAlpha, probeJitterScale, dirRes)
      based on both the image and the stats.
""")
```

Output header shows `"Phase 12a triage (image + probe stats)"` vs `"Phase 6a triage (image only)"`.

---

## Output file layout

```
tools/
  frame_<T>.png               ← 3D-only screenshot (no ImGui)
  probe_stats_<T>.json        ← probe stats (same timestamp T)
  frame_<T>.md                ← Claude analysis output
```

---

## Backward compatibility

- P-key screenshot: `pendingStatsDump` stays false → `launchAnalysis("", "")` → script
  called without stats arg → old Phase 6a behaviour exactly.
- `autoCaptureDelaySeconds = 0.0`: trigger condition `> 0.0f` never met → no auto-capture.
- `autoCaptured` is local-static: always single-shot per app run, regardless of slider.

---

## Known limitations / future work

| Limitation | Phase |
|---|---|
| `autoCaptured` doesn't reset if slider is changed mid-run | 12b or manual button |
| Single screenshot (mode 0 only) | Phase 12b: 3-mode burst |
| No RenderDoc atlas extraction | Phase 12c |
| `system()` call blocks the detached thread | acceptable for offline analysis |
