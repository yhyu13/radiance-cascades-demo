# Critique Summary — codex_plan/class

**Date:** 2026-04-30
**Reviewer:** Claude (Sonnet 4.6)
**Scope:** All 11 class docs in `doc/codex_plan/class/`
**Live code references:** `res/shaders/radiance_debug.frag`, `res/shaders/raymarch.frag`,
`src/demo3d.cpp`, `res/shaders/radiance_3d.comp`

---

## Findings

| # | Doc | Severity | Issue |
|---|---|---|---|
| 1 | `00_jargon_index.md` | **High** | Mode 3/4/5/6 definitions describe `radiance_debug.frag` overlay modes but are not labelled as such — conflicts with `raymarch.frag` `uRenderMode` 3/4/5/6 which have completely different meanings |
| 2 | `00_jargon_index.md` | Medium | `uRenderMode` values 1-5 (normals, depth, indirect×5, direct-only, step heatmap) are not defined anywhere in the jargon index |
| 3 | `09_current_code_map.md` | Low | Title header reads `# 08 Current Code Map` but filename is `09_current_code_map.md` |
| 4 | `10_phase5d_trilinear_upper_lookup.md` | Low | Title header reads `# 09 Phase 5d` but filename is `10_phase5d_trilinear_upper_lookup.md` |
| 5 | `06_phase5_directional_atlas.md` | Low | "Phase 5 debug views" section lists modes (atlas raw, hit-type heatmap, nearest-bin, bilinear) without clarifying these are the `radiance_debug.frag` overlay modes, not `uRenderMode` values — inherits the issue from finding 1 |

---

## Strong points (verified against live code)

- Doc 07 D-scaling values (`C0=D4, C1=D8, C2=D16, C3=D16`) are correct — confirmed by
  `demo3d.cpp:451` log string and `initCascades()` at line 1530.
- Doc 07 correctly states D=2 was dropped as degenerate — confirmed by `demo3d.cpp:1528-1529`
  comment ("D=2 is degenerate: all 4 bin centers land on the octahedral equatorial fold (z=0)").
- Doc 10 conclusion ("current latest branch already does 8-neighbor trilinear spatial blend
  for the non-co-located directional path") is accurate — `sampleUpperDirTrilinear()` is
  implemented in `radiance_3d.comp` and `uUpperToCurrentScale=2` activates it.
- Doc 08 description of Phase 5g (cosine-weighted hemisphere integral over C0 atlas,
  8-probe trilinear) and 5h (normal-offset shadow ray) is accurate.
- Doc 04 cascade intervals (C0: 0.02-0.125 m, C1: 0.125-0.5 m, C2: 0.5-2.0 m,
  C3: 2.0-8.0 m) are correct.
- The overall narrative arc (Phases 1→5, bake-side vs renderer-side upgrades) is accurate
  and well-structured.

---

## One-line verdict

The class series is conceptually sound and factually accurate in all Phase 3-10 content. The
single highest-priority fix is the mode-numbering collision in the jargon index, which will
mislead any reader who tries to cross-reference mode numbers between the class docs and the
actual shaders.
