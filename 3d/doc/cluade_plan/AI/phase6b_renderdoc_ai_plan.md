# Phase 6b — RenderDoc In-Process Capture + Resource Snapshot Analysis

**Date:** 2026-04-30 (revised after Review 02; GPU perf analysis section added 2026-05-03)  
**Status:** NOT YET IMPLEMENTED — design spec only. No C++ or Python code has been
written for this phase. All code blocks are design intent, not deployed code.

**Goal:** Phase 6b is a **two-component GPU analysis tool**:
- **(A) Resource snapshot** — extract named 3D textures at end-of-frame for AI visual
  inspection of atlas content, SDF geometry, and reduction output.
- **(B) GPU performance timing** — walk all dispatches and draws, report per-pass GPU
  cost in µs. Added during Phase 14c to address the sub-ms timing unreliability of the
  app-side `cascadeTimeMs` counter.

Component B is substantially larger than a texture snapshot helper. Together they form a
mini GPU analysis framework. Component A can be shipped independently of Component B.

---

## Scope: opt-in local developer tool

This workflow requires:
- RenderDoc installed at `C:\Program Files\RenderDoc\`
- `ANTHROPIC_API_KEY` in the shell environment
- A live internet connection during analysis

It is **not** part of CI, not part of branch validation, and not run automatically.
If RenderDoc is not installed, the app silently disables the `G` hotkey with a console
message and continues normally. The Python script is only invoked via the hotkey — never
by CMake or test runners.

---

## Problem and capability gaps addressed

Phase 6a/12b/14a cover image-space and temporal analysis well. Phase 6b addresses three
specific gaps those tools cannot fill:

1. **Reliable per-pass GPU timing** — `cascadeTimeMs` in probe_stats is unreliable below
   ~1ms (GPU timer variance dominates). RenderDoc GPU timestamps are nanosecond-precision
   and per-dispatch. This is the primary Phase 14c open question: how much does extending
   C0/C1 tMax actually cost in GPU µs?

2. **Pipeline-internal texture inspection** — e.g., whether the C1 probe atlas has
   populated directional bins across the full probe grid (not just the aggregated surfPct).
   The final rendered frame cannot expose this.

3. **Atlas-level hypothesis validation** — e.g., "are open-air probe bins writing zero
   alpha or the sky-sentinel?" cannot be determined from the rendered output or the
   surfPct scalar metric alone.

Phase 6b does **not** replace or subsume the sequence capture tool. Its diagnostic reach
is narrower: end-of-frame resource state only.

### Limitation: resource snapshot, not event-walking

The analysis reads resource state at the end of the captured frame. Bugs invisible to
this approach:
- **Transient intermediate corruption** — garbage written by one pass and overwritten by
  a later pass before frame end
- **Pass-ordering errors** — the final texture is correct but produced by the wrong route
- **Frame-to-frame drift** — use the sequence capture tool (Phase 14a) for those

Event walking (`controller.SetFrameEvent(eventId, True)` before each texture save to
read intermediate resource state) is a future extension beyond the current scope.

---

## Prerequisites and labeling maintenance cost

- RenderDoc installed at `C:\Program Files\RenderDoc\`
  - `renderdoc.dll` — in-process capture DLL
  - `renderdoccmd.exe` — CLI tool (provides Python runtime)
  - `renderdoc_app.h` — public-domain capture API header

Labeling is **not a one-time prerequisite — it is an ongoing maintenance cost**.
The Python script matches resources and dispatches by string name. If labels drift from
code reality, the automation degrades silently: timing rows fall back to "Dispatch N",
resource lookups return "not found", and the output looks plausible but is wrong.

Rules to maintain:
- Texture labels (`glObjectLabel(GL_TEXTURE, ...)`) must be updated if a texture is
  renamed or replaced.
- Program labels (`glObjectLabel(GL_PROGRAM, ...)`) must be updated if a shader file is
  renamed. The label and the `PASS_KEYWORDS` dict key in the Python script must change
  together in the same commit.
- Adding a new cascade dispatch without a keyword entry produces an unlabeled row with
  no compile-time warning — only a runtime `[6b WARNING]` print.

The Python script prints `[6b WARNING] Dispatch {eventId} unrecognized: {name}` for any
dispatch that does not match a PASS_KEYWORDS entry, making labeling drift visible.

---

## Files to create / modify

| File | Action | Purpose |
|---|---|---|
| `lib/renderdoc/renderdoc_app.h` | Create (copy) | In-process capture API header |
| `CMakeLists.txt` | Modify | Add `lib/renderdoc/` as include dir |
| `src/demo3d.h` | Modify | Add RenderDoc fields + method declarations |
| `src/demo3d.cpp` | Modify | `initRenderDoc()`, GL object labels, `G` hotkey, `beginRdocFrameIfPending()`, `endRdocFrameIfPending()`, `launchRdocAnalysis()` |
| `src/main3d.cpp` | Modify | Call `beginRdocFrameIfPending()` / `endRdocFrameIfPending()` around main loop body |
| `tools/analyze_renderdoc.py` | Create | qrenderdoc Python analysis + Claude API |

---

## C++ changes

### demo3d.h additions

```cpp
// Phase 6b — RenderDoc in-process capture (private fields)
#ifdef _WIN32
#include "renderdoc_app.h"
#endif

