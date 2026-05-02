#!/usr/bin/env python3
"""
Phase 6a / 12a / 12b / 14a: Visual artifact triage via Claude.
Single-image: python analyze_screenshot.py <image.png> <output_dir> [probe_stats.json]
Burst:        python analyze_screenshot.py --burst <m0.png> <m3.png> <m6.png> <output_dir> [stats.json]
Sequence:     python analyze_screenshot.py --sequence <output_dir> <f0.png> <f1.png> ... [stats.json]
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


PROMPT_BURST = textwrap.dedent("""
    You are performing visual triage on three render-mode views of the same 3D Cornell
    Box frame, captured consecutively (same scene, same cascade state):

    - Mode 0: Final composite (direct + indirect GI, bilateral blur applied if enabled)
    - Mode 3: Indirect GI × 5 (indirect term only, amplified 5× for visibility)
    - Mode 6: GI only (raw indirect GI without amplification)

    Instructions:
    1. Compare the three images to isolate GI contribution: where is indirect light
       bright vs absent relative to the direct-only expectation?
    2. Locate banding artifacts in mode 6 (GI only) — describe their shape and spacing
       to help distinguish probe-grid vs directional-bin vs cascade-boundary sources.
    3. Assess whether the indirect term in mode 3 looks spatially smooth or noisy,
       and whether the bilateral blur in mode 0 has over-smoothed any edges.
    4. Rate each mode: Poor / Fair / Good / Excellent.
    5. Suggest one concrete parameter change based on all three views.

    Be specific about location. Do not speculate about artifacts you cannot see.
""")


def analyze_burst(m0: str, m3: str, m6: str,
                  output_dir: str, stats_path: str | None = None) -> None:
    labels = [
        ("Final render (mode 0)", m0),
        ("Indirect × 5 (mode 3)", m3),
        ("GI only (mode 6)",      m6),
    ]
    content = []
    for label, path in labels:
        img_b64 = base64.standard_b64encode(
            pathlib.Path(path).read_bytes()).decode("utf-8")
        content.append({"type": "text", "text": f"### {label}"})
        content.append({"type": "image",
                        "source": {"type": "base64",
                                   "media_type": "image/png",
                                   "data": img_b64}})

    stats_text = ""
    if stats_path:
        try:
            stats_text = pathlib.Path(stats_path).read_text(encoding="utf-8")
        except OSError as e:
            print(f"[12b] Warning: could not read stats: {e}", file=sys.stderr)

    prompt = PROMPT_BURST
    if stats_text:
        prompt += PROMPT_STATS_SUFFIX.format(stats=stats_text)
    content.append({"type": "text", "text": prompt})

    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1500,
        messages=[{"role": "user", "content": content}],
    )

    stem = pathlib.Path(m0).stem          # "frame_T_m0"
    if stem.endswith("_m0"):
        stem = stem[:-3]                  # "frame_T"
    out = pathlib.Path(output_dir) / (stem + ".md")
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    out.write_text(
        f"# AI Visual Triage — Burst (Modes 0 / 3 / 6)\n\n"
        f"**Mode 0:** `{m0}`  \n"
        f"**Mode 3:** `{m3}`  \n"
        f"**Mode 6:** `{m6}`  \n"
        f"**Stats:** `{stats_path or 'none'}`  \n"
        f"**Analyzed:** {ts}  \n"
        f"**Model:** claude-opus-4-7\n\n---\n\n{msg.content[0].text}\n",
        encoding="utf-8",
    )
    print(f"[12b] Burst analysis saved: {out}")


PROMPT_SEQUENCE = textwrap.dedent("""
    You are analyzing a sequence of {n} consecutive frames from a 3D Cornell Box renderer
    that uses temporal jitter + EMA (exponential moving average) for global illumination.
    All frames share the same render mode and scene state. Frames are labeled f0, f1, ...
    in capture order.

    The renderer offsets probe positions by a small random jitter each frame, cycling through
    a pattern of 4 positions every jitterHoldFrames frames. Temporal EMA blends each fresh
    bake with the history at weight temporalAlpha. If alpha is too high or jitter amplitude
    too large, the per-frame GI estimate varies significantly before EMA settles, causing
    visible flickering or "swimming" on lit surfaces.

    Instructions:
    1. Describe what is visually STABLE across all frames (1 sentence).
    2. Identify regions that CHANGE between frames. For each:
       - Where is it? (specific surface, corner, edge)
       - How large is the change? (subtle shimmer vs. large tonal jump)
       - What is the temporal pattern?
         * Random per-frame noise → EMA alpha too high or jitter amplitude too large
         * Periodic every 2 or 4 frames → jitter cycle not yet damped by EMA at current alpha
         * Slow monotonic drift → EMA converging (acceptable)
    3. Rate temporal stability: Unstable / Marginal / Stable / Excellent.
    4. Suggest one concrete parameter change (temporalAlpha, probeJitterScale,
       jitterHoldFrames, or jitterPatternSize) to improve stability.

    Be specific about location. Do not speculate about artifacts you cannot see.
