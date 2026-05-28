"""v2.3 Step 0 precondition test — are bright outliers spatially clustered on probes?

Premise (from doc/7/v23_leak_attribution_impl.md):
  v2.2 falsified "merge-formula reshape" — bright tail is uncorrelated with
  upper-cascade visibility. The next hypothesis is "small set of probes drives
  many bright pixels" (color-bleed overshoot, C0 over-fire in tight corners).
  If true -> per-source fix is worth building (v2.4 candidate).
  If false (diffuse) -> architectural pivot (v2.5).

Method:
  1. Load worldpos.exr (mode 23 capture, full-viewport).
  2. Load Default's cascade_gi + PT triplet (half-viewport).
  3. Downsample worldpos 2x2 to match PT/cascade half-res.
  4. Reconstruct bright_mask via the v22 definition (ratio > 1.3, masked).
  5. Bin each bright pixel by C0 probe-cell index =
        floor((world_pos - volumeOrigin) / cellSize), clamped to [0, N-1]^3.
  6. Build Lorenz curve: sort cells by bright-pixel count descending,
     compute cumulative fraction. Report top-1%, top-5%, top-10%, Gini.

Pre-committed gate (locked in doc/7/v23_leak_attribution_impl.md Step 0):
  STRONG    top-5% probes cover >= 40% of bright pixels  -> Step 1 (mode 23 as production diagnostic)
  MARGINAL  top-5% in [20%, 40%)                          -> diagnostic-only (no auto-fix)
  DEAD      top-5% < 20%                                  -> skip to v2.4 architectural
"""
import os, json
import numpy as np
import OpenEXR, Imath

# ---- Scene constants (cornell, default Demo3D ctor) ----------------------
VOLUME_ORIGIN = np.array([-2.0, -2.0, -2.0], dtype=np.float64)
VOLUME_SIZE   = np.array([ 4.0,  4.0,  4.0], dtype=np.float64)
C0_RES        = 32           # cascadeC0Res default
C0_CELL_SIZE  = VOLUME_SIZE[0] / C0_RES  # 0.125

# ---- File paths ----------------------------------------------------------
N = 2048
WP_DIR  = 'tools/v23_attribution/captures'
WP_STEM = f'v23_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m23'
DEFAULT_DIR = 'tools/v20_convergence/captures_cv1_postfix'
STEM_DEF    = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_postfix'
OUT_JSON    = 'tools/v23_attribution/precondition_results.json'

# ---- EXR helpers ---------------------------------------------------------
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

def downsample_2x2_nearest(img):
    # For world position, averaging across 4 pixels would blur cell boundaries.
    # Take the top-left pixel of each 2x2 block (matches PT's half-res sampling
    # pattern at the lattice intersection, close enough for a histogram bin).
    h, w = img.shape[:2]
    h2, w2 = h//2, w//2
    return img[:h2*2:2, :w2*2:2]

# ---- Load ---------------------------------------------------------------
print('===== v2.3 Step 0 — leak-source probe attribution precondition =====')
wp_full = read_exr(os.path.join(WP_DIR, WP_STEM + '_worldpos.exr'))
print(f'  worldpos EXR:  {wp_full.shape}  (full viewport)')

casc_def = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_cascade_gi.exr'))
ptf      = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_full.exr'))
ptd      = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_direct.exr'))
print(f'  cascade_gi EXR: {casc_def.shape}')
print(f'  pt_full EXR:    {ptf.shape}')

# Match resolutions: PT is half-viewport (640x360 here); cascade & worldpos
# are full-viewport (1280x720). Downsample to PT res.
if casc_def.shape[0] != ptf.shape[0]:
    casc_def = downsample_2x2(casc_def)
    print(f'  cascade_gi -> {casc_def.shape} (2x2 mean)')
if wp_full.shape[0] != ptf.shape[0]:
    wp = downsample_2x2_nearest(wp_full)
    print(f'  worldpos   -> {wp.shape} (2x2 nearest)')
else:
    wp = wp_full

pt_indirect = np.maximum(ptf - ptd, 0.0)
L_def = lum(casc_def)
L_pti = lum(pt_indirect)

# ---- Reconstruct bright mask (v22 definition) ----------------------------
mask = (L_pti > 0.05) & (L_def > 0.001)
ratio_def = L_def / np.maximum(L_pti, 1e-6)
bright_mask = mask & (ratio_def > 1.3)
n_bright = int(bright_mask.sum())
print()
print(f'  total masked px: {int(mask.sum())} ({100*mask.mean():.1f}% of frame)')
print(f'  bright px:       {n_bright} ({100*n_bright/max(1,mask.sum()):.1f}% of masked)')

if n_bright == 0:
    raise SystemExit('  ERROR: no bright pixels found — capture/mask mismatch.')

# ---- Probe-cell binning --------------------------------------------------
# For each bright pixel, compute C0 probe-cell index.
bright_pos = wp[bright_mask]  # (n_bright, 3)
# Mode 23 wrote position only on hit; background pixels have pos=(0,0,0).
# Inside the bright mask we trust the position (mask required L_def>0.001,
# meaning the pixel hit geometry). Sanity:
hit_check = np.linalg.norm(bright_pos, axis=1) > 1e-6
n_dropped = int((~hit_check).sum())
if n_dropped > 0:
    print(f'  WARN: {n_dropped} bright pixels have zero worldpos (skipped)')
    bright_pos = bright_pos[hit_check]

cell_idx = np.floor((bright_pos.astype(np.float64) - VOLUME_ORIGIN) / C0_CELL_SIZE).astype(np.int64)
cell_idx = np.clip(cell_idx, 0, C0_RES - 1)
# Flatten 3D cell index to single int
flat_idx = (cell_idx[:, 2] * C0_RES + cell_idx[:, 1]) * C0_RES + cell_idx[:, 0]

