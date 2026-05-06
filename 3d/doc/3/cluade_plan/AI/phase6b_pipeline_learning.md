# Phase 6b — RenderDoc AI Analysis Pipeline: Design Learning

**Date:** 2026-05-05  
**Covers:** What the original plan assumed, what actually worked, the final two-script architecture, and all RenderDoc Python API discoveries made during implementation.

---

## 1. Original pipeline (the plan)

The original `phase6b_renderdoc_ai_plan.md` assumed a single-script invocation:

```
renderdoccmd.exe python tools/analyze_renderdoc.py <capture.rdc> <output_dir>
```

The assumption was that `renderdoccmd.exe` had a `python` subcommand (analogous to how some tools expose embedded scripting). The single script would:
1. `import renderdoc as rd` — open and replay the capture
2. Walk `controller.GetRootActions()` for GPU timing via `ActionDescription.duration`
3. Extract texture slices with `controller.SaveTexture()`
4. Call the `anthropic` SDK to send PNGs to Claude
5. Write a `_pipeline.md` report

**This plan had three fatal flaws discovered during implementation.**

---

## 2. What actually broke and why

### Flaw 1: `renderdoccmd.exe` has no `python` subcommand

```
renderdoccmd.exe help
```
Output: `capture`, `convert`, `embed`, `extract`, `help`, `inject`, `remoteserver`, `replay`, `test`, `thumb`, `version`

No `python`. The correct tool is **`qrenderdoc.exe --py <script>`** — the full GUI application with an embedded Python 3.6 interpreter. `renderdoccmd.exe` is a headless CLI with no scripting entry point.

### Flaw 2: qrenderdoc embeds Python 3.6 — `anthropic` won't install

`qrenderdoc.exe` ships `python36.dll`. The `anthropic` Python package requires 3.7+. Installing it into qrenderdoc's embedded interpreter is impossible. **A single script cannot both `import renderdoc` and `import anthropic`.**

### Flaw 3: `ActionDescription.duration` doesn't exist in RenderDoc v1.42

The plan called `a.duration` (nanoseconds) on each action. In RenderDoc v1.42's Python bindings, `ActionDescription` has no `duration` attribute. GPU timing requires `controller.FetchCounters()` instead.

---

## 3. Final architecture: two-script pipeline

The solution decouples extraction (needs `renderdoc`) from analysis (needs `anthropic`):

```
C++ launchRdocAnalysis()
    │
    ├─ Step 1 (blocking): qrenderdoc.exe --py rdoc_extract.py
    │       • import renderdoc as rd
    │       • Open + replay the .rdc file
    │       • Walk action tree for dispatch names + FetchCounters for GPU timing
    │       • Extract 5 named 3D texture slices as PNGs (mid-z slice per volume)
    │       • Write <stem>_manifest.json  (timing + PNG paths + Claude prompts)
    │       • Write <stem>_extract.log    (qrenderdoc stdout isn't visible in parent console)
    │       • os._exit(0)                 (sys.exit() doesn't terminate qrenderdoc)
    │
    └─ Step 2 (after step 1 exits): python analyze_renderdoc.py
            • Read <stem>_manifest.json
            • Print extract.log to parent console
            • For each texture PNG: base64-encode → send to Claude API
            • renderdoccmd.exe thumb → extract final-frame thumbnail → send to Claude
            • Write <stem>_pipeline.md report
```

C++ (in a detached thread):

```cpp
// Step 1: extract inside qrenderdoc (blocks until os._exit(0))
std::string extractCmd = envPrefix
    + " && \"C:/Program Files/RenderDoc/qrenderdoc.exe\" --py \"" + extractPath + "\"";
system(extractCmd.c_str());

// Step 2: analyze with Claude (reads manifest written by step 1)
std::string analyzeCmd = envPrefix + " && python \"" + analyzePath + "\"";
system(analyzeCmd.c_str());
```

`envPrefix` sets `RDOC_CAPTURE` and `RDOC_OUTDIR` as **absolute paths** (see §4.3).

---

## 4. RenderDoc Python API discoveries (v1.42)

### 4.1 Status / result codes

The plan used `rd.ReplayStatus.Succeeded`. In v1.42 this enum moved to `rd.ResultCode`.

