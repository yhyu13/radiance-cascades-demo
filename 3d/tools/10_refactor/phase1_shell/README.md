# Phase 1 Shell Parity

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase1_shell\run_phase1_shell_parity.ps1
```

The runner builds once, executes the default legacy runtime directly and through the `App3D`/`Demo3DBackend` seam, then requires identical backend selection, scene/shader revisions, and screenshot bytes.
