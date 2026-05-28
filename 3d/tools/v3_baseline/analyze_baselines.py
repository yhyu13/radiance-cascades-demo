import argparse
import json
import math
import os
from pathlib import Path

import Imath
import numpy as np
import OpenEXR


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


def analyze_stem(directory: Path, stem: str, pt_threshold: float = 0.05) -> dict:
    paths = {
        "cascade_gi": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"stem": stem, "status": "missing", "missing": missing}

    casc = read_exr(paths["cascade_gi"])
    pt_full = read_exr(paths["pt_full"])
    pt_direct = read_exr(paths["pt_direct"])
    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)

    if casc.shape[:2] != pt_indirect.shape[:2]:
        casc = downsample_2x2(casc)

    casc_lum = lum(casc)
    pt_lum = lum(pt_indirect)
    mask = (pt_lum > pt_threshold) & (casc_lum > 0.001)
    if not np.any(mask):
        return {"stem": stem, "status": "empty_mask", "valid": 0}

    rel = (casc_lum[mask] - pt_lum[mask]) / np.maximum(pt_lum[mask], 1e-6)
    ratio = casc_lum[mask] / np.maximum(pt_lum[mask], 1e-6)
    return {
        "stem": stem,
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


def verdict_from_rows(rows: dict[int, dict]) -> tuple[str, dict]:
    deltas = {}
    ns = sorted(n for n, r in rows.items() if r.get("status") == "ok")
    for prev, cur in zip(ns, ns[1:]):
        a = rows[prev]["pt_indirect_mean"]
        b = rows[cur]["pt_indirect_mean"]
        deltas[f"{prev}_to_{cur}"] = abs(b - a) / max(abs(a), 1e-6)

    d2048 = deltas.get("1024_to_2048")
    if d2048 is None:
        return "PENDING", deltas
    if d2048 <= 0.05:
        return "CONVERGED", deltas
    if d2048 <= 0.10:
        return "MARGINAL", deltas
    return "PROVISIONAL", deltas


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="sponza")
    ap.add_argument("--hybrid", type=int, choices=[0, 1], default=0)
    ap.add_argument("--n", type=int, nargs="*", default=[128, 256, 512, 1024, 2048])
    ap.add_argument("--out", default="tools/v3_baseline/baseline_metrics.json")
    ap.add_argument("--pt-threshold", type=float, default=0.05,
                   help="PT indirect luminance threshold for valid pixel mask (default 0.05)")
    args = ap.parse_args()

    if args.scene != "sponza":
        raise SystemExit("only --scene=sponza is currently wired")

    directory = Path(
        "tools/v3_baseline/captures_sponza_hybon"
        if args.hybrid
        else "tools/v3_baseline/captures_sponza_default"
    )
    rows = {}
    for n in args.n:
        stem = f"v3base_sponza_cam0_mbon_g100_hyb{args.hybrid}_N{n:04d}_m17"
        rows[n] = analyze_stem(directory, stem, args.pt_threshold)

    verdict, deltas = verdict_from_rows(rows) if args.hybrid == 0 else ("N/A", {})
    result = {
        "scene": args.scene,
        "hybrid": args.hybrid,
        "directory": str(directory),
        "verdict": verdict,
        "pt_mean_deltas": deltas,
        "rows": {str(k): v for k, v in rows.items()},
    }

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