private:
    RENDERDOC_API_1_6_0* rdoc          = nullptr;
    bool                 pendingRdocCapture = false;
    std::string          captureDir    = "doc/cluade_plan/AI/captures";
    std::string          analysisDir   = "doc/cluade_plan/AI/analysis";  // shared with 6a

    void initRenderDoc();
    void launchRdocAnalysis(const std::string& capturePath);

public:
    void beginRdocFrameIfPending();   // call before BeginDrawing()
    void endRdocFrameIfPending();     // call after EndDrawing()
```

### demo3d.cpp — initRenderDoc()

Call from the Demo3D constructor **after** `InitWindow()` (after the GL context exists):

```cpp
void Demo3D::initRenderDoc() {
#ifdef _WIN32
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (!mod) {
        mod = LoadLibraryA("C:/Program Files/RenderDoc/renderdoc.dll");
    }
    if (!mod) {
        std::cout << "[6b] RenderDoc DLL not found — GPU capture disabled.\n";
        return;
    }
    pRENDERDOC_GetAPI getApi =
        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    if (!getApi || !getApi(eRENDERDOC_API_Version_1_6_0, (void**)&rdoc)) {
        std::cout << "[6b] RenderDoc API init failed.\n";
        rdoc = nullptr;
        return;
    }
    std::filesystem::create_directories(captureDir);
    std::string capTemplate = captureDir + "/rdoc_frame";
    rdoc->SetCaptureFilePathTemplate(capTemplate.c_str());
    rdoc->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    std::cout << "[6b] RenderDoc in-process API loaded OK. Press G to capture.\n";
#endif
}
```

### demo3d.cpp — glObjectLabel() calls

Add after each texture allocation so RenderDoc can identify resources by name.
Place these immediately after the `glBindTexture` call in the texture setup block:

```cpp
// Phase 6b: label textures for RenderDoc resource identification
glObjectLabel(GL_TEXTURE, sdfTexture,         -1, "sdfTexture");
glObjectLabel(GL_TEXTURE, albedoTexture,      -1, "albedoTexture");
glObjectLabel(GL_TEXTURE, probeAtlasTexture,  -1, "probeAtlasTexture");
glObjectLabel(GL_TEXTURE, probeGridTexture,   -1, "probeGridTexture");
// Label the backbuffer / final RT when it is allocated:
glObjectLabel(GL_TEXTURE, backbufferTex,      -1, "backbuffer");
```

`glObjectLabel` is core in OpenGL 4.3 (already required by this project for compute
shaders). It is a no-op if no debug context is active — which is acceptable for an
opt-in local tool.

### demo3d.cpp — processInput()

```cpp
// Phase 6b: G = GPU frame capture
if (IsKeyPressed(KEY_G)) {
    if (rdoc) {
        pendingRdocCapture = true;
        std::cout << "[6b] RenderDoc capture queued for next frame." << std::endl;
    } else {
        std::cout << "[6b] RenderDoc not loaded — capture unavailable." << std::endl;
    }
}
```

### demo3d.cpp — beginRdocFrameIfPending() / endRdocFrameIfPending()

All RenderDoc capture protocol is encapsulated in Demo3D; main3d.cpp sees only the two
method calls:

```cpp
void Demo3D::beginRdocFrameIfPending() {
#ifdef _WIN32
    if (pendingRdocCapture && rdoc)
        rdoc->StartFrameCapture(nullptr, nullptr);
#endif
}

