"""v2.4 A/B analyzer — Default (C0 D=8) vs C0HD (C0 D=16) at cornell/cam0/N=2048.

Reads the two captures, recomputes the standard MBRC quality metrics
(ratio, |p95|, dim%, bright%), emits the delta vs Default, and applies the
pre-committed v2.4 gate from doc/7/v24_c0_dirres_bump_impl.md.

Pre-committed verdict bands (locked before sweep):
  STRONG:    |p95| drops ≥30%, ratio shift ≤0.05, bright% drops ≥3pp, dim% not worse by >3pp
  MARGINAL:  |p95| drops 10-30%, ratio shift ≤0.10, bright% drops 1-3pp, dim% not worse by >5pp
  DEAD:      otherwise (revert + pivot to v2.5)
"""
import os, json
import numpy as np
import OpenEXR, Imath

N = 2048
DEFAULT_DIR = 'tools/v20_convergence/captures_cv1_postfix'
VARIANT_DIR = 'tools/v24_c0_hd/captures'
STEM_DEF    = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_postfix'
STEM_VAR    = f'v24_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_c0d16'
OUT_JSON    = 'tools/v24_c0_hd/v24_results.json'

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
    return 0.2126*rgb[...,0] + 0.7152*rgb[...,1] + 0.0722*rgb[...,2]

def downsample_2x2(img):
    h, w = img.shape[:2]; h2, w2 = h//2, w//2
    img = img[:h2*2, :w2*2]
    return img.reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))

def compute_metrics(casc, ptf, ptd, label):
    if casc.shape[0] != ptf.shape[0]:
        casc = downsample_2x2(casc)
    pti = np.maximum(ptf - ptd, 0.0)
    L_c = lum(casc); L_pti = lum(pti)
    mask = (L_pti > 0.05) & (L_c > 0.001)
    ratio_arr = L_c / np.maximum(L_pti, 1e-6)
    masked_ratio = ratio_arr[mask]
    rel_err = (L_c - L_pti) / np.maximum(L_pti, 1e-6)
    rel_err_m = rel_err[mask]
    metrics = {
        'label': label,
        'mask_px': int(mask.sum()),
        'mean_lum_cascade': float(np.mean(L_c[mask])),
        'mean_lum_pt_indirect': float(np.mean(L_pti[mask])),
        'mean_ratio': float(np.mean(masked_ratio)),
        'median_abs_rel_err': float(np.median(np.abs(rel_err_m))),
        'p95_abs_rel_err':    float(np.percentile(np.abs(rel_err_m), 95)),
        'dim_pct':    float(100.0 * np.mean(masked_ratio < 0.5)),
        'bright_pct': float(100.0 * np.mean(masked_ratio > 1.3)),
    }
    return metrics

print('===== v2.4 A/B analysis — Default (C0 D=8) vs C0HD (C0 D=16) =====')

# Baseline Default
casc_d = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_cascade_gi.exr'))
ptf_d  = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_full.exr'))
ptd_d  = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_direct.exr'))
m_def  = compute_metrics(casc_d, ptf_d, ptd_d, 'Default_D8')

# Variant C0 D=16
casc_v = read_exr(os.path.join(VARIANT_DIR, STEM_VAR + '_cascade_gi.exr'))
ptf_v  = read_exr(os.path.join(VARIANT_DIR, STEM_VAR + '_pt_full.exr'))
ptd_v  = read_exr(os.path.join(VARIANT_DIR, STEM_VAR + '_pt_direct.exr'))
m_var  = compute_metrics(casc_v, ptf_v, ptd_v, 'C0HD_D16')

# Pretty table
def fmt_row(m):
    return (f"  {m['label']:14s}  ratio={m['mean_ratio']:.3f}  "
            f"|p50|={m['median_abs_rel_err']:.3f}  |p95|={m['p95_abs_rel_err']:.3f}  "
            f"dim%={m['dim_pct']:.1f}  bright%={m['bright_pct']:.1f}")
print(fmt_row(m_def))
print(fmt_row(m_var))

delta = {
    'd_ratio':    m_var['mean_ratio']        - m_def['mean_ratio'],
    'd_p50':      m_var['median_abs_rel_err'] - m_def['median_abs_rel_err'],
    'd_p95':      m_var['p95_abs_rel_err']    - m_def['p95_abs_rel_err'],
    'd_dim_pct':  m_var['dim_pct']            - m_def['dim_pct'],
    'd_bright_pct': m_var['bright_pct']       - m_def['bright_pct'],
    'p95_pct_change': 100.0 * (m_var['p95_abs_rel_err'] - m_def['p95_abs_rel_err']) / m_def['p95_abs_rel_err'],
}
print()
print(f"  Δ ratio       : {delta['d_ratio']:+.3f}")
print(f"  Δ |p50|       : {delta['d_p50']:+.3f}")
print(f"  Δ |p95|       : {delta['d_p95']:+.3f}  ({delta['p95_pct_change']:+.1f}%)")
print(f"  Δ dim%        : {delta['d_dim_pct']:+.2f} pp")
print(f"  Δ bright%     : {delta['d_bright_pct']:+.2f} pp")

# ---- Pre-committed gate -------------------------------------------------
p95_drop_pct = -delta['p95_pct_change']  # positive = improvement
ratio_shift  = abs(delta['d_ratio'])
bright_drop  = -delta['d_bright_pct']
dim_change   = delta['d_dim_pct']  # positive = worse (more dim pixels)

print()
print('=== Pre-committed gate (locked in doc/7/v24_c0_dirres_bump_impl.md) ===')
print(f'  |p95| drop:    {p95_drop_pct:+.1f}%   (STRONG ≥30%, MARGINAL 10-30%, DEAD <10%)')
print(f'  ratio shift:   {ratio_shift:.3f}     (STRONG ≤0.05, MARGINAL ≤0.10, DEAD >0.10)')
print(f'  bright% drop:  {bright_drop:+.2f} pp (STRONG ≥3pp, MARGINAL 1-3pp)')
print(f'  dim% change:   {dim_change:+.2f} pp  (STRONG ≤+3pp, MARGINAL ≤+5pp)')

# Apply gate
if   (p95_drop_pct >= 30 and ratio_shift <= 0.05 and bright_drop >= 3 and dim_change <= 3):
    verdict = 'STRONG'
    action  = 'Ship as Default; v2.x terminus. Confirm at cornell/cam2 next.'
elif (p95_drop_pct >= 10 and ratio_shift <= 0.10 and bright_drop >= 1 and dim_change <= 5):
    verdict = 'MARGINAL'
    action  = 'Ship as opt-in preset "C0HD"; document bake-cost trade.'
else:
    verdict = 'DEAD'
    action  = 'Revert (CLI flag not promoted); pivot to v2.5 architectural.'

print()
print(f'  ===> Verdict: {verdict}')
print(f'        Action: {action}')

out = {
    'N': N,
    'scene': 'cornell/cam0',
    'baseline': m_def,
    'variant':  m_var,
    'delta':    delta,
    'verdict':  verdict,
    'action':   action,
}
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
with open(OUT_JSON, 'w') as f:
    json.dump(out, f, indent=2)
print(f'\nSaved: {OUT_JSON}')
