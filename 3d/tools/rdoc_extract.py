#!/usr/bin/env python3
"""
Phase 6b: Extract GPU timing + texture slices from a RenderDoc capture.

Runs inside qrenderdoc.exe --py (provides the renderdoc Python module).
Python 3.6 compatible (qrenderdoc embeds Python 3.6).

Writes to <RDOC_OUTDIR>:
  <stem>_<texname>.png      -- one PNG per extracted texture slice
  <stem>_manifest.json      -- GPU timing rows + texture paths + per-stage prompts
  <stem>_extract.log        -- log of this script (qrenderdoc stdout not visible in console)

Invocation (from C++ launchRdocAnalysis via system()):
  set "RDOC_CAPTURE=<path.rdc>" && set "RDOC_OUTDIR=<dir>" && qrenderdoc.exe --py rdoc_extract.py

NOTE: os._exit(0) is used at the end (not sys.exit). Inside qrenderdoc's embedded Python,
sys.exit() raises SystemExit which is caught by the Qt/Python layer and does NOT terminate
the process. os._exit() bypasses all cleanup and immediately exits qrenderdoc.
"""
import sys
import os
import json
import pathlib
import traceback

# ---------------------------------------------------------------------------
# Log file: qrenderdoc's stdout is not visible in the parent console.
# All output goes to <RDOC_OUTDIR>/<stem>_extract.log instead.
# ---------------------------------------------------------------------------
_log_file = None

def log(msg):
    print(msg, flush=True)
    if _log_file:
        _log_file.write(msg + "\n")
        _log_file.flush()

# renderdoc module is only available inside qrenderdoc.exe --py
try:
    import renderdoc as rd
except ImportError:
    print("[extract] ERROR: renderdoc module not available — must run via qrenderdoc.exe --py")
    sys.exit(2)