void Demo3D::endRdocFrameIfPending() {
#ifdef _WIN32
    if (!pendingRdocCapture || !rdoc) return;
    rdoc->EndFrameCapture(nullptr, nullptr);
    pendingRdocCapture = false;

    uint32_t n = rdoc->GetNumCaptures();
    char capPath[512]; uint32_t pathLen = sizeof(capPath); uint64_t ts;
    if (rdoc->GetCapture(n - 1, capPath, &pathLen, &ts)) {
        std::string path(capPath, pathLen > 0 ? pathLen - 1 : 0);  // strip null
        std::cout << "[6b] Capture saved: " << path << std::endl;
        launchRdocAnalysis(path);
    }
#endif
}
```

### demo3d.cpp — launchRdocAnalysis()

```cpp
void Demo3D::launchRdocAnalysis(const std::string& capturePath) {
    std::thread([capturePath, this]() {
        std::string rdCmd = "\"C:/Program Files/RenderDoc/renderdoccmd.exe\"";
        std::string cmd = rdCmd + " python tools/analyze_renderdoc.py \""
                        + capturePath + "\" \""
                        + analysisDir + "\"";
        int ret = system(cmd.c_str());
        if (ret != 0)
            std::cerr << "[6b] RenderDoc analysis script failed (exit " << ret << ")\n";
    }).detach();
}
```

### main3d.cpp — capture around the main loop body

```cpp
demo->beginRdocFrameIfPending();   // Phase 6b: start capture if requested

BeginDrawing();
    ClearBackground(BLACK);
    BeginMode3D(demo->getRaylibCamera());
        demo->render();
    EndMode3D();
    demo->takeScreenshot();        // Phase 6a: capture if pendingScreenshot
    rlImGuiBegin();
        demo->renderUI();
    rlImGuiEnd();
    DrawFPS(10, 10);
EndDrawing();

demo->endRdocFrameIfPending();    // Phase 6b: end capture, save, launch analysis
```

---

## Python analysis script — `tools/analyze_renderdoc.py`

Invoked as: `renderdoccmd.exe python tools/analyze_renderdoc.py <capture.rdc> <output_dir>`

RenderDoc's `renderdoccmd.exe python` provides the `renderdoc` module in the Python
environment. The analysis is **resource snapshot**: each named texture is read at its
state as of the last captured event.

```python
#!/usr/bin/env python3
"""
Phase 6b: Resource snapshot analysis of a RenderDoc capture via qrenderdoc + Claude.
Invocation: renderdoccmd.exe python analyze_renderdoc.py <capture.rdc> <output_dir>
Requires: ANTHROPIC_API_KEY env var.

Note: This is resource snapshot analysis, not event-walking pipeline analysis.
Resources are read at their state at the end of the captured frame.
"""
import sys, base64, pathlib, datetime, anthropic
import renderdoc as rd

