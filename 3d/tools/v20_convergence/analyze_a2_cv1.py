#!/usr/bin/env python3
"""A2 gate: post-A2 CV1 (cascade vs PT-reference) with the CORRECTED mask.

Per plan-review High 9: validity is derived from the reference alone
(valid = pt_indirect_lum > 0.05).  A valid pixel where the cascade is fully
dark is a FAILURE (ratio -> 0), not excluded.  Reports coverage counts.
Statistic named exactly: p95(|ln(cascade/PT)|).
"""
import numpy as np
import OpenEXR
import Imath
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..") + os.sep
STEM = ROOT + "cv1_a2_cornell_cam0_N1024_m17"


def read_exr(path):
    f = OpenEXR.InputFile(path)
    dw = f.header()['dataWindow']
    w = dw.max.x - dw.min.x + 1
    h = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    r = np.frombuffer(f.channel('R', pt), dtype=np.float32).reshape(h, w)
    g = np.frombuffer(f.channel('G', pt), dtype=np.float32).reshape(h, w)
    b = np.frombuffer(f.channel('B', pt), dtype=np.float32).reshape(h, w)
    return np.stack([r, g, b], axis=-1)


def lum(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def downsample_2x2(img):
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    return img[:h2 * 2, :w2 * 2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


casc = read_exr(STEM + "_cascade_gi.exr")
ptf = read_exr(STEM + "_pt_full.exr")
ptd = read_exr(STEM + "_pt_direct.exr")
pt_indirect = np.maximum(ptf - ptd, 0.0)
if casc.shape[0] != ptf.shape[0]:
    casc = downsample_2x2(casc)

casc_lum = lum(casc)
pti_lum = lum(pt_indirect)

# Corrected mask (High 9): reference-derived validity only.
valid = pti_lum > 0.05
n_total = int(valid.size)
n_valid = int(valid.sum())
n_excl = n_total - n_valid

ratio = np.where(valid, casc_lum / np.maximum(pti_lum, 1e-9), np.nan)
ratio_valid = ratio[valid]
K_RATIO_FLOOR = 1.0 / 64.0   # physical floor (cascade noise floor), not 1e-6
log_ratio = np.log(np.clip(ratio_valid, K_RATIO_FLOOR, 1e6))

casc_zero_pct = 100.0 * np.mean(ratio_valid < 1e-6)

print("=== CV1 (cascade vs PT, reference-derived validity mask) ===")
print(f"pixels total={n_total} valid={n_valid} ({100.0*n_valid/n_total:.1f}%) excluded={n_excl}")
print(f"casc_mean        = {np.mean(casc_lum[valid]):.4f}")
print(f"pt_indirect_mean = {np.mean(pti_lum[valid]):.4f}")
print(f"ratio_mean       = {np.mean(ratio_valid):.4f}   (NOT the gate)")
print(f"dim%%  (ratio<0.7) = {100.0*np.mean(ratio_valid < 0.7):.1f}%%   (gate: <= 5%%)")
print(f"bright%%(ratio>1.3) = {100.0*np.mean(ratio_valid > 1.3):.1f}%%   (gate: <= 5%%)")
print(f"casc==0 coverage = {casc_zero_pct:.1f}%% of valid px (subset of dim%%; not an independent gate)")
print(f"p95(|ln ratio|)  = {np.percentile(np.abs(log_ratio), 95):.4f}   (gate: <= 0.50, floored at 1/64)")

# Old mask (pti>0.05 & casc>0.001) for apples-to-apples vs historical baseline
old_mask = (pti_lum > 0.05) & (casc_lum > 0.001)
ro = (casc_lum[old_mask] / np.maximum(pti_lum[old_mask], 1e-9))
print("--- OLD mask (pti>0.05 & casc>0.001) for historical comparison ---")
print(f"old-mask ratio_mean = {np.mean(ro):.4f}  (historical baseline ~0.977)")
print(f"old-mask n_pixels   = {int(old_mask.sum())} (excluded {n_total - int(old_mask.sum())} dark pixels)")
