"""M1 Stage 11c — Light-type discriminator analyzer.

Compares Cornell point-light baseline (reused from Stage 8/11b) against new
Cornell directional-light capture. Reports per-pixel mean/median ratios,
per-cell weighted ratios, absolute cascade_gi + pt_gi luma means, green-wall
asymmetry cross-check, and the bridged-fraction verdict per plan §3.
"""

import importlib.util
import json
import re
from collections import defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_cornell_light_type")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
EPS = 1e-6
ATLAS_RES = 32

VARIANTS = {
    "cornell_point_baseline": {
        "dir": "tools/v3_m1_source_energy_ab/captures_cornell_baseline",
        "stem": "m1stage8_cornell_baseline_N2048_m17",
        "log": None,
    },
    "cornell_directional": {
        "dir": "tools/v3_m1_cornell_light_type/captures_cornell_directional",
        "stem": "m1stage11c_cornell_directional_N2048_m17",
        "log": "tools/v3_m1_cornell_light_type/captures_cornell_directional/m1stage11c_cornell_directional_N2048_m17.log",
    },
}


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec); assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def grep_light_setup(log_path: str | None) -> dict:
    """SC6: dump lightPosition / useDirectionalLight / lightDirection from the capture log."""
    if log_path is None or not Path(log_path).exists():
        return {"source": "not_available"}
    lines = Path(log_path).read_text(encoding="utf-8", errors="ignore").splitlines()
    out = {"source": log_path}
    for line in lines:
        if "useDirectionalLight" in line:
            out["useDirectionalLight"] = line.strip()
        elif "lightDirection=" in line and "lightDirection=(" in line:
            out["lightDirection"] = line.strip()
        elif "lightPosition reset" in line or "lightPosition=" in line:
            out["lightPosition"] = line.strip()
    return out


def classify_albedo(rgb: np.ndarray) -> np.ndarray:
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    is_red   = (r > 0.3) & (g < 0.3) & (b < 0.3)
    is_green = (g > 0.3) & (r < 0.3) & (b < 0.3)
    is_white = (r > 0.4) & (g > 0.4) & (b > 0.4) & (abs(r - g) < 0.2) & (abs(g - b) < 0.2)
    out = np.zeros(rgb.shape[:2], dtype=np.int32)
    out[is_white] = 3
    out[is_red]   = 1
    out[is_green] = 2
    out[(out == 0) & ((r + g + b) > 0.1)] = 4
    return out