# ---------------------------------------------------------------------------
# Stage descriptors — name label set via glObjectLabel(), human label,
# analysis prompt focus.
# ---------------------------------------------------------------------------
STAGES = [
    ("sdfTexture",        "SDF Volume (Signed Distance Field)",
     "Inspect for missing geometry, incorrect distances, or holes in the SDF. "
     "The SDF should show smooth gradient values. Hard seams or flat regions "
     "indicate voxelization or SDF computation errors."),

    ("albedoTexture",     "Albedo Volume",
     "Inspect surface colors. Red wall on left, green on right, white elsewhere. "
     "Flag incorrect or missing color regions."),

    ("probeAtlasTexture", "Probe Directional Atlas (C0)",
     "Each probe owns a D×D tile (D=4: 16 bins). Neighboring tiles should have "
     "smoothly varying colors. Flag: uniform gray tiles (probe not baked), "
     "random noise (merge error), or tiles identical across all probes (no directional variation)."),

    ("probeGridTexture",  "Isotropic Probe Grid (reduction output)",
     "This is the direction-averaged radiance per probe. Should show smooth spatial "
     "gradients reflecting the room lighting. Flag: all-black probes, sharp grid steps, "
     "incorrect color (e.g., green wall color appearing on floor probes far from the wall)."),
]

FINAL_PROMPT = """You are analyzing the final rendered frame from a radiance cascades
3D Cornell Box renderer. This is heuristic visual triage — describe artifacts by name
and location. Where the artifact geometry is tightly coupled to a known cause (e.g.,
regular banding at exactly the probe cell period strongly implies the isotropic reduction
texture is being read), state that cause. Do NOT infer software-level causes (wrong
uniform, wrong texture unit) from visual appearance alone — those require pipeline
inspection.

Known artifact types:
- Probe-grid banding: regular grid-shaped patterns in indirect light
- Cascade boundary seams: ring-shaped discontinuities
- Color bleeding errors: wrong wall color on surfaces that should not have it
- Shadow acne: dark speckles on lit surfaces
- Directional bin banding: hard angular color steps (~36°) in indirect light
- Missing shadows: surfaces fully lit despite occlusion

Describe artifacts by name and location. If clean, say so.
Rate quality: Poor / Fair / Good / Excellent."""

# ---------------------------------------------------------------------------
# GPU performance analysis — walk all compute dispatches, collect GPU duration.
# Each dispatch's duration is in nanoseconds (0 if GPU timing unavailable).
# Dispatches are named by their source shader program (via glObjectLabel on
# the program) or fall back to "Dispatch N".
# ---------------------------------------------------------------------------

# Radiance cascades pass order within a single frame (stagger varies which
# cascades are actually dispatched each frame):
#   For each active cascade (C3→C0):
#     [voxelize.comp] — only on scene change
#     [sdf_analytic.comp or sdf_3d.comp] — once per frame (if dirty)
#     [radiance_3d.comp] — cascade bake (per active cascade)
#     [reduction_3d.comp] — atlas→grid reduction (per active cascade)
#     [temporal_blend.comp] — EMA blend atlas + grid (if temporal ON, non-fused)
#   [inject_radiance.comp] — indirect radiance injection
#   [raymarch.frag] — final raymarching pass
#   [gi_blur.frag] — GI bilateral blur pass

PASS_KEYWORDS = {
    "voxelize":        "Voxelization",
    "sdf_analytic":    "SDF (analytic)",
    "sdf_3d":          "SDF (voxel)",
    "radiance_3d":     "Cascade bake",
    "reduction_3d":    "Cascade reduction",
    "temporal_blend":  "Temporal EMA blend",
    "inject_radiance": "Radiance injection",
    "raymarch":        "Raymarching",
    "gi_blur":         "GI blur",
}

def collect_gpu_timing(controller) -> list[dict]:
    """Walk all actions and collect GPU timing for compute dispatches and draws."""
    rows = []
    def walk(actions):
        for a in actions:
            # rd.ActionFlags.Dispatch is set on compute dispatches;
            # rd.ActionFlags.Drawcall is set on rasterization draws.
            is_dispatch = bool(a.flags & rd.ActionFlags.Dispatch)
            is_draw     = bool(a.flags & rd.ActionFlags.Drawcall)
            if is_dispatch or is_draw:
                duration_us = a.duration / 1000.0 if a.duration > 0 else None
                # Map action name → human pass label (keyword match on program name)
                label = a.name
                for kw, human in PASS_KEYWORDS.items():
                    if kw in (a.name or "").lower():
                        label = human
                        break
                if label == a.name:  # no keyword matched
                    print(f"[6b WARNING] Dispatch {a.eventId} unrecognized: {a.name!r} "
                          f"— add to PASS_KEYWORDS or update glObjectLabel")
                rows.append({
                    "event_id":    a.eventId,
                    "name":        a.name or "(unnamed)",
                    "label":       label,
                    "duration_us": duration_us,
                    "type":        "dispatch" if is_dispatch else "draw",
                })
            walk(a.children)
    walk(controller.GetRootActions())
    return rows

