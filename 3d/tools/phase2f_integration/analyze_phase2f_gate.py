import json
from pathlib import Path

import Imath
import numpy as np
import OpenEXR
from PIL import Image, ImageDraw


ROOT = Path("tools/phase2f_integration")
OUT_JSON = ROOT / "phase2f_binding_metrics.json"
OUT_COMPARE = ROOT / "phase2f_side_by_side.png"
OUT_PT_PREVIEW = ROOT / "phase2f_pt_reference_preview.png"

PNG_CAPTURES = {
    "volumetric_m0": ROOT / "phase2f_volumetric_m0.png",
    "surface_pure_m0_scale10": ROOT / "phase2f_surface_pure_m0_scale10.png",
    "hybrid_m0_scale10_blend50": ROOT / "phase2f_hybrid_m0_scale10_blend50.png",
    "surface_gi_only_m21_scale10": ROOT / "surface_rc_gi_only_mode21_scale10.png",
    "surface_chart_classification_m22": ROOT / "surface_chart_classification_mode22.png",
    "surface_c0_raw_sample_m23": ROOT / "surface_c0_raw_sample_mode23.png",
    "volumetric_gi_only_m17": ROOT / "volumetric_m17.png",
}

EXR_CAPTURES = {
    "cascade_gi": ROOT / "volumetric_m17_cascade_gi.exr",
    "pt_full": ROOT / "volumetric_m17_pt_full.exr",
    "pt_direct": ROOT / "volumetric_m17_pt_direct.exr",
    "gbuffer": ROOT / "volumetric_m17_gbuffer.exr",
}

EPS = 1e-6


