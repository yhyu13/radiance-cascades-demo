"""§4.0 prerequisite empirical leak test — quantitative diff across viewpoints.

For each viewpoint, compute m0-vs-m4 RGB RMSE and identify where mode 0 is
significantly brighter than mode 4 (potential leak signature: m0 picks up
cross-wall light that m4 correctly occludes).

Output: JSON with per-viewpoint metrics + diff-overshoot heatmaps.
"""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

VIEWS = [
    ("cam.md (corridor)",   "phase1_sponza_m0.png",     "phase1_sponza_m4.png"),
    ("V5 near-wall",        "phase4_0_v5_backwall_m0.png",  "phase4_0_v5_backwall_m4.png"),
    ("V6 pitched-up",       "phase4_0_v6_pitchup_m0.png",   "phase4_0_v6_pitchup_m4.png"),
]


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0


def metrics(a, b):
    """a = mode 0 (potentially leaky), b = mode 4 (occluded).
    Look for regions where mode 0 is significantly brighter than mode 4
    (positive overshoot = leak signature)."""
    d = a - b
    abs_d = np.abs(d)
    overshoot = np.maximum(d, 0.0).mean(axis=2)  # only positive (m0 > m4)
    deficit   = np.maximum(-d, 0.0).mean(axis=2) # negative (m0 < m4) — should be small
    return {
        "rmse_rgb": float(np.sqrt((d * d).mean())),
        "mae_rgb": float(abs_d.mean()),
        "mean_overshoot_m0_brighter_than_m4": float(overshoot.mean()),
        "mean_deficit_m0_dimmer_than_m4":     float(deficit.mean()),
        "frac_pixels_overshoot_gt_0.05": float((overshoot > 0.05).mean()),
        "frac_pixels_overshoot_gt_0.10": float((overshoot > 0.10).mean()),
        "p99_overshoot": float(np.percentile(overshoot, 99)),
        "max_overshoot": float(overshoot.max()),
    }


def main():
    out = {}
    for name, p_m0, p_m4 in VIEWS:
        a_path = ROOT / p_m0
        b_path = ROOT / p_m4
        if not a_path.exists() or not b_path.exists():
            out[name] = {"skipped": "missing files"}
            continue
        a = load(a_path)
        b = load(b_path)
        if a.shape != b.shape:
            out[name] = {"skipped": f"shape mismatch {a.shape} vs {b.shape}"}
            continue
        out[name] = metrics(a, b)
        out[name]["m0"] = p_m0
        out[name]["m4"] = p_m4

        # Save an "overshoot" heatmap — bright pixels = mode 0 leaked light here
        overshoot = np.maximum(a - b, 0.0).mean(axis=2)
        gain = 8.0  # boost for visibility
        vis = np.clip(overshoot * gain, 0.0, 1.0)
        vis = np.power(vis, 1.0 / 2.2)
        rgb = (np.stack([vis, vis * 0.5, vis * 0.2], axis=2) * 255).astype(np.uint8)
        out_name = f"phase4_0_overshoot_{name.split()[0].replace('.', '').replace('(','')}.png"
        Image.fromarray(rgb, "RGB").save(ROOT / out_name)
        out[name]["overshoot_heatmap"] = out_name

    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
