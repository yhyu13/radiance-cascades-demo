"""v2.3 Step 2 — Hand-inspection of the 235 contributing C0 cells.

Goal: identify the SPATIAL PATTERN of the bright-leak cells so we can scope a
v2.4 fix. The Step 0 verdict was MARGINAL within-set concentration but
STRONG sparsity (0.7% of volume). The natural hypothesis is "saturated-wall
color-bleed" — cells near the red wall (leftWall, x≈-1) and green wall
(rightWall, x≈+1) accumulating overshoot from albedo·cos integrand.

This script tests that hypothesis:
  1. Per-axis bright-mass histogram (32 bins per axis = C0 grid size)
  2. Distance-to-nearest-wall (red, green, floor, ceiling, back) histogram
  3. Per-wall mass fraction (which wall dominates)
  4. Bounding box of contributing cells

Output: console table + tools/v23_attribution/cluster_inspection.json
"""
import os, json
import numpy as np
import OpenEXR, Imath

# Same scene constants as precondition.py
VOLUME_ORIGIN = np.array([-2.0, -2.0, -2.0], dtype=np.float64)
VOLUME_SIZE   = np.array([ 4.0,  4.0,  4.0], dtype=np.float64)
C0_RES        = 32
C0_CELL_SIZE  = VOLUME_SIZE[0] / C0_RES

# Cornell wall positions (from CornellBox-Original.obj)
WALLS = {
    'left_RED':    ('x_min', -1.00),  # Kd (0.63, 0.065, 0.05)
    'right_GREEN': ('x_max',  1.00),  # Kd (0.14, 0.45,  0.091)
    'back':        ('z_min', -1.04),  # white
    'floor':       ('y_min',  0.00),  # white
    'ceiling':     ('y_max',  1.99),  # white (light here)
}

N = 2048
WP_DIR  = 'tools/v23_attribution/captures'
WP_STEM = f'v23_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m23'
DEFAULT_DIR = 'tools/v20_convergence/captures_cv1_postfix'
STEM_DEF    = f'cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17_postfix'
OUT_JSON    = 'tools/v23_attribution/cluster_inspection.json'

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

def downsample_2x2_nearest(img):
    h, w = img.shape[:2]; h2, w2 = h//2, w//2
    return img[:h2*2:2, :w2*2:2]

print('===== v2.3 Step 2 — cluster inspection =====')
wp_full = read_exr(os.path.join(WP_DIR, WP_STEM + '_worldpos.exr'))
casc    = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_cascade_gi.exr'))
ptf     = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_full.exr'))
ptd     = read_exr(os.path.join(DEFAULT_DIR, STEM_DEF + '_pt_direct.exr'))

if casc.shape[0] != ptf.shape[0]:
    casc = downsample_2x2(casc)
if wp_full.shape[0] != ptf.shape[0]:
    wp = downsample_2x2_nearest(wp_full)
else:
    wp = wp_full

pti = np.maximum(ptf - ptd, 0.0)
L_def = lum(casc); L_pti = lum(pti)
mask = (L_pti > 0.05) & (L_def > 0.001)
ratio = L_def / np.maximum(L_pti, 1e-6)
bright = mask & (ratio > 1.3)

bright_pos = wp[bright].astype(np.float64)
valid = np.linalg.norm(bright_pos, axis=1) > 1e-6
bright_pos = bright_pos[valid]
# Also retain the leak excess per pixel (Default - PT_indirect_lum) for mass weighting
leak_excess = (L_def - L_pti)[bright][valid]  # how much brighter than PT
leak_excess = np.maximum(leak_excess, 0.0)

print(f'  bright pixels analyzed: {bright_pos.shape[0]}')
print(f'  bright lum excess (Default - PT_indirect) sum: {float(leak_excess.sum()):.3f}')

# ---- Per-axis cell histogram (bright-pixel COUNT) ------------------------
cell_idx = np.floor((bright_pos - VOLUME_ORIGIN) / C0_CELL_SIZE).astype(np.int64)
cell_idx = np.clip(cell_idx, 0, C0_RES - 1)

def axis_hist(axis_label, axis_idx):
    counts = np.bincount(cell_idx[:, axis_idx], minlength=C0_RES)
    mass   = np.bincount(cell_idx[:, axis_idx], weights=leak_excess, minlength=C0_RES)
    print(f'\n  {axis_label}-axis cell histogram (32 bins):')
    print(f'    cell:   ' + ''.join(f'{i%10}' for i in range(C0_RES)))
    print(f'    count*: ' + ''.join('#' if c > 0 else '.' for c in counts))
    # Top-5 cells along this axis
    top = np.argsort(counts)[::-1][:5]
    print(f'    top-5 cells by pixel count: ' + ', '.join(
        f'cell{c}(n={counts[c]},m={mass[c]:.2f})' for c in top))
    return counts.tolist(), mass.tolist()

