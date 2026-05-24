"""MBRC v2.0 P2 — Per-pixel dominant-direction-bin distribution analyzer.

Reads the mode 22 EXR sidecars for cam0 and cam2. Each pixel encodes:
    R = (dx_best + 0.5) / D    # analyzer recovers dx = floor(R * D)
    G = (dy_best + 0.5) / D    # analyzer recovers dy = floor(G * D)
    B = top_contrib / total_contrib (dominance fraction in [0,1], 0 if no GI)

For each camera, build a per-bin histogram of dominant-bin pixel counts (only
pixels with B > 0 are counted — these are the surface pixels that have GI
contribution). Then compute the histogram overlap as

    overlap = sum_b min(h0_norm[b], h2_norm[b])

where h{0,2}_norm are the normalized (sum=1) histograms.

Pre-committed verdict bands (see p2_dombin_capture.ps1):
    overlap >= 0.70  -> P2_OVERLAP_HIGH
    overlap in [0.40, 0.70) -> P2_OVERLAP_MEDIUM
    overlap < 0.40   -> P2_OVERLAP_LOW

Outputs JSON with per-camera dominant bins, full histograms, overlap metric,
verdict, and a small ASCII heatmap of each camera's bin-occupancy.

Atlas direction-resolution D is inferred from the data (max(round(x*D))+1 for
D in a few common candidates; sanity check: dx and dy fall in [0, D-1]).
"""

import json
import math
import sys
from pathlib import Path

try:
    import OpenEXR
    import Imath
    HAVE_OPENEXR = True
except ImportError:
    HAVE_OPENEXR = False

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy required", file=sys.stderr)
    sys.exit(1)


def load_exr_rgb(path: Path):
    """Return (H, W, 3) float32 array from an RGB EXR."""
    if HAVE_OPENEXR:
        f = OpenEXR.InputFile(str(path))
        hdr = f.header()
        dw = hdr['dataWindow']
        w = dw.max.x - dw.min.x + 1
        h = dw.max.y - dw.min.y + 1
        pt = Imath.PixelType(Imath.PixelType.FLOAT)
        chans = ['R', 'G', 'B']
        out = np.zeros((h, w, 3), dtype=np.float32)
        for i, c in enumerate(chans):
            raw = f.channel(c, pt)
            out[:, :, i] = np.frombuffer(raw, dtype=np.float32).reshape(h, w)
        return out

    # Fallback: tinyexr via imageio (best-effort)
    try:
        import imageio.v3 as iio
        arr = iio.imread(str(path))
        if arr.ndim == 3 and arr.shape[2] >= 3:
            return arr[:, :, :3].astype(np.float32)
        raise RuntimeError(f"unexpected EXR shape {arr.shape}")
    except Exception as e:
        raise RuntimeError(f"no EXR reader available (install python-openexr or imageio[ffmpeg]); {e}")


def infer_D(arr: np.ndarray, dominance_min: float = 1e-4) -> int:
    """Infer atlas direction resolution D from R/G channels.

    The shader encodes R = (dx_best + 0.5) / D and G = (dy_best + 0.5) / D
    where dx_best, dy_best in [0..D-1]. The set of allowed R values for a
    given D is exactly {(k+0.5)/D : k=0..D-1}. The smallest D consistent
    with the data is the answer.

    Earlier version used a loose `err < 0.5/D` check; D=4 false-positived on
    D=16 data because the threshold accepted any R value within 1/(2D) of
    the D-quantized centers — which spans the whole unit interval for D=4.
    Fixed by checking that the COUNT of distinct R values matches D AND
    every observed value matches one of the D allowed centers within FP
    tolerance.
    """
    mask = arr[:, :, 2] > dominance_min
    if not mask.any():
        return 8
    r = arr[mask, 0]
    g = arr[mask, 1]

    # Round R values to nearest allowed center under each candidate D and
    # check the residual is < FP tolerance, not < bin width.
    fp_tol = 5e-4  # comfortable margin around float32 round-trip of (k+0.5)/D
    for D in (4, 8, 16, 32):
        centers = (np.arange(D) + 0.5) / D
        # nearest-center distance for each observed R
        diff_r = np.abs(r[:, None] - centers[None, :]).min(axis=1).max()
        diff_g = np.abs(g[:, None] - centers[None, :]).min(axis=1).max()
        if diff_r < fp_tol and diff_g < fp_tol:
            return D
    return 8


