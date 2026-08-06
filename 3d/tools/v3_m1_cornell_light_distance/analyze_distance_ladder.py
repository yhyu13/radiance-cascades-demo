"""M1 Stage 11d — Light-distance ladder analyzer.

Cornell point light at varying heights {0.8, 3.2, 5.0, 25.0} + directional
(Stage 11c). Reports per-pixel + per-cell ratio, absolute cascade/pt luma
trends with distance, and the monotonicity verdict per plan §3.
"""

import importlib.util
import json
from collections import defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_cornell_light_distance")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
EPS = 1e-6
ATLAS_RES = 32


VARIANTS = [
    # (tag, light_y or 'directional', dir, stem)
    ("near_baseline_orig", 0.8,
        "tools/v3_m1_source_energy_ab/captures_cornell_baseline",
        "m1stage8_cornell_baseline_N2048_m17"),
    ("near_baseline_reverify", 0.8,
        "tools/v3_m1_cornell_light_distance/captures_near_baseline_reverify",
        "m1stage11d_near_baseline_reverify_N2048_m17"),
    ("mid_0p8_x4", 3.2,
        "tools/v3_m1_cornell_light_distance/captures_mid_0p8_x4",
        "m1stage11d_mid_0p8_x4_N2048_m17"),
    ("far_5", 5.0,
        "tools/v3_m1_cornell_light_distance/captures_far_5",
        "m1stage11d_far_5_N2048_m17"),
    ("far_25", 25.0,
        "tools/v3_m1_cornell_light_distance/captures_far_25",
        "m1stage11d_far_25_N2048_m17"),
    ("directional", 100.0,
        "tools/v3_m1_cornell_light_type/captures_cornell_directional",
        "m1stage11c_cornell_directional_N2048_m17"),
]


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec); assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


MOD = load_local_module()


