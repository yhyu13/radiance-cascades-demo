"""v2.2 Step 0 precondition test — does bright tail overlap with aFactor bite?

Premise: LS (DM+ST+WS) clamped the bright tail at the cost of global dimming.
If Default's bright outliers are the SAME pixels LS attenuated heavily, v2.2
(luminance-gated aFactor) has signal — gate the attenuation by upper-cascade
luminance and we recover Default's mean while keeping LS's bright clamp.

If the bright tail comes from pixels LS DIDN'T touch (e.g. lower-cascade
surface-hit term), v2.2 is dead and we go to v2.3 attribution directly.

Inputs:
  - Default sweep: captures_cv1_postfix/N2048
  - LS sweep:      captures_cv1_postfix_leaksupp/N2048
  - Shared PT:     either dir's pt_full + pt_direct
Output: r_attenuation + Spearman corr + percentile table.

Pre-committed gate (locked in doc/7/v22_aFactor_reshape_impl.md Step 0):
  STRONG   r_attenuation >= 2.0  -> proceed
  MARGINAL [1.2, 2.0)            -> proceed, add 2nd-seed capture at Step 4
  DEAD     < 1.2                 -> skip to v2.3
"""
import os, json, sys
import numpy as np
import OpenEXR, Imath

N = 2048
DEFAULT_DIR = 'tools/v20_convergence/captures_cv1_postfix'
LS_DIR      = 'tools/v20_convergence/captures_cv1_postfix_leaksupp'
STEM_DEF = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_postfix'
STEM_LS  = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_leaksupp'

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

def spearman(x, y):
    rx = np.argsort(np.argsort(x))
    ry = np.argsort(np.argsort(y))
    return float(np.corrcoef(rx, ry)[0, 1])

# Load
casc_def = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_cascade_gi.exr'))
casc_ls  = read_exr(os.path.join(LS_DIR,      STEM_LS  + '_cascade_gi.exr'))
ptf      = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_full.exr'))
ptd      = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_direct.exr'))
pt_indirect = np.maximum(ptf - ptd, 0.0)

# Match resolutions
if casc_def.shape[0] != ptf.shape[0]:
    casc_def = downsample_2x2(casc_def)
if casc_ls.shape[0] != ptf.shape[0]:
    casc_ls = downsample_2x2(casc_ls)

L_def = lum(casc_def)
L_ls  = lum(casc_ls)
L_pti = lum(pt_indirect)

# Analyzer mask
mask = (L_pti > 0.05) & (L_def > 0.001)
print(f'Mask: {mask.sum()} px ({100*mask.mean():.1f}%)')

# Per-pixel aFactor bite proxy: how much LS reduced Default
delta = L_def - L_ls
# Default ratio vs PT_indirect
ratio_def = L_def / np.maximum(L_pti, 1e-6)

# Bright mask: where Default is >1.3x PT (the "bright%" definition)
bright_mask = mask & (ratio_def > 1.3)
not_bright_mask = mask & (ratio_def <= 1.3)
print(f'Bright pixels: {bright_mask.sum()} ({100*bright_mask.sum()/mask.sum():.1f}% of masked)')
print(f'Non-bright:    {not_bright_mask.sum()} ({100*not_bright_mask.sum()/mask.sum():.1f}% of masked)')

# Core gate metric
delta_bright = delta[bright_mask]
delta_other  = delta[not_bright_mask]
mean_db = float(np.mean(delta_bright))
mean_do = float(np.mean(delta_other))
r_atten = mean_db / max(mean_do, 1e-9)

print()
print('=== Core gate metric ===')
print(f'  mean(delta) on bright   pixels: {mean_db:.5f}')
print(f'  mean(delta) on non-bright px:   {mean_do:.5f}')
print(f'  r_attenuation = {r_atten:.3f}')

# Spearman over the union (informational)
in_mask = mask
sp = spearman(ratio_def[in_mask].astype(np.float64),
              delta[in_mask].astype(np.float64))
print(f'  spearman(Default_ratio, delta) over mask: {sp:+.3f}')

# Histograms for Step 1 (percentiles of delta on bright pixels)
print()
print('=== delta distribution on BRIGHT pixels (drives Step 1 sweep range) ===')
pcts = [10, 30, 50, 70, 90, 99]
for p in pcts:
    v = float(np.percentile(delta_bright, p))
    print(f'  p{p:02d} = {v:+.5f}')

print()
print('=== delta distribution on NON-BRIGHT pixels (for comparison) ===')
for p in pcts:
    v = float(np.percentile(delta_other, p))
    print(f'  p{p:02d} = {v:+.5f}')

# Also: where does LS dim aggressively? upper-lum proxy from delta+(1-l)
# Step 1 wants to gate on L_upper, which we approximate by delta/(uGIStrength*(1-l)*(1-aFactor)).
# Since we don't have l or aFactor per-pixel, fall back to using delta itself as the
# gate variable. (The shader-time gate will see L_upper directly; this is just the proxy
# we have access to from the captured outputs.)
print()
print('=== Decision per pre-committed gate ===')
if r_atten >= 2.0:
    verdict = 'STRONG'
    action = 'Proceed to Step 1 (4-value smoothstep sweep) + Step 2 (mode 1 + mode 2)'
elif r_atten >= 1.2:
    verdict = 'MARGINAL'
    action = 'Proceed BUT add 2nd-seed capture at Step 4'
else:
    verdict = 'DEAD'
    action = 'Skip to v2.3 attribution; document v2.2 as N/A'
print(f'  r_attenuation = {r_atten:.3f}')
print(f'  verdict       = {verdict}')
print(f'  action        = {action}')

# Save JSON for the impl doc to consume
out = {
    'N': N,
    'mask_px': int(mask.sum()),
    'bright_px': int(bright_mask.sum()),
    'not_bright_px': int(not_bright_mask.sum()),
    'mean_delta_bright': mean_db,
    'mean_delta_not_bright': mean_do,
    'r_attenuation': r_atten,
    'spearman_ratio_delta': sp,
    'delta_bright_percentiles': {f'p{p:02d}': float(np.percentile(delta_bright, p)) for p in pcts},
    'delta_other_percentiles':  {f'p{p:02d}': float(np.percentile(delta_other,  p)) for p in pcts},
    'verdict': verdict,
    'action':  action,
}
os.makedirs('tools/v22_aFactor', exist_ok=True)
with open('tools/v22_aFactor/precondition_results.json', 'w') as f:
    json.dump(out, f, indent=2)
print()
print('Saved: tools/v22_aFactor/precondition_results.json')
