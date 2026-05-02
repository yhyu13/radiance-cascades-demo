#!/usr/bin/env python3
"""
Phase 6a / 12a: Visual artifact triage via Claude.
Usage: python analyze_screenshot.py <image.png> <output_dir> [probe_stats.json]
Requires: ANTHROPIC_API_KEY env var, pip install anthropic
Opt-in local developer tool -- not part of CI or branch validation.
"""
import sys
import base64
import pathlib
import textwrap
import datetime
import anthropic

# load from .env file
from dotenv import load_dotenv
load_dotenv()

PROMPT_BASE = textwrap.dedent("""
    You are performing heuristic visual triage on a real-time 3D Cornell Box rendered
    with a radiance cascades global illumination system. The system stores per-direction
    probe radiance in a directional atlas and merges it across 4 cascade levels (C0 near
    to C3 far).

    This is triage -- your goal is to name and locate artifacts. Where the artifact
    geometry is tightly coupled to a known physical cause, state it:
    - Regular banding at exactly the probe cell spacing (~12.5 cm for C0) -> likely
      the isotropic reduction texture is being sampled instead of the directional atlas
    - Hard angular color steps at ~36 degree intervals -> directional bin banding (D=4, 8 bins)
    - Ring-shaped discontinuity at a fixed distance -> cascade boundary seam

    Do NOT infer software-level causes (wrong texture unit, wrong uniform, bad shader
    branch) from visual appearance alone -- those require pipeline inspection.

    Known artifact types:
    - Probe-grid banding: regular grid-shaped patterns in indirect light
    - Cascade boundary seams: ring-shaped discontinuities where cascade levels meet
    - Color bleeding errors: wrong wall color (red/green) tinting surfaces incorrectly
    - Shadow acne / self-shadowing: dark speckles on directly lit surfaces
    - Directional bin banding: hard angular color steps in indirect (~36 degree steps at D=4)
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

PROMPT_STATS_SUFFIX = textwrap.dedent("""
    Additional context — current probe statistics:
    {stats}

    Use the stats to:
    - Assess cascade convergence (surfPct, anyPct — low means under-sampled probes)
    - Cross-check luminance variance with visible banding (high variance = spatial structure)
    - Suggest one concrete parameter change (e.g. temporalAlpha, probeJitterScale, dirRes)
      based on both the image and the stats.
""")


def analyze(image_path: str, output_dir: str, stats_path: str | None = None) -> None:
    img_bytes = pathlib.Path(image_path).read_bytes()
    b64 = base64.standard_b64encode(img_bytes).decode("utf-8")

    stats_text = ""
    if stats_path:
        try:
            stats_text = pathlib.Path(stats_path).read_text(encoding="utf-8")
        except OSError as e:
            print(f"[12a] Warning: could not read stats file: {e}", file=sys.stderr)

    prompt = PROMPT_BASE
    if stats_text:
        prompt += PROMPT_STATS_SUFFIX.format(stats=stats_text)

    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1024,
        messages=[{"role": "user", "content": [
            {"type": "image",
             "source": {"type": "base64", "media_type": "image/png", "data": b64}},
            {"type": "text", "text": prompt},
        ]}],
    )
    text = msg.content[0].text

    stem = pathlib.Path(image_path).stem
    out = pathlib.Path(output_dir) / (stem + ".md")
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    header_note = (
        "Phase 12a triage (image + probe stats)" if stats_text
        else "Phase 6a triage (image only)"
    )
    out.write_text(
        f"# AI Visual Triage\n\n"
        f"**Image:** `{image_path}`  \n"
        f"**Stats:** `{stats_path or 'none'}`  \n"
        f"**Analyzed:** {ts}  \n"
        f"**Model:** claude-opus-4-7  \n"
        f"**Note:** {header_note} — geometry-coupled causes stated; "
        f"software-level root causes require Phase 6b pipeline inspection.\n\n"
        f"---\n\n{text}\n",
        encoding="utf-8",
    )
    print(f"[6a/12a] Analysis saved: {out}")
    print(text)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: analyze_screenshot.py <image.png> <output_dir> [probe_stats.json]")
        sys.exit(1)
    stats = sys.argv[3] if len(sys.argv) > 3 else None
    analyze(sys.argv[1], sys.argv[2], stats)
