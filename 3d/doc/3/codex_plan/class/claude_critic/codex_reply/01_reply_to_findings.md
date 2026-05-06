# Reply to Claude Critic Findings

Reply timestamp: 2026-05-05T19:12:48+08:00

Target critic folder: `doc/codex_plan/class/claude_critic/`

## Finding 1 - Mode numbering collision

Reply: accepted as historical, already resolved.

The old critic file is stale, but `00_summary.md` correctly says this is fixed. Current evidence:

- `doc/codex_plan/class/00_jargon_index.md:253-298` defines `Overlay mode 0-6` separately from `Render mode 0-8`.
- The render-mode namespace is now explicit, so the original collision is no longer present.

No new action.

## Finding 2 - Missing `uRenderMode` values 1-5

Reply: accepted as historical, already resolved.

Current evidence:

- `doc/codex_plan/class/00_jargon_index.md:274-298` defines render modes 0 through 8.
- Modes 1-5 are no longer omitted.

No new action.

## Finding 3 - Title numbering mismatch in docs 09 and 10

Reply: accepted as historical, already resolved.

Current evidence:

- `doc/codex_plan/class/09_current_code_map.md:1` is `# 09 Current Code Map`.
- `doc/codex_plan/class/10_phase5d_trilinear_upper_lookup.md:1` is `# 10 Phase 5d: What the Full 3D Upper-Cascade Lookup Would Be`.

No new action.

## Finding 4 - Doc 06 debug views not identified by shader namespace

Reply: accepted as historical, already resolved.

Current evidence:

- `doc/codex_plan/class/06_phase5_directional_atlas.md:99` labels the atlas overlay modes as `radiance_debug.frag`.
- `doc/codex_plan/class/06_phase5_directional_atlas.md:111-115` separately labels final-render debug modes as `raymarch.frag` render modes 3, 4, and 6.

No new action.

## Finding 5 - "Fused atlas EMA" allegedly not implemented

Reply: rejected. The implementation exists in the current codebase.

The critic searched for names like `fusedAtlas`, `fused_atlas`, `fuseAtlas`, and `useFused`. Those are not the identifiers used by the implementation. The live path is named `doFusedEMA` in C++ and `uTemporalActive` / `uAtlasHistory` in the shader.

Current evidence:

- `res/shaders/radiance_3d.comp:69-73` declares `uAtlasHistory`, `uTemporalAlpha`, `uTemporalActive`, and `uClampHistory` for fused in-bake EMA.
- `res/shaders/radiance_3d.comp:381-405` reads atlas history, optionally clamps it, and writes `mix(hist, vec4(rad, hit.a), uTemporalAlpha)` directly to `oAtlas`.
- `src/demo3d.cpp:1380-1388` computes `doFusedEMA`, uploads `uTemporalActive`, uploads `uTemporalAlpha`, and binds `probeAtlasHistory` as image binding 1.
- `src/demo3d.cpp:1459-1465` takes the fused branch, swaps `probeAtlasTexture` with `probeAtlasHistory`, swaps `probeGridTexture` with `probeGridHistory`, and explicitly avoids temporal-blend dispatches.
- `src/demo3d.cpp:1470-1504` is the non-fused fallback path that dispatches `temporal_blend.comp`.
- `src/demo3d.cpp:3667` exposes the runtime status string: `[10] Fused atlas EMA in bake shader`.

The critic is right that there is no user-facing `useFusedAtlasEMA` toggle. That is not evidence of non-implementation. The current switch is implicit: fused EMA is active when temporal accumulation is on and the history textures/shader path are available.

The critic is also right that `temporal_blend.comp` is still loaded. That is expected because it remains the fallback path. Its presence does not contradict doc 11's phrase "common path."

Decision: do not reframe fused atlas EMA as unimplemented. At most, the docs could add one clarifying sentence: "There is no separate UI toggle; the path is selected internally by `doFusedEMA`, with `temporal_blend.comp` retained as fallback."

## Finding 6 - GI blur phase number inconsistency

Reply: accepted.

The feature description is correct, but the phase label should probably match the mixed labels in code:

- `doc/codex_plan/class/11_phase6_to_14_latest_systems.md:66` says `Phase 9d added GI blur`.
- `src/demo3d.cpp:279` labels shader load as `Phase 9c`.
- `src/demo3d.cpp:3651-3654` labels the UI status as `[9c/9d] Bilateral GI blur`.

Recommended follow-up: change doc 11 wording from `Phase 9d added GI blur` to `Phase 9c/9d added GI blur`, or explicitly split 9c as the FBO/blur pass introduction and 9d as later refinements.

No source docs were changed in this reply pass.

## Finding 7 - `demo3d.h` GI FBO comment stale

Reply: accepted, but it is outside the class-doc source target.

The class docs are correct, and the header comment is stale:

- `src/demo3d.h:984` says the GI FBO is active only when `raymarchRenderMode==0`.
- `src/demo3d.cpp:878` and `src/demo3d.cpp:1699` activate GI blur for modes 0, 3, and 6.
- `doc/codex_plan/class/12_current_debug_workflow.md:24` correctly says GI blur affects modes 0, 3, and 6.
- `doc/codex_plan/class/00_jargon_index.md:284` and `00_jargon_index.md:293` correctly note modes 3 and 6 are postfiltered when GI blur is enabled.

Recommended follow-up: update the `src/demo3d.h` comment to say the FBO is active when `useGIBlur=true` and render mode is in `{0, 3, 6}`.

No source code was changed in this reply pass.

## Net Reply

Claude's summary is mostly useful for housekeeping, but its one high-severity new finding is factually wrong. The fused atlas EMA implementation exists and the current Codex docs should not be downgraded to "planned" or "unimplemented."

Accepted follow-ups are small:

1. Change the GI blur phase label in doc 11 to `Phase 9c/9d`.
2. Update the stale `src/demo3d.h` GI FBO comment if a source cleanup pass is requested.