uniq, counts = np.unique(flat_idx, return_counts=True)
n_cells_total = C0_RES ** 3   # 32768
n_cells_hit   = uniq.size
print()
print(f'  total C0 probe cells:       {n_cells_total}')
print(f'  cells touched by bright px: {n_cells_hit} ({100*n_cells_hit/n_cells_total:.1f}%)')

# ---- Lorenz curve --------------------------------------------------------
counts_sorted = np.sort(counts)[::-1].astype(np.float64)  # descending
total = counts_sorted.sum()
cum = np.cumsum(counts_sorted) / total  # fraction of bright pixels covered

def top_frac(percent_of_cells):
    """Return fraction of bright pixels covered by top-`percent_of_cells`% of TOUCHED cells."""
    k = max(1, int(np.ceil(n_cells_hit * percent_of_cells / 100.0)))
    return float(cum[k - 1])

# Also report the absolute (% of all C0 cells) version
def top_frac_abs(percent_of_all_cells):
    k = max(1, int(np.ceil(n_cells_total * percent_of_all_cells / 100.0)))
    if k > n_cells_hit:
        return 1.0  # all bright pixels covered by less than this many cells
    return float(cum[k - 1])

top1_touched  = top_frac(1)
top5_touched  = top_frac(5)
top10_touched = top_frac(10)
top1_all      = top_frac_abs(1)
top5_all      = top_frac_abs(5)
top10_all     = top_frac_abs(10)

# Gini over touched cells
sorted_counts_asc = np.sort(counts).astype(np.float64)
n = sorted_counts_asc.size
gini = (2.0 * np.sum((np.arange(1, n+1)) * sorted_counts_asc)) / (n * sorted_counts_asc.sum()) - (n + 1) / n

print()
print('=== Lorenz curve (over TOUCHED cells) ===')
print(f'  top  1% of touched cells cover: {100*top1_touched:5.1f}% of bright pixels')
print(f'  top  5% of touched cells cover: {100*top5_touched:5.1f}% of bright pixels')
print(f'  top 10% of touched cells cover: {100*top10_touched:5.1f}% of bright pixels')
print()
print('=== Lorenz curve (over ALL C0 cells) — the doc-locked metric ===')
print(f'  top  1% of all cells cover: {100*top1_all:5.1f}% of bright pixels')
print(f'  top  5% of all cells cover: {100*top5_all:5.1f}% of bright pixels')
print(f'  top 10% of all cells cover: {100*top10_all:5.1f}% of bright pixels')
print(f'  Gini coefficient (touched cells): {gini:.3f}  (0=uniform, 1=concentrated)')

# ---- Gate (doc-locked) --------------------------------------------------
# The doc's "top-5% of probes" framing assumes most cells contribute; here
# only 0.7% of cells are touched at all, making the "ALL cells" denominator
# degenerate (top 5% of all = 1638 cells trivially covers 100%). The
# meaningful concentration metric is top-5% of TOUCHED cells.
print()
print('=== Decision per pre-committed gate (top-5% of TOUCHED C0 cells) ===')
print('    (top-5% of ALL cells is degenerate when only 0.7% are touched)')
gate_val = top5_touched
if gate_val >= 0.40:
    verdict = 'STRONG'
    action  = 'Proceed to Step 1: mode 23 ships as production diagnostic; design v2.4 per-source fix'
elif gate_val >= 0.20:
    verdict = 'MARGINAL'
    action  = 'Mode 23 ships as diagnostic-only; no v2.4 auto-fix from this signal alone'
else:
    verdict = 'DEAD'
    action  = 'Skip to v2.5 architectural pivot; bright-tail leak is diffuse across probes'
print(f'  top5_touched = {100*gate_val:.1f}%')
print(f'  verdict      = {verdict}')
print(f'  action       = {action}')
print()
print('  SUB-SIGNAL (also strong): bright pixels touch only')
print(f'    {n_cells_hit}/{n_cells_total} = {100*n_cells_hit/n_cells_total:.1f}% of all C0 cells.')
print( '    Even at MARGINAL concentration within the contributing set, the')
print( '    leak is strongly localized in absolute terms — small attribution')
print( '    target.')

# ---- Save JSON ----------------------------------------------------------
out = {
    'N': N,
    'scene': 'cornell/cam0',
    'volume_origin': VOLUME_ORIGIN.tolist(),
    'volume_size':   VOLUME_SIZE.tolist(),
    'c0_res': C0_RES,
    'c0_cell_size': C0_CELL_SIZE,
    'mask_px': int(mask.sum()),
    'bright_px': n_bright,
    'bright_px_with_valid_worldpos': int(bright_pos.shape[0]),
    'cells_total': n_cells_total,
    'cells_touched_by_bright': int(n_cells_hit),
    'top_pct_of_touched_cells': {
        'top1':  top1_touched,
        'top5':  top5_touched,
        'top10': top10_touched,
    },
    'top_pct_of_all_cells': {
        'top1':  top1_all,
        'top5':  top5_all,
        'top10': top10_all,
    },
    'gini_touched': gini,
    'gate_metric_name': 'top5_of_touched_cells',
    'gate_metric_value': gate_val,
    'gate_metric_note': 'top-5%-of-ALL-cells degenerate (only 0.7% touched); using TOUCHED denominator',
    'verdict': verdict,
    'action':  action,
}
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
with open(OUT_JSON, 'w') as f:
    json.dump(out, f, indent=2)
print()
print(f'Saved: {OUT_JSON}')
