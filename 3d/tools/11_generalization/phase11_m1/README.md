# Phase 11 M1 — UV2 island extractor + atlas packer (CPU)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\11_generalization\phase11_m1\run_phase11_m1.ps1
```

CPU-only gate. No GPU, no shader dispatch, no G0–G10 mutation.

Validates:

- unique-UV2 island extraction (two planar quads → two charts)
- tiled UV0 refused (`TiledUv`, empty charts)
- folded non-planar island refused (`PlanarRmsTooHigh`)
- R4 pack into 1024×256 band templates, two pages (`y=0` / `y=1536`)
- R5 optional gutter (`minGutterTexels`)
- R6 resolution: `max(64, align_up(extent.u / texelScale, 64))`, `resolution.y = 256`
- `mod(uv, gRes)` contract: `logicalBase.x % resolution.x == 0`
- Sponza OBJ diagnostic: **no authored UV2** (tiled albedo `vt`). Fail-closed. Not sold as general surface RC.

Q5 answer: author a unique UV2 pass before M3. Meshlet fallback is not used here.
