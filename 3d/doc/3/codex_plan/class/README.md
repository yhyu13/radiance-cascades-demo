# Radiance Cascades Class Notes

This folder is a low-jargon, linear explanation of the 3D radiance-cascades branch.

Last synced against the local codebase: 2026-05-05.

Use this reading order:

1. `00_jargon_index.md`
2. `01_big_picture.md`
3. `02_phase1_scene_and_sdf.md`
4. `03_phase2_single_bounce_gi.md`
5. `04_phase3_four_cascades.md`
6. `05_phase4_quality_fixes.md`
7. `06_phase5_directional_atlas.md`
8. `07_phase5_spatial_layout_and_scaling.md`
9. `08_phase5_final_renderer_directional_and_shadows.md`
10. `09_current_code_map.md`
11. `10_phase5d_trilinear_upper_lookup.md`
12. `11_phase6_to_14_latest_systems.md`
13. `12_current_debug_workflow.md`

If you only need the shortest path:

1. Read `01_big_picture.md`
2. Read `04_phase3_four_cascades.md`
3. Read `06_phase5_directional_atlas.md`
4. Read `11_phase6_to_14_latest_systems.md`
5. Read `12_current_debug_workflow.md`
6. Skim `00_jargon_index.md` when a term is unclear

What this set tries to do:

- explain what problem each phase was solving
- explain why the next phase was needed
- translate the branch jargon into plain language
- connect the current Phase 14 / RenderDoc-era implementation back to the early simpler phases

What this set does not try to do:

- preserve every historical experiment
- copy every claim from `doc/cluade_plan`
- present every visual-quality claim as fully runtime-validated

This is a teaching set, not a source of truth for validation status. For raw history, see:

- `doc/cluade_plan`
- `doc/cluade_plan/PLAN.md`
- `git log --oneline`
