# Phase 2 Reference Scene

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase2_reference_scene\run_phase2_reference_scene.ps1
```

This CPU-only gate validates the immutable ShaderToy Cornell parity scene, typed trace results, exact eight-chart contract, and the fixed 2368-byte std430 upload representation. Phase 2 does not render the reference scene or allocate cascade textures.
