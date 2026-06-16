from __future__ import annotations

import argparse
import json
from pathlib import Path

import Imath
import numpy as np
import OpenEXR
from PIL import Image, ImageDraw


ROOT = Path("tools/milestone_c_quality")
CAPTURE_DIR = ROOT / "captures"
EPS = 1e-6


def read_exr(path: Path, channels=("R", "G", "B")) -> np.ndarray:
    f = OpenEXR.InputFile(str(path))
    dw = f.header()["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    h = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    planes = [
        np.frombuffer(f.channel(ch, pt), dtype=np.float32).reshape(h, w)
        for ch in channels
    ]
    return np.stack(planes, axis=-1)


def luma(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def downsample_2x2_mean(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    return img[: h2 * 2, : w2 * 2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


def downsample_2x2_center(img: np.ndarray) -> np.ndarray:
    return img[1:img.shape[0]:2, 1:img.shape[1]:2]


def aces(color: np.ndarray) -> np.ndarray:
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    return np.clip((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0)


def preview(linear: np.ndarray) -> np.ndarray:
    return np.clip(aces(np.maximum(linear, 0.0)), 0.0, 1.0) ** (1.0 / 2.2)


def save_preview(path: Path, rgb: np.ndarray) -> dict:
    arr = (preview(rgb) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(path)
    return image_stats(path)


def image_stats(path: Path) -> dict:
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    lum = luma(img)
    return {
        "path": str(path),
        "exists": path.exists(),
        "width": int(img.shape[1]),
        "height": int(img.shape[0]),
        "bytes": int(path.stat().st_size),
        "mean_luma_8bit": float(lum.mean() * 255.0),
        "nonblack_pixels": int(np.any(img > 0.0, axis=2).sum()),
        "nonblack_fraction": float(np.any(img > 0.0, axis=2).mean()),
    }


def diagnostic_image_stats(path: Path) -> dict:
    if not path.exists():
        return {"exists": False, "path": str(path)}
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    lum = luma(img)
    red = (img[..., 0] > 0.8) & (img[..., 1] < 0.1) & (img[..., 2] < 0.1)
    nonblack = np.any(img > 0.0, axis=2)
    return {
        "exists": True,
        "path": str(path),
        "width": int(img.shape[1]),
        "height": int(img.shape[0]),
        "bytes": int(path.stat().st_size),
        "mean_luma_8bit": float(lum.mean() * 255.0),
        "max_luma_8bit": float(lum.max() * 255.0),
        "nonblack_pixels": int(nonblack.sum()),
        "nonblack_fraction": float(nonblack.mean()),
        "red_invalid_pixels": int(red.sum()),
        "red_invalid_fraction": float(red.mean()),
    }


def compare(candidate: np.ndarray, reference: np.ndarray, mask: np.ndarray) -> dict:
    if not np.any(mask):
        return {"status": "empty_mask", "valid_pixels": 0}

    cand_l = luma(candidate)
    ref_l = luma(reference)
    diff_l = cand_l - ref_l
    rel = diff_l[mask] / np.maximum(ref_l[mask], EPS)
    ratio = cand_l[mask] / np.maximum(ref_l[mask], EPS)

    return {
        "status": "ok",
        "valid_pixels": int(mask.sum()),
        "rmse_luma": float(np.sqrt(np.mean(diff_l[mask] * diff_l[mask]))),
        "mae_luma": float(np.mean(np.abs(diff_l[mask]))),
        "mean_candidate_luma": float(np.mean(cand_l[mask])),
        "mean_reference_luma": float(np.mean(ref_l[mask])),
        "mean_luma_ratio": float(np.mean(cand_l[mask]) / max(float(np.mean(ref_l[mask])), EPS)),
        "ratio_self_mean": float(np.mean(ratio)),
        "ratio_self_p95_abs_minus_1": float(np.percentile(np.abs(ratio - 1.0), 95)),
        "relative_error_p95_abs": float(np.percentile(np.abs(rel), 95)),
        "dim_pct_ratio_lt_0p7": float(np.mean(ratio < 0.7) * 100.0),
        "bright_pct_ratio_gt_1p3": float(np.mean(ratio > 1.3) * 100.0),
    }


def paths_for(variant: str, n: int) -> dict[str, Path]:
    stem = f"cquality_sponza_{variant}_N{n:04d}_m17"
    return {
        "stem": Path(stem),
        "png": CAPTURE_DIR / f"{stem}.png",
        "gi": CAPTURE_DIR / f"{stem}_cascade_gi.exr",
        "pt_full": CAPTURE_DIR / f"{stem}_pt_full.exr",
        "pt_direct": CAPTURE_DIR / f"{stem}_pt_direct.exr",
        "gbuffer": CAPTURE_DIR / f"{stem}_gbuffer.exr",
        "log": CAPTURE_DIR / f"{stem}.log",
    }


def analyze_variant(variant: str, n: int) -> tuple[dict, dict]:
    p = paths_for(variant, n)
    missing = [str(path) for key, path in p.items() if key not in ("stem", "log") and not path.exists()]
    if missing:
        return {"status": "missing", "missing": missing}, {}

    gi = read_exr(p["gi"])
    pt_full = read_exr(p["pt_full"])
    pt_direct = read_exr(p["pt_direct"])
    gbuffer = read_exr(p["gbuffer"], channels=("R", "G", "B", "A"))

    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)
    if gi.shape[:2] != pt_full.shape[:2]:
        gi = downsample_2x2_mean(gi)
    if gbuffer.shape[:2] != pt_full.shape[:2]:
        gbuffer = downsample_2x2_center(gbuffer)

    finite = np.isfinite(gi)
    surface = gbuffer[..., 3] > 0.0
    valid_full = surface & (luma(pt_full) > 0.05)
    valid_gi = surface & (luma(pt_indirect) > 0.01)
    gi_lum = luma(gi)
    surface_gi_lum = gi_lum[surface] if np.any(surface) else gi_lum.reshape(-1)

    candidate_full = pt_direct + gi
    preview_path = ROOT / f"sponza_{variant}_gi_preview.png"
    preview_stats = save_preview(preview_path, gi)

    metrics = {
        "status": "ok",
        "paths": {key: str(path) for key, path in p.items() if key != "stem"},
        "resolution": {"gi": [int(gi.shape[1]), int(gi.shape[0])], "pt": [int(pt_full.shape[1]), int(pt_full.shape[0])]},
        "finite_fraction": float(finite.mean()),
        "negative_fraction": float((gi < -1e-6).mean()),
        "positive_gi_fraction": float((gi_lum > 0.0).mean()),
        "nonzero_gi_fraction": float((gi_lum > 1e-5).mean()),
        "mean_gi_luma": float(np.mean(surface_gi_lum)),
        "p95_gi_luma": float(np.percentile(surface_gi_lum, 95)),
        "p99_gi_luma": float(np.percentile(surface_gi_lum, 99)),
        "max_gi_luma": float(np.max(surface_gi_lum)),
        "gi_vs_pt_indirect_linear": compare(gi, pt_indirect, valid_gi),
        "composite_vs_pt_full_linear": compare(candidate_full, pt_full, valid_full),
        "preview": preview_stats,
    }
    return metrics, {"gi": gi, "pt_indirect": pt_indirect}


def save_side_by_side(images: dict[str, np.ndarray]) -> dict:
    labels = [
        ("volumetric GI", images["volumetric_gi"]),
        ("surface-RC GI scale1", images["surface_rc_gi"]),
        ("surface-RC GI scale10", images["surface_rc_scale10_gi"]),
        ("PT indirect", images["pt_indirect"]),
        ("surface abs diff", np.abs(images["surface_rc_gi"] - images["pt_indirect"])),
    ]
    tiles = []
    for label, linear in labels:
        arr = (preview(linear) * 255.0 + 0.5).astype(np.uint8)
        img = Image.fromarray(arr, "RGB").resize((320, 180), Image.Resampling.BILINEAR)
        tile = Image.new("RGB", (320, 204), (16, 16, 16))
        tile.paste(img, (0, 0))
        draw = ImageDraw.Draw(tile)
        draw.text((8, 184), label, fill=(235, 235, 235))
        tiles.append(tile)

    out = ROOT / "sponza_c_quality_linear_side_by_side.png"
    canvas = Image.new("RGB", (320 * len(tiles), 204), (0, 0, 0))
    for i, tile in enumerate(tiles):
        canvas.paste(tile, (i * 320, 0))
    canvas.save(out)
    return image_stats(out)


def save_diagnostic_side_by_side(diagnostics: dict[str, dict]) -> dict:
    labels = [
        ("mode 21 GI only", CAPTURE_DIR / "cquality_diag_m21_gi_only.png"),
        ("mode 22 chart", CAPTURE_DIR / "cquality_diag_m22_chart.png"),
        ("mode 23 C0 raw", CAPTURE_DIR / "cquality_diag_m23_c0_raw.png"),
    ]
    if not all(path.exists() for _, path in labels):
        return {"status": "missing"}

    tiles = []
    for label, path in labels:
        img = Image.open(path).convert("RGB").resize((320, 180), Image.Resampling.BILINEAR)
        tile = Image.new("RGB", (320, 204), (16, 16, 16))
        tile.paste(img, (0, 0))
        draw = ImageDraw.Draw(tile)
        draw.text((8, 184), label, fill=(235, 235, 235))
        tiles.append(tile)

    out = ROOT / "sponza_c_quality_diagnostics_side_by_side.png"
    canvas = Image.new("RGB", (320 * len(tiles), 204), (0, 0, 0))
    for i, tile in enumerate(tiles):
        canvas.paste(tile, (i * 320, 0))
    canvas.save(out)
    stats = image_stats(out)
    stats["diagnostic_inputs"] = diagnostics
    return stats


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--n", type=int, default=512)
    parser.add_argument("--quality-threshold", type=float, default=0.50)
    parser.add_argument("--competitive-factor", type=float, default=1.25)
    args = parser.parse_args()

    ROOT.mkdir(parents=True, exist_ok=True)

    volumetric, vol_images = analyze_variant("volumetric", args.n)
    surface, surface_images = analyze_variant("surface_rc", args.n)
    surface_scale10, surface_scale10_images = analyze_variant("surface_rc_scale10", args.n)

    side_by_side = {"status": "missing"}
    if volumetric.get("status") == "ok" and surface.get("status") == "ok" and surface_scale10.get("status") == "ok":
        side_by_side = save_side_by_side({
            "volumetric_gi": vol_images["gi"],
            "surface_rc_gi": surface_images["gi"],
            "surface_rc_scale10_gi": surface_scale10_images["gi"],
            "pt_indirect": surface_images["pt_indirect"],
        })

    diagnostics = {
        "mode21_surface_gi_only_scale10": diagnostic_image_stats(CAPTURE_DIR / "cquality_diag_m21_gi_only.png"),
        "mode22_surface_chart_classification": diagnostic_image_stats(CAPTURE_DIR / "cquality_diag_m22_chart.png"),
        "mode23_surface_c0_raw_scale10": diagnostic_image_stats(CAPTURE_DIR / "cquality_diag_m23_c0_raw.png"),
    }
    diagnostic_side_by_side = save_diagnostic_side_by_side(diagnostics)

    vol_gi = volumetric.get("gi_vs_pt_indirect_linear", {})
    surf_gi = surface.get("gi_vs_pt_indirect_linear", {})
    vol_rmse = vol_gi.get("rmse_luma")
    surf_rmse = surf_gi.get("rmse_luma")
    surf_p95 = surf_gi.get("ratio_self_p95_abs_minus_1")

    gates = {
        "volumetric_linear_measured": "PASS" if volumetric.get("status") == "ok" else "FAIL",
        "surface_rc_linear_measured": "PASS" if surface.get("status") == "ok" else "FAIL",
        "surface_rc_finite": "PASS" if surface.get("finite_fraction", 0.0) >= 0.999999 else "FAIL",
        "surface_rc_nonblack_linear": "PASS" if surface.get("nonzero_gi_fraction", 0.0) > 0.01 else "FAIL",
        "surface_rc_quality_p95_abs_rel_le_0p50": "PASS"
        if surf_p95 is not None and surf_p95 <= args.quality_threshold else "FAIL",
        "surface_rc_competitive_with_volumetric_rmse": "PASS"
        if surf_rmse is not None and vol_rmse is not None and surf_rmse <= vol_rmse * args.competitive_factor else "FAIL",
        "side_by_side_created": "PASS" if side_by_side.get("exists") else "FAIL",
    }
    gates["c_quality_gate_for_phase4"] = "PASS" if all(v == "PASS" for v in gates.values()) else "FAIL"

    out = {
        "test": "milestone_c_quality_sponza_linear_exr_pt_gate",
        "note": "Linear EXR/PT gate. Surface-RC is compared against PT indirect using physical surface-gi-scale=1; Phase 4 remains blocked unless c_quality_gate_for_phase4 passes.",
        "samples": args.n,
        "thresholds": {
            "surface_p95_abs_ratio_error": args.quality_threshold,
            "surface_rmse_vs_volumetric_factor": args.competitive_factor,
        },
        "variants": {
            "volumetric": volumetric,
            "surface_rc": surface,
            "surface_rc_scale10_diagnostic": surface_scale10,
        },
        "side_by_side": side_by_side,
        "diagnostics": diagnostics,
        "diagnostic_side_by_side": diagnostic_side_by_side,
        "gates": gates,
    }
    out_path = ROOT / "c_quality_metrics.json"
    out_path.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps({"out": str(out_path), "gates": gates}, indent=2))
    return 0 if gates["volumetric_linear_measured"] == "PASS" and gates["surface_rc_linear_measured"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
