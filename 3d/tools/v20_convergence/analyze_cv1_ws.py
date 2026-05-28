"""Compare PRE-fix vs POST-fix Default vs POST-fix Leak-suppressed CV1 sweeps.

Note: 'WS' columns in the rendered table now mean Leak-suppressed (DM+ST+WS)
after the 2026-05-25 third-gate fix (the old captures_cv1_postfix_ws/ sweep
was a no-op because DM was off — see project_st_gates_phase3 memory)."""
import os, sys, json
import numpy as np
import OpenEXR, Imath

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
    h, w = img.shape[:2]
    h2, w2 = h//2, w//2
    img = img[:h2*2, :w2*2]
    return img.reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))

def analyze(casc_path, ptfull_path, ptdir_path):
    casc = read_exr(casc_path)
    ptf  = read_exr(ptfull_path)
    ptd  = read_exr(ptdir_path)
    pt_indirect = np.maximum(ptf - ptd, 0.0)
    if casc.shape[0] != ptf.shape[0]:
        casc = downsample_2x2(casc)
    casc_lum = lum(casc)
    pti_lum  = lum(pt_indirect)
    mask = (pti_lum > 0.05) & (casc_lum > 0.001)
    ratio = casc_lum[mask] / pti_lum[mask]
    log_ratio = np.log(np.clip(ratio, 1e-6, 1e6))
    return {
        'casc_mean': float(np.mean(casc_lum[mask])),
        'pt_indirect_mean': float(np.mean(pti_lum[mask])),
        'ratio_mean': float(np.mean(ratio)),
        'ratio_p05': float(np.percentile(ratio, 5)),
        'ratio_p50': float(np.percentile(ratio, 50)),
        'ratio_p95': float(np.percentile(ratio, 95)),
        'log_p95_abs': float(np.percentile(np.abs(log_ratio), 95)),
        'dim_pct':   float(100.0 * np.mean(ratio < 0.7)),
        'bright_pct':float(100.0 * np.mean(ratio > 1.3)),
        'n_pixels':  int(mask.sum()),
    }

def sweep(prefix, tag):
    base = 'tools/v20_convergence/' + prefix
    rows = []
    for N in [128, 256, 512, 1024, 2048]:
        stem = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_{tag}'
        casc  = os.path.join(base, stem + '_cascade_gi.exr')
        ptf   = os.path.join(base, stem + '_pt_full.exr')
        ptd   = os.path.join(base, stem + '_pt_direct.exr')
        if not os.path.exists(casc):
            print(f'MISSING: {casc}')
            continue
        r = analyze(casc, ptf, ptd)
        r['N'] = N
        rows.append(r)
    return rows

print('=== PRE-fix (captures_cv1) ===')
pre = sweep('captures_cv1/', 'baseline')
print('=== POST-fix no-WS (captures_cv1_postfix) ===')
post = sweep('captures_cv1_postfix/', 'postfix')
print('=== POST-fix +LeakSupp DM+ST+WS (captures_cv1_postfix_leaksupp) ===')
ws = sweep('captures_cv1_postfix_leaksupp/', 'leaksupp')

print()
print('| N    | pre ratio | post ratio | +LS ratio | pre |p95| | post |p95| | +LS |p95| | pre dim% | post dim% | +LS dim% | pre brt% | post brt% | +LS brt% |')
print('|------|----------:|-----------:|----------:|----------:|-----------:|----------:|---------:|----------:|---------:|---------:|----------:|---------:|')
for i in range(len(ws)):
    p = pre[i] if i < len(pre) else None
    q = post[i] if i < len(post) else None
    w = ws[i]
    def s(d, k): return f'{d[k]:.3f}' if d else '   - '
    def sp(d, k): return f'{d[k]:.1f}' if d else '  - '
    print(f'| {w["N"]:4d} | {s(p,"ratio_mean")} | {s(q,"ratio_mean")} | {s(w,"ratio_mean")} | {s(p,"log_p95_abs")} | {s(q,"log_p95_abs")} | {s(w,"log_p95_abs")} | {sp(p,"dim_pct")} | {sp(q,"dim_pct")} | {sp(w,"dim_pct")} | {sp(p,"bright_pct")} | {sp(q,"bright_pct")} | {sp(w,"bright_pct")} |')

# Verdict bands
print()
n_max = ws[-1]
print(f'=== Verdict @ N={n_max["N"]} (post-fix +LeakSupp) ===')
print(f'  ratio_mean: {n_max["ratio_mean"]:.3f}')
print(f'  |p95| log:  {n_max["log_p95_abs"]:.3f}')
print(f'  dim_pct:    {n_max["dim_pct"]:.1f}%')
print(f'  bright_pct: {n_max["bright_pct"]:.1f}%')
print(f'  casc_mean:  {n_max["casc_mean"]:.4f}')
print(f'  pt_ind_mean:{n_max["pt_indirect_mean"]:.4f}')

r = n_max["ratio_mean"]
p95 = n_max["log_p95_abs"]
if 0.85 <= r <= 1.15: band = 'CV1_CASCADE_NEAR_PT (HYBRID-RETIREMENT READY)'
elif 0.60 <= r < 0.85: band = 'CV1_CASCADE_DIM_MILD'
elif 1.15 < r <= 1.40: band = 'CV1_CASCADE_BRIGHT_MILD'
else: band = 'OUT OF BAND'
print(f'  BAND 1: {band}')
leak_target_p95 = 1.0
if p95 <= leak_target_p95:
    print(f'  LEAK-TAIL: |p95|={p95:.3f} <= target {leak_target_p95:.2f} -> PASS')
else:
    print(f'  LEAK-TAIL: |p95|={p95:.3f} > target {leak_target_p95:.2f} -> STILL WIDE')

# Save JSON
out = {'pre': pre, 'post': post, 'ws': ws}
with open('tools/v20_convergence/captures_cv1_postfix_leaksupp/cv1_leaksupp_results.json', 'w') as f:
    json.dump(out, f, indent=2)
print('Saved: tools/v20_convergence/captures_cv1_postfix_leaksupp/cv1_leaksupp_results.json')
