from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent

IMAGES = {
    "sponza_volumetric_before": ROOT / "sponza_volumetric_before.png",
    "sponza_surface_rc_after": ROOT / "sponza_surface_rc_after.png",
    "sponza_surface_rc_gi_only": ROOT / "sponza_surface_rc_gi_only.png",
    "sponza_chart_classification": ROOT / "sponza_chart_classification_m22.png",
    "cornell_control": ROOT / "cornell_control.png",
}

JSONS = {
    "sponza_uv": ROOT / "sponza_uv_roundtrip_metrics.json",
    "cornell_control": ROOT / "cornell_control_metrics.json",
}


def read_rgb(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def luma(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def image_stats(path: Path) -> dict:
    rgb = read_rgb(path)
    lum = luma(rgb)
    nonblack = np.any(rgb > 0.0, axis=2)
    return {
        "path": str(path),
        "exists": path.exists(),
        "width": int(rgb.shape[1]),
        "height": int(rgb.shape[0]),
        "mean_luma_8bit": float(lum.mean() * 255.0),
        "max_luma_8bit": float(lum.max() * 255.0),
        "p95_luma_8bit": float(np.percentile(lum, 95) * 255.0),
        "nonblack_pixels": int(nonblack.sum()),
        "nonblack_fraction": float(nonblack.mean()),
        "dark_fraction_luma_lt_5": float((lum * 255.0 < 5.0).mean()),
        "bright_fraction_luma_gt_240": float((lum * 255.0 > 240.0).mean()),
    }


def image_delta(a_path: Path, b_path: Path) -> dict:
    a = read_rgb(a_path)
    b = read_rgb(b_path)
    d = b - a
    dl = luma(d)
    abs_dl = np.abs(dl)
    before_l = luma(a)
    after_l = luma(b)
    valid = before_l > (1.0 / 255.0)
    ratio = np.ones_like(before_l)
    ratio[valid] = after_l[valid] / np.maximum(before_l[valid], 1e-6)
    return {
        "a_path": str(a_path),
        "b_path": str(b_path),
        "rms_luma_8bit": float(math.sqrt(float(np.mean(dl * dl))) * 255.0),
        "mae_luma_8bit": float(np.mean(abs_dl) * 255.0),
        "p95_abs_luma_delta_8bit": float(np.percentile(abs_dl, 95) * 255.0),
        "changed_fraction": float(np.any(np.abs(d) > (0.5 / 255.0), axis=2).mean()),
        "mean_luma_ratio_after_over_before": float(np.mean(after_l[valid]) / max(float(np.mean(before_l[valid])), 1e-6)) if np.any(valid) else None,
        "ratio_self_mean": float(np.mean(ratio[valid])) if np.any(valid) else None,
        "ratio_self_p95_abs_minus_1": float(np.percentile(np.abs(ratio[valid] - 1.0), 95)) if np.any(valid) else None,
        "dim_pct": float((ratio[valid] < 0.95).mean() * 100.0) if np.any(valid) else None,
        "bright_pct": float((ratio[valid] > 1.05).mean() * 100.0) if np.any(valid) else None,
    }


def load_json(path: Path) -> dict:
    if not path.exists():
        return {"exists": False, "path": str(path)}
    data = json.loads(path.read_text(encoding="utf-8"))
    data["exists"] = True
    data["path"] = str(path)
    return data


def make_side_by_side(out_path: Path) -> dict:
    labels = [
        ("volumetric before", IMAGES["sponza_volumetric_before"]),
        ("surface-RC after", IMAGES["sponza_surface_rc_after"]),
        ("surface-RC GI only", IMAGES["sponza_surface_rc_gi_only"]),
        ("chart classification", IMAGES["sponza_chart_classification"]),
    ]
    tiles = []
    for label, path in labels:
        img = Image.open(path).convert("RGB").resize((320, 240), Image.Resampling.BILINEAR)
        tile = Image.new("RGB", (320, 264), (16, 16, 16))
        tile.paste(img, (0, 0))
        draw = ImageDraw.Draw(tile)
        draw.text((8, 244), label, fill=(235, 235, 235))
        tiles.append(tile)

    canvas = Image.new("RGB", (320 * len(tiles), 264), (0, 0, 0))
    for i, tile in enumerate(tiles):
        canvas.paste(tile, (i * 320, 0))
    canvas.save(out_path)
    return image_stats(out_path)


def main() -> None:
    image_rows = {name: image_stats(path) for name, path in IMAGES.items()}
    delta_rows = {
        "surface_rc_after_vs_volumetric_before": image_delta(
            IMAGES["sponza_volumetric_before"],
            IMAGES["sponza_surface_rc_after"],
        ),
    }
    uv_rows = {name: load_json(path) for name, path in JSONS.items()}
    side_by_side_stats = make_side_by_side(ROOT / "sponza_milestone_c_side_by_side.png")

    gates = {
        "sponza_uv_roundtrip": "PASS" if uv_rows["sponza_uv"].get("result") == "PASS" else "FAIL",
        "cornell_uv_roundtrip": "PASS" if uv_rows["cornell_control"].get("result") == "PASS" else "FAIL",
        "sponza_gi_only_nonblack": "PASS" if image_rows["sponza_surface_rc_gi_only"]["nonblack_pixels"] > 0 else "FAIL",
        "sponza_before_after_distinct": "PASS" if delta_rows["surface_rc_after_vs_volumetric_before"]["changed_fraction"] > 0.01 else "FAIL",
        "side_by_side_created": "PASS" if side_by_side_stats["nonblack_pixels"] > 0 else "FAIL",
    }
    gates["milestone_c_png_gate"] = "PASS" if all(v == "PASS" for v in gates.values()) else "FAIL"

    out = {
        "test": "milestone_c_sponza_surface_rc_png_gate",
        "note": "PNG-space gate only; EXR/PT quality comparison remains separate from this surface-chart integration gate.",
        "images": image_rows,
        "png_deltas": delta_rows,
        "uv_roundtrip": uv_rows,
        "side_by_side": side_by_side_stats,
        "gates": gates,
    }
    out_path = ROOT / "milestone_c_quality_metrics.json"
    out_path.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps({"out": str(out_path), "gates": gates}, indent=2))


if __name__ == "__main__":
    main()
