import json
import importlib.util
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_atlas_attribution")
STAGE5 = Path("tools/v3_m1_shader_probe_diag/shader_probe_diag_results.json")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
SCENE = "sponza"
N = 2048
NORMALS = {
    "+x": np.array([1.0, 0.0, 0.0], dtype=np.float32),
    "-x": np.array([-1.0, 0.0, 0.0], dtype=np.float32),
    "+y": np.array([0.0, 1.0, 0.0], dtype=np.float32),
    "-y": np.array([0.0, -1.0, 0.0], dtype=np.float32),
    "+z": np.array([0.0, 0.0, 1.0], dtype=np.float32),
    "-z": np.array([0.0, 0.0, -1.0], dtype=np.float32),
}
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def luma(rgb):
    return float(np.dot(np.asarray(rgb, dtype=np.float32), LUMA))


def trilinear_weights(frac):
    fx, fy, fz = frac
    return {
        (0, 0, 0): (1 - fx) * (1 - fy) * (1 - fz),
        (1, 0, 0): fx * (1 - fy) * (1 - fz),
        (0, 1, 0): (1 - fx) * fy * (1 - fz),
        (1, 1, 0): fx * fy * (1 - fz),
        (0, 0, 1): (1 - fx) * (1 - fy) * fz,
        (1, 0, 1): fx * (1 - fy) * fz,
        (0, 1, 1): (1 - fx) * fy * fz,
        (1, 1, 1): fx * fy * fz,
    }


def stage5_cell_map():
    data = json.loads(STAGE5.read_text(encoding="utf-8"))
    rows = data["scenes"][SCENE]["dominant_shader_cells_bad"]
    return {row["key"]: row for row in rows}


def measured_normals_by_cell():
    mod = load_local_module()
    stem = f"m1atlas_{SCENE}_baseline_N{N:04d}_m17"
    directory = ROOT / f"captures_{SCENE}"
    cascade = mod.read_exr(directory / f"{stem}_cascade_gi.exr")
    pt_full = mod.read_exr(directory / f"{stem}_pt_full.exr")
    pt_direct = mod.read_exr(directory / f"{stem}_pt_direct.exr")
    gbuffer = mod.read_exr(directory / f"{stem}_gbuffer.exr", channels=("R", "G", "B", "A"))
    diag = mod.read_exr(directory / f"{stem}_probe_diag.exr", channels=("R", "G", "B", "A"))
    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = mod.downsample_2x2_center(diag)
    ratio = mod.lum(cascade) / np.maximum(mod.lum(pt_gi), 1e-6)
    mask = (mod.lum(pt_gi) > 0.05) & (mod.lum(cascade) > 0.001) & (gbuffer[..., 3] > 0.0) & (ratio > 1.3)
    shader_pg = diag[..., :3] * 32.0
    shader_cell = np.floor(np.clip(shader_pg, 0.0, 31.0)).astype(np.int32)
    normals = gbuffer[..., :3] * 2.0 - 1.0
    normals = normals / np.maximum(np.linalg.norm(normals, axis=-1, keepdims=True), 1e-6)
    grouped = {}
    for cell in [(7, 5, 4), (6, 5, 4), (6, 4, 4)]:
        cmask = mask & np.all(shader_cell == np.array(cell, dtype=np.int32), axis=-1)
        if np.any(cmask):
            n = np.mean(normals[cmask], axis=0)
            n = n / max(float(np.linalg.norm(n)), 1e-6)
            grouped[str(cell)] = {
                "normal": [float(v) for v in n],
                "count": int(np.sum(cmask)),
                "mean_ratio": float(np.mean(ratio[cmask])),
            }
    return grouped