# ---------------------------------------------------------------------------
# Pass keyword mapping (substring match on glObjectLabel'd program name)
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Pipeline stages to extract
# (resource label, human label, Claude analysis prompt)
# ---------------------------------------------------------------------------
STAGES = [
    (
        "sdfTexture",
        "SDF Volume (Signed Distance Field)",
        "Inspect for missing geometry, incorrect distances, or holes in the SDF. "
        "The SDF should show smooth gradient values. Hard seams or flat regions "
        "indicate voxelization or SDF computation errors.",
    ),
    (
        "albedoTexture",
        "Albedo Volume",
        "Inspect surface colors. Red wall on left, green on right, white elsewhere. "
        "Flag incorrect or missing color regions.",
    ),
    (
        "cascade0_probeAtlas",
        "C0 Probe Directional Atlas",
        "Each probe owns a D x D tile. Neighboring tiles should have smoothly varying "
        "colors. Flag: uniform gray tiles (probe not baked), random noise (merge error). "
        "Bins with positive alpha = surface hit; negative = sky; zero = miss.",
    ),
    (
        "cascade1_probeAtlas",
        "C1 Probe Directional Atlas",
        "Same as C0 but for cascade 1 (tMax ~1.0wu after Phase 14c). "
        "Expect near-100% surface coverage. Flag dead tiles (all-zero alpha).",
    ),
    (
        "cascade0_probeGrid",
        "C0 Isotropic Probe Grid (reduction output)",
        "Direction-averaged radiance per probe. Should show smooth spatial gradients "
        "reflecting room lighting. Flag: all-black probes, sharp grid steps, wrong colors.",
    ),
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def last_event_id(actions):
    eid = 0
    for a in actions:
        child_max = last_event_id(a.children)
        eid = max(eid, a.eventId, child_max)
    return eid


def _resolve_action_name(a, sd_file):
    """Resolve the best display name for an action, checking the parent debug group."""
    _GENERIC = {"gldispatchcompute()", "gldrawarrays()", "gldrawelements()",
                "gldrawrangeelementsbasevertex()", "(unnamed)", ""}

    def _getname(obj):
        n = getattr(obj, "customName", None) or ""
        if not n and sd_file is not None and hasattr(obj, "GetName"):
            try:
                n = str(obj.GetName(sd_file)) or ""
            except Exception:
                pass
        if not n and hasattr(obj, "GetName"):
            try:
                n = str(obj.GetName(None)) or ""
            except Exception:
                pass
        if not n:
            n = getattr(obj, "name", None) or ""
        return n.strip()

    name = _getname(a)
    # If still a generic GL call name, try the parent debug group name.
    if name.lower() in _GENERIC:
        parent = getattr(a, "parent", None)
        if parent is not None:
            pname = _getname(parent)
            if pname and pname.lower() not in _GENERIC:
                name = pname
    return name or "(unnamed)"


def collect_gpu_timing(controller, sd_file=None):
    rows = []

    def walk(actions):
        for a in actions:
            is_dispatch = bool(a.flags & rd.ActionFlags.Dispatch)
            is_draw = bool(a.flags & rd.ActionFlags.Drawcall)
            if is_dispatch or is_draw:
                # `duration` existed in RenderDoc <1.30; newer builds require counter queries.
                raw_dur = getattr(a, "duration", None)
                duration_us = (raw_dur / 1000.0) if (raw_dur is not None and raw_dur > 0) else None
                raw_name = _resolve_action_name(a, sd_file)
                label = raw_name
                matched = False
                for kw, human in PASS_KEYWORDS.items():
                    if kw in label.lower():
                        label = human
                        matched = True
                        break
                if not matched:
                    log("[extract WARNING] Dispatch {} unrecognized: {!r}".format(
                        a.eventId, label))
                rows.append({
                    "event_id":    a.eventId,
                    "name":        raw_name,
                    "label":       label,
                    "duration_us": duration_us,
                    "type":        "dispatch" if is_dispatch else "draw",
                })
            walk(a.children)

    walk(controller.GetRootActions())
    return rows


def try_fetch_gpu_counters(controller):
    """Fetch GPU duration per-event via RenderDoc performance counters API.
    Returns {event_id: duration_us} or {} if unsupported."""
    try:
        available = controller.EnumerateCounters()
        if not available:
            log("[extract] No GPU counters available (driver support needed).")
            return {}

        # Log first few counters for diagnostics.
        for c in list(available)[:8]:
            try:
                d = controller.DescribeCounter(c)
                log("[extract] Counter {}: name={!r} unit={}".format(c, d.name, d.unit))
            except Exception:
                pass

        # Find a GPU duration counter (name contains "duration" or "gpu time").
        dur_id = None
        for c in available:
            try:
                d = controller.DescribeCounter(c)
                if "duration" in d.name.lower() or "gpu time" in d.name.lower():
                    dur_id = c
                    break
            except Exception:
                continue

        if dur_id is None:
            log("[extract] No GPU duration counter found among {} counters.".format(len(available)))
            return {}

        log("[extract] Fetching GPU counter {} ...".format(dur_id))
        results = controller.FetchCounters([dur_id])
        dur_desc = controller.DescribeCounter(dur_id)
        bw   = getattr(dur_desc, "resultByteWidth", 4)
        unit = str(getattr(dur_desc, "unit", ""))
        log("[extract] Counter unit={} byteWidth={}".format(unit, bw))

        timing = {}
        for r in results:
            val = r.value.d if bw == 8 else r.value.f
            # Convert to microseconds.
            if "second" in unit.lower():
                val_us = val * 1e6
            elif "milli" in unit.lower():
                val_us = val * 1e3
            else:
                val_us = val   # assume already µs
            timing[r.eventId] = val_us

        log("[extract] GPU counters: {} events timed.".format(len(timing)))
        return timing
    except Exception as e:
        log("[extract] GPU counter fetch failed: {}".format(e))
        log(traceback.format_exc())
        return {}


def find_resource(controller, name):
    matches = [r for r in controller.GetResources() if r.name == name]
    return matches[0] if matches else None


def extract_texture_slice(controller, res_id, out_path, z_slice=0):
    try:
        texsave = rd.TextureSave()
        texsave.resourceId = res_id
        texsave.mip = 0
        texsave.slice.sliceIndex = z_slice
        texsave.alpha = rd.AlphaMapping.Discard
        texsave.destType = rd.FileType.PNG
        controller.SaveTexture(texsave, str(out_path))
        if pathlib.Path(str(out_path)).exists():
            return str(out_path)
        log("[extract] SaveTexture produced no file: {}".format(out_path))
        return None
    except Exception as e:
        log("[extract] tex extract failed: {}".format(e))
        return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    global _log_file

    cap_path = os.environ.get("RDOC_CAPTURE")
    out_dir_str = os.environ.get("RDOC_OUTDIR", ".")

    # Fallback to positional args
    if not cap_path and len(sys.argv) >= 2:
        cap_path = sys.argv[1]

    out_dir = pathlib.Path(out_dir_str)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Open log file — qrenderdoc's stdout is not visible in the parent console.
    stem = pathlib.Path(cap_path).stem if cap_path else "rdoc_extract"
    log_path = out_dir / ("{}_extract.log".format(stem))
    _log_file = open(str(log_path), "w")

    if not cap_path:
        log("[extract] ERROR: RDOC_CAPTURE env var not set.")
        os._exit(1)

    log("[extract] RenderDoc Python API version check: {}".format(rd.GetVersionString()))

    # Resolve the "Succeeded" sentinel — the attribute path varies by RenderDoc version:
    #   v1.x: rd.ReplayStatus.Succeeded
    #   v1.2+: rd.ResultCode.Succeeded  (some builds)
    #   fallback: status is integer 0
    def is_ok(status):
        for attr in ("ReplayStatus", "ResultCode"):
            enum = getattr(rd, attr, None)
            if enum is not None:
                ok = getattr(enum, "Succeeded", None)
                if ok is not None:
                    return status == ok
        # Integer fallback: 0 = Succeeded in all known RenderDoc versions.
        return int(status) == 0

    # Normalize to backslashes — qrenderdoc may require Windows-style paths.
    cap_path_norm = cap_path.replace("/", "\\")
    log("[extract] Opening capture: {}".format(cap_path_norm))
    try:
        cap = rd.OpenCaptureFile()
        status = cap.OpenFile(cap_path_norm, "", None)
    except Exception as e:
        log("[extract] EXCEPTION in OpenFile: {}".format(e))
        log(traceback.format_exc())
        os._exit(1)

    log("[extract] OpenFile status: {}".format(status))
    if not is_ok(status):
        log("[extract] Failed to open capture.")
        os._exit(1)
    log("[extract] Capture opened OK, starting replay...")

    try:
        result = cap.OpenCapture(rd.ReplayOptions(), None)
        # Some RenderDoc versions return (status, controller); others return controller directly.
        if isinstance(result, tuple):
            replay_status, controller = result[0], result[1]
            log("[extract] OpenCapture status: {}".format(replay_status))
            if not is_ok(replay_status):
                log("[extract] OpenCapture failed.")
                os._exit(1)
        else:
            controller = result
        if controller is None:
            log("[extract] OpenCapture returned None controller.")
            os._exit(1)
        log("[extract] Replay controller obtained.")
    except Exception as e:
        log("[extract] EXCEPTION in OpenCapture: {}".format(e))
        log(traceback.format_exc())
        os._exit(1)

    # Get structured data file for action name resolution (newer RenderDoc API).
    sd_file = None
    if hasattr(cap, "GetStructuredData"):
        try:
            sd_file = cap.GetStructuredData()
            log("[extract] Structured data obtained for name resolution.")
        except Exception as e:
            log("[extract] GetStructuredData failed (dispatch names may be unnamed): {}".format(e))

    # Collect GPU timing walk (action tree traversal for pass names + legacy duration).
    log("[extract] Collecting GPU timing...")
    timing_rows = collect_gpu_timing(controller, sd_file)

    # Augment with FetchCounters GPU duration (works when driver exposes perf counters).
    log("[extract] Attempting GPU performance counter fetch...")
    gpu_timing = try_fetch_gpu_counters(controller)
    if gpu_timing:
        for row in timing_rows:
            eid = row["event_id"]
            if eid in gpu_timing:
                row["duration_us"] = gpu_timing[eid]

    # Seek to last event so textures reflect final frame state
    root_actions = controller.GetRootActions()
    last_eid = last_event_id(root_actions)
    if last_eid:
        controller.SetFrameEvent(last_eid, True)

    # Extract texture slices
    manifest = {
        "cap_path":   cap_path,
        "timing":     timing_rows,
        "textures":   {},
    }

    for (name, label, prompt) in STAGES:
        log("[extract] Extracting: {}".format(label))
        res = find_resource(controller, name)
        if res is None:
            log("[extract] Resource '{}' not found (check glObjectLabel calls)".format(name))
            manifest["textures"][name] = {
                "label":  label,
                "prompt": prompt,
                "path":   None,
                "error":  "resource not found",
            }
            continue

        mid_z = 0
        try:
            # GetTexture / GetTextureDescription varies by RenderDoc version.
            tex_info = None
            for fn in ("GetTexture", "GetTextureDescription"):
                if hasattr(controller, fn):
                    tex_info = getattr(controller, fn)(res.resourceId)
                    break
            if tex_info is None:
                # Fallback: search GetTextures() list.
                if hasattr(controller, "GetTextures"):
                    rid = res.resourceId
                    for td in controller.GetTextures():
                        if td.resourceId == rid:
                            tex_info = td
                            break
            if tex_info is not None:
                depth = getattr(tex_info, "depth", 1) or getattr(tex_info, "arraysize", 1) or 1
                if depth > 1:
                    mid_z = depth // 2
                log("[extract] {} depth={} -> z_slice={}".format(name, depth, mid_z))
            else:
                log("[extract] {} texture info unavailable -> z_slice=0".format(name))
        except Exception as e:
            log("[extract] GetTexture info failed (z_slice=0): {}".format(e))

        out_path = out_dir / ("{}_{}.png".format(stem, name))
        result = extract_texture_slice(controller, res.resourceId, out_path, z_slice=mid_z)
        manifest["textures"][name] = {
            "label":  label,
            "prompt": prompt,
            "path":   result,
            "error":  None if result else "extraction failed",
        }

    controller.Shutdown()
    cap.Shutdown()

    # Write manifest for analyze_renderdoc.py to consume
    manifest_path = out_dir / ("{}_manifest.json".format(stem))
    with open(str(manifest_path), "w") as f:
        json.dump(manifest, f, indent=2)
    log("[extract] Manifest written: {}".format(manifest_path))
    log("[extract] Done.")

    _log_file.close()
    # os._exit() bypasses Python/Qt cleanup and immediately terminates qrenderdoc.
    # sys.exit() raises SystemExit which the Qt/Python layer catches and ignores.
    os._exit(0)


try:
    main()
except Exception as e:
    if _log_file:
        _log_file.write("[extract] UNHANDLED EXCEPTION: {}\n".format(e))
        _log_file.write(traceback.format_exc())
        _log_file.flush()
    print("[extract] UNHANDLED EXCEPTION: {}".format(e))
    traceback.print_exc()
    os._exit(1)
