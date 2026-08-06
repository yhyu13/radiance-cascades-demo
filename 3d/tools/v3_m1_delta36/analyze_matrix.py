import json
from pathlib import Path

import Imath
import numpy as np
import OpenEXR


CONDITIONS = ["baseline", "delta3", "delta6", "both"]
SCENES = ["cornell", "sponza"]
N = 2048


def read_exr(path: Path) -> np.ndarray:
    f = OpenEXR.InputFile(str(path))
    dw = f.header()["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    h = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    r = np.frombuffer(f.channel("R", pt), dtype=np.float32).reshape(h, w)
    g = np.frombuffer(f.channel("G", pt), dtype=np.float32).reshape(h, w)
    b = np.frombuffer(f.channel("B", pt), dtype=np.float32).reshape(h, w)
    return np.stack([r, g, b], axis=-1)


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def downsample_2x2(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    img = img[: h2 * 2, : w2 * 2]
    return img.reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


def analyze_stem(directory: Path, stem: str) -> dict:
    paths = {
        "png": directory / f"{stem}.png",
        "cascade_gi": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    casc = read_exr(paths["cascade_gi"])
    pt_full = read_exr(paths["pt_full"])
    pt_direct = read_exr(paths["pt_direct"])
    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)
    if casc.shape[:2] != pt_indirect.shape[:2]:
        casc = downsample_2x2(casc)

    casc_lum = lum(casc)
    pt_lum = lum(pt_indirect)
    mask = (pt_lum > 0.05) & (casc_lum > 0.001)
    if not np.any(mask):
        return {"status": "empty_mask", "valid": 0}

    ratio = casc_lum[mask] / np.maximum(pt_lum[mask], 1e-6)
    rel = ratio - 1.0
    return {
        "status": "ok",
        "cascade_mean": float(np.mean(casc_lum[mask])),
        "pt_indirect_mean": float(np.mean(pt_lum[mask])),
        "ratio_self": float(np.mean(ratio)),
        "p05_rel": float(np.percentile(rel, 5)),
        "p50_rel": float(np.percentile(rel, 50)),
        "p95_rel": float(np.percentile(rel, 95)),
        "abs_p95": float(np.percentile(np.abs(rel), 95)),
        "dim_pct": float(100.0 * np.mean(ratio < 0.7)),
        "bright_pct": float(100.0 * np.mean(ratio > 1.3)),
        "valid": int(mask.sum()),
    }


def as_metric_dict(obj: dict) -> dict:
    return {
        "ratio_self": float(obj["ratio_self"]),
        "abs_p95": float(obj["abs_p95"]),
        "bright_pct": float(obj["bright_pct"]),
        "dim_pct": float(obj["dim_pct"]),
        "valid": int(obj.get("valid", 0)),
    }


def delta_metrics(base: dict, cur: dict) -> dict:
    if cur.get("status") != "ok":
        return {"status": cur.get("status", "bad")}
    p95_drop_pct = 100.0 * (base["abs_p95"] - cur["abs_p95"]) / max(base["abs_p95"], 1e-6)
    return {
        "status": "ok",
        "ratio_shift_abs": abs(cur["ratio_self"] - base["ratio_self"]),
        "abs_p95_drop_pct": p95_drop_pct,
        "bright_drop_pp": base["bright_pct"] - cur["bright_pct"],
        "dim_regress_pp": cur["dim_pct"] - base["dim_pct"],
    }


def matrix_delta_metrics(base: dict, cur: dict) -> dict:
    if base.get("status") != "ok":
        return {"status": "missing_matrix_baseline"}
    if cur.get("status") != "ok":
        return {"status": cur.get("status", "bad")}
    p95_drop_pct = 100.0 * (base["abs_p95"] - cur["abs_p95"]) / max(base["abs_p95"], 1e-6)
    return {
        "status": "ok",
        "ratio_error_improve_abs": abs(base["ratio_self"] - 1.0) - abs(cur["ratio_self"] - 1.0),
        "abs_p95_drop_pct": p95_drop_pct,
        "bright_drop_pp": base["bright_pct"] - cur["bright_pct"],
        "dim_regress_pp": cur["dim_pct"] - base["dim_pct"],
    }


def classify_scene(d: dict) -> str:
    if d.get("status") != "ok":
        return "MISSING"
    strong = (
        d["ratio_shift_abs"] >= 0.05
        and d["abs_p95_drop_pct"] >= 30.0
        and d["bright_drop_pp"] >= 3.0
        and d["dim_regress_pp"] <= 2.0
    )
    marginal = (
        d["ratio_shift_abs"] >= 0.10
        or d["abs_p95_drop_pct"] >= 10.0
        or d["bright_drop_pp"] >= 1.0
    ) and d["dim_regress_pp"] <= 2.0
    if strong:
        return "STRONG"
    if marginal:
        return "MARGINAL"
    return "DEAD"


def classify_matrix_scene(d: dict) -> str:
    if d.get("status") != "ok":
        return "MISSING"
    strong = (
        d["ratio_error_improve_abs"] >= 0.10
        and d["abs_p95_drop_pct"] >= 30.0
        and d["bright_drop_pp"] >= 3.0
        and d["dim_regress_pp"] <= 2.0
    )
    marginal = (
        d["ratio_error_improve_abs"] >= 0.15
        or d["abs_p95_drop_pct"] >= 15.0
        or d["bright_drop_pp"] >= 5.0
    ) and d["dim_regress_pp"] <= 2.0
    if strong:
        return "STRONG"
    if marginal:
        return "MARGINAL"
    return "DEAD"


def combined_verdict(per_scene: dict) -> str:
    vals = list(per_scene.values())
    if any(v in ("MISSING", "DEAD") for v in vals):
        return "DEAD"
    if all(v == "STRONG" for v in vals):
        return "STRONG"
    return "MARGINAL"


def main() -> int:
    lock = json.loads(Path("tools/v3_baseline/baseline_lock.json").read_text(encoding="utf-8-sig"))
    baselines = {
        "cornell": as_metric_dict(lock["captures"]["cornell_cam0_cascade_off"]["metrics"]),
        "sponza": as_metric_dict(lock["captures"]["sponza_cam0_cascade_off"]["metrics"]),
    }

    rows = {}
    comparisons = {}
    matrix_comparisons = {}
    verdicts = {}
    matrix_verdicts = {}
    for scene in SCENES:
        rows[scene] = {}
        comparisons[scene] = {}
        matrix_comparisons[scene] = {}
        verdicts[scene] = {}
        matrix_verdicts[scene] = {}
        directory = Path(f"tools/v3_m1_delta36/captures_{scene}")
        for cond in CONDITIONS:
            stem = f"m1d36_{scene}_{cond}_N{N:04d}_m17"
            rows[scene][cond] = analyze_stem(directory, stem)
            comparisons[scene][cond] = delta_metrics(baselines[scene], rows[scene][cond])
            verdicts[scene][cond] = classify_scene(comparisons[scene][cond])
        for cond in CONDITIONS:
            if cond == "baseline":
                matrix_comparisons[scene][cond] = {
                    "status": "reference",
                    "ratio_error_improve_abs": 0.0,
                    "abs_p95_drop_pct": 0.0,
                    "bright_drop_pp": 0.0,
                    "dim_regress_pp": 0.0,
                }
                matrix_verdicts[scene][cond] = "REFERENCE"
                continue
            matrix_comparisons[scene][cond] = matrix_delta_metrics(rows[scene]["baseline"], rows[scene][cond])
            matrix_verdicts[scene][cond] = classify_matrix_scene(matrix_comparisons[scene][cond])

    combined = {
        cond: combined_verdict({scene: verdicts[scene][cond] for scene in SCENES})
        for cond in CONDITIONS
    }
    matrix_combined = {}
    for cond in CONDITIONS:
        if cond == "baseline":
            matrix_combined[cond] = "REFERENCE"
        else:
            matrix_combined[cond] = combined_verdict({scene: matrix_verdicts[scene][cond] for scene in SCENES})

    result = {
        "N": N,
        "baseline_metrics": baselines,
        "rows": rows,
        "comparisons_vs_m0": comparisons,
        "comparisons_vs_matrix_baseline": matrix_comparisons,
        "scene_verdicts": verdicts,
        "scene_verdicts_vs_matrix_baseline": matrix_verdicts,
        "combined_verdicts": combined,
        "combined_verdicts_vs_matrix_baseline": matrix_combined,
        "drop_rule": {
            "delta6_standalone": matrix_combined["delta6"],
            "drop_delta6_if_dead": matrix_combined["delta6"] == "DEAD",
        },
    }
    out = Path("tools/v3_m1_delta36/matrix_results.json")
    out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