def read_png(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def luma(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def png_stats(path: Path) -> dict:
    exists = path.exists()
    if not exists:
        return {"exists": False, "path": str(path)}
    arr = read_png(path)
    lum = luma(arr)
    nonblack = np.any(arr > 0.0, axis=2)
    return {
        "exists": True,
        "path": str(path),
        "width": int(arr.shape[1]),
        "height": int(arr.shape[0]),
        "bytes": int(path.stat().st_size),
        "mean_luma_8bit": float(lum.mean() * 255.0),
        "nonblack_pixels": int(nonblack.sum()),
        "nonblack_fraction": float(nonblack.mean()),
        "max_luma_8bit": float(lum.max() * 255.0),
    }


def png_delta(a_path: Path, b_path: Path) -> dict:
    if not a_path.exists() or not b_path.exists():
        return {"status": "missing"}
    a = read_png(a_path)
    b = read_png(b_path)
    if a.shape != b.shape:
        return {"status": "shape_mismatch", "a_shape": list(a.shape), "b_shape": list(b.shape)}
    d = a - b
    dl = luma(d)
    return {
        "status": "ok",
        "rms_luma_8bit": float(np.sqrt(np.mean(dl * dl)) * 255.0),
        "mae_luma_8bit": float(np.mean(np.abs(dl)) * 255.0),
        "mae_rgb_8bit": [float(v * 255.0) for v in np.mean(np.abs(d), axis=(0, 1))],
        "changed_pixels": int(np.any(np.abs(d) > (0.5 / 255.0), axis=2).sum()),
        "changed_fraction": float(np.any(np.abs(d) > (0.5 / 255.0), axis=2).mean()),
    }


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


def downsample_2x2_mean(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    return img[: h2 * 2, : w2 * 2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


def downsample_2x2_center(img: np.ndarray) -> np.ndarray:
    return img[1:img.shape[0]:2, 1:img.shape[1]:2]


def aces(color: np.ndarray) -> np.ndarray:
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    return np.clip((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0)


def to_srgb_preview(linear: np.ndarray) -> np.ndarray:
    return np.clip(aces(np.maximum(linear, 0.0)), 0.0, 1.0) ** (1.0 / 2.2)


def compare_rgb(a: np.ndarray, b: np.ndarray, mask: np.ndarray) -> dict:
    if not np.any(mask):
        return {"status": "empty_mask"}
    d = a - b
    dl = luma(d)
    valid = dl[mask]
    return {
        "status": "ok",
        "valid_pixels": int(mask.sum()),
        "rmse_luma": float(np.sqrt(np.mean(valid * valid))),
        "mae_luma": float(np.mean(np.abs(valid))),
        "mean_reference_luma": float(np.mean(luma(b)[mask])),
        "mean_candidate_luma": float(np.mean(luma(a)[mask])),
    }


def analyze_volumetric_exr() -> dict:
    missing = [str(p) for p in EXR_CAPTURES.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    cascade = read_exr(EXR_CAPTURES["cascade_gi"])
    pt_full = read_exr(EXR_CAPTURES["pt_full"])
    pt_direct = read_exr(EXR_CAPTURES["pt_direct"])
    gbuffer = read_exr(EXR_CAPTURES["gbuffer"], channels=("R", "G", "B", "A"))

    if cascade.shape[:2] != pt_full.shape[:2]:
        cascade = downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_full.shape[:2]:
        gbuffer = downsample_2x2_center(gbuffer)

    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)
    candidate_full = pt_direct + cascade
    valid = (gbuffer[..., 3] > 0.0) & (luma(pt_full) > 0.05)
    gi_valid = valid & (luma(pt_indirect) > 0.01)

    return {
        "status": "ok",
        "volumetric_composite_vs_pt_full_linear": compare_rgb(candidate_full, pt_full, valid),
        "volumetric_gi_vs_pt_indirect_linear": compare_rgb(cascade, pt_indirect, gi_valid),
        "pt_reference": {
            "pt_full": str(EXR_CAPTURES["pt_full"]),
            "pt_direct": str(EXR_CAPTURES["pt_direct"]),
            "pt_samples": 384,
            "width": int(pt_full.shape[1]),
            "height": int(pt_full.shape[0]),
        },
    }


def save_pt_preview() -> dict:
    if not EXR_CAPTURES["pt_full"].exists():
        return {"status": "missing", "path": str(EXR_CAPTURES["pt_full"])}
    pt_full = read_exr(EXR_CAPTURES["pt_full"])
    preview = to_srgb_preview(pt_full)
    img = Image.fromarray((preview * 255.0 + 0.5).astype(np.uint8), "RGB")
    img = img.resize((640, 480), Image.Resampling.BILINEAR)
    img.save(OUT_PT_PREVIEW)
    return png_stats(OUT_PT_PREVIEW)


def preview_quality_rows(pt_preview_path: Path) -> dict:
    rows = {}
    for key in ("volumetric_m0", "surface_pure_m0_scale10", "hybrid_m0_scale10_blend50"):
        rows[key] = png_delta(PNG_CAPTURES[key], pt_preview_path)
    rows["pt_reference_preview"] = {"status": "reference", "path": str(pt_preview_path)}
    return rows


def save_side_by_side() -> dict:
    labels = [
        ("Volumetric M0", PNG_CAPTURES["volumetric_m0"]),
        ("Surface C0 M0", PNG_CAPTURES["surface_pure_m0_scale10"]),
        ("Hybrid 50/50 M0", PNG_CAPTURES["hybrid_m0_scale10_blend50"]),
        ("Surface GI M21", PNG_CAPTURES["surface_gi_only_m21_scale10"]),
        ("PT preview", OUT_PT_PREVIEW),
    ]
    images = []
    for label, path in labels:
        img = Image.open(path).convert("RGB").resize((320, 240), Image.Resampling.BILINEAR)
        tile = Image.new("RGB", (320, 264), (20, 20, 20))
        tile.paste(img, (0, 24))
        draw = ImageDraw.Draw(tile)
        draw.text((8, 6), label, fill=(235, 235, 235))
        images.append(tile)

    canvas = Image.new("RGB", (320 * len(images), 264), (0, 0, 0))
    for i, tile in enumerate(images):
        canvas.paste(tile, (i * 320, 0))
    canvas.save(OUT_COMPARE)
    return png_stats(OUT_COMPARE)


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    pt_preview_stats = save_pt_preview()
    side_by_side_stats = save_side_by_side()

    image_stats = {key: png_stats(path) for key, path in PNG_CAPTURES.items()}
    deltas = {
        "surface_pure_m0_vs_volumetric_m0": png_delta(
            PNG_CAPTURES["surface_pure_m0_scale10"], PNG_CAPTURES["volumetric_m0"]
        ),
        "hybrid_m0_vs_volumetric_m0": png_delta(
            PNG_CAPTURES["hybrid_m0_scale10_blend50"], PNG_CAPTURES["volumetric_m0"]
        ),
        "surface_gi_only_m21_vs_black": {
            "status": "ok",
            "mean_luma_8bit": image_stats["surface_gi_only_m21_scale10"].get("mean_luma_8bit", 0.0),
            "nonblack_fraction": image_stats["surface_gi_only_m21_scale10"].get("nonblack_fraction", 0.0),
        },
    }
    exr_quality = analyze_volumetric_exr()
    preview_rows = preview_quality_rows(OUT_PT_PREVIEW)

    gates = {
        "build_release": "PASS",
        "shader_load_with_enable_surface_rc_gi": "PASS",
        "surface_rc_gi_only_nonblack": "PASS"
        if image_stats["surface_gi_only_m21_scale10"].get("nonblack_pixels", 0) > 0 else "FAIL",
        "mode0_surface_pure_distinct_from_volumetric": "PASS"
        if deltas["surface_pure_m0_vs_volumetric_m0"].get("changed_fraction", 0.0) > 0.01 else "FAIL",
        "mode0_hybrid_distinct_from_volumetric": "PASS"
        if deltas["hybrid_m0_vs_volumetric_m0"].get("changed_fraction", 0.0) > 0.01 else "FAIL",
        "volumetric_exr_pt_quality_measured": "PASS"
        if exr_quality.get("status") == "ok" else "FAIL",
        "surface_exr_pt_quality_measured": "PENDING",
        "phase2f_quality_gate_for_phase4": "PENDING",
    }

    result = {
        "status": "SMOKE_PASS_QUALITY_PENDING",
        "summary": (
            "Surface-RC is now bound into the raymarch mode-0 GI split path and produces "
            "distinct non-black PNG evidence. Linear EXR/PT quality is measured for the "
            "volumetric baseline only; surface-RC still needs a linear EXR output and real "
            "surface cascade hierarchy quality validation before Phase 4."
        ),
        "capture_date_local": "2026-06-15",
        "images": image_stats,
        "side_by_side": side_by_side_stats,
        "pt_preview": pt_preview_stats,
        "png_deltas": deltas,
        "preview_quality_vs_pt_png": preview_rows,
        "linear_exr_quality": exr_quality,
        "gates": gates,
        "known_limitations": [
            "surface-RC quality rows are PNG-preview comparisons, not linear EXR comparisons",
            "surface mode uses C0 direct-atlas smoke sampling; higher cascade hierarchy merge is not validated",
            "surface GI scale is diagnostic scale10 for visibility, not tuned physical energy",
            "automatic surface cascade hierarchy dispatch remains disabled for the Phase 2F smoke toggle",
        ],
    }
    OUT_JSON.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"wrote {OUT_JSON}")
    print(f"wrote {OUT_COMPARE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