def measure(mod, key: str, v: dict) -> dict:
    d_path = Path(v["dir"]); stem = v["stem"]
    paths = {
        "cascade":   d_path / f"{stem}_cascade_gi.exr",
        "pt_full":   d_path / f"{stem}_pt_full.exr",
        "pt_direct": d_path / f"{stem}_pt_direct.exr",
        "gbuffer":   d_path / f"{stem}_gbuffer.exr",
        "diag":      d_path / f"{stem}_probe_diag.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing[:3]}

    cascade   = mod.read_exr(paths["cascade"])
    pt_full   = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer   = mod.read_exr(paths["gbuffer"], channels=("R","G","B","A"))
    diag      = mod.read_exr(paths["diag"],    channels=("R","G","B","A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = mod.downsample_2x2_center(diag)

    casc_l = mod.lum(cascade)
    pt_l = mod.lum(pt_gi)
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (gbuffer[..., 3] > 0.0)
    if not np.any(valid):
        return {"status": "empty_valid"}

    r = ratio[valid]
    # Absolute luma means (SC1).
    cascade_luma_mean_valid = float(np.mean(casc_l[valid]))
    pt_luma_mean_valid      = float(np.mean(pt_l[valid]))

    # Per-cell aggregate (Stage 11b methodology).
    valid_cell = valid & (diag[..., 3] > 0.0)
    per_cell_weighted_ratio = None
    if np.any(valid_cell):
        p000 = np.floor(np.clip(diag[..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)
        cells_casc = defaultdict(list)
        cells_pt = defaultdict(list)
        ys, xs = np.nonzero(valid_cell)
        for y, x in zip(ys.tolist(), xs.tolist()):
            key_cell = (int(p000[y, x, 0]), int(p000[y, x, 1]), int(p000[y, x, 2]))
            cells_casc[key_cell].append(float(casc_l[y, x]))
            cells_pt[key_cell].append(float(pt_l[y, x]))
        per_cell = []
        for k in cells_casc.keys():
            cm = float(np.mean(cells_casc[k])); pm = float(np.mean(cells_pt[k]))
            if pm > EPS:
                per_cell.append({"count": len(cells_casc[k]), "ratio": cm / pm})
        if per_cell:
            ratios = np.array([r["ratio"] for r in per_cell])
            counts = np.array([r["count"] for r in per_cell])
            per_cell_weighted_ratio = float(np.sum(ratios * counts) / np.sum(counts))

    # Per-region split.
    albedo_class = classify_albedo(gbuffer[..., :3])
    region_stats = {}
    for cls, label in [(1, "red"), (2, "green"), (3, "white"), (4, "other")]:
        m = valid & (albedo_class == cls)
        if np.sum(m) > 50:
            rr = ratio[m]
            region_stats[label] = {
                "count": int(np.sum(m)),
                "ratio_median": float(np.median(rr)),
                "ratio_mean":   float(np.mean(rr)),
            }

    return {
        "status": "ok",
        "valid": int(np.sum(valid)),
        "ratio_mean":   float(np.mean(r)),
        "ratio_median": float(np.median(r)),
        "per_cell_weighted_ratio": per_cell_weighted_ratio,
        "cascade_gi_luma_mean_valid": cascade_luma_mean_valid,
        "pt_gi_luma_mean_valid":      pt_luma_mean_valid,
        "per_region": region_stats,
        "light_setup": grep_light_setup(v.get("log")),
    }


def bridged_fraction(baseline_ratio: float, new_ratio: float) -> float:
    """1 - (1 - new) / (1 - baseline) = fraction of gap-to-1.0 closed."""
    if abs(1.0 - baseline_ratio) < EPS:
        return 0.0
    return 1.0 - (1.0 - new_ratio) / (1.0 - baseline_ratio)


def verdict_for(b: dict, n: dict) -> dict:
    if b.get("status") != "ok" or n.get("status") != "ok":
        return {"verdict": "MISSING_DATA"}
    new_mean = n["ratio_mean"]
    base_mean = b["ratio_mean"]
    bf = bridged_fraction(base_mean, new_mean)

    if new_mean >= 0.90:
        verdict = "LIGHT_TYPE_DOMINANT"
    elif new_mean >= 0.75:
        verdict = "LIGHT_TYPE_MAJOR"
    elif new_mean >= 0.55:
        verdict = "LIGHT_TYPE_PARTIAL"
    else:
        verdict = "LIGHT_TYPE_RULED_OUT"

    # Cascade vs PT absolute-change asymmetry (SC5 interpretation aid).
    casc_change = n["cascade_gi_luma_mean_valid"] / max(b["cascade_gi_luma_mean_valid"], EPS)
    pt_change   = n["pt_gi_luma_mean_valid"]      / max(b["pt_gi_luma_mean_valid"],      EPS)

    # Cross-check: green-wall asymmetry (Stage 11b SC11).
    b_green = b["per_region"].get("green", {}).get("ratio_median")
    n_green = n["per_region"].get("green", {}).get("ratio_median")
    b_red   = b["per_region"].get("red",   {}).get("ratio_median")
    n_red   = n["per_region"].get("red",   {}).get("ratio_median")
    asymmetry_baseline = (b_red - b_green) if (b_red and b_green) else None
    asymmetry_new      = (n_red - n_green) if (n_red and n_green) else None

    return {
        "verdict": verdict,
        "baseline_ratio_mean": base_mean,
        "new_ratio_mean": new_mean,
        "bridged_fraction": bf,
        "cascade_luma_change_factor": casc_change,
        "pt_luma_change_factor": pt_change,
        "cascade_vs_pt_change_asymmetry": casc_change / max(pt_change, EPS),
        "green_wall_asymmetry_baseline_red_minus_green": asymmetry_baseline,
        "green_wall_asymmetry_new_red_minus_green":      asymmetry_new,
    }


def main() -> int:
    mod = load_local_module()
    measurements = {k: measure(mod, k, v) for k, v in VARIANTS.items()}
    v = verdict_for(measurements["cornell_point_baseline"], measurements["cornell_directional"])
    output = {"variants": measurements, "verdict": v}
    out = ROOT / "light_type_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    summary = {
        "verdict": v.get("verdict"),
        "bridged_fraction": round(v.get("bridged_fraction", -1), 4),
        "baseline_ratio_mean": round(v.get("baseline_ratio_mean", -1), 4),
        "new_ratio_mean": round(v.get("new_ratio_mean", -1), 4),
        "cascade_luma_change_factor": round(v.get("cascade_luma_change_factor", -1), 4),
        "pt_luma_change_factor": round(v.get("pt_luma_change_factor", -1), 4),
        "cascade_vs_pt_change_asymmetry": round(v.get("cascade_vs_pt_change_asymmetry", -1), 4),
        "green_wall_asymmetry_before": v.get("green_wall_asymmetry_baseline_red_minus_green"),
        "green_wall_asymmetry_after":  v.get("green_wall_asymmetry_new_red_minus_green"),
        "per_cell_weighted_baseline": round(measurements["cornell_point_baseline"].get("per_cell_weighted_ratio", -1) or -1, 4),
        "per_cell_weighted_new":      round(measurements["cornell_directional"].get("per_cell_weighted_ratio", -1) or -1, 4),
        "light_setup_directional": measurements["cornell_directional"].get("light_setup"),
        "out": str(out),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
