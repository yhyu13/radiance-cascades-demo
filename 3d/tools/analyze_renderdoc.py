#!/usr/bin/env python3
"""
Phase 6b: GPU analysis of a RenderDoc capture.

Two-phase pipeline:
  Phase 1 (rdoc_extract.py via qrenderdoc.exe --py):
    -- Opens the .rdc, replays, extracts texture slices + GPU timing to disk.
    -- Writes <stem>_manifest.json describing what was extracted.

  Phase 2 (this script, via regular Python):
    -- Reads the manifest. If present: uses extracted PNGs + timing for analysis.
    -- Also extracts the capture thumbnail via renderdoccmd.exe and analyzes the final frame.
    -- Calls Claude API, writes <stem>_pipeline.md report.

Invocation (set by C++ launchRdocAnalysis before calling this script):
  Env vars: RDOC_CAPTURE=<path.rdc>  RDOC_OUTDIR=<output_dir>
  Or positional args: python analyze_renderdoc.py <capture.rdc> <output_dir>

API key loaded from tools/.env (ANTHROPIC_API_KEY).
"""
import sys
import os
import base64
import pathlib
import datetime
import subprocess
import json

# ---------------------------------------------------------------------------
# Load .env (tools/.env alongside this script)
# ---------------------------------------------------------------------------
def load_dotenv():
    env_path = pathlib.Path(__file__).parent / ".env"
    if not env_path.exists():
        return
    with open(env_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export "):]
            if "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip())

load_dotenv()

# ---------------------------------------------------------------------------
# Argument resolution
# ---------------------------------------------------------------------------
cap_path   = os.environ.get("RDOC_CAPTURE")
output_dir = os.environ.get("RDOC_OUTDIR")

if len(sys.argv) >= 3:
    cap_path   = cap_path   or sys.argv[1]
    output_dir = output_dir or sys.argv[2]

if not cap_path or not output_dir:
    print("[analyze] ERROR: capture path or output dir not set.")
    print("  Set RDOC_CAPTURE / RDOC_OUTDIR or pass as positional args.")
    sys.exit(1)

api_key  = os.environ.get("ANTHROPIC_API_KEY", "")
base_url = os.environ.get("ANTHROPIC_BASE_URL", "")

if not api_key:
    print("[analyze] ERROR: ANTHROPIC_API_KEY not set (check tools/.env).")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Anthropic client
# ---------------------------------------------------------------------------
try:
    import anthropic
except ImportError:
    print("[analyze] ERROR: 'anthropic' package not found.")
    print("  pip install anthropic")
    sys.exit(1)


def make_client():
    kwargs = {"api_key": api_key}
    if base_url:
        kwargs["base_url"] = base_url
    return anthropic.Anthropic(**kwargs)


def ask_claude(client, b64_png, prompt):
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1024,
        messages=[{"role": "user", "content": [
            {"type": "image",
             "source": {"type": "base64", "media_type": "image/png", "data": b64_png}},
            {"type": "text", "text": prompt},
        ]}],
    )
    return msg.content[0].text


# ---------------------------------------------------------------------------
# GPU timing formatter (reads from manifest JSON rows)
# ---------------------------------------------------------------------------
def format_perf_table(rows):
    lines = ["| Pass | Type | GPU time (µs) |", "|---|---|---|"]
    total_us = 0.0
    for r in rows:
        dur = "{:.1f}".format(r["duration_us"]) if r["duration_us"] is not None else "N/A"
        if r["duration_us"]:
            total_us += r["duration_us"]
        lines.append("| {} | {} | {} |".format(r["label"], r["type"], dur))
    lines.append("| **Total** | | **{:.1f}** |".format(total_us))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Final-frame prompt
# ---------------------------------------------------------------------------
FINAL_PROMPT = """You are analyzing the final rendered frame from a radiance cascades
3D Cornell Box renderer.

Known artifact types:
- Probe-grid banding: regular grid-shaped patterns in indirect light
- Cascade boundary seams: ring-shaped discontinuities
- Color bleeding errors: wrong wall color on wrong surfaces
- Shadow acne: dark speckles on lit surfaces
- Directional bin banding: hard angular color steps (~36 degrees) in indirect light
- Missing shadows: surfaces fully lit despite occlusion
- Outer-wall drift: monotonic brightening/darkening of wall edges

Describe artifacts by name and location. If clean, say so.
Rate quality: Poor / Fair / Good / Excellent."""


# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------
def main():
    print("[analyze] Processing capture: {}".format(cap_path))
    ts   = datetime.datetime.now().isoformat(timespec="seconds")
    stem = pathlib.Path(cap_path).stem

    pathlib.Path(output_dir).mkdir(parents=True, exist_ok=True)

    report = [
        "# RenderDoc GPU Analysis\n\n"
        "**Capture:** `{}`  \n"
        "**Analyzed:** {}  \n"
        "**Model:** claude-opus-4-7  \n\n---\n".format(cap_path, ts)
    ]

    client = make_client()

    # ------------------------------------------------------------------
    # Read extraction manifest (produced by rdoc_extract.py)
    # ------------------------------------------------------------------
    manifest_path = pathlib.Path(output_dir) / ("{}_manifest.json".format(stem))
    manifest = None
    if manifest_path.exists():
        print("[analyze] Manifest found: {}".format(manifest_path))
        with open(str(manifest_path)) as f:
            manifest = json.load(f)
        # Print the extract log so it appears in the parent console
        log_path = pathlib.Path(output_dir) / ("{}_extract.log".format(stem))
        if log_path.exists():
            print("[analyze] Extract log:")
            # Log written by qrenderdoc.exe under system ANSI encoding; decode tolerantly.
            log_bytes = log_path.read_bytes()
            for enc in ("utf-8", "gbk", "cp1252", "latin-1"):
                try:
                    print(log_bytes.decode(enc))
                    break
                except UnicodeDecodeError:
                    continue
    else:
        print("[analyze] No manifest found — texture/timing analysis skipped.")
        report.append(
            "## Note\n"
            "*`rdoc_extract.py` manifest not found. "
            "Run via `qrenderdoc.exe --py rdoc_extract.py` to enable texture/timing analysis.*\n"
        )

    # ------------------------------------------------------------------
    # GPU timing table
    # ------------------------------------------------------------------
    if manifest and manifest.get("timing"):
        timing_rows = manifest["timing"]
        has_timing = any(r["duration_us"] for r in timing_rows)
        report.append(
            "## GPU Performance (per dispatch/draw)\n\n"
            + format_perf_table(timing_rows) + "\n\n"
            + ("> N/A = driver did not expose timing. "
               "Enable via RenderDoc → Tools → Settings → 'Allow GPU timing'.\n"
               if not has_timing else "")
        )
    elif manifest:
        report.append("## GPU Performance\n*No dispatch/draw events found in capture.*\n")

    # ------------------------------------------------------------------
    # Texture analysis (from extracted PNGs in manifest)
    # ------------------------------------------------------------------
    if manifest and manifest.get("textures"):
        for name, info in manifest["textures"].items():
            label   = info.get("label", name)
            prompt  = info.get("prompt", "Describe what you see.")
            png_path = info.get("path")
            error   = info.get("error")

            if error or not png_path:
                report.append("## {}\n*Extraction failed: {}*\n".format(label, error))
                continue

            png_file = pathlib.Path(png_path)
            if not png_file.exists():
                report.append("## {}\n*PNG file not found: {}*\n".format(label, png_path))
                continue

            print("[analyze] Analyzing: {}".format(label))
            raw = png_file.read_bytes()
            b64 = base64.standard_b64encode(raw).decode("utf-8")
            analysis = ask_claude(client, b64, prompt)
            report.append("## {}\n\n{}\n".format(label, analysis))

    # ------------------------------------------------------------------
    # Final frame: extract thumbnail via renderdoccmd, then analyze
    # ------------------------------------------------------------------
    rdoccmd = pathlib.Path("C:/Program Files/RenderDoc/renderdoccmd.exe")
    auto_thumb = pathlib.Path(output_dir) / ("{}_thumb.png".format(stem))
    resolved_thumb = ""

    if rdoccmd.exists():
        print("[analyze] Extracting thumbnail via renderdoccmd...")
        result = subprocess.run(
            [str(rdoccmd), "thumb", "--out", str(auto_thumb), "--format", "png", cap_path],
            capture_output=True, text=True
        )
        if result.returncode == 0 and auto_thumb.exists():
            resolved_thumb = str(auto_thumb)
            print("[analyze] Thumbnail extracted: {}".format(resolved_thumb))
        else:
            print("[analyze] Thumbnail extraction failed: {}".format(result.stderr.strip()))

    if resolved_thumb and pathlib.Path(resolved_thumb).exists():
        print("[analyze] Analyzing final frame thumbnail...")
        raw = pathlib.Path(resolved_thumb).read_bytes()
        b64_thumb = base64.standard_b64encode(raw).decode("utf-8")
        analysis = ask_claude(client, b64_thumb, FINAL_PROMPT)
        report.append("## Final Frame (from capture thumbnail)\n\n{}\n".format(analysis))
    else:
        report.append("## Final Frame\n*Thumbnail not available.*\n")

    # ------------------------------------------------------------------
    # Write report
    # ------------------------------------------------------------------
    out = pathlib.Path(output_dir) / ("{}_pipeline.md".format(stem))
    out.write_text("\n".join(report), encoding="utf-8")
    print("[analyze] Analysis saved: {}".format(out))


main()
