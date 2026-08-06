import json
import math
from collections import defaultdict
from pathlib import Path

import Imath
import numpy as np
import OpenEXR


SCENES = ["cornell", "sponza"]
N = 2048
ROOT = Path("tools/v3_m1_local_sampling")
FOVY_DEG = 60.0
VOLUME_MIN = np.array([-2.0, -2.0, -2.0], dtype=np.float32)
VOLUME_MAX = np.array([2.0, 2.0, 2.0], dtype=np.float32)
VOLUME_SIZE = VOLUME_MAX - VOLUME_MIN
EPS = 1e-6


def read_exr(path: Path, channels=("R", "G", "B")) -> np.ndarray:
    f = OpenEXR.InputFile(str(path))
    dw = f.header()["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    h = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    arrays = [
        np.frombuffer(f.channel(ch, pt), dtype=np.float32).reshape(h, w)
        for ch in channels
    ]
    return np.stack(arrays, axis=-1)


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def downsample_2x2_mean(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    return img[: h2 * 2, : w2 * 2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


def downsample_2x2_center(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    return img[1:h:2, 1:w:2]


def first_camera(path: Path) -> tuple[np.ndarray, np.ndarray]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    cam = data["cameras"][0]
    return (
        np.array(cam["position"], dtype=np.float32),
        np.array(cam["target"], dtype=np.float32),
    )


def camera_for_scene(scene: str) -> tuple[np.ndarray, np.ndarray]:
    if scene == "sponza":
        return first_camera(Path("tools/v20_pre_measurement/sponza_cam.json"))
    return first_camera(Path("tools/v20_pre_measurement/cameras.json"))


def normalize(v: np.ndarray) -> np.ndarray:
    return v / max(float(np.linalg.norm(v)), EPS)


def ray_dirs(width: int, height: int, cam_pos: np.ndarray, cam_target: np.ndarray) -> np.ndarray:
    forward = normalize(cam_target - cam_pos)
    world_up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    right = normalize(np.cross(forward, world_up))
    up = normalize(np.cross(right, forward))
    aspect = float(width) / float(height)
    tan_y = math.tan(math.radians(FOVY_DEG) * 0.5)

    xs = (np.arange(width, dtype=np.float32) + 0.5) / float(width)
    ys = (np.arange(height, dtype=np.float32) + 0.5) / float(height)
    ndc_x = xs * 2.0 - 1.0
    # EXR rows are top-left after the C++ flip; shader vUV.y is bottom-left.
    ndc_y = (1.0 - ys) * 2.0 - 1.0
    xx, yy = np.meshgrid(ndc_x, ndc_y)
    dirs = (
        forward[None, None, :]
        + right[None, None, :] * (xx[..., None] * tan_y * aspect)
        + up[None, None, :] * (yy[..., None] * tan_y)
    )
    return dirs / np.maximum(np.linalg.norm(dirs, axis=-1, keepdims=True), EPS)


def intersect_box(cam_pos: np.ndarray, dirs: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    inv = 1.0 / np.where(np.abs(dirs) < EPS, np.sign(dirs) * EPS + EPS, dirs)
    t0 = (VOLUME_MIN[None, None, :] - cam_pos[None, None, :]) * inv
    t1 = (VOLUME_MAX[None, None, :] - cam_pos[None, None, :]) * inv
    tsm = np.minimum(t0, t1)
    tsx = np.maximum(t0, t1)
    t_near = np.max(tsm, axis=-1)
    t_far = np.min(tsx, axis=-1)
    return t_near, t_far


def axis_label(n: np.ndarray) -> str:
    axes = ("x", "y", "z")
    i = int(np.argmax(np.abs(n)))
    return ("+" if n[i] >= 0.0 else "-") + axes[i]


def add_bin(groups: dict, key, rel: float, ratio: float) -> None:
    g = groups[key]
    g["count"] += 1
    g["rel_sum"] += rel
    g["ratio_sum"] += ratio
    g["abs_rel_sum"] += abs(rel)
    g["bright"] += 1 if ratio > 1.3 else 0
    g["dim"] += 1 if ratio < 0.7 else 0


def summarize_groups(groups: dict, min_count: int = 5, limit: int = 10) -> dict:
    rows = []
    for key, g in groups.items():
        if g["count"] < min_count:
            continue
        c = float(g["count"])
        rows.append({
            "key": str(key),
            "count": g["count"],
            "mean_rel": g["rel_sum"] / c,
            "mean_ratio": g["ratio_sum"] / c,
            "mean_abs_rel": g["abs_rel_sum"] / c,
            "bright_pct": 100.0 * g["bright"] / c,
            "dim_pct": 100.0 * g["dim"] / c,
        })
    return {
        "worst_bright": sorted(rows, key=lambda r: r["mean_rel"], reverse=True)[:limit],
        "worst_dim": sorted(rows, key=lambda r: r["mean_rel"])[:limit],
        "worst_abs": sorted(rows, key=lambda r: r["mean_abs_rel"], reverse=True)[:limit],
    }


def analyze_scene(scene: str) -> dict:
    directory = ROOT / f"captures_{scene}"
    stem = f"m1local_{scene}_baseline_N{N:04d}_m17"
    paths = {
        "cascade": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
        "gbuffer": directory / f"{stem}_gbuffer.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    cascade = read_exr(paths["cascade"])
    pt_full = read_exr(paths["pt_full"])
    pt_direct = read_exr(paths["pt_direct"])
    gbuffer = read_exr(paths["gbuffer"], channels=("R", "G", "B", "A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = downsample_2x2_center(gbuffer)

    casc_l = lum(cascade)
    pt_l = lum(pt_gi)
    depth = gbuffer[..., 3]
    mask = (pt_l > 0.05) & (casc_l > 0.001) & (depth > 0.0)
    if not np.any(mask):
        return {"status": "empty_mask", "valid": 0}

    ratio = casc_l / np.maximum(pt_l, EPS)
    rel = ratio - 1.0
    cam_pos, cam_target = camera_for_scene(scene)
    h, w = pt_l.shape
    dirs = ray_dirs(w, h, cam_pos, cam_target)
    t_near, t_far = intersect_box(cam_pos, dirs)
    t = t_near + depth * np.maximum(t_far - t_near, 0.001)
    pos = cam_pos[None, None, :] + dirs * t[..., None]
    uvw = np.clip((pos - VOLUME_MIN[None, None, :]) / VOLUME_SIZE[None, None, :], 0.0, 0.999999)
    cell = np.floor(uvw * 32.0).astype(np.int32)
    normal = gbuffer[..., :3] * 2.0 - 1.0
    normal = normal / np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), EPS)

    tile_groups = defaultdict(lambda: {"count": 0, "rel_sum": 0.0, "ratio_sum": 0.0, "abs_rel_sum": 0.0, "bright": 0, "dim": 0})
    cell_groups = defaultdict(lambda: {"count": 0, "rel_sum": 0.0, "ratio_sum": 0.0, "abs_rel_sum": 0.0, "bright": 0, "dim": 0})
    depth_groups = defaultdict(lambda: {"count": 0, "rel_sum": 0.0, "ratio_sum": 0.0, "abs_rel_sum": 0.0, "bright": 0, "dim": 0})
    normal_groups = defaultdict(lambda: {"count": 0, "rel_sum": 0.0, "ratio_sum": 0.0, "abs_rel_sum": 0.0, "bright": 0, "dim": 0})

    ys, xs = np.nonzero(mask)
    for y, x in zip(ys.tolist(), xs.tolist()):
        r = float(ratio[y, x])
        e = float(rel[y, x])
        tile = (int(x * 16 // w), int(y * 9 // h))
        c = tuple(int(v) for v in cell[y, x])
        db = int(min(9, max(0, math.floor(float(depth[y, x]) * 10.0))))
        ax = axis_label(normal[y, x])
        add_bin(tile_groups, tile, e, r)
        add_bin(cell_groups, c, e, r)
        add_bin(depth_groups, db, e, r)
        add_bin(normal_groups, ax, e, r)

    return {
        "status": "ok",
        "valid": int(mask.sum()),
        "screen": {
            "ratio_self": float(np.mean(ratio[mask])),
            "abs_p95": float(np.percentile(np.abs(rel[mask]), 95)),
            "bright_pct": float(100.0 * np.mean(ratio[mask] > 1.3)),
            "dim_pct": float(100.0 * np.mean(ratio[mask] < 0.7)),
        },
        "bins": {
            "screen_tile_16x9": summarize_groups(tile_groups, min_count=5),
            "c0_cell": summarize_groups(cell_groups, min_count=3),
            "depth_decile": summarize_groups(depth_groups, min_count=5),
            "normal_axis": summarize_groups(normal_groups, min_count=5),
        },
    }


def main() -> int:
    results = {"N": N, "scenes": {scene: analyze_scene(scene) for scene in SCENES}}
    sponza = results["scenes"].get("sponza", {})
    if sponza.get("status") == "ok":
        normal_bins = sponza["bins"]["normal_axis"]["worst_abs"]
        cell_bins = sponza["bins"]["c0_cell"]["worst_abs"]
        tile_bins = sponza["bins"]["screen_tile_16x9"]["worst_abs"]
        depth_bins = sponza["bins"]["depth_decile"]["worst_abs"]
        dominant_normal = normal_bins[0] if normal_bins else None
        dominant_cell_by_count = max(cell_bins, key=lambda r: r["count"]) if cell_bins else None
        dominant_tile_by_count = max(tile_bins, key=lambda r: r["count"]) if tile_bins else None
        dominant_depth_by_count = max(depth_bins, key=lambda r: r["count"]) if depth_bins else None
        results["sponza_local_summary"] = {
            "valid": sponza["valid"],
            "dominant_normal": dominant_normal,
            "dominant_c0_cell_by_count": dominant_cell_by_count,
            "dominant_tile_by_count": dominant_tile_by_count,
            "dominant_depth_by_count": dominant_depth_by_count,
            "direction": "localized_final_sampling_or_normal_directional_gi_contract",
        }
    out = ROOT / "local_sampling_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