Robust check:
```python
def is_ok(status):
    for attr in ("ReplayStatus", "ResultCode"):
        enum = getattr(rd, attr, None)
        if enum:
            ok = getattr(enum, "Succeeded", None)
            if ok is not None:
                return status == ok
    return int(status) == 0   # integer fallback
```

### 4.2 OpenCapture return type

In some RenderDoc versions `cap.OpenCapture(rd.ReplayOptions(), None)` returns a `(status, controller)` tuple; in others it returns `controller` directly.

```python
result = cap.OpenCapture(rd.ReplayOptions(), None)
if isinstance(result, tuple):
    replay_status, controller = result[0], result[1]
else:
    controller = result
```

### 4.3 Absolute paths required for qrenderdoc

`qrenderdoc.exe` runs from `C:\Program Files\RenderDoc\` as its working directory. Any relative path in `RDOC_OUTDIR` resolves there, not in the project root.

Fix in C++ before launching:
```cpp
std::string outDir = std::filesystem::absolute(std::filesystem::path(rdocAnalysisDir)).string();
```

`rdocCaptureDir` = `tools/captures/` (where `.rdc` files are saved).  
`rdocAnalysisDir` = `tools/analysis/` (where extract PNGs, manifest, and pipeline.md are written).

### 4.4 `sys.exit()` does NOT terminate qrenderdoc

Inside qrenderdoc's embedded Python, `sys.exit()` raises `SystemExit` which the Qt/Python integration layer catches and ignores. The process stays alive and hangs the parent `system()` call indefinitely.

The only way to terminate qrenderdoc from Python is:
```python
import os
os._exit(0)   # bypasses all Python/Qt cleanup, immediately terminates the process
```

### 4.5 qrenderdoc stdout is invisible in the parent console

`qrenderdoc.exe` is a Qt GUI application. `print()` inside its Python goes to its own Qt console, not inherited from the parent process that launched it via `system()`.

Solution: write all log output to `<stem>_extract.log` and have `analyze_renderdoc.py` read + print it:
```python
# rdoc_extract.py
_log_file = None

def log(msg):
    print(msg, flush=True)         # for qrenderdoc's own console (debugging)
    if _log_file:
        _log_file.write(msg + "\n")
        _log_file.flush()
```

### 4.6 ActionDescription attributes in v1.42

The action object has no `.name` or `.duration`. Actual attributes:
```
actionId, eventId, customName, flags, children, parent,
dispatchDimension, dispatchThreadsDimension,
numIndices, numInstances, vertexOffset, instanceOffset,
copySource, copyDestination, outputs, depthOut,
markerColor, GetName()
```

Name resolution chain (best → fallback):
```python
name = a.customName or ""
if not name and sd_file:          # sd_file = cap.GetStructuredData()
    name = a.GetName(sd_file)     # returns "glDispatchCompute()" etc.
if not name:
    name = a.GetName(None)        # may work, may throw
# Check parent debug group for pass label:
if name in GENERIC_GL_CALLS:
    parent = a.parent
    if parent:
        name = parent.customName or parent.GetName(sd_file)
```

### 4.7 GPU timing: FetchCounters, not ActionDescription.duration

`FetchCounters` is the correct API for GPU duration in v1.42:

```python
available = controller.EnumerateCounters()
dur_id = None
for c in available:
    desc = controller.DescribeCounter(c)
    if "duration" in desc.name.lower():
        dur_id = c
        break

results = controller.FetchCounters([dur_id])
desc = controller.DescribeCounter(dur_id)
# desc.unit = CounterUnit.Seconds → multiply by 1e6 for µs
# desc.resultByteWidth: 8 → r.value.d (float64), 4 → r.value.f (float32)

timing = {r.eventId: r.value.d * 1e6 for r in results}
```

On this machine (NVIDIA + OpenGL), counter 1 = `GPU Duration`, unit = Seconds, byteWidth = 8, returns real wall-clock GPU time per draw/dispatch event.

**Confirmed timings from first live capture (rdoc_frame_capture.rdc):**

| Event | Type | GPU time |
|---|---|---|
| Dispatch (cascade bake C0?) | dispatch | 11.6 ms |
| Dispatch (cascade reduction C0?) | dispatch | 0.19 ms |
| Dispatch (cascade bake C1?) | dispatch | 6.9 ms |
| Dispatch (cascade reduction C1?) | dispatch | 0.22 ms |
| Draw (raymarch) | draw | 6.0 ms |
| Draw (GI blur) | draw | 1.7 ms |
| Draw (debug/UI) | draw | 0.01 ms |
| **Total** | | **26.7 ms** |

### 4.8 Texture depth / z-slice selection

`controller.GetTexture()` doesn't exist. Use `controller.GetTextures()` to find a `TextureDescription` by `resourceId`, then read `.depth`:

```python
for td in controller.GetTextures():
    if td.resourceId == res.resourceId:
        mid_z = td.depth // 2   # pick middle z-slice for 3D volumes
        break
