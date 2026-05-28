"""v2.5 Axis A — per-cascade contribution isolation analyzer.

Reads cascade-only-{C0..C0+CL} captures at cornell/cam0/N=2048 and emits the
bright%/ratio/|p95|/dim% growth curve as a function of max cascade level.
Applies the pre-committed gate from doc/7/v25_architectural_scope.md.

Pre-committed verdict bands:
  CLEAR ATTRIBUTION: any single transition (L->L+1) accounts for >=70% of
                     total bright% growth from L=0 to L=max -> that transition
                     is the v2.5 fix target.
  MULTI-SOURCE:      growth spread across >=2 transitions with no dominant ->
                     pivot to Axis B (bake audit) or C (basis jitter).
"""
import os, json
import numpy as np
import OpenEXR, Imath

N = 2048
LEVELS = [0, 1, 2, 3]
VARIANT_DIR = 'tools/v25_axisA/captures'
STEM_FMT    = f'v25A_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_maxL{{L}}'
OUT_JSON    = 'tools/v25_axisA/v25A_results.json'

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
    return {
        'label': label,
        'mask_px': int(mask.sum()),
        'mean_ratio': float(np.mean(masked_ratio)),
        'median_abs_rel_err': float(np.median(np.abs(rel_err_m))),
        'p95_abs_rel_err':    float(np.percentile(np.abs(rel_err_m), 95)),
        'dim_pct':    float(100.0 * np.mean(masked_ratio < 0.5)),
        'bright_pct': float(100.0 * np.mean(masked_ratio > 1.3)),
    }

print(f'===== v2.5/A per-cascade contribution isolation — cornell/cam0/N={N} =====')

results = []
for L in LEVELS:
    stem = STEM_FMT.format(L=L)
    casc = read_exr(os.path.join(VARIANT_DIR, stem + '_cascade_gi.exr'))
    ptf  = read_exr(os.path.join(VARIANT_DIR, stem + '_pt_full.exr'))
    ptd  = read_exr(os.path.join(VARIANT_DIR, stem + '_pt_direct.exr'))
    m = compute_metrics(casc, ptf, ptd, f'maxL{L}')
    results.append(m)

print()
print(f"  {'level':<8} {'ratio':>7} {'|p50|':>7} {'|p95|':>7} {'dim%':>6} {'bright%':>8}")
for m in results:
    print(f"  {m['label']:<8} {m['mean_ratio']:>7.3f} {m['median_abs_rel_err']:>7.3f} "
          f"{m['p95_abs_rel_err']:>7.3f} {m['dim_pct']:>6.1f} {m['bright_pct']:>8.2f}")

print()
print('  Per-transition (L -> L+1) deltas:')
print(f"  {'trans':<8} {'d_ratio':>9} {'d_p95':>9} {'d_dim%':>8} {'d_bright%':>11}")
transitions = []
for i in range(len(results) - 1):
    a, b = results[i], results[i+1]
    d = {
        'transition': f'{a["label"]}->{b["label"]}',
        'd_ratio':       b['mean_ratio']         - a['mean_ratio'],
        'd_p95':         b['p95_abs_rel_err']    - a['p95_abs_rel_err'],
        'd_dim_pct':     b['dim_pct']            - a['dim_pct'],
        'd_bright_pct':  b['bright_pct']         - a['bright_pct'],
    }
    transitions.append(d)
    print(f"  {a['label']+'>'+b['label']:<8} {d['d_ratio']:>+9.3f} {d['d_p95']:>+9.3f} "
          f"{d['d_dim_pct']:>+8.2f} {d['d_bright_pct']:>+11.2f}")

total_growth = results[-1]['bright_pct'] - results[0]['bright_pct']
print()
print(f'  Total bright% from L=0 to L={LEVELS[-1]}: {total_growth:+.2f} pp')

print()
print('=== Pre-committed gate (doc/7/v25_architectural_scope.md) ===')

shares = []
if abs(total_growth) > 0.1:
    for d in transitions:
        share = d['d_bright_pct'] / total_growth if total_growth != 0 else 0.0
        shares.append(share)
        print(f"  {d['transition']:<14} bright% share = {100*share:+.1f}%")
else:
    print(f"  bright% essentially flat across levels (|total| < 0.1 pp); leak is at L=0 itself.")
    shares = [0.0] * len(transitions)

dominant_idx = -1
dominant_share = 0.0
for i, s in enumerate(shares):
    if abs(s) > abs(dominant_share):
        dominant_share = s
        dominant_idx = i

verdict = None
action = None
if abs(total_growth) <= 0.1:
    bright_at_L0 = results[0]['bright_pct']
    if bright_at_L0 >= 5.0:
        verdict = 'CLEAR_AT_C0'
        action  = (f'Leak is intrinsic to C0 bake itself (bright%={bright_at_L0:.1f} at L=0, '
                   f'grows {total_growth:+.2f} pp through L={LEVELS[-1]}). '
                   'Proceed to Axis B (bake-side cone integral audit), targeting C0 bake shader.')
    else:
        verdict = 'NO_LEAK_FOUND'
        action  = 'Bright tail absent under cap — re-check capture; baseline mismatch.'
elif abs(dominant_share) >= 0.70:
    verdict = 'CLEAR_ATTRIBUTION'
    action  = (f'Transition {transitions[dominant_idx]["transition"]} accounts for {100*dominant_share:+.1f}% '
               f'of bright% growth — that cascade-merge step is the v2.5 fix target.')
else:
    verdict = 'MULTI_SOURCE'
    action  = (f'Growth spread across transitions (max share {100*dominant_share:+.1f}% at '
               f'{transitions[dominant_idx]["transition"] if dominant_idx >= 0 else "n/a"}). '
               'No single merge level dominates; pivot to Axis B (audit) or C (basis jitter).')

print()
print(f'  ===> Verdict: {verdict}')
print(f'        Action: {action}')

out = {
    'N': N,
    'levels': LEVELS,
    'scene': 'cornell/cam0',
    'per_level':   results,
    'transitions': transitions,
    'total_bright_pct_growth': total_growth,
    'verdict':     verdict,
    'action':      action,
}
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
with open(OUT_JSON, 'w') as f:
    json.dump(out, f, indent=2)
print(f'\nSaved: {OUT_JSON}')
