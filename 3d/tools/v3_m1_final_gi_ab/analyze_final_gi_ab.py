import importlib.util
import json
from pathlib import Path


ROOT = Path("tools/v3_m1_final_gi_ab")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
SCENES = ["cornell", "sponza"]
CONDITIONS = ["diron", "diroff"]
N = 2048


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def analyze_variant(mod, scene: str, condition: str) -> dict:
    stem = f"m1finalgi_{scene}_{condition}_N{N:04d}_m17"
    # Reuse Stage 3 analyzer logic by temporarily matching its expected paths.
    old_root = mod.ROOT
    try:
        mod.ROOT = ROOT
        def analyze_scene_override(s):
            local_dir = mod.ROOT / f"captures_{s}"
            old_stem = f"m1local_{s}_baseline_N{mod.N:04d}_m17"
            new_stem = stem if s == scene else old_stem
            paths = {
                "cascade": local_dir / f"{new_stem}_cascade_gi.exr",
                "pt_full": local_dir / f"{new_stem}_pt_full.exr",
                "pt_direct": local_dir / f"{new_stem}_pt_direct.exr",
                "gbuffer": local_dir / f"{new_stem}_gbuffer.exr",
            }
            missing = [str(p) for p in paths.values() if not p.exists()]
            if missing:
                return {"status": "missing", "missing": missing}
            # Copy of original with a stem override would be cleaner as a shared helper;
            # for this focused phase, temporarily symlink by assigning expected names is
            # more brittle on Windows, so call the extracted private path by patching text
            # is avoided. The implementation below mirrors Stage 3 in a compact form.
            return analyze_paths(mod, scene, paths)

        return analyze_scene_override(scene)
    finally:
        mod.ROOT = old_root