x_counts, x_mass = axis_hist('X', 0)
y_counts, y_mass = axis_hist('Y', 1)
z_counts, z_mass = axis_hist('Z', 2)

# ---- Wall-distance analysis ---------------------------------------------
print('\n  Per-wall: contributing cell mass within K cells of each wall')
print('  (cell-distance, not world-distance; K=0 means abuts wall plane)')

wall_results = {}
for name, (kind, coord_world) in WALLS.items():
    axis = {'x': 0, 'y': 1, 'z': 2}[kind[0]]
    # World wall coord -> cell index
    wall_cell = (coord_world - VOLUME_ORIGIN[axis]) / C0_CELL_SIZE
    # Distance from each bright pixel's cell to the wall plane, in cells
    dist_cells = np.abs(cell_idx[:, axis] - wall_cell)
    masses = {}
    for K in [0, 1, 2, 3, 5, 8]:
        sel = dist_cells <= K
        masses[K] = {
            'pix_count': int(sel.sum()),
            'pix_frac':  float(sel.mean()),
            'leak_mass': float(leak_excess[sel].sum()),
            'leak_frac': float(leak_excess[sel].sum() / max(leak_excess.sum(), 1e-9)),
        }
    print(f'    {name:14s} ({kind}={coord_world:+.2f}, cell={wall_cell:5.1f}):')
    print(f'                   K=0:{100*masses[0]["leak_frac"]:5.1f}%  K=1:{100*masses[1]["leak_frac"]:5.1f}%  '
          f'K=2:{100*masses[2]["leak_frac"]:5.1f}%  K=3:{100*masses[3]["leak_frac"]:5.1f}%  '
          f'K=5:{100*masses[5]["leak_frac"]:5.1f}%  K=8:{100*masses[8]["leak_frac"]:5.1f}%')
    wall_results[name] = {
        'wall_world_coord': coord_world,
        'wall_cell_coord':  float(wall_cell),
        'by_K_cells': masses,
    }

# ---- Bounding box of contributing cells ----------------------------------
uniq_cells = np.unique(cell_idx, axis=0)
bb_min = uniq_cells.min(axis=0).tolist()
bb_max = uniq_cells.max(axis=0).tolist()
bb_world_min = (np.array(bb_min) * C0_CELL_SIZE + VOLUME_ORIGIN).tolist()
bb_world_max = ((np.array(bb_max) + 1) * C0_CELL_SIZE + VOLUME_ORIGIN).tolist()
print()
print(f'  contributing-cell bbox (cell idx): min={bb_min}, max={bb_max}')
print(f'  contributing-cell bbox (world):     min={[round(v,3) for v in bb_world_min]} '
      f'max={[round(v,3) for v in bb_world_max]}')

# ---- Cross-wall combined: how much leak sits within K cells of EITHER red OR green wall
print()
print('  Combined left_RED + right_GREEN (saturated-wall hypothesis):')
red_cell   = wall_results['left_RED']['wall_cell_coord']
green_cell = wall_results['right_GREEN']['wall_cell_coord']
dist_red   = np.abs(cell_idx[:, 0] - red_cell)
dist_green = np.abs(cell_idx[:, 0] - green_cell)
for K in [0, 1, 2, 3, 5]:
    sel = (dist_red <= K) | (dist_green <= K)
    frac_px   = float(sel.mean())
    frac_mass = float(leak_excess[sel].sum() / max(leak_excess.sum(), 1e-9))
    print(f'    within K={K} cells of red OR green wall: '
          f'pix={100*frac_px:5.1f}%  leak_mass={100*frac_mass:5.1f}%')

out = {
    'capture': WP_STEM,
    'bright_px_analyzed': int(bright_pos.shape[0]),
    'leak_excess_total': float(leak_excess.sum()),
    'x_axis_counts': x_counts, 'x_axis_mass': x_mass,
    'y_axis_counts': y_counts, 'y_axis_mass': y_mass,
    'z_axis_counts': z_counts, 'z_axis_mass': z_mass,
    'walls': wall_results,
    'bbox_cells': {'min': bb_min, 'max': bb_max},
    'bbox_world': {'min': bb_world_min, 'max': bb_world_max},
}
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
with open(OUT_JSON, 'w') as f:
    json.dump(out, f, indent=2)
print(f'\nSaved: {OUT_JSON}')
