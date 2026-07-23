# Phase 4 Local Single-Cascade Transport

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase4_transport\run_phase4_transport.ps1
```

Validates gates G5 (payload contract) and G8 (material/direct-light transport):

- Independent golden fixtures (`transport_golden_v1.json`) derived from `shader_toy/Common.glsl` TraceRay and `CubeA.glsl` local shading, covering sky, diffuse frontface (lit and occluded), diffuse backface, black uncharted, reflective, emissive (synthetic category), and weight coupling.
- Full-band transport for all six cascades into RGBA32F write atlases with readback evidence: distance alpha, negative sky alpha, finite nonnegative RGB, inactive texels in cleared state, no boolean alpha collapse.
- Temporal feedback `B(hit)` is disabled (zero) and no upper merge runs in Phase 4.

Regenerate fixtures after any intentional transport-semantics change:

```powershell
py -3 .\tools\10_refactor\phase4_transport\generate_transport_golden.py
```