def analyze_paths(mod, scene: str, paths: dict) -> dict:
    import math
    from collections import defaultdict

    import numpy as np

    cascade = mod.read_exr(paths["cascade"])
    pt_full = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer = mod.read_exr(paths["gbuffer"], channels=("R", "G", "B", "A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)

    casc_l = mod.lum(cascade)
    pt_l = mod.lum(pt_gi)
    depth = gbuffer[..., 3]
    mask = (pt_l > 0.05) & (casc_l > 0.001) & (depth > 0.0)
    if not np.any(mask):
        return {"status": "empty_mask", "valid": 0}

    ratio = casc_l / np.maximum(pt_l, mod.EPS)
    rel = ratio - 1.0
    cam_pos, cam_target = mod.camera_for_scene(scene)
    h, w = pt_l.shape
    dirs = mod.ray_dirs(w, h, cam_pos, cam_target)
    t_near, t_far = mod.intersect_box(cam_pos, dirs)
    t = t_near + depth * np.maximum(t_far - t_near, 0.001)
    pos = cam_pos[None, None, :] + dirs * t[..., None]
    uvw = np.clip((pos - mod.VOLUME_MIN[None, None, :]) / mod.VOLUME_SIZE[None, None, :], 0.0, 0.999999)
    cell = np.floor(uvw * 32.0).astype(np.int32)
    normal = gbuffer[..., :3] * 2.0 - 1.0
    normal = normal / np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), mod.EPS)

    def group():
        return defaultdict(lambda: {"count": 0, "rel_sum": 0.0, "ratio_sum": 0.0, "abs_rel_sum": 0.0, "bright": 0, "dim": 0})

    tile_groups = group()
    cell_groups = group()
    depth_groups = group()
    normal_groups = group()
    ys, xs = np.nonzero(mask)
    for y, x in zip(ys.tolist(), xs.tolist()):
        r = float(ratio[y, x])
        e = float(rel[y, x])
        mod.add_bin(tile_groups, (int(x * 16 // w), int(y * 9 // h)), e, r)
        mod.add_bin(cell_groups, tuple(int(v) for v in cell[y, x]), e, r)
        mod.add_bin(depth_groups, int(min(9, max(0, math.floor(float(depth[y, x]) * 10.0)))), e, r)
        mod.add_bin(normal_groups, mod.axis_label(normal[y, x]), e, r)

    bins = {
        "screen_tile_16x9": mod.summarize_groups(tile_groups, min_count=5),
        "c0_cell": mod.summarize_groups(cell_groups, min_count=3),
        "depth_decile": mod.summarize_groups(depth_groups, min_count=5),
        "normal_axis": mod.summarize_groups(normal_groups, min_count=5),
    }
    summary = {
        "dominant_normal": bins["normal_axis"]["worst_abs"][0] if bins["normal_axis"]["worst_abs"] else None,
        "dominant_c0_cell_by_count": max(bins["c0_cell"]["worst_abs"], key=lambda r: r["count"]) if bins["c0_cell"]["worst_abs"] else None,
        "dominant_depth_by_count": max(bins["depth_decile"]["worst_abs"], key=lambda r: r["count"]) if bins["depth_decile"]["worst_abs"] else None,
    }
    return {
        "status": "ok",
        "valid": int(mask.sum()),
        "screen": {
            "ratio_self": float(np.mean(ratio[mask])),
            "abs_p95": float(np.percentile(np.abs(rel[mask]), 95)),
            "bright_pct": float(100.0 * np.mean(ratio[mask] > 1.3)),
            "dim_pct": float(100.0 * np.mean(ratio[mask] < 0.7)),
        },
        "local_summary": summary,
        "bins": bins,
    }


def compare(a: dict, b: dict) -> dict:
    if a.get("status") != "ok" or b.get("status") != "ok":
        return {"status": "missing"}
    return {
        "status": "ok",
        "ratio_delta_diroff_minus_diron": b["screen"]["ratio_self"] - a["screen"]["ratio_self"],
        "abs_p95_delta_diroff_minus_diron": b["screen"]["abs_p95"] - a["screen"]["abs_p95"],
        "bright_delta_pp": b["screen"]["bright_pct"] - a["screen"]["bright_pct"],
        "dim_delta_pp": b["screen"]["dim_pct"] - a["screen"]["dim_pct"],
    }


def main() -> int:
    mod = load_local_module()
    results = {"N": N, "scenes": {}}
    for scene in SCENES:
        rows = {cond: analyze_variant(mod, scene, cond) for cond in CONDITIONS}
        results["scenes"][scene] = {
            "conditions": rows,
            "diroff_vs_diron": compare(rows["diron"], rows["diroff"]),
        }

    sponza_cmp = results["scenes"].get("sponza", {}).get("diroff_vs_diron", {})
    cornell_cmp = results["scenes"].get("cornell", {}).get("diroff_vs_diron", {})
    if sponza_cmp.get("status") == "ok" and cornell_cmp.get("status") == "ok":
        sponza_improves = sponza_cmp["ratio_delta_diroff_minus_diron"] < -0.25
        cornell_regresses = cornell_cmp["dim_delta_pp"] > 5.0 or cornell_cmp["abs_p95_delta_diroff_minus_diron"] > 0.1
        if sponza_improves and not cornell_regresses:
            verdict = "DIRECTIONAL_GI_SUSPECT"
        elif sponza_improves:
            verdict = "DIRECTIONAL_GI_SUSPECT_BUT_CORNELL_REGRESSES"
        else:
            verdict = "DIRECTIONAL_GI_NOT_PRIMARY"
        results["verdict"] = verdict

    out = ROOT / "final_gi_ab_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    summary = {
        "out": str(out),
        "verdict": results.get("verdict", "NO_VERDICT"),
        "cornell": {
            "diron": results["scenes"]["cornell"]["conditions"]["diron"].get("screen"),
            "diroff": results["scenes"]["cornell"]["conditions"]["diroff"].get("screen"),
            "diroff_vs_diron": results["scenes"]["cornell"].get("diroff_vs_diron"),
        },
        "sponza": {
            "diron": results["scenes"]["sponza"]["conditions"]["diron"].get("screen"),
            "diroff": results["scenes"]["sponza"]["conditions"]["diroff"].get("screen"),
            "diroff_vs_diron": results["scenes"]["sponza"].get("diroff_vs_diron"),
            "diron_local": results["scenes"]["sponza"]["conditions"]["diron"].get("local_summary"),
            "diroff_local": results["scenes"]["sponza"]["conditions"]["diroff"].get("local_summary"),
        },
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