def format_perf_table(rows: list[dict]) -> str:
    """Render a markdown table of per-pass GPU timing."""
    lines = ["| Pass | Type | GPU time (µs) |",
             "|---|---|---|"]
    total_us = 0.0
    for r in rows:
        dur = f"{r['duration_us']:.1f}" if r['duration_us'] is not None else "N/A"
        if r['duration_us']:
            total_us += r['duration_us']
        lines.append(f"| {r['label']} | {r['type']} | {dur} |")
    lines.append(f"| **Total** | | **{total_us:.1f}** |")
    return "\n".join(lines)

def tex_slice_to_b64(controller, res_id, z_slice=0) -> str | None:
    """Save one Z-slice of a 3D texture as PNG and return base64."""
    try:
        texsave = rd.TextureSave()
        texsave.resourceId       = res_id
        texsave.mip              = 0
        texsave.slice.sliceIndex = z_slice
        texsave.alpha            = rd.AlphaMapping.Discard
        texsave.destType         = rd.FileType.PNG
        buf = rd.BufferFileOutput()
        controller.SaveTexture(texsave, buf)
        raw = bytes(buf)
        if not raw:
            return None
        return base64.standard_b64encode(raw).decode("utf-8")
    except Exception as e:
        print(f"  Warning: tex extract failed: {e}")
        return None

def ask_claude(client, b64_png: str, prompt: str) -> str:
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=512,
        messages=[{"role": "user", "content": [
            {"type": "image",
             "source": {"type": "base64", "media_type": "image/png", "data": b64_png}},
            {"type": "text", "text": prompt},
        ]}],
    )
    return msg.content[0].text

def find_resource(controller, name: str):
    """Find a resource by exact glObjectLabel name."""
    matches = [r for r in controller.GetResources() if r.name == name]
    return matches[0] if matches else None

def find_backbuffer(controller) -> rd.ResourceId | None:
    """
    Find the final render target by matching screen dimensions.
    Preferred: if the backbuffer was labeled with glObjectLabel("backbuffer"),
    use find_resource(controller, "backbuffer") instead.
    Fallback: find the largest RGBA texture matching the swapchain dimensions.
    """
    labeled = find_resource(controller, "backbuffer")
    if labeled:
        return labeled.resourceId

    # Fallback heuristic: largest RGBA texture (screen-resolution)
    # This may be unreliable if multiple textures share screen dimensions.
    textures = controller.GetTextures()
    candidates = sorted(
        [t for t in textures if t.format.compCount == 4],
        key=lambda t: t.width * t.height, reverse=True
    )
    if candidates:
        print("  [6b] Warning: backbuffer label not found, using largest RGBA texture heuristic.")
        return candidates[0].resourceId
    return None

