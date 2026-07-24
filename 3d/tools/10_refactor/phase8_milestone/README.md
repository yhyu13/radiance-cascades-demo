# Phase 8: Cornell Semantic Parity Milestone

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase8_milestone\run_phase8_milestone.ps1
```

Runs **G0–G10 from a clean launch** in one flow and emits a single
`semantic_parity_report.json`:

- G0 reproducible legacy baseline
- G1 shell parity (legacy-direct vs App3D-wrapped)
- G1 chart contract
- G2/G3/G4 layout kernel
- G5/G8 local transport
- G6 upper merge
- G7/G10 temporal feedback + determinism
- G9 final consumer
- Legacy Cornell validation
- **PT quality comparison** (non-blocking): NEW vs parity-scene PT luminance and energy decomposition

The milestone passes only when every gate passes. The PT comparison is a
**non-blocking quality report** — semantic parity is established by the gates,
not by pixel equality (the reference has no final compositor, and the display
layer is declared native policy).

Deliberate differences from the ShaderToy reference are documented in
`doc/10_refactor/semantic_parity_differences.md` and must be present for the
milestone to be meaningful.

Label: **semantic parity, not general mesh support.**
