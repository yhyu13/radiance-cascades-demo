import importlib.util
import json
import math
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_shader_probe_diag")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
SCENES = ["cornell", "sponza"]
N = 2048
ATLAS_RES = 32.0
EPS = 1e-6


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def group_stats(values):
    rows = []
    for key, vals in values.items():
        arr = np.asarray(vals, dtype=np.float32)
        rows.append({
            "key": str(key),
            "count": int(arr.shape[0]),
            "mean_ratio": float(np.mean(arr[:, 0])),
            "mean_shader_luma": float(np.mean(arr[:, 1])),
            "mean_pg_fraction": [float(v) for v in np.mean(arr[:, 2:5], axis=0)],
        })
    return sorted(rows, key=lambda r: r["count"], reverse=True)[:12]


def analyze_scene(mod, scene: str) -> dict:
    directory = ROOT / f"captures_{scene}"
    stem = f"m1shaderdiag_{scene}_baseline_N{N:04d}_m17"
    paths = {
        "cascade": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
        "gbuffer": directory / f"{stem}_gbuffer.exr",
        "diag": directory / f"{stem}_probe_diag.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    cascade = mod.read_exr(paths["cascade"])
    pt_full = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer = mod.read_exr(paths["gbuffer"], channels=("R", "G", "B", "A"))
    diag = mod.read_exr(paths["diag"], channels=("R", "G", "B", "A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = mod.downsample_2x2_center(diag)

    casc_l = mod.lum(cascade)
    pt_l = mod.lum(pt_gi)
    depth = gbuffer[..., 3]
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (depth > 0.0) & (diag[..., 3] > 0.0)
    if not np.any(valid):
        return {"status": "empty_mask", "valid": 0}

    ratio = casc_l / np.maximum(pt_l, EPS)
    bad = valid & (ratio > 1.3)
    cam_pos, cam_target = mod.camera_for_scene(scene)
    h, w = pt_l.shape
    dirs = mod.ray_dirs(w, h, cam_pos, cam_target)
    t_near, t_far = mod.intersect_box(cam_pos, dirs)
    t = t_near + depth * np.maximum(t_far - t_near, 0.001)
    pos = cam_pos[None, None, :] + dirs * t[..., None]
    uvw = np.clip((pos - mod.VOLUME_MIN[None, None, :]) / mod.VOLUME_SIZE[None, None, :], 0.0, 0.999999)
    offline_cell = np.floor(uvw * ATLAS_RES).astype(np.int32)
    offline_centered_cell = np.floor(np.clip(uvw * ATLAS_RES - 0.5, 0.0, ATLAS_RES - 1.0)).astype(np.int32)

    shader_pg = diag[..., :3] * ATLAS_RES
    shader_cell = np.floor(np.clip(shader_pg, 0.0, ATLAS_RES - 1.0)).astype(np.int32)
    shader_frac = shader_pg - np.floor(shader_pg)
    mismatch_legacy = np.any(shader_cell != offline_cell, axis=-1)
    mismatch_centered = np.any(shader_cell != offline_centered_cell, axis=-1)

    cell_groups = defaultdict(list)
    offline_groups = defaultdict(list)
    offline_centered_groups = defaultdict(list)
    pair_counts = Counter()
    centered_pair_counts = Counter()
    ys, xs = np.nonzero(bad)
    for y, x in zip(ys.tolist(), xs.tolist()):
        sc = tuple(int(v) for v in shader_cell[y, x])
        oc = tuple(int(v) for v in offline_cell[y, x])
        occ = tuple(int(v) for v in offline_centered_cell[y, x])
        row = [
            float(ratio[y, x]),
            float(diag[y, x, 3]),
            float(shader_frac[y, x, 0]),
            float(shader_frac[y, x, 1]),
            float(shader_frac[y, x, 2]),
        ]
        cell_groups[sc].append(row)
        offline_groups[oc].append(row)
        offline_centered_groups[occ].append(row)
        pair_counts[(oc, sc)] += 1
        centered_pair_counts[(occ, sc)] += 1

    pair_rows = [
        {"offline": str(k[0]), "shader": str(k[1]), "count": int(v)}
        for k, v in pair_counts.most_common(12)
    ]
    centered_pair_rows = [
        {"offline_centered": str(k[0]), "shader": str(k[1]), "count": int(v)}
        for k, v in centered_pair_counts.most_common(12)
    ]
    bad_count = int(np.sum(bad))
    return {
        "status": "ok",
        "valid": int(np.sum(valid)),
        "bad_ratio_gt_1p3": bad_count,
        "screen": {
            "ratio_self": float(np.mean(ratio[valid])),
            "bad_pct": float(100.0 * np.mean(ratio[valid] > 1.3)),
            "shader_luma_mean": float(np.mean(diag[..., 3][valid])),
        },
        "coordinate_agreement": {
            "legacy_valid_mismatch_pct": float(100.0 * np.mean(mismatch_legacy[valid])),
            "legacy_bad_mismatch_pct": float(100.0 * np.mean(mismatch_legacy[bad])) if bad_count else 0.0,
            "centered_valid_mismatch_pct": float(100.0 * np.mean(mismatch_centered[valid])),
            "centered_bad_mismatch_pct": float(100.0 * np.mean(mismatch_centered[bad])) if bad_count else 0.0,
        },
        "dominant_shader_cells_bad": group_stats(cell_groups),
        "dominant_offline_cells_bad": group_stats(offline_groups),
        "dominant_offline_centered_cells_bad": group_stats(offline_centered_groups),
        "dominant_offline_to_shader_pairs_bad": pair_rows,
        "dominant_offline_centered_to_shader_pairs_bad": centered_pair_rows,
    }


def main() -> int:
    mod = load_local_module()
    results = {"N": N, "atlas_res": int(ATLAS_RES), "scenes": {}}
    for scene in SCENES:
        results["scenes"][scene] = analyze_scene(mod, scene)

    sponza = results["scenes"].get("sponza", {})
    if sponza.get("status") == "ok":
        mismatch = sponza["coordinate_agreement"]["centered_bad_mismatch_pct"]
        legacy_mismatch = sponza["coordinate_agreement"]["legacy_bad_mismatch_pct"]
        dom_shader = sponza["dominant_shader_cells_bad"][0] if sponza["dominant_shader_cells_bad"] else None
        dom_offline = sponza["dominant_offline_cells_bad"][0] if sponza["dominant_offline_cells_bad"] else None
        dom_offline_centered = sponza["dominant_offline_centered_cells_bad"][0] if sponza["dominant_offline_centered_cells_bad"] else None
        if mismatch > 25.0:
            verdict = "OFFLINE_RECONSTRUCTION_CONTRACT_MISMATCH"
        elif dom_shader and dom_shader["count"] >= 200:
            verdict = "SHADER_COORDINATE_CLUSTER_CONFIRMED"
        else:
            verdict = "SHADER_COORDINATE_SPREAD"
        results["verdict"] = verdict
        results["sponza_summary"] = {
            "legacy_bad_mismatch_pct": legacy_mismatch,
            "centered_bad_mismatch_pct": mismatch,
            "dominant_shader_cell": dom_shader,
            "dominant_offline_cell": dom_offline,
            "dominant_offline_centered_cell": dom_offline_centered,
        }

    out = ROOT / "shader_probe_diag_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    summary = {
        "out": str(out),
        "verdict": results.get("verdict", "NO_VERDICT"),
        "sponza_summary": results.get("sponza_summary"),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
