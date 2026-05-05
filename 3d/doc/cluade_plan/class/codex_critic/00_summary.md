# Codex Critic Summary

Review timestamp: 2026-05-05T19:01:51+08:00

Target reviewed: `doc/cluade_plan/class/`

Output:

- `02_current_codebase_review.md` - current-codebase critique of the Claude class notes.

Scope:

- Source Claude class files were not edited.
- Findings were checked against the live C++ and shader code in `src/` and `res/shaders/`.
- The older `01_class_review.md` remains as historical review output.

Top risks:

1. The docs repeatedly teach the old D=4/default-size model, but the runtime default is `dirRes=8` with scaled per-cascade D.
2. Several pages still describe directional final rendering as C0-atlas-only; the current renderer binds the selected cascade's grid and atlas.
3. Phase 5d is stale: current defaults are non-co-located cascades with spatial trilinear upper lookup enabled.
4. Temporal accumulation is now fused into the bake path in the normal case; `temporal_blend.comp` is a fallback, not the main path.
5. Phase 14 range-scaling docs misstate the C1 interval math.