def measure(tag: str, dir_str: str, stem: str) -> dict:
    d = Path(dir_str)
    paths = {
        "cascade":   d / f"{stem}_cascade_gi.exr",
        "pt_full":   d / f"{stem}_pt_full.exr",
        "pt_direct": d / f"{stem}_pt_direct.exr",
        "gbuffer":   d / f"{stem}_gbuffer.exr",
        "diag":      d / f"{stem}_probe_diag.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing[:3]}

    cascade   = MOD.read_exr(paths["cascade"])
    pt_full   = MOD.read_exr(paths["pt_full"])
    pt_direct = MOD.read_exr(paths["pt_direct"])
    gbuffer   = MOD.read_exr(paths["gbuffer"], channels=("R","G","B","A"))
    diag      = MOD.read_exr(paths["diag"],    channels=("R","G","B","A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = MOD.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = MOD.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = MOD.downsample_2x2_center(diag)

    casc_l = MOD.lum(cascade)
    pt_l = MOD.lum(pt_gi)
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (gbuffer[..., 3] > 0.0)
    if not np.any(valid):
        return {"status": "empty_valid"}

    r = ratio[valid]
    result = {
        "status": "ok",
        "tag": tag,
        "valid": int(np.sum(valid)),
        "ratio_mean":   float(np.mean(r)),
        "ratio_median": float(np.median(r)),
        "cascade_gi_luma_mean_valid": float(np.mean(casc_l[valid])),
        "pt_gi_luma_mean_valid":      float(np.mean(pt_l[valid])),
    }

    valid_cell = valid & (diag[..., 3] > 0.0)
    if np.any(valid_cell):
        p000 = np.floor(np.clip(diag[..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)
        cells_casc = defaultdict(list); cells_pt = defaultdict(list)
        ys, xs = np.nonzero(valid_cell)
        for y, x in zip(ys.tolist(), xs.tolist()):
            k = (int(p000[y, x, 0]), int(p000[y, x, 1]), int(p000[y, x, 2]))
            cells_casc[k].append(float(casc_l[y, x]))
            cells_pt[k].append(float(pt_l[y, x]))
        per_cell = []
        for k in cells_casc.keys():
            cm = float(np.mean(cells_casc[k])); pm = float(np.mean(cells_pt[k]))
            if pm > EPS:
                per_cell.append({"count": len(cells_casc[k]), "ratio": cm / pm})
        if per_cell:
            ratios = np.array([r["ratio"] for r in per_cell])
            counts = np.array([r["count"] for r in per_cell])
            result["per_cell_weighted_ratio"] = float(np.sum(ratios * counts) / np.sum(counts))

    return result


def verdict(rows: list[dict]) -> dict:
    ok_rows = [r for r in rows if r.get("status") == "ok" and r.get("tag") != "near_baseline_reverify"]
    if len(ok_rows) < 3:
        return {"verdict": "MISSING_DATA"}

    # Sort by distance.
    distance_map = {r["tag"]: r for r in ok_rows}
    ordered_tags = ["near_baseline_orig", "mid_0p8_x4", "far_5", "far_25", "directional"]
    ordered = [distance_map[t] for t in ordered_tags if t in distance_map]
    ratios = [r["ratio_mean"] for r in ordered]
    casc_lumas = [r["cascade_gi_luma_mean_valid"] for r in ordered]
    pt_lumas   = [r["pt_gi_luma_mean_valid"]      for r in ordered]

    # Monotonicity test: ratio[i+1] >= ratio[i] for all i (allow small tolerance).
    diffs = [ratios[i+1] - ratios[i] for i in range(len(ratios) - 1)]
    is_monotonic = all(d > -0.02 for d in diffs)
    total_rise   = ratios[-1] - ratios[0]
    largest_step = max(diffs) if diffs else 0.0
    largest_drop = min(diffs) if diffs else 0.0

    # Reverify sanity: does the new near_baseline_reverify match the orig within 1%?
    reverify = next((r for r in rows if r.get("tag") == "near_baseline_reverify" and r.get("status") == "ok"), None)
    reverify_check = None
    if reverify:
        orig = distance_map.get("near_baseline_orig")
        if orig:
            rel_diff = abs(reverify["ratio_mean"] - orig["ratio_mean"]) / max(orig["ratio_mean"], EPS)
            reverify_check = {
                "orig_ratio_mean":     orig["ratio_mean"],
                "reverify_ratio_mean": reverify["ratio_mean"],
                "rel_diff":            rel_diff,
                "within_1pct":         rel_diff < 0.01,
            }

    if is_monotonic and total_rise > 0.30:
        verdict_label = "H-A'_SUPPORTED_MONOTONIC"
    elif total_rise > 0.30 and largest_step > 0.25 and not is_monotonic:
        verdict_label = "H-A'_AMBIGUOUS_DISCRETE_JUMP"
    elif total_rise < 0.10:
        verdict_label = "H-A'_FALSIFIED_FLAT"
    elif not is_monotonic and largest_drop < -0.10:
        verdict_label = "H-A'_FALSIFIED_NON_MONOTONIC"
    else:
        verdict_label = "INCONCLUSIVE"

    return {
        "verdict": verdict_label,
        "ratios_by_distance": [{"tag": r["tag"], "ratio_mean": r["ratio_mean"]} for r in ordered],
        "absolute_cascade_luma": casc_lumas,
        "absolute_pt_luma":      pt_lumas,
        "ratio_diffs":           diffs,
        "is_monotonic":          is_monotonic,
        "total_rise":            total_rise,
        "largest_step":          largest_step,
        "largest_drop":          largest_drop,
        "reverify_check":        reverify_check,
    }


def main() -> int:
    rows = [measure(tag, d, s) for (tag, _y, d, s) in VARIANTS]
    output = {"variants": rows, "verdict": verdict(rows)}
    out = ROOT / "distance_ladder_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    summary = {
        "verdict": output["verdict"].get("verdict"),
        "ratios_by_distance": output["verdict"].get("ratios_by_distance"),
        "is_monotonic": output["verdict"].get("is_monotonic"),
        "total_rise": output["verdict"].get("total_rise"),
        "reverify_match_within_1pct": (output["verdict"].get("reverify_check", {}) or {}).get("within_1pct"),
        "absolute_cascade_luma": output["verdict"].get("absolute_cascade_luma"),
        "absolute_pt_luma":      output["verdict"].get("absolute_pt_luma"),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
