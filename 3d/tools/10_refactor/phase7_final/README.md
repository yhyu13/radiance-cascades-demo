# Phase 7 Final Consumer

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase7_final\run_phase7_final.ps1
```

Validates gate G9 (final consumer):

- Converged C0 hierarchy (8 frames) rendered through the completed read[C0] view; 76,800 pixels match the CPU final-view oracle.
- Disabled reference renders the declared baseline (sky + direct only) and matches the CPU baseline exactly.
- Surface classification matches golden camera probes (charts, reflective, black uncharted).
- Distinct resource identities: the final read view never aliases hierarchy write textures.
- No upper-cascade stub: the converged view differs from baseline where indirect light exists.
- Artifacts: `reference_final_view.png` and `reference_baseline_view.png` for human inspection.

Interactive first-visual (reference pipeline with temporal feedback, ESC to quit):

```powershell
.\build\RadianceCascades3D.exe --runtime-shell=app3d --reference-render
```

Declared native display policy: exact four-bin reconstruction from the completed C0 read view; visible-surface albedo applied; no `1/pi`; direct light composited separately per pixel; reflective surfaces zero; linear display map.

Regenerate camera probes:

```powershell
py -3 .\tools\10_refactor\phase7_final\generate_final_golden.py
```