""")


def analyze_sequence(frame_paths: list, output_dir: str,
                     stats_path: str | None = None) -> None:
    n = len(frame_paths)
    content = []
    for i, path in enumerate(frame_paths):
        img_b64 = base64.standard_b64encode(
            pathlib.Path(path).read_bytes()).decode("utf-8")
        content.append({"type": "text", "text": f"### Frame f{i}"})
        content.append({"type": "image",
                        "source": {"type": "base64",
                                   "media_type": "image/png",
                                   "data": img_b64}})

    stats_text = ""
    if stats_path:
        try:
            stats_text = pathlib.Path(stats_path).read_text(encoding="utf-8")
        except OSError as e:
            print(f"[14a] Warning: could not read stats: {e}", file=sys.stderr)

    prompt = PROMPT_SEQUENCE.format(n=n)
    if stats_text:
        prompt += PROMPT_STATS_SUFFIX.format(stats=stats_text)
    content.append({"type": "text", "text": prompt})

    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=2000,
        messages=[{"role": "user", "content": content}],
    )

    stem = pathlib.Path(frame_paths[0]).stem   # "frame_T_f0"
    if stem.endswith("_f0"):
        stem = stem[:-3]                        # "frame_T"
    out = pathlib.Path(output_dir) / (stem + "_seq.md")
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    frame_list = "\n".join(f"**f{i}:** `{p}`  " for i, p in enumerate(frame_paths))
    out.write_text(
        f"# AI Visual Triage — Sequence ({n} frames)\n\n"
        f"{frame_list}\n"
        f"**Stats:** `{stats_path or 'none'}`  \n"
        f"**Analyzed:** {ts}  \n"
        f"**Model:** claude-opus-4-7\n\n---\n\n{msg.content[0].text}\n",
        encoding="utf-8",
    )
    print(f"[14a] Sequence analysis saved: {out}")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--sequence":
        # --sequence <output_dir> <f0.png> <f1.png> ... [stats.json]
        if len(sys.argv) < 4:
            print("Usage: analyze_screenshot.py --sequence <output_dir> <f0.png> <f1.png> ..."
                  " [stats.json]")
            sys.exit(1)
        output_dir = sys.argv[2]
        # Optional stats: last arg ends in .json and there are at least 2 frame paths
        if sys.argv[-1].endswith(".json") and len(sys.argv) > 4:
            stats = sys.argv[-1]
            frame_paths = sys.argv[3:-1]
        else:
            stats = None
            frame_paths = sys.argv[3:]
        analyze_sequence(frame_paths, output_dir, stats)
    elif len(sys.argv) >= 2 and sys.argv[1] == "--burst":
        if len(sys.argv) < 6:
            print("Usage: analyze_screenshot.py --burst <m0> <m3> <m6> <output_dir> [stats]")
            sys.exit(1)
        stats = sys.argv[6] if len(sys.argv) > 6 else None
        analyze_burst(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], stats)
    else:
        if len(sys.argv) < 3:
            print("Usage: analyze_screenshot.py <image.png> <output_dir> [probe_stats.json]")
            sys.exit(1)
        stats = sys.argv[3] if len(sys.argv) > 3 else None
        analyze(sys.argv[1], sys.argv[2], stats)
