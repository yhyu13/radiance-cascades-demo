"""Verify Phase 3 Sponza metrics on wider mask thresholds (Phase 4B)."""
import numpy as np, Imath, OpenEXR
from pathlib import Path

def read_exr(path):
    f = OpenEXR.InputFile(str(path))
    dw = f.header()['dataWindow']
    w, h = dw.max.x - dw.min.x + 1, dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    r = np.frombuffer(f.channel('R', pt), dtype=np.float32).reshape(h, w)
    g = np.frombuffer(f.channel('G', pt), dtype=np.float32).reshape(h, w)
    b = np.frombuffer(f.channel('B', pt), dtype=np.float32).reshape(h, w)
    return np.stack([r, g, b], axis=-1)

def lum(rgb):
    return 0.2126*rgb[...,0] + 0.7152*rgb[...,1] + 0.0722*rgb[...,2]

def analyze_prefix(prefix, label, thresholds=[0.05, 0.01, 0.005]):
    base = Path(prefix)
    casc = read_exr(base.with_name(base.name + '_cascade_gi.exr'))
    pt_full = read_exr(base.with_name(base.name + '_pt_full.exr'))
    pt_direct = read_exr(base.with_name(base.name + '_pt_direct.exr'))
    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)
    
    if casc.shape[:2] != pt_indirect.shape[:2]:
        h2, w2 = casc.shape[0] // 2, casc.shape[1] // 2
        casc = casc[:h2*2, :w2*2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))
    
    casc_lum = lum(casc)
    pt_lum = lum(pt_indirect)
    
    print(f"\n=== {label} ===")
    print(f"{'threshold':>12} {'valid':>8} {'ratio':>8} {'|p95|':>8} {'bright%':>8} {'dim%':>8}")
    print("-" * 60)
    
    for th in thresholds:
        mask = (pt_lum > th) & (casc_lum > 0.001)
        if not np.any(mask):
            print(f"{th:>12.3f} {'EMPTY':>8}")
            continue
        ratio = casc_lum[mask] / np.maximum(pt_lum[mask], 1e-6)
        rel = (casc_lum[mask] - pt_lum[mask]) / np.maximum(pt_lum[mask], 1e-6)
        a95 = np.percentile(np.abs(rel), 95)
        rmean = np.mean(ratio)
        print(f"{th:>12.3f} {int(mask.sum()):>8} {rmean:>8.4f} {a95:>8.4f} {100*np.mean(ratio>1.3):>7.2f}% {100*np.mean(ratio<0.7):>7.2f}%")

# Phase 3 pscene capture (gain=0.10)
analyze_prefix('v4_phase3_sponza_pscene', 'Sponza pscene (gain=0.10, Phase 3)')

# Sponza default baseline (gain=1.0)
analyze_prefix(
    'tools/v3_baseline/captures_sponza_default/v3base_sponza_cam0_mbon_g100_hyb0_N2048_m17',
    'Sponza default (gain=1.0, M0 baseline)')