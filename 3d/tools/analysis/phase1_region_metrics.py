"""Per-region RMSE for Phase 1 Mode 4 verification (decision-gate secondary criterion).

The aggregate RMSE in `phase1_diff_metrics.py` was 0.0192 in Sponza (m4 vs m3) — at
the 0.02 threshold. Per-region RMSE on three meaningful crops verifies whether the
aggregate hides any region failing the threshold individually.

Crops chosen for the cam.md Sponza viewpoint at 1280x720:
  - lit_floor        : bright lit corridor floor with railing/step detail
  - shadowed_corner  : dark upper-left where wall meets ceiling
  - right_wall_cols  : receding columns on the right (where mode 4 should occlude)

Output JSON to stdout + crop visualizations as side-by-side PNGs.
"""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent  # tools/

# (y0, y1, x0, x1) — image is (H=720, W=1280); numpy slicing is [y0:y1, x0:x1].
REGIONS = {
    "lit_floor":       (500, 680, 300,  900),
    "shadowed_corner": (  0, 200,   0,  300),
    "right_wall_cols": (150, 550, 950, 1280),
}


def load_rgb01(p: Path) -> np.ndarray:
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0


def region_metrics(a: np.ndarray, b: np.ndarray) -> dict:
    d = a - b
    abs_d = np.abs(d)
    l1 = abs_d.mean(axis=2)
    return {
        "rmse_rgb": float(np.sqrt((d * d).mean())),
        "mae_rgb": float(abs_d.mean()),
        "p99_l1": float(np.percentile(l1, 99)),
        "max_l1": float(l1.max()),
        "frac_l1_gt_0.02": float((l1 > 0.02).mean()),
        "frac_l1_gt_0.05": float((l1 > 0.05).mean()),
        "shape": list(a.shape),
    }


def annotate_regions(p: Path, out: Path) -> None:
    """Draw the crop rectangles on a copy of `p` so the regions are visible."""
    img = Image.open(p).convert("RGB").copy()
    draw = ImageDraw.Draw(img)
    colors = {
        "lit_floor":       (40, 220, 40),    # green
        "shadowed_corner": (220, 220, 40),   # yellow
        "right_wall_cols": (220, 60, 60),    # red
    }
    for name, (y0, y1, x0, x1) in REGIONS.items():
        c = colors[name]
        draw.rectangle([x0, y0, x1 - 1, y1 - 1], outline=c, width=3)
        draw.text((x0 + 6, y0 + 6), name, fill=c)
    img.save(out)


def main() -> None:
    pairs = [
        ("m3", "m4"),  # primary: matches reference
        ("m0", "m4"),  # sanity: differs from baseline
        ("m0", "m3"),  # reference scale
    ]
    out = {"regions": {k: list(v) for k, v in REGIONS.items()},
           "image_shape_HxWxC": [720, 1280, 3]}
    captures = {m: load_rgb01(ROOT / f"phase1_sponza_{m}.png") for m in ("m0", "m3", "m4")}

    for ref, test in pairs:
        a, b = captures[ref], captures[test]
        per_region = {}
        for name, (y0, y1, x0, x1) in REGIONS.items():
            ar = a[y0:y1, x0:x1, :]
            br = b[y0:y1, x0:x1, :]
            per_region[name] = region_metrics(ar, br)
        out[f"sponza__{ref}_vs_{test}"] = per_region

    annotate_regions(ROOT / "phase1_sponza_m4.png",
                     ROOT / "phase1_sponza_regions_overlay.png")
    out["overlay"] = "phase1_sponza_regions_overlay.png"

    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
