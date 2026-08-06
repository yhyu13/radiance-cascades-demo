#!/usr/bin/env python3
"""
hybrid_quality_metrics.py — Phase 8 Day 2

Reads the 5 PNG captures from --hybrid-ab-sweep + sweep_metadata.json, computes
per-pixel quality metrics for each hybrid composition variant against the PT
ground-truth reference, and emits:
  - metrics.json (parseable for tooling)
  - metrics.md   (markdown table for human review)

Metrics per variant {cascade_only, hybrid_mix, hybrid_max, hybrid_variance}:
  - rmse_vs_pt          — full-image RMSE in linear light
  - mean_brightness     — mean luminance
  - brightness_ratio    — mean(variant) / mean(pt_reference)
  - rmse_per_region     — 4x4 grid RMSE (find local hotspots)
  - blue_pixel_count    — pixels where variant is significantly DIMMER than PT
                          (proxy for mode 19 dominant-blue diagnostic)
  - red_pixel_count     — pixels where variant is significantly BRIGHTER than PT

Convention: PNGs are sRGB-encoded tonemapped output (raymarch.frag ACES + pow(1/2.2)).
We decode to linear via inverse sRGB curve before doing arithmetic. RMSE in linear
light is what matters for visual fidelity claims.

Usage:
    python hybrid_quality_metrics.py <sweep_dir>
    python hybrid_quality_metrics.py tools/hybrid_validation/cornell_orig/

Outputs land in <sweep_dir>/ alongside the captures.

L9 discipline: includes cascade_only baseline so relative improvement is unambiguous.
L9 noise-floor: see --noise-stride for repeated-sample variance estimation if needed.
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


# --------------------------------------------------------------------------------
# sRGB <-> linear conversion (IEC 61966-2-1)
# --------------------------------------------------------------------------------

def srgb_to_linear(rgb_u8: np.ndarray) -> np.ndarray:
    """Decode sRGB-encoded 8-bit RGB to linear-light float in [0,1]."""
    x = rgb_u8.astype(np.float32) / 255.0
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def luminance(rgb_linear: np.ndarray) -> np.ndarray:
    """Rec.709 luminance from linear RGB. Last axis = channels."""
    return (rgb_linear[..., 0] * 0.2126
            + rgb_linear[..., 1] * 0.7152
            + rgb_linear[..., 2] * 0.0722)


# --------------------------------------------------------------------------------
# Metric primitives
# --------------------------------------------------------------------------------

def rmse_linear(a: np.ndarray, b: np.ndarray) -> float:
    """RMSE in linear light, per-channel averaged."""
    diff = a - b
    return float(np.sqrt((diff * diff).mean()))


def per_region_rmse(a: np.ndarray, b: np.ndarray, grid: int = 4) -> list:
    """4x4 (or NxN) grid RMSE — find local hotspots that full-image RMSE hides."""
    h, w, _ = a.shape
    rh, rw = h // grid, w // grid
    out = []
    for gy in range(grid):
        row = []
        for gx in range(grid):
            ay = gy * rh
            ax = gx * rw
            sub_a = a[ay:ay + rh, ax:ax + rw, :]
            sub_b = b[ay:ay + rh, ax:ax + rw, :]
            row.append(rmse_linear(sub_a, sub_b))
        out.append(row)
    return out


def dim_bright_counts(variant_lum: np.ndarray,
                      pt_lum: np.ndarray,
                      threshold: float = 0.02) -> tuple:
    """Mode-19-style blue/red pixel counts.
    blue = variant significantly DIMMER than PT (cascade under-integration pattern).
    red  = variant significantly BRIGHTER than PT.
    threshold is in linear-luminance units (~ 0.02 ≈ 1 step at 8-bit sRGB mid-tones).
    """
    delta = variant_lum - pt_lum
    return (int((delta < -threshold).sum()),  # dim ("blue")
            int((delta >  threshold).sum()))  # bright ("red")


# --------------------------------------------------------------------------------
# Per-variant metrics
# --------------------------------------------------------------------------------

def compute_variant_metrics(variant_path: Path, pt_path: Path) -> dict:
    """Compute the full metric bundle for one variant vs PT reference."""
    var_img = np.array(Image.open(variant_path).convert("RGB"))
    pt_img  = np.array(Image.open(pt_path).convert("RGB"))

    if var_img.shape != pt_img.shape:
        return {"error": f"shape mismatch: variant {var_img.shape} vs pt {pt_img.shape}"}

    var_lin = srgb_to_linear(var_img)
    pt_lin  = srgb_to_linear(pt_img)

    var_lum = luminance(var_lin)
    pt_lum  = luminance(pt_lin)

    var_mean = float(var_lum.mean())
    pt_mean  = float(pt_lum.mean())

    blue, red = dim_bright_counts(var_lum, pt_lum)
    total_pixels = int(var_lum.size)

    return {
        "rmse_vs_pt":         rmse_linear(var_lin, pt_lin),
        "mean_brightness":    var_mean,
        "brightness_ratio":   (var_mean / pt_mean) if pt_mean > 1e-6 else 0.0,
        "rmse_per_region":    per_region_rmse(var_lin, pt_lin),
        "blue_pixel_count":   blue,
        "red_pixel_count":    red,
        "blue_pct":           100.0 * blue / total_pixels,
        "red_pct":            100.0 * red  / total_pixels,
        "total_pixels":       total_pixels,
    }


# --------------------------------------------------------------------------------
# Markdown report
# --------------------------------------------------------------------------------

def render_markdown(metadata: dict, metrics: dict) -> str:
    lines = []
    lines.append(f"# Hybrid v1.2 Quality Metrics — Phase 8")
    lines.append("")
    lines.append(f"- Version: `{metadata.get('version', 'unknown')}`")
    lines.append(f"- Hybrid rays/frame: `{metadata.get('hybrid_rays_per_frame')}`")
    lines.append(f"- Hybrid EMA alpha: `{metadata.get('hybrid_ema_alpha')}`")
    lines.append(f"- Cascade variance prior: `{metadata.get('hybrid_cascade_variance')}`")
    lines.append(f"- Confidence samples: `{metadata.get('hybrid_confidence_samples')}`")
    lines.append(f"- Bilateral radius: `{metadata.get('hybrid_blur_radius')}`")
    lines.append(f"- Stabilize frames per stage: `{metadata.get('stabilize_frames')}`")
    lines.append("")
    lines.append("## Per-variant metrics vs PT reference")
    lines.append("")
    lines.append("| Variant | RMSE | Mean L | L ratio to PT | Blue % | Red % |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for name in ("cascade_only", "hybrid_mix", "hybrid_max", "hybrid_variance"):
        m = metrics.get(name, {})
        if "error" in m:
            lines.append(f"| {name} | ERROR | — | — | — | — |")
            continue
        lines.append(f"| {name} | {m['rmse_vs_pt']:.4f} | {m['mean_brightness']:.4f} "
                     f"| {m['brightness_ratio']:.3f} | {m['blue_pct']:.2f}%% | {m['red_pct']:.2f}%% |")
    lines.append("")
    lines.append("## Per-region (4x4) RMSE — local hotspots")
    lines.append("")
    for name in ("cascade_only", "hybrid_mix", "hybrid_max", "hybrid_variance"):
        m = metrics.get(name, {})
        if "error" in m or "rmse_per_region" not in m:
            continue
        lines.append(f"### {name}")
        lines.append("```")
        for row in m["rmse_per_region"]:
            lines.append("  " + "  ".join(f"{v:.4f}" for v in row))
        lines.append("```")
        lines.append("")
    lines.append("## Interpretation guide")
    lines.append("")
    lines.append("- **Lower RMSE = closer to PT truth.** Compare each variant to `cascade_only` baseline.")
    lines.append("- **L ratio**: 1.0 = perfect brightness match. < 1.0 = dimmer than PT (under-integration).")
    lines.append("- **Blue %**: fraction of pixels significantly dimmer than PT. Cascade-only typically has high Blue % on Sponza.")
    lines.append("- **Red %**: fraction significantly brighter than PT (over-integration, e.g. MB gain too high).")
    lines.append("- **Per-region RMSE**: identifies regions where the variant fails. Hot 4x4 cell = candidate for v2 cascade-MB-delta dispatch.")
    lines.append("")
    return "\n".join(lines)


# --------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Hybrid v1.2 quality metrics")
    ap.add_argument("sweep_dir", help="Directory containing sweep_metadata.json + captures")
    args = ap.parse_args()

    sweep_dir = Path(args.sweep_dir)
    meta_path = sweep_dir / "sweep_metadata.json"
    if not meta_path.exists():
        print(f"ERROR: {meta_path} not found. Did the sweep complete?", file=sys.stderr)
        sys.exit(1)

    metadata = json.loads(meta_path.read_text())
    captures = metadata.get("captures", {})
    if "pt_reference" not in captures:
        print("ERROR: pt_reference capture missing from metadata", file=sys.stderr)
        sys.exit(1)

    pt_path = Path(captures["pt_reference"])
    if not pt_path.is_absolute():
        pt_path = sweep_dir / pt_path
    if not pt_path.exists():
        # Try just the basename in the sweep dir (raylib TakeScreenshot strips paths)
        pt_path = sweep_dir / Path(captures["pt_reference"]).name
    if not pt_path.exists():
        print(f"ERROR: PT reference PNG not found ({captures['pt_reference']})", file=sys.stderr)
        sys.exit(1)

    metrics = {}
    for name in ("cascade_only", "hybrid_mix", "hybrid_max", "hybrid_variance"):
        if name not in captures:
            metrics[name] = {"error": "capture missing from metadata"}
            continue
        var_path = Path(captures[name])
        if not var_path.is_absolute():
            var_path = sweep_dir / var_path
        if not var_path.exists():
            var_path = sweep_dir / Path(captures[name]).name
        if not var_path.exists():
            metrics[name] = {"error": f"capture file not found: {captures[name]}"}
            continue
        print(f"[metrics] computing {name} vs PT...", file=sys.stderr)
        metrics[name] = compute_variant_metrics(var_path, pt_path)

    # Write outputs
    out_json = sweep_dir / "metrics.json"
    out_md   = sweep_dir / "metrics.md"
    out_json.write_text(json.dumps({"metadata": metadata, "metrics": metrics}, indent=2))
    out_md.write_text(render_markdown(metadata, metrics))
    print(f"[metrics] wrote {out_json}", file=sys.stderr)
    print(f"[metrics] wrote {out_md}",   file=sys.stderr)


if __name__ == "__main__":
    main()
