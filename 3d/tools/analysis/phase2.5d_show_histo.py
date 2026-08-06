"""Display the W3 64-bin histogram from phase2.5d_sdf_distribution_v2.json."""
import json
import sys
from pathlib import Path

p = Path(sys.argv[1] if len(sys.argv) > 1 else "tools/phase2.5d_sdf_distribution_v2.json")
d = json.loads(p.read_text())
print(f"data_kind: {d.get('data_kind', 'MISSING')}")
print(f"diag_alpha_mode: {d['diag_alpha_mode']}")
print(f"scene: {d['scene']}")

for c in d['cascades']:
    if c.get('diag_surface_total', 0) > 0:
        h = c['diag_histo']
        bins = c['diag_histo_bins']
        total = c['diag_surface_total']
        nonzero = [(i, n) for i, n in enumerate(h) if n > 0]
        print(f"\nC{c['index']}: histo_bins={bins}, total={total}, mean={c['diag_surface_mean']:.4f}")
        print(f"  non-zero bins ({len(nonzero)}/{bins}):")
        for i, n in nonzero[:24]:
            pct = 100.0 * n / total
            bar = '#' * max(1, int(pct))
            print(f"    bin{i:3d} a in ({i/bins:.4f}, {(i+1)/bins:.4f}]: {n:>6} ({pct:5.2f}%)  {bar}")
        if len(nonzero) > 24:
            print(f"    ... ({len(nonzero) - 24} more non-zero bins)")