```

Without this, `z_slice=0` is used — which for a 128-deep SDF volume is the very front face (usually empty).

### 4.9 Log file encoding on Chinese Windows (GBK)

qrenderdoc writes Python `print()` output in the system ANSI encoding (GBK on `zh-CN` machines). Any non-ASCII character in log messages (e.g. `→`) produces bytes that `utf-8` decoding rejects.

Two fixes:
1. **rdoc_extract.py**: use ASCII-only log strings (`->` not `→`)
2. **analyze_renderdoc.py**: read the log with encoding fallback:
```python
log_bytes = log_path.read_bytes()
for enc in ("utf-8", "gbk", "cp1252", "latin-1"):
    try:
        print(log_bytes.decode(enc)); break
    except UnicodeDecodeError:
        continue
```

---

## 5. Pass naming: glPushDebugGroup vs glObjectLabel

### Original approach (plan): glObjectLabel on programs

The plan relied on `glObjectLabel(GL_PROGRAM, prog, -1, "radiance_3d.comp")` to make dispatch names visible in the RenderDoc Python API via `a.name`.

**Why it didn't work:** In v1.42, `a.name` doesn't exist. `a.GetName(sd_file)` returns the GL API call string (`"glDispatchCompute()"`) not the program label. Program labels appear in the RenderDoc GUI's resource inspector but not in the `ActionDescription.name` field.

### Actual approach: glPushDebugGroup + parent traversal

Wrapping each dispatch in a named debug group makes the group's name appear in `ActionDescription.customName` of the GROUP action (not the dispatch child). The dispatch's `a.parent` points back to the group:

```cpp
glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "radiance_3d");
glDispatchCompute(wg.x, wg.y, wg.z);
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
glPopDebugGroup();
```

Python side:
```python
if name.lower() in GENERIC_GL_CALLS:
    parent = getattr(a, "parent", None)
    if parent:
        pname = parent.customName or parent.GetName(sd_file)
        if pname and pname.lower() not in GENERIC_GL_CALLS:
            name = pname   # e.g. "radiance_3d" → matches PASS_KEYWORDS
```

Passes labeled in demo3d.cpp:
| Debug group name | PASS_KEYWORDS match | Human label |
|---|---|---|
| `sdf_analytic` | `"sdf_analytic"` | SDF (analytic) |
| `radiance_3d` | `"radiance_3d"` | Cascade bake |
| `reduction_3d` | `"reduction_3d"` | Cascade reduction |
| `temporal_blend` | `"temporal_blend"` | Temporal EMA blend |
| `raymarch` | `"raymarch"` | Raymarching |
| `gi_blur` | `"gi_blur"` | GI blur |

---

## 6. Full end-to-end flow (current implementation)

```
User presses G (or --auto-rdoc fires after N seconds)
        │
        ▼
C++: pendingRdocCapture = true
        │
        ▼  (next frame boundary, before BeginDrawing)
C++: rdoc->StartFrameCapture(nullptr, nullptr)
        │
        ▼  (full frame: SDF, cascade bakes, reduction, temporal, raymarch, GI blur)
        │  Each major dispatch wrapped in glPushDebugGroup/glPopDebugGroup
        │
        ▼  (after EndDrawing)
