# Phase 3 Parity Layout Kernel

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase3_layout\run_phase3_layout.ps1
```

Validates gates G2 (cascade layout), G3 (square-ring direction mapping), and G4 (interval contract):

- CPU layout oracle checked against independently generated double-precision fixtures (`layout_golden_v1.json`, derived from `shader_toy/CubeA.glsl`).
- GLSL layout decode cross-checked against the CPU oracle over fixture and coverage samples.
- Six RGBA32F atlas pairs (1024x512 per cascade) allocated and validated; band-marker readback proves all six cascades are reachable with distinct locked reaches.
- No radiance, feedback, or merge code is enabled; direction and interval outputs are diagnostics only.

Regenerate fixtures after any intentional layout-constant change:

```powershell
py -3 .\tools\10_refactor\phase3_layout\generate_layout_golden.py
```
