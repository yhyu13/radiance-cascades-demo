# Class Docs — Radiance Cascades 3D

A linear walkthrough of the full system, written for someone who knows GPU programming
but has not read the phase implementation docs. Read in order.

---

## Reading order

| Doc | Topic | Key question answered |
|---|---|---|
| [00 Jargon Index](00_jargon_index.md) | Glossary | What does term X mean? |
| [01 Scene and Pipeline](01_scene_and_pipeline.md) | What we render, how | What does each render pass do? |
| [02 Probes and Cascades](02_probes_and_cascades.md) | Why 4 levels, how intervals work | Why [0.125, 0.5, 2.0, 8.0] m? |
| [03 Phases 1–4](03_phases_1_to_4.md) | Foundation before Phase 5 | What was broken and why? |
| [04 Phase 5a: Direction Encoding](04_phase5a_direction_encoding.md) | Octahedral bins | How do we address directions by index? |
| [05 Phase 5b: Atlas](05_phase5b_atlas.md) | Per-direction texture storage | How is radiance laid out in the atlas? |
| [06 Phase 5c: Directional Merge](06_phase5c_directional_merge.md) | Reading atlas by direction | Why does directional merge fix color bleeding? |
| [07 Phase 5d: Probe Layout](07_phase5d_probe_layout.md) | Co-located vs non-co-located | Why non-co-located makes physical sense |
| [08 Phase 5e: D Scaling](08_phase5e_direction_scaling.md) | Per-cascade bin count | Why D=2 is degenerate; what D scaling changes |
| [09 Phase 5f: Bilinear](09_phase5f_bilinear.md) | Smooth bin transitions | Why GL_NEAREST blocks hardware bilinear; manual blend math |
| [10 Configuration](10_c0_resolution_and_configuration.md) | All knobs and their effects | Full parameter space and memory costs |

---

## The core chain (Phase 5 in one paragraph)

Phase 5a replaces Fibonacci sphere directions with octahedral bins (D×D grid over the
sphere), making direction→index conversion analytic. Phase 5b stores one radiance value
per bin per probe in an atlas texture (GL_NEAREST required). Phase 5b-1 averages the
bins back to the isotropic probeGridTexture so the renderer still works. Phase 5c
changes the merge: instead of pulling an averaged isotropic value from the upper cascade,
a miss now reads the upper cascade's atlas at the exact same direction bin — giving
directionally correct far-field radiance. Phase 5d lets each cascade use a different
probe count (halving per level), matching probe density to each level's coherence scale.
Phase 5e optionally scales D per level (more bins for far cascades that need directional
precision). Phase 5f adds bilinear blending across 4 surrounding direction bins, removing
the hard 36° bin-boundary steps that Phase 5c's nearest-bin lookup produced.

---

## Key numbers

| Parameter | Default value |
|---|---|
| Scene volume | 4 m × 4 m × 4 m |
| SDF grid | 64³ |
| C0 probe resolution | 32³ |
| baseInterval (d) | 0.125 m |
| Direction bins per probe (D=4) | 16 |
| Atlas per cascade (co-located, D=4) | 128×128×32 RGBA16F = 4 MB |
| Cascade levels | 4 (C0–C3) |
| C0 interval | [0.02, 0.125] m |
| C3 interval | [2.0, 8.0] m |