def main():
    cap_path   = sys.argv[1]
    output_dir = sys.argv[2]

    print(f"[6b] Loading capture: {cap_path}")
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(cap_path, "", None)
    if status != rd.ReplayStatus.Succeeded:
        print(f"[6b] Failed to open capture: {status}")
        sys.exit(1)

    controller = cap.OpenCapture(rd.ReplayOptions(), None)
    client     = anthropic.Anthropic()

    # Collect GPU timing before seeking to end (timing walk uses root actions)
    timing_rows = collect_gpu_timing(controller)

    # Seek to last event so all resources reflect their final frame state
    root_actions = controller.GetRootActions()
    def last_event(actions):
        eid = 0
        for a in actions:
            eid = max(eid, a.eventId, last_event(a.children))
        return eid
    last_eid = last_event(root_actions)
    if last_eid:
        controller.SetFrameEvent(last_eid, True)

    ts   = datetime.datetime.now().isoformat(timespec="seconds")
    stem = pathlib.Path(cap_path).stem
    report = [f"# RenderDoc Resource Snapshot + GPU Performance Analysis\n\n"
              f"**Capture:** `{cap_path}`  \n"
              f"**Analyzed:** {ts}  \n"
              f"**Model:** claude-opus-4-7\n"
              f"**Note:** Resource snapshot (state at end of frame) + per-dispatch GPU timing.\n\n---\n"]

    # GPU performance table (before visual analysis)
    if timing_rows:
        report.append("## GPU Performance (per dispatch/draw)\n\n"
                      + format_perf_table(timing_rows) + "\n\n"
                      + "> GPU duration = 0 or N/A means the driver did not expose timing "
                        "for that event. Enable GPU timing in RenderDoc settings if all entries show N/A.\n")
    else:
        report.append("## GPU Performance\n*No dispatch/draw events found in capture.*\n")

    for (name, label, stage_prompt) in STAGES:
        print(f"[6b] Analyzing: {label}")
        res = find_resource(controller, name)
        if res is None:
            report.append(
                f"## {label}\n"
                f"*Resource '{name}' not found — ensure glObjectLabel() is called in C++.*\n")
            continue
        tex_info = controller.GetTexture(res.resourceId)
        mid_z = max(0, tex_info.depth // 2) if hasattr(tex_info, 'depth') else 0
        b64 = tex_slice_to_b64(controller, res.resourceId, z_slice=mid_z)
        if b64 is None:
            report.append(f"## {label}\n*Texture extraction failed.*\n")
            continue
        analysis = ask_claude(client, b64, stage_prompt)
        report.append(f"## {label}\n\n{analysis}\n")

    # Final frame
    print("[6b] Analyzing final frame...")
    bb_id = find_backbuffer(controller)
    if bb_id:
        b64_final = tex_slice_to_b64(controller, bb_id)
        if b64_final:
            analysis = ask_claude(client, b64_final, FINAL_PROMPT)
            report.append(f"## Final Frame\n\n{analysis}\n")
    else:
        report.append("## Final Frame\n*Backbuffer resource not identified.*\n")

    controller.Shutdown()
    cap.Shutdown()

    out = pathlib.Path(output_dir) / (stem + "_pipeline.md")
    out.write_text("\n".join(report), encoding="utf-8")
    print(f"[6b] Analysis saved: {out}")

if __name__ == "__main__":
    main()
```

---

## GPU performance analysis design

RenderDoc's Python API exposes per-event GPU timing via `ActionDescription.duration`
(nanoseconds, 0 if GPU timing is disabled or unsupported). The script walks
`controller.GetRootActions()` recursively and collects all compute dispatches
(`ActionFlags.Dispatch`) and rasterization draws (`ActionFlags.Drawcall`).

### Pass identification

Dispatch events are matched against `PASS_KEYWORDS` (substring match on action name,
which RenderDoc derives from the compute program label set via `glObjectLabel(GL_PROGRAM, ...)`
or falls back to a generic "Dispatch N").

**Required C++ glObjectLabel calls for program objects** (not just textures):
```cpp
// After linking each compute program (in loadShaders or similar):
glObjectLabel(GL_PROGRAM, shaderProg, -1, "voxelize.comp");
glObjectLabel(GL_PROGRAM, shaderProg, -1, "radiance_3d.comp");  // etc.
```
Without program labels, RenderDoc names dispatches "Dispatch N" and keyword matching
degrades to sequential ordering.

### Output: GPU performance table (Phase 14c relevance)

The timing table directly answers the performance question raised in Phase 14b/14c:
_"How much does extending C0/C1 tMax actually cost in GPU time?"_

Expected cascade bake rows (one per active cascade per frame, stagger permitting):
```
| Cascade bake (C3) | dispatch | ~Xµs |
| Cascade reduction (C3) | dispatch | ~Xµs |
| Cascade bake (C2)     | dispatch | ~Xµs |
...
| Cascade bake (C0) | dispatch | ~Xµs |   ← highest with c0MinRange=1.0
| Cascade bake (C1) | dispatch | ~Xµs |   ← raised by c1MinRange=1.0
```

### GPU timing availability

GPU timing is driver- and capture-mode-dependent. On NVIDIA with OpenGL:
- Timestamp queries are available (GL_ARB_timer_query)
- RenderDoc collects them if "Allow GPU timing" is enabled in RenderDoc settings
- If `duration=0` for all events: enable via RenderDoc → Tools → Settings →
  "Allow GPU timing" checkbox, then recapture

### glObjectLabel for compute programs

Compute programs need labels at link time, not bind time. Label each in the shader
loader immediately after `glLinkProgram`:
```cpp
glLinkProgram(prog);
glObjectLabel(GL_PROGRAM, prog, -1, shaderName.c_str());  // "radiance_3d.comp", etc.
```
This is separate from texture labels and is only needed for dispatch identification.

---

## CMakeLists.txt change

```cmake
include_directories(${CMAKE_SOURCE_DIR}/lib/renderdoc)
```

---

## renderdoc_app.h acquisition

Copy from one of:
- `C:\Program Files\RenderDoc\renderdoc_app.h` (if RenderDoc installs it there)
- https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/renderdoc/api/app/renderdoc_app.h

This header is public-domain (CC0) and safe to include in the repo.

---

## Important notes

### `renderdoccmd.exe python` vs standalone Python

The qrenderdoc `renderdoc` module is only available inside RenderDoc's own Python
environment. The analysis script must be run via:
```
"C:\Program Files\RenderDoc\renderdoccmd.exe" python analyze_renderdoc.py <args>
```
Not via a regular `python` interpreter.

### glObjectLabel() requirement

The Python script's `find_resource()` function matches on `r.name == name`. Without
`glObjectLabel()` calls in C++, RenderDoc exposes generic names like "Texture 3" and
all stage lookups will return "Resource not found." Adding the label calls is a
**prerequisite** for the script to produce useful output.

### RenderDoc and Raylib/GLFW

RenderDoc hooks into OpenGL via its DLL injection. Raylib creates the GL context, so
RenderDoc must be loaded **after** `InitWindow()`. The `initRenderDoc()` call belongs
in the Demo3D constructor body (after setup), not before.

### Capture file path

`SetCaptureFilePathTemplate("doc/cluade_plan/AI/captures/rdoc_frame")` sets the base
path. RenderDoc appends `_<n>.rdc` automatically. `GetCapture()` retrieves the actual
path after capture.

---

## Verification

| Step | Expected |
|---|---|
| Build | Zero new errors |
| App starts without RenderDoc | Console: `[6b] RenderDoc DLL not found — GPU capture disabled.` |
| App starts with RenderDoc | Console: `[6b] RenderDoc in-process API loaded OK. Press G to capture.` |
| Press `G` | Console: `[6b] Capture saved: .../rdoc_frame_1.rdc` |
| `.rdc` file exists | `doc/cluade_plan/AI/captures/rdoc_frame_1.rdc` |
| Python script runs | Console: `[6b] Analysis saved: ...pipeline.md` within ~30s |
| `.md` content | Per-stage sections with artifact descriptions; no "Resource not found" if labels applied |
| Can also open `.rdc` manually | RenderDoc GUI opens and shows all passes |
| GPU timing present in report | `## GPU Performance` table shows non-zero µs values |
| GPU timing all zero | Enable "Allow GPU timing" in RenderDoc → Tools → Settings, recapture |
| Dispatch names resolved | Table shows "Cascade bake", "Cascade reduction" etc., not "Dispatch N" |
| Phase 14c validation | C0 and C1 bake costs both elevated vs C2/C3 baseline |
