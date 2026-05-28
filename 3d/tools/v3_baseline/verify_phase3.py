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

base = Path('v4_phase3_sponza_pscene')
casc = read_exr(base.with_name(base.name + '_cascade_gi.exr'))
pt_full = read_exr(base.with_name(base.name + '_pt_full.exr'))
pt_direct = read_exr(base.with_name(base.name + '_pt_direct.exr'))
pt_indirect = np.maximum(pt_full - pt_direct, 0.0)

# Cascade GI is 720x1280 (full), PT is 360x640 (half). Downsample cascade 2x.
if casc.shape[:2] != pt_indirect.shape[:2]:
    h2, w2 = casc.shape[0] // 2, casc.shape[1] // 2
    casc = casc[:h2*2, :w2*2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))
    # PT should now be same size; but if not, downsample it too
    if casc.shape[:2] != pt_indirect.shape[:2]:
        h2, w2 = pt_indirect.shape[0] // 2, pt_indirect.shape[1] // 2
        pt_indirect = pt_indirect[:h2*2, :w2*2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))

casc_lum = lum(casc)
pt_lum = lum(pt_indirect)
mask = (pt_lum > 0.05) & (casc_lum > 0.001)
valid = int(mask.sum())

ratio = casc_lum[mask] / np.maximum(pt_lum[mask], 1e-6)
rel = (casc_lum[mask] - pt_lum[mask]) / np.maximum(pt_lum[mask], 1e-6)

a95 = np.percentile(np.abs(rel), 95)
rmean = np.mean(ratio)
bpt = 100*np.mean(ratio > 1.3)
dpt = 100*np.mean(ratio < 0.7)

print(f'valid={valid}')
print(f'cascade_mean={np.mean(casc_lum[mask]):.6f}')
print(f'pt_indirect_mean={np.mean(pt_lum[mask]):.6f}')
print(f'ratio_self={rmean:.6f}')
print(f'abs_p95={a95:.6f}')
print(f'bright_pct={bpt:.4f}%')
print(f'dim_pct={dpt:.4f}%')
print()
print('Stage 9 ref: ratio=1.040 |p95|=0.253 bright=1.73%')
print(f'Gate |p95|<=0.30: {"PASS" if a95 <= 0.30 else "FAIL"}')
print(f'Gate ratio in [0.96,1.08]: {"PASS" if 0.96 <= rmean <= 1.08 else "FAIL"}')