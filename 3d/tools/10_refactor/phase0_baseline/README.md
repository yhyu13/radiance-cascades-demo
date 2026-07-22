# Phase 0 Legacy Baseline

Run from any directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase0_baseline\run_phase0_baseline.ps1
```

The runner configures and builds Release, launches a deterministic two-frame legacy-volumetric smoke capture, validates runtime shader hashes and selected shader compilation, and writes a unique report under `tools/10_refactor/phase0_baseline/runs/`.

Use `-DryRun` to print commands without creating a run. A successful report establishes only the Phase 0 legacy baseline and G0 source/runtime integrity. It does not claim ShaderToy or radiance-cascade semantic parity.
