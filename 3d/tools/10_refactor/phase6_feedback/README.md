# Phase 6 Temporal Hit-Chart Feedback

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase6_feedback\run_phase6_feedback.ps1
```

Validates gates G7 (temporal feedback) and G10 (determinism/stability):

- Exact four-bin previous-generation C0 reconstruction (`CubeA.glsl:166-170`) verified against independent address fixtures.
- Controlled cross-chart bounce: a seeded X1 wall chart changes floor-chart destinations; changing the destination's own previous texel does not emulate a bounce; reset suppresses lookup; no read/write alias; previous generation only after a completed swap.
- Full C5→C0 hierarchy with merge + feedback runs eight frames twice: finite bounded converging C0 energy, byte-identical determinism, failure injection leaves read set and generation unchanged.

Regenerate address fixtures:

```powershell
py -3 .\tools\10_refactor\phase6_feedback\generate_feedback_golden.py
```
