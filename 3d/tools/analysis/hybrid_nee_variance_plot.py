#!/usr/bin/env python3
"""
hybrid_nee_variance_plot.py — v1.3.1 improvement #1

Reads the captures produced by tools/hybrid_validation/v13_nee_variance/run_variance_sweep.ps1
and computes a per-frame RMSE-vs-frame-count plot to answer:

    "Does NEE (alpha=0.5) reduce variance vs cosine-only (alpha=0.0) at a fixed
    frame budget on a scene with shadow boundaries?"

Reference image: mean of the last N PT (mode 16) captures, decoded to linear light.

Outputs (in <sweep_dir>/):
  - reference.png         — averaged PT ground truth (linear-space mean, re-encoded sRGB)
  - rmse_curves.png       — log-x convergence plot, two curves (a00 cosine, a05 NEE)
  - metrics.json          — per-frame RMSE for both curves + half-life summary
  - metrics.md            — human-readable summary

Usage:
    python hybrid_nee_variance_plot.py tools/hybrid_validation/v13_nee_variance/
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def srgb_to_linear(rgb_u8: np.ndarray) -> np.ndarray:
    x = rgb_u8.astype(np.float32) / 255.0
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def linear_to_srgb_u8(rgb_lin: np.ndarray) -> np.ndarray:
    x = np.clip(rgb_lin, 0.0, 1.0).astype(np.float32)
    srgb = np.where(x <= 0.0031308, x * 12.92, 1.055 * (x ** (1.0 / 2.4)) - 0.055)
    return (np.clip(srgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def load_linear(path: Path) -> np.ndarray:
    img = Image.open(path).convert("RGB")
    return srgb_to_linear(np.asarray(img))


def list_captures(directory: Path, prefix: str):
    """Return [(frame_no, path), ...] sorted by frame_no."""
    out = []
    for p in directory.glob(f"{prefix}_f*.png"):
        try:
            n = int(p.stem.split("_f")[-1])
        except ValueError:
            continue
        out.append((n, p))
    return sorted(out, key=lambda x: x[0])


def rmse_linear(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.sqrt(np.mean((a - b) ** 2)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sweep_dir", type=Path, help="Directory containing run_variance_sweep.ps1 output")
    args = ap.parse_args()

    sweep_dir = args.sweep_dir
    cap_dir   = sweep_dir / "captures"
    if not cap_dir.is_dir():
        sys.exit(f"[err] captures dir not found: {cap_dir}")

    # 1. Build PT reference (mean of all ptref_f* in linear light)
    pt_files = [p for _, p in list_captures(cap_dir, "ptref")]
    if not pt_files:
        sys.exit("[err] no ptref_f*.png captures found")
    print(f"[ref] averaging {len(pt_files)} PT captures")
    acc = None
    for p in pt_files:
        lin = load_linear(p)
        acc = lin if acc is None else (acc + lin)
    reference = acc / float(len(pt_files))
    Image.fromarray(linear_to_srgb_u8(reference)).save(sweep_dir / "reference.png")
    print(f"[ref] saved {sweep_dir / 'reference.png'}")

    # 2. RMSE per frame for both alpha sweeps
    curves = {}
    for tag, label in [("a00", "alpha=0.0 (cosine-only)"),
                       ("a05", "alpha=0.5 (DI-cone MIS)")]:
        items = list_captures(cap_dir, tag)
        if not items:
            print(f"[warn] no captures for {tag}")
            continue
        frames, rmses = [], []
        for n, p in items:
            lin = load_linear(p)
            if lin.shape != reference.shape:
                print(f"[warn] shape mismatch {p}: {lin.shape} vs ref {reference.shape}")
                continue
            frames.append(n)
            rmses.append(rmse_linear(lin, reference))
        curves[tag] = {"label": label, "frames": frames, "rmse": rmses}
        print(f"[{tag}] {len(frames)} frames; final RMSE = {rmses[-1]:.5f}")

    if len(curves) < 2:
        sys.exit("[err] need both a00 and a05 curves to compare")

    # 3. Plot
    fig, ax = plt.subplots(figsize=(9, 5.5))
    colors = {"a00": "#d62728", "a05": "#1f77b4"}
    for tag, c in curves.items():
        ax.plot(c["frames"], c["rmse"], label=c["label"], color=colors.get(tag, None), linewidth=1.6)
    ax.set_xscale("log")
    ax.set_xlabel("frame index (log scale)")
    ax.set_ylabel("RMSE vs PT reference (linear light)")
    ax.set_title("v1.3 NEE convergence — cornell-orig-alcove")
    ax.grid(True, which="both", linestyle="--", alpha=0.3)
    ax.legend(loc="upper right")
    fig.tight_layout()
    plot_path = sweep_dir / "rmse_curves.png"
    fig.savefig(plot_path, dpi=120)
    print(f"[plot] saved {plot_path}")

    # 4. Half-life summary: frame index where RMSE first drops below 50% of frame-10 value
    summary = {}
    for tag, c in curves.items():
        rmses = c["rmse"]
        if not rmses:
            continue
        baseline = rmses[0]
        target   = 0.5 * baseline
        half_frame = None
        for f, r in zip(c["frames"], rmses):
            if r <= target:
                half_frame = f
                break
        final_rmse = rmses[-1]
        summary[tag] = {
            "label": c["label"],
            "rmse_first_frame": baseline,
            "rmse_last_frame":  final_rmse,
            "half_life_frame":  half_frame,
            "improvement_vs_first_pct": 100.0 * (1.0 - final_rmse / baseline) if baseline > 0 else None,
        }

    # 5. Compare last-frame RMSE
    a00_final = curves.get("a00", {}).get("rmse", [None])[-1]
    a05_final = curves.get("a05", {}).get("rmse", [None])[-1]
    relative_pct = None
    if a00_final and a05_final:
        relative_pct = 100.0 * (a00_final - a05_final) / a00_final  # >0 ⇒ NEE wins

    # 6. Dump json + md
    metrics = {
        "scene": "cornell-orig-alcove",
        "reference_frames_averaged": len(pt_files),
        "curves": {tag: {"label": c["label"], "frames": c["frames"], "rmse": c["rmse"]}
                   for tag, c in curves.items()},
        "summary": summary,
        "nee_vs_cosine_last_frame_improvement_pct": relative_pct,
    }
    (sweep_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    md_lines = [
        "# v1.3 NEE Variance Sweep — cornell-orig-alcove",
        "",
        f"PT reference: mean of {len(pt_files)} mode-16 captures.",
        "",
        "## Per-curve summary",
        "",
        "| Curve | First-frame RMSE | Last-frame RMSE | Half-life frame | Improvement vs first |",
        "|---|---:|---:|---:|---:|",
    ]
    for tag, s in summary.items():
        hf = s["half_life_frame"] if s["half_life_frame"] is not None else "—"
        impr = f"{s['improvement_vs_first_pct']:.1f}%" if s["improvement_vs_first_pct"] is not None else "—"
        md_lines.append(f"| {s['label']} | {s['rmse_first_frame']:.5f} | {s['rmse_last_frame']:.5f} | {hf} | {impr} |")
    md_lines += [
        "",
        f"## NEE vs cosine (last-frame RMSE)",
        "",
        f"- Last-frame RMSE (a00 cosine-only): {a00_final:.5f}" if a00_final else "- a00 missing",
        f"- Last-frame RMSE (a05 DI-cone):     {a05_final:.5f}" if a05_final else "- a05 missing",
        f"- Relative improvement (>0 ⇒ NEE wins): {relative_pct:.2f}%" if relative_pct is not None else "- relative N/A",
        "",
        "See `rmse_curves.png` for the convergence plot and `reference.png` for the PT ground truth.",
    ]
    (sweep_dir / "metrics.md").write_text("\n".join(md_lines), encoding="utf-8")
    print(f"[done] wrote metrics.json + metrics.md")

    # 7. Console verdict
    print("")
    if relative_pct is not None:
        verdict = "WIN" if relative_pct > 0.5 else ("LOSS" if relative_pct < -0.5 else "TIE")
        print(f"VERDICT: NEE {verdict} ({relative_pct:+.2f}% last-frame RMSE vs cosine)")


if __name__ == "__main__":
    main()
