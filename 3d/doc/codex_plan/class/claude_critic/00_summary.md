# Critique Summary — codex_plan/class

**Date:** 2026-05-05
**Reviewer:** Claude (Sonnet 4.6)
**Scope:** All 13 class docs in `doc/codex_plan/class/` (00–12)
**Live code references:** `src/demo3d.h`, `src/demo3d.cpp`, `res/shaders/` (via grep)

---

## Status of prior findings (from 2026-04-30 review)

All four prior findings are now **resolved**:

| Prior # | Doc | Prior Status | Current Status |
|---|---|---|---|
| 1 | `00_jargon_index.md` — mode namespace collision | Unfixed | **Fixed** — overlay and render mode entries are now fully namespaced (`Overlay mode N` vs `Render mode N`) |
| 2 | `00_jargon_index.md` — missing uRenderMode 1-5 | Unfixed | **Fixed** — Render modes 0-8 are all defined |
| 3 | `09_current_code_map.md` — title header mismatch | Unfixed | **Fixed** — header now reads `# 09 Current Code Map` |
| 4 | `10_phase5d_trilinear_upper_lookup.md` — title mismatch | Unfixed | **Fixed** — header now reads `# 10 Phase 5d...` |

---

## New findings (2026-05-05)

| # | Doc | Severity | Issue |
|---|---|---|---|
| 5 | `11_phase6_to_14_latest_systems.md`, `00_jargon_index.md` | **High** | "Fused atlas EMA" described as a concrete Phase 10 feature, but no such implementation exists in `src/demo3d.h` or `src/demo3d.cpp`. Zero grep hits for any fused-EMA toggle or path. |
| 6 | `11_phase6_to_14_latest_systems.md` | Low | GI blur attributed to "Phase 9d" — code UI labels it `[9c/9d]` and `cluade_plan` docs consistently call it Phase 9c. Inconsistent cross-reference. |
| 7 | `src/demo3d.h` (code, not doc) | Low | `giFBO` comment says "Only active when … `raymarchRenderMode==0`" — contradicted by live UI string "(Settings panel, modes 0/3/6)". Header is stale; docs 12 and jargon index are correct. |

---

## Strong points (verified against 2026-05-05 codebase)

- All Phase 9–14 parameter defaults in `01_big_picture.md` and `11_phase6_to_14_latest_systems.md` match `Demo3D::Demo3D()`: `temporalAlpha=0.05`, `probeJitterScale=0.06`, `jitterPatternSize=8`, `jitterHoldFrames=2`, `staggerMaxInterval=8`, `c0MinRange=1.0`, `c1MinRange=1.0`, `useGIBlur=true`, `giBlurRadius=8`.
- Scaled D formula in `00_jargon_index.md` and `07_phase5_spatial_layout_and_scaling.md` (`min(16, dirRes << i)` with `dirRes=8` → D8/D16/D16/D16) is correct.
- Staggered update rule in `00_jargon_index.md` (`min(2^i, staggerMaxInterval)`) matches code.
- `12_current_debug_workflow.md` correct that GI blur affects render modes 0, 3, and 6 — confirmed by `demo3d.cpp` UI string `(Settings panel, modes 0/3/6)`.
- `10_phase5d_trilinear_upper_lookup.md` 8-neighbor conclusion is accurate.
- `09_current_code_map.md` file inventory is accurate for all named shaders and tools.

---

## One-line verdict

The series is accurate and up-to-date on all runtime defaults and debug workflow details. The one actionable correction is finding 5: "fused atlas EMA" is presented as a real implemented feature in both the jargon index and doc 11, but no implementation exists in the codebase — this should be reframed as a design note or future direction, not a current code path.