def per_pixel_bin_index(arr: np.ndarray, D: int):
    """Return (dx, dy, dominance, mask) arrays. Mask = valid GI pixels."""
    r = arr[:, :, 0]
    g = arr[:, :, 1]
    b = arr[:, :, 2]
    dx = np.clip(np.floor(r * D), 0, D - 1).astype(np.int32)
    dy = np.clip(np.floor(g * D), 0, D - 1).astype(np.int32)
    mask = b > 1e-4
    return dx, dy, b, mask


def build_histogram(dx: np.ndarray, dy: np.ndarray, mask: np.ndarray, D: int):
    """Return D*D-bin histogram (flattened, idx = dy*D + dx)."""
    flat = (dy * D + dx)[mask]
    hist = np.bincount(flat.astype(np.int64), minlength=D * D).astype(np.float64)
    return hist


def histogram_overlap(h0: np.ndarray, h2: np.ndarray) -> float:
    """Sum of per-bin min after normalization. Bounded in [0, 1]."""
    s0 = h0.sum()
    s2 = h2.sum()
    if s0 <= 0 or s2 <= 0:
        return 0.0
    return float(np.minimum(h0 / s0, h2 / s2).sum())


def js_divergence(h0: np.ndarray, h2: np.ndarray) -> float:
    """Jensen-Shannon divergence (base 2). Bounded in [0, 1]."""
    s0 = h0.sum()
    s2 = h2.sum()
    if s0 <= 0 or s2 <= 0:
        return 1.0
    p = h0 / s0
    q = h2 / s2
    m = 0.5 * (p + q)
    eps = 1e-12
    def kl(a, b):
        nz = a > eps
        return float((a[nz] * np.log2(a[nz] / np.maximum(b[nz], eps))).sum())
    return 0.5 * (kl(p, m) + kl(q, m))


def per_row_metrics(h0: np.ndarray, h2: np.ndarray, D: int):
    """Per-row JS divergence + per-row overlap between cam0 and cam2.

    Reshapes the D*D-bin flat histograms to (D, D) where row=dy, col=dx, then
    computes the JS / overlap between the per-dx distributions WITHIN each row.
    Returns a list of per-row dicts and a mass-weighted average.

    Rationale: the headline overlap on a 2D bin grid is inflated by shared
    structure on either axis. cornell-orig-alcove P2 measurement: cam0 + cam2
    both concentrate on row dy=1 (forced by surface-normal upper-hemisphere
    geometry), so the dy-axis is shared and the headline overlap reads
    ~MEDIUM. The dx-axis WITHIN dy=1 is the actual asymmetry. Per-row JS
    isolates that axis. The mass-weighted average uses both cams' row mass
    (sum then halve), so rows that one cam ignores but the other concentrates
    on still contribute proportionally.
    """
    g0 = h0.reshape(D, D)
    g2 = h2.reshape(D, D)
    rows = []
    total_m0 = h0.sum()
    total_m2 = h2.sum()
    for dy in range(D):
        r0 = g0[dy]
        r2 = g2[dy]
        m0 = float(r0.sum())
        m2 = float(r2.sum())
        share0 = m0 / max(total_m0, 1.0)
        share2 = m2 / max(total_m2, 1.0)
        if m0 <= 0 and m2 <= 0:
            rows.append({"dy": dy, "share0": 0.0, "share2": 0.0,
                         "row_js": 0.0, "row_overlap": 1.0,
                         "p0": [], "p2": []})
            continue
        if m0 <= 0 or m2 <= 0:
            rows.append({"dy": dy, "share0": share0, "share2": share2,
                         "row_js": 1.0, "row_overlap": 0.0,
                         "p0": (r0 / max(m0, 1.0)).tolist(),
                         "p2": (r2 / max(m2, 1.0)).tolist()})
            continue
        p0 = r0 / m0
        p2 = r2 / m2
        row_overlap = float(np.minimum(p0, p2).sum())
        # JS within this row (treat row as a D-bin distribution).
        row_js = float(js_divergence(r0, r2))
        rows.append({
            "dy": dy,
            "share0": share0,
            "share2": share2,
            "row_js": row_js,
            "row_overlap": row_overlap,
            "p0": p0.tolist(),
            "p2": p2.tolist(),
        })
    # Mass-weighted average using mean(share0, share2) per row, normalized.
    weights = np.array([(r["share0"] + r["share2"]) * 0.5 for r in rows], dtype=np.float64)
    wsum = float(weights.sum())
    if wsum > 0:
        weights = weights / wsum
    js_vals = np.array([r["row_js"] for r in rows], dtype=np.float64)
    ov_vals = np.array([r["row_overlap"] for r in rows], dtype=np.float64)
    weighted_js = float((weights * js_vals).sum())
    weighted_overlap = float((weights * ov_vals).sum())
    return rows, weighted_js, weighted_overlap