def analyze_target_with_normal(target, stage5_rows, normal_label, normal):
    key = str(tuple(target["p000"])).replace(" ", "")
    # Stage 5 JSON string keys include spaces from Python tuple formatting.
    row = None
    for k, v in stage5_rows.items():
        if k.replace(" ", "") == key:
            row = v
            break
    frac = row["mean_pg_fraction"] if row else [0.5, 0.5, 0.5]
    tw = trilinear_weights(frac)

    probe_rows = []
    bin_rows = []
    final_rgb = np.zeros(3, dtype=np.float64)
    total_unorm_luma = 0.0
    low_wsum_hot = False

    for nb in target["neighbors"]:
        off = tuple(nb["offset"])
        tri = float(tw.get(off, 0.0))
        weighted = []
        wsum = 0.0
        unorm_luma = 0.0
        for b in nb["bins"]:
            rgb = np.asarray(b["rgb"], dtype=np.float64)
            alpha = float(b["alpha"])
            direction = np.asarray(b["dir"], dtype=np.float64)
            wcos = max(0.0, float(np.dot(direction, normal)))
            w = wcos * alpha
            wsum += w
            unorm_luma += luma(rgb) * w
            weighted.append((b, rgb, wcos, w))

        probe_rgb = np.zeros(3, dtype=np.float64)
        if wsum > 1e-4:
            for b, rgb, wcos, w in weighted:
                contrib_rgb = rgb * (w / wsum)
                probe_rgb += contrib_rgb
                final_contrib_rgb = contrib_rgb * tri
                bin_rows.append({
                    "probe": nb["probe"],
                    "offset": nb["offset"],
                    "bin": b["bin"],
                    "tri_weight": tri,
                    "wcos": wcos,
                    "alpha": float(b["alpha"]),
                    "rgb": b["rgb"],
                    "luma": b["luma"],
                    "normalized_probe_luma_contrib": luma(contrib_rgb),
                    "final_luma_contrib": luma(final_contrib_rgb),
                })
        final_rgb += probe_rgb * tri
        total_unorm_luma += unorm_luma * tri
        probe_l = luma(probe_rgb)
        if wsum < 0.15 and probe_l > 0.05:
            low_wsum_hot = True
        probe_rows.append({
            "probe": nb["probe"],
            "offset": nb["offset"],
            "tri_weight": tri,
            "wsum": float(wsum),
            "unormalized_luma": float(unorm_luma),
            "normalized_probe_luma": probe_l,
            "final_luma_contrib": probe_l * tri,
        })

    probe_rows.sort(key=lambda r: r["final_luma_contrib"], reverse=True)
    bin_rows.sort(key=lambda r: r["final_luma_contrib"], reverse=True)
    final_luma = luma(final_rgb)
    top_probe_share = probe_rows[0]["final_luma_contrib"] / max(final_luma, 1e-6) if probe_rows else 0.0
    top_bin_share = bin_rows[0]["final_luma_contrib"] / max(final_luma, 1e-6) if bin_rows else 0.0
    if low_wsum_hot:
        shape = "ALPHA_RENORMALIZATION_RISK"
    elif top_bin_share > 0.45:
        shape = "HOT_BIN"
    elif top_probe_share > 0.55:
        shape = "HOT_PROBE"
    else:
        shape = "BROAD_LOCAL_ENERGY"

    return {
        "p000": target["p000"],
        "stage5": row,
        "normal": normal_label,
        "mean_pg_fraction_used": frac,
        "final_luma_reconstructed": final_luma,
        "trilinear_unormalized_luma": float(total_unorm_luma),
        "top_probe_share": float(top_probe_share),
        "top_bin_share": float(top_bin_share),
        "shape": shape,
        "top_probes": probe_rows[:8],
        "top_bins": bin_rows[:12],
    }


def analyze_target(target, stage5_rows, measured_normals):
    mkey = str(tuple(target["p000"]))
    measured = None
    if mkey in measured_normals:
        normal = np.asarray(measured_normals[mkey]["normal"], dtype=np.float32)
        measured = analyze_target_with_normal(target, stage5_rows, "measured", normal)
        measured["measured_normal"] = measured_normals[mkey]
    candidates = [
        analyze_target_with_normal(target, stage5_rows, label, normal)
        for label, normal in NORMALS.items()
    ]
    all_candidates = ([measured] if measured is not None else []) + candidates
    stage5_luma = candidates[0].get("stage5", {}).get("mean_shader_luma") if candidates[0].get("stage5") else None
    if stage5_luma is not None:
        best = min(all_candidates, key=lambda r: abs(r["final_luma_reconstructed"] - stage5_luma))
        best["luma_reconstruction_ratio"] = best["final_luma_reconstructed"] / max(stage5_luma, 1e-6)
    else:
        best = max(all_candidates, key=lambda r: r["final_luma_reconstructed"])
        best["luma_reconstruction_ratio"] = None
    best["normal_candidates"] = [
        {
            "normal": c["normal"],
            "final_luma_reconstructed": c["final_luma_reconstructed"],
            "shape": c["shape"],
            "top_probe_share": c["top_probe_share"],
            "top_bin_share": c["top_bin_share"],
        }
        for c in candidates
    ]
    return best


def main() -> int:
    stage5_rows = stage5_cell_map()
    normals = measured_normals_by_cell()
    atlas_path = ROOT / f"captures_{SCENE}" / f"m1atlas_{SCENE}_baseline_N{N:04d}_m17_atlas_attribution.json"
    atlas = json.loads(atlas_path.read_text(encoding="utf-8"))
    targets = [analyze_target(t, stage5_rows, normals) for t in atlas["targets"]]
    shape_counts = {}
    for t in targets:
        shape_counts[t["shape"]] = shape_counts.get(t["shape"], 0) + 1
    poor_reconstruction = [
        t for t in targets
        if t.get("luma_reconstruction_ratio") is not None and t["luma_reconstruction_ratio"] < 0.75
    ]
    if poor_reconstruction:
        verdict = "ATTRIBUTION_RECONSTRUCTION_MISMATCH"
    else:
        verdict = max(shape_counts.items(), key=lambda kv: kv[1])[0] if shape_counts else "NO_TARGETS"
    results = {
        "scene": SCENE,
        "N": N,
        "source": str(atlas_path),
        "stage5": str(STAGE5),
        "measured_normals": normals,
        "verdict": verdict,
        "targets": targets,
    }
    out = ROOT / "atlas_attribution_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    summary = {
        "out": str(out),
        "verdict": verdict,
        "targets": [
            {
                "p000": t["p000"],
                "shape": t["shape"],
                "final_luma_reconstructed": t["final_luma_reconstructed"],
                "top_probe_share": t["top_probe_share"],
                "top_bin_share": t["top_bin_share"],
                "top_probe": t["top_probes"][0] if t["top_probes"] else None,
                "top_bin": t["top_bins"][0] if t["top_bins"] else None,
            }
            for t in targets
        ],
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