C++: rdoc->EndFrameCapture(nullptr, nullptr)
     rdoc->GetCapture(n-1, capPath, ...) → e.g. "tools/captures/rdoc_frame_1.rdc"
     launchRdocAnalysis(capPath) — spawns detached thread:
        │
        ├─ system("set RDOC_CAPTURE=<abs_path> && set RDOC_OUTDIR=<abs_path>"
        │          " && qrenderdoc.exe --py rdoc_extract.py")
        │
        │   Inside qrenderdoc (Python 3.6):
        │     • cap.OpenFile(cap_path)
        │     • cap.OpenCapture() → controller
        │     • cap.GetStructuredData() → sd_file
        │     • collect_gpu_timing(controller, sd_file):
        │         - Walk GetRootActions() for dispatches and draws
        │         - Resolve name via customName / GetName(sd_file) / parent.customName
        │         - Match against PASS_KEYWORDS
        │     • try_fetch_gpu_counters(controller):
        │         - EnumerateCounters() → find "GPU Duration"
        │         - FetchCounters([dur_id]) → {event_id: µs}
        │         - Merge into timing_rows
        │     • SetFrameEvent(last_eid, True) → seek to end-of-frame
        │     • GetTextures() to find mid-z for each 3D volume
        │     • SaveTexture() → 5 PNGs to RDOC_OUTDIR
        │     • Write <stem>_manifest.json
        │     • Write <stem>_extract.log
        │     • os._exit(0)
        │
        └─ system("python analyze_renderdoc.py")
        
            Regular Python (3.10+, has anthropic):
              • Read manifest.json
              • Print extract.log
              • GPU timing table → markdown section
              • For each texture PNG:
                  base64 encode → Claude API (claude-opus-4-7) → markdown section
              • renderdoccmd.exe thumb → final-frame PNG → Claude → markdown section
              • Write <stem>_pipeline.md
```

---

## 7. Maintenance rules

| Change | Required update |
|---|---|
| Add a new compute shader | Add `glPushDebugGroup`/`glPopDebugGroup` around its dispatch; add entry to `PASS_KEYWORDS` in rdoc_extract.py |
| Rename a compute shader | Update the debug group label string in C++ AND the `PASS_KEYWORDS` key |
| Add a new pipeline texture to inspect | Add `glObjectLabel(GL_TEXTURE, ...)` after creation; add entry to `STAGES` in rdoc_extract.py |
| RenderDoc version update | Re-run `dir(rd)` dump diagnostic (toggle on in rdoc_extract.py); check for API changes in `ActionDescription`, `CaptureFile`, `ReplayController` |
| New `[extract WARNING] unrecognized` line | A dispatch has no debug group — wrap it |

---

## 8. Files

| File | Role |
|---|---|
| `tools/rdoc_extract.py` | qrenderdoc Python 3.6 — opens capture, FetchCounters, saves PNGs, writes manifest |
| `tools/analyze_renderdoc.py` | Regular Python — reads manifest, calls Claude API, writes report |
| `src/rdoc_helper.h/.cpp` | C++ RenderDoc DLL loader (isolates `<windows.h>` clash with raylib) |
| `src/demo3d.cpp` | `initRenderDoc()`, G-key capture, `launchRdocAnalysis()`, `glObjectLabel`, `glPushDebugGroup` |
| `src/main3d.cpp` | `beginRdocFrameIfPending()` / `endRdocFrameIfPending()` around the main loop |
| `lib/renderdoc/renderdoc_app.h` | Public-domain capture API header (CC0) |

---

## 9. Known gaps

| Gap | Workaround / fix path |
|---|---|
| Dispatch names still `glDispatchCompute()` on old captures | ~~Resolved~~ — all 6 pass types wrapped in `glPushDebugGroup`; take a new capture |
| GPU timing N/A on some machines | RenderDoc → Tools → Settings → "Allow GPU timing"; GPU counter ID 1 = `GPU Duration` on NVIDIA/OpenGL |
| C1 probe atlas coverage ~78-82% not ~100% | Phase 14c regression; investigate tMax or coverage computation |
| Probe grid analysis sometimes generic (not image-specific) | PNG file very small (1KB) when z_slice=0 was used; fixed with mid-z selection |
| `glObjectLabel(GL_PROGRAM, ...)` not surfaced in `a.name` | Use `glPushDebugGroup` wrapping instead; program labels still visible in RenderDoc GUI |
| `--auto-analyze` exits before `--auto-rdoc` fires | Burst capture fires at ~5s; RDC at 8s. Run with `--auto-rdoc` only; kill process after ~90s |
| Paths hardcoded to old `doc/.../captures` dir | ~~Resolved~~ — both dirs now under `tools/captures` and `tools/analysis` |
