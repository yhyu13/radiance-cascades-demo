"""Compute pixel-difference metrics for Phase 1 mode 4 verification.

For each (scene, ref_mode, test_mode) triple, compute:
  - linear-RGB RMSE in [0,1] (proxy for FLIP / perceptual diff)
  - max per-pixel L1 error
  - fraction of pixels with L1 > 0.05 ("noticeable" threshold)
Also writes a heatmap PNG (per-pixel L1, scaled and gamma-corrected for visibility).

Output is JSON to stdout + heatmap PNGs alongside the inputs.
"""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent  # tools/
TOOLS = ROOT


def load_rgb01(p: Path) -> np.ndarray:
    img = Image.open(p).convert("RGB")
    return np.asarray(img, dtype=np.float32) / 255.0


def diff_metrics(a: np.ndarray, b: np.ndarray) -> dict:
    assert a.shape == b.shape, f"shape mismatch {a.shape} vs {b.shape}"
    d = a - b
    abs_d = np.abs(d)
    l1_per_pixel = abs_d.mean(axis=2)  # average across RGB
    return {
        "rmse_rgb": float(np.sqrt((d * d).mean())),
        "mae_rgb": float(abs_d.mean()),
        "max_l1_pixel": float(l1_per_pixel.max()),
        "p99_l1_pixel": float(np.percentile(l1_per_pixel, 99)),
        "frac_pixels_l1_gt_0.05": float((l1_per_pixel > 0.05).mean()),
        "frac_pixels_l1_gt_0.10": float((l1_per_pixel > 0.10).mean()),
        "shape": list(a.shape),
    }


def write_heatmap(a: np.ndarray, b: np.ndarray, out: Path, gain: float = 4.0) -> None:
    d = np.abs(a - b).mean(axis=2)
    d_vis = np.clip(d * gain, 0.0, 1.0)
    d_vis = np.power(d_vis, 1.0 / 2.2)  # gamma boost for visibility
    rgb = (np.stack([d_vis] * 3, axis=2) * 255).astype(np.uint8)
    Image.fromarray(rgb, "RGB").save(out)


def main() -> None:
    pairs = [
        # (scene, ref, test) — primary: m4 vs m3; sanity: m4 vs m0
        ("sponza",  3, 4),
        ("sponza",  0, 4),
        ("sponza",  0, 3),
        ("cornell", 3, 4),
        ("cornell", 0, 4),
        ("cornell", 0, 3),
    ]
    out = {}
    for scene, ref, test in pairs:
        scene_key = "sponza" if scene == "sponza" else "cornell"
        a_path = TOOLS / f"phase1_{scene_key}_m{ref}.png"
        b_path = TOOLS / f"phase1_{scene_key}_m{test}.png"
        if not a_path.exists() or not b_path.exists():
            print(f"SKIP {scene_key} m{ref} vs m{test}: missing input", file=sys.stderr)
            continue
        a = load_rgb01(a_path)
        b = load_rgb01(b_path)
        m = diff_metrics(a, b)
        m["a"] = a_path.name
        m["b"] = b_path.name
        heatmap_out = TOOLS / f"phase1_diff_{scene_key}_m{ref}_vs_m{test}.png"
        write_heatmap(a, b, heatmap_out)
        m["heatmap"] = heatmap_out.name
        out[f"{scene_key}__m{ref}_vs_m{test}"] = m

    # Mode 0 regression: m0 captured today vs prior baseline (sponza only — the prior
    # baseline for cornell-orig wasn't captured under the same camera, so skip).
    prior = TOOLS / "sponza_visibility_mode0.png"
    today = TOOLS / "phase1_sponza_m0.png"
    if prior.exists() and today.exists():
        a = load_rgb01(prior)
        b = load_rgb01(today)
        if a.shape == b.shape:
            m = diff_metrics(a, b)
            m["a"] = prior.name
            m["b"] = today.name
            heatmap_out = TOOLS / "phase1_diff_sponza_m0_vs_priorBaseline.png"
            write_heatmap(a, b, heatmap_out)
            m["heatmap"] = heatmap_out.name
            out["sponza__m0_today_vs_prior_baseline"] = m
        else:
            out["sponza__m0_today_vs_prior_baseline"] = {
                "skipped": "shape mismatch",
                "today_shape": list(b.shape),
                "prior_shape": list(a.shape),
            }

    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