def top_bins(hist: np.ndarray, D: int, k: int = 5):
    """Return list of (dx, dy, share) for the top-k bins."""
    total = hist.sum()
    if total <= 0:
        return []
    idx = np.argsort(hist)[::-1][:k]
    out = []
    for i in idx:
        if hist[i] <= 0:
            break
        dy, dx = divmod(int(i), D)  # note: idx = dy*D + dx -> divmod(idx, D) = (dy, dx)
        out.append({"dx": int(dx), "dy": int(dy), "share": float(hist[i] / total)})
    return out


def ascii_heatmap(hist: np.ndarray, D: int):
    """Render D*D histogram as ASCII rows (row = dy, col = dx)."""
    total = hist.sum()
    if total <= 0:
        return ["(empty)"]
    grid = hist.reshape(D, D) / total
    glyphs = " .:-=+*#%@"
    g_n = len(glyphs)
    peak = float(grid.max())
    lines = []
    for dy in range(D):
        row = ""
        for dx in range(D):
            v = grid[dy, dx] / max(peak, 1e-12)
            ix = min(g_n - 1, int(v * g_n))
            row += glyphs[ix]
        lines.append(row)
    return lines


def verdict_band(overlap: float) -> str:
    if overlap >= 0.70:
        return "P2_OVERLAP_HIGH"
    if overlap >= 0.40:
        return "P2_OVERLAP_MEDIUM"
    return "P2_OVERLAP_LOW"


