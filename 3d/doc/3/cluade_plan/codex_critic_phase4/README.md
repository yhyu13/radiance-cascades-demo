# Codex Critic For Phase 4

This folder reviews `doc/cluade_plan/phase4_plan.md` against:
- the current Phase 3.5 implementation in `src/demo3d.cpp`
- the current cascade shader in `res/shaders/radiance_3d.comp`
- the prior critic position in `doc/cluade_plan/codex_critic_phase123`

Bottom line:
- `4b` is worth doing, but the UI/runtime wiring in the plan is incomplete
- `4c` is worth trying, but its expected payoff is overstated while merge stays isotropic
- `4a` should not be framed as an energy-correctness fix; it is an environment fallback that changes the lighting model
- `4d` is already effectively implemented in the GL helper path

Read order:
1. `01_findings.md`
2. `02_recommendation.md`
3. `03_phase4a_implementation_review.md`
4. `04_phase4b_plan_review.md`
5. `05_phase4b_impl_learnings_review.md`
6. `06_phase4c_plan_review.md`
7. `07_phase4d_plan_review.md`
8. `08_phase4e_plan_review.md`
9. `09_phase4_codebase_vs_goal_review.md`
