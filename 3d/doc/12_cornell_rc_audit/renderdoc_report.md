# RenderDoc Debug Report — Volumetric RC, Cornell Box

**Capture:** `tools/captures/rdoc_frame_frame420.rdc` (31.3 MB) — fresh, 2026-08-17
**Frame:** 420 (post-warm-up, stable)
**Backend:** `shell=legacy runtimeBackend=legacy-direct name=legacy-volumetric view=final-composite mode=0 scene=analytic:1`
**Scene:** analytic Cornell box (7 box primitives via `sdf_analytic.comp`), 128³ volume, 4 cascades (C0 32³/D8, C1 16³/D16, C2 8³/D16, C3 4³/D16, non-co-located)
**GPU:** RTX 2080 SUPER, OpenGL 3.3 + GLSL 430 compute

This report uses the `renderdoc-gpu-debug` skill: `qrenderdoc.exe --py tools/rdoc_extract.py`
(replay + texture export + GPU-counter timing) + `renderdoccmd.exe thumb` (final frame).

---

## 1. Dispatch timeline + GPU timing

```
eid  57  radiance_3d   C0 bake        3719 µs
eid  68  reduction_3d  C0 reduction     33 µs
eid 127  radiance_3d   C1 bake        6865 µs
eid 138  reduction_3d  C1 reduction    186 µs
eid 197  radiance_3d   C2 bake       11210 µs   ← dominant
eid 208  reduction_3d  C2 reduction    184 µs
eid 267  radiance_3d   C3 bake        6816 µs
eid 278  reduction_3d  C3 reduction    213 µs
eid 347  raymarch      final image    6636 µs
eid 375  gi_blur       GI blur        1799 µs
eid 406  glDrawElements ImGui           11 µs
(+ clears ~45 µs)
```

**GPU total ≈ 37.7 ms (~26 FPS).** Same structure as the Sponza frame, and the same
cost-shape: **C2 bake is the most expensive dispatch (11.2 ms) despite having only 8³=512
probes** — confirming bake cost scales with ray-interval length (`tMin..tMax` ×4 per
cascade), not probe count. Raymarch (6.6 ms) is the second-largest single step.

App-side per-cascade mean luminance (from the run log, post-warm-up) is balanced:
`C0=0.052, C1=0.056, C2=0.049, C3=0.029` — unlike Sponza's strong cascade imbalance
(multi-bounce under-emit), the analytic Cornell cascades carry comparable energy.

---

## 2. Exported stage buffers

| Resource | Export | What it shows |
|---|---|---|
| `sdfTexture` (z=64) | `rdoc_frame_frame420_sdfTexture.png` | signed-distance cross-section of the analytic box walls — clean, continuous, no holes |
| `albedoTexture` (z=64) | `rdoc_frame_frame420_albedoTexture.png` | red/green wall colors at the correct faces |
| `cascade0_probeAtlas` (z=16) | `rdoc_frame_frame420_cascade0_probeAtlas.png` | 32³ probes × 8×8 directional tiles — smoothly varying lit-wall radiance |
| `cascade1_probeAtlas` (z=8) | `rdoc_frame_frame420_cascade1_probeAtlas.png` | 16³ probes × 16×16 tiles, coarser spatial coverage |
| `cascade0_probeGrid` (z=16) | `rdoc_frame_frame420_cascade0_probeGrid.png` | isotropic (D²-averaged) probe grid — smooth luminance field |
| backbuffer | `rdoc_frame_frame420_thumb.png` | final Cornell render (mode 0, GI blur applied) |

The SDF and albedo slices confirm the analytic Cornell produces a **clean signed field**
(no conservative-band clamp, no hollow shell) — consistent with the audit's verdict that
the Cornell SDF is "correct / good enough", whereas Sponza's mesh path is not.

---

## 3. Debug takeaways (what to look for in this capture)

These map directly to the audit findings in `rc_audit_report.md`:

1. **C2 bake (eid=197) is the perf hotspot** — 11.2 ms for 512 probes. The bake loop
   (`radiance_3d.comp:622-634`) marches `raymarchSDF` over `[0.5, 2.0]` for C2. Long
   interval → many steps. This is the lever if Cornell perf matters (it currently
   doesn't dominate the parity problem).
2. **Raymarch (eid=347) is the consumer** — 6.6 ms. Its `sampleProbeDir` is where the
   missing `(4/D²)` + missing octahedral Jacobian live (`raymarch.frag:431-456`). To
   confirm the defect visually, export a mode-17 (GI-only) EXR and compare against the
   `reference_transport.comp` mode-2 final view — the audit's M3/M6 gates.
3. **Merge α semantics** — the atlas `.a` is binary (0/1), but `sampleUpperDirWeighted`
   reads it as a hit distance (`radiance_3d.comp:316-327`). This path is inactive by
   default (`uUseWeightedSample=0`), so it does not affect this default capture; it is
   a latent defect that activates when WeightedSample is enabled.
4. **No voxelize/sdf_3d dispatches in this frame** — expected: the analytic SDF is baked
   once at scene load (`sdf_analytic.comp`), so the per-frame loop is only
   bake→reduce→march→blur.

---

## 4. Reproduction commands

```powershell
# Rebuild (exe is current with the working tree; source mtimes ≤ exe mtime)
& "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release   # from 3d/build

# Capture the analytic Cornell (frame 420) headlessly
cd D:\GitRepo-My\radiance-cascades-demo\3d
.\build\RadianceCascades3D.exe --runtime-shell=legacy --auto-rdoc --exit-frames=3000
# → tools/captures/rdoc_frame_frame420.rdc

# Replay + extract stages + GPU timing
$env:RDOC_CAPTURE = "D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures\rdoc_frame_frame420.rdc"
$env:RDOC_OUTDIR = "D:\GitRepo-My\radiance-cascades-demo\3d\tools\captures"
& "C:\Program Files\RenderDoc\qrenderdoc.exe" --py "D:\GitRepo-My\radiance-cascades-demo\3d\tools\rdoc_extract.py"

# Final frame
& "C:\Program Files\RenderDoc\renderdoccmd.exe" thumb --out "...\rdoc_frame_frame420_thumb.png" --format png "...\rdoc_frame_frame420.rdc"
```

Known non-fatal issues observed during the run (not blockers for this report):
- `analyze_screenshot.py` / `analyze_renderdoc.py` fail under the default **Python 2.7**
  interpreter (non-ASCII source). They are the Claude-API post-processors, not the
  RenderDoc extractor; `rdoc_extract.py` runs under qrenderdoc's embedded Python 3.6 and
  succeeds.
- `[PHASE0] OpenGL error 0x501` at demo init — GL_INVALID_VALUE during setup, does not
  affect the captured frame.
- First-frame cascade meanLum is NaN (uninitialized atlas on frame 1) — gone by frame 420.