def main():
    # CLI form:  analyze_p2_dombin.py [<cam0_exr> <cam2_exr> <out_json>]
    # default (no args): original D=4 default-engine capture paths.
    if len(sys.argv) == 4:
        cam0_path = Path(sys.argv[1])
        cam2_path = Path(sys.argv[2])
        out_json  = Path(sys.argv[3])
    elif len(sys.argv) == 1:
        base = Path("tools/v20_arch_diagnostic/captures_p2_dombin")
        cam0_path = base / "alcove_cam0_M0_b2_mboff_dombin_m22_dombin.exr"
        cam2_path = base / "alcove_cam2_M0_b2_mboff_dombin_m22_dombin.exr"
        out_json  = base / "p2_dombin_results.json"
    else:
        print(f"usage: {sys.argv[0]} [<cam0.exr> <cam2.exr> <out.json>]", file=sys.stderr)
        sys.exit(2)

    if not cam0_path.exists() or not cam2_path.exists():
        print(f"ERROR: missing EXR(s):\n  {cam0_path}\n  {cam2_path}", file=sys.stderr)
        sys.exit(1)

    print(f"[p2] reading {cam0_path}")
    arr0 = load_exr_rgb(cam0_path)
    print(f"[p2] reading {cam2_path}")
    arr2 = load_exr_rgb(cam2_path)

    D = infer_D(arr0)
    print(f"[p2] inferred D={D} (atlas direction resolution per side)")

    dx0, dy0, dom0, m0 = per_pixel_bin_index(arr0, D)
    dx2, dy2, dom2, m2 = per_pixel_bin_index(arr2, D)
    h0 = build_histogram(dx0, dy0, m0, D)
    h2 = build_histogram(dx2, dy2, m2, D)

    px0 = int(m0.sum())
    px2 = int(m2.sum())
    overlap = histogram_overlap(h0, h2)
    js = js_divergence(h0, h2)
    band = verdict_band(overlap)

    print()
    print(f"[p2] cam0 valid GI pixels: {px0} / {m0.size} ({100*px0/m0.size:.1f}%)")
    print(f"[p2] cam2 valid GI pixels: {px2} / {m2.size} ({100*px2/m2.size:.1f}%)")
    print(f"[p2] cam0 mean dominance: {float(dom0[m0].mean()):.3f}")
    print(f"[p2] cam2 mean dominance: {float(dom2[m2].mean()):.3f}")
    print()
    print(f"[p2] histogram overlap (sum of per-bin min): {overlap:.4f}")
    print(f"[p2] Jensen-Shannon divergence (base 2):     {js:.4f}")
    print(f"[p2] VERDICT: {band}")
    print()

    print("[p2] cam0 top 5 dominant bins:")
    for b in top_bins(h0, D, 5):
        print(f"      (dx={b['dx']}, dy={b['dy']}) share={b['share']:.3f}")
    print("[p2] cam2 top 5 dominant bins:")
    for b in top_bins(h2, D, 5):
        print(f"      (dx={b['dx']}, dy={b['dy']}) share={b['share']:.3f}")
    print()

    print("[p2] cam0 bin-occupancy heatmap (row=dy, col=dx, glyph density = share):")
    for line in ascii_heatmap(h0, D):
        print(f"      {line}")
    print("[p2] cam2 bin-occupancy heatmap:")
    for line in ascii_heatmap(h2, D):
        print(f"      {line}")
    print()

    # Per-row breakdown (catches dx-axis collapse hidden by shared dy-row).
    rows, weighted_row_js, weighted_row_overlap = per_row_metrics(h0, h2, D)
    print(f"[p2] per-row JS (mass-weighted across rows):     {weighted_row_js:.4f}")
    print(f"[p2] per-row overlap (mass-weighted across rows): {weighted_row_overlap:.4f}")
    print(f"[p2] per-row details (row mass shares + within-row JS/overlap):")
    print(f"      dy   share0    share2    row_overlap    row_js")
    for r in rows:
        print(f"      {r['dy']:>2d}   {r['share0']:.3f}    {r['share2']:.3f}    "
              f"{r['row_overlap']:.4f}        {r['row_js']:.4f}")

    results = {
        "scene": "cornell-orig-alcove",
        "mode": 22,
        "D": D,
        "cam0": {
            "valid_pixels": px0,
            "total_pixels": int(m0.size),
            "mean_dominance": float(dom0[m0].mean()) if px0 else 0.0,
            "histogram": h0.tolist(),
            "top_bins": top_bins(h0, D, 8),
        },
        "cam2": {
            "valid_pixels": px2,
            "total_pixels": int(m2.size),
            "mean_dominance": float(dom2[m2].mean()) if px2 else 0.0,
            "histogram": h2.tolist(),
            "top_bins": top_bins(h2, D, 8),
        },
        "overlap": overlap,
        "js_divergence": js,
        "per_row": {
            "weighted_js": weighted_row_js,
            "weighted_overlap": weighted_row_overlap,
            "rows": rows,
        },
        "verdict": band,
        "bands": {
            "P2_OVERLAP_HIGH":   "overlap >= 0.70 (bake-side framing INCOMPLETE)",
            "P2_OVERLAP_MEDIUM": "overlap in [0.40, 0.70) (partial bake-side asymmetry)",
            "P2_OVERLAP_LOW":    "overlap < 0.40 (bake-side per-bin framing CONFIRMED)",
        },
    }
    out_json.write_text(json.dumps(results, indent=2))
    print()
    print(f"[p2] wrote {out_json}")


if __name__ == "__main__":
    main()
