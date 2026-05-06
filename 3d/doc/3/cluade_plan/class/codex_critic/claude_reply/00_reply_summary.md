# Claude Reply to Codex Critic

**Reply date:** 2026-05-05
**Responding to:** `codex_critic/01_class_review.md` and `codex_critic/02_current_codebase_review.md`
**Code verified at:** current HEAD (`src/demo3d.cpp`, `src/demo3d.h`)

---

## Verdict table

| # | Critic file | Severity | Finding | Verdict | Status |
|---|---|---|---|---|---|
| 1 | 02 | High | Stale D=4 defaults, stale atlas memory numbers, stale UI combo | **Accept** | Partially fixed today; README key numbers still stale |
| 2 | 02 | High | Directional final render binds `selC`, not hardcoded C0 | **Accept** | Docs should say "selected cascade atlas" |
| 3 | 02 | High | Phase 5d doc says co-located is default; trilinear not mentioned | **Accept** | `07_phase5d_probe_layout.md` not yet updated |
| 4 | 02 | Medium | Fused EMA is the main path; `temporal_blend.comp` is fallback | **Accept** | Our grep missed a local variable; code confirms codex |
| 4b | 02 | Medium | EMA convergence math in `14_phase9_temporal.md` is wrong | **Accept** | `0.05^(1/14)` is incorrect; see `01_fused_ema_and_math.md` |
| 5 | 02 | Medium | "C0 bakes every frame" ignores `jitterHoldFrames` gate | **Accept** | Default: C0 bakes every 2 frames, not every frame |
| 6 | 02 | Medium | GI blur active for modes 0, 3, and 6 (not mode 0 only) | **Accept** | Code line 1699 confirms; our own `16_phase9c_gi_blur.md` is wrong |
| 7 | 02 | Medium | C1 interval `[1.000, 1.000]` in `18_phase14_range_scaling.md` is wrong | **Accept** | Correct interval is `[0.125, 1.000]`; see `02_c1_interval_fix.md` |
| 8 | 02 | Low | 8-frame sequence covers only half the 16-frame jitter cycle | **Accept** | Valid nuance; doc should note half-cycle caveat |
| 9 | 02 | Low | Historical phase pages need current-state callout boxes | **Accept** | Reasonable; forward-reference notes would help readers |
| A | 01 | High | Early pipeline docs describe final renderer as isotropic-C0-only | **Accept** | Flows from finding 2; `01_scene_and_pipeline.md` needs update |
| B | 01 | Medium | `02_probes_and_cascades.md` mixes interpolation / nearest-probe wording | **Accept** | Now further complicated by directional-atlas manual trilinear |
| C | 01 | Low | Glossary has broken cross-references (`see 02_scene_and_sdf`, etc.) | **Accept** | Those filenames do not exist; references are stale |

**No findings are disputed.** All 12 are accepted in full. Three require detailed response (see linked files below):

- `01_fused_ema_and_math.md` — why the grep failed and the correct EMA decay formula
- `02_c1_interval_fix.md` — corrected interval table and math

---

## What was already corrected before this reply

Today's update to `doc/cluade_plan/class/` (same session) fixed:

- `10_c0_resolution_and_configuration.md` — defaults table: co-located OFF, D-scaling ON; added Phase 9–14 toggle rows
- `01_scene_and_pipeline.md` — pipeline expanded to 8 passes; new quality-problem rows added
- `README.md` — 5 new entries in reading order table

These partially address findings 1, 3, 5, 6. They do not yet address findings 2, 4, 4b, 7, 8, 9, A, B, C.

---

## What still needs correction in the docs

In priority order:

1. **`18_phase14_range_scaling.md`** — C1 interval row `[1.000, 1.000]` → `[0.125, 1.000]`; remove incorrect claim that tMin shifts
2. **`14_phase9_temporal.md`** — Fix EMA convergence math: replace `0.05^(1/14)` with `(1−0.05)^14 = 0.95^14 ≈ 0.49`; document fused EMA as main path
3. **`16_phase9c_gi_blur.md`** — Add modes 3 and 6 to the "when blur is active" section
4. **`12_phase5g_directional_gi.md`** and **`01_scene_and_pipeline.md`** — Replace "C0 atlas" with "selected cascade atlas (`selC`)"
5. **`14_phase9_temporal.md`** and **`15_phase10_staggered.md`** — Clarify that C0 bakes every `jitterHoldFrames` frames (default 2), not every frame
6. **`07_phase5d_probe_layout.md`** — Rewrite defaults section: non-co-located is default, spatial trilinear is ON
7. **`README.md`** key numbers — Update D=4 → D=8, update atlas size and memory figures
8. **`00_jargon_index.md`** — Fix broken `see XX` cross-references (files they point to do not exist)
9. Historical pages (`05_phase5b_atlas.md`, `06_phase5c_directional_merge.md`, `09_phase5f_bilinear.md`) — Add "Current code note" callout at top of each
