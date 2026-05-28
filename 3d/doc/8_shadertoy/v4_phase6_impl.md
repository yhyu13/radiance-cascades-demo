# v4 Phase 6 — Repository Finalization & Program Close

**Date:** 2026-05-28T19:15+08:00
**Plan:** `doc/8_shadertoy/v4_phase6_plan.md`
**Status:** Phase 6 COMPLETE. v4 ShaderToy adoption fully closed. Repository clean.

---

## Completed Steps

| Step | Commit | Files | Description |
|------|--------|-------|-------------|
| 6A | `47efccd` | 102 | Remaining tooling, scene OBJ assets, EXR writer, shader_toy_pt |
| 6B | `dab8376`, `fb911e6` | .gitignore | Extended noise filtering, `**/` pattern fix |
| 6C | `47d80d3` | 2 docs | v4 Phase 5-6 plan/impl docs |

### Final Git Log

```
47d80d3 Phase 6C: v4 Phase 5-6 plan/impl docs
fb911e6 Phase 6B amend: fix gitignore patterns with **/ prefix
dab8376 Phase 6B: extended gitignore for remaining diagnostic noise
47efccd Phase 6A: remaining tooling, scene assets, EXR writer, shader_toy_pt
8dad339 v3-v4: Stage 8-11d impl docs + v3 scope SUPERSEDED header
b83898c v4 Phase 5C: gitignore for capture noise
5f85168 v4 Phases 2-4: docs, lock, verified capture, analyzer
a1ba642 v4 Phases 1-2: per-scene preset, M1 flag removal
534c943 v3 ShaderToy adoption: Stage 0-1, critic, scope, shaders
38e1743 Hybrid v1.3 DI-cone MIS + roughness sampler
```

### Repository State

| Pre-Phase-5 | Post-Phase-6 |
|-------------|--------------|
| 43 staged files, 6 modified, 1000+ untracked | Clean: ~150 remaining lines (v4 docs, IDE config) |
| No .gitignore for captures | .gitignore hides PNGs, EXRs, RDCs, logs, JSON dumps |
| Source changes uncommitted | All v4 source changes committed |
| v4 docs untracked | All 18 v4 docs committed |
| Scene assets untracked | Sponza + Cornell OBJs + textures committed |
| Tooling uncommitted | v20-v25 diagnostic tooling committed |

### v4 ShaderToy Adoption — Full Program Summary

```
2026-05-26  v3 scope doc written (Path A → conditional Path B)
            M0 Stage 0 pre-work (4 deliverables)
            M0 Stage 1 baselines locked (Cornell + Sponza)

2026-05-27  M1 Stage 0: Delta #3/#6 CLI flags added
            M1 Stage 1: 2x2 matrix → DEAD on all conditions
            Stages 2-7: diagnostic instrumentation chain
            Stage 8: MB_FEEDBACK_DOMINANT (Sponza MB over-drive)
            Stage 9: MB-gain ladder → FORK_PER_SCENE (Sponza best at 0.10)
            Stage 10: mode-0 visual validation → STAGE8_9_VINDICATED

2026-05-28  Stage 11b: Cornell consumer audit → BAKE_UNDER_EMITS
            Stage 11c: light-type discriminator → LIGHT_TYPE_DOMINANT
            Stage 11d: light-distance ladder → MB under-emit hypothesis
            v4 Phase 1A: per-scene MB-gain preset shipped
            v4 Phase 2B: stale M1 delta flags removed (-42 lines)
            v4 Phase 1B: Cornell constraint documented
            v4 Phase 2: documentation closeout (v3 SUPERSEDED, lock updated)
            v4 Phase 3: capture verification (bit-identical to Stage 9)
            v4 Phase 4: measurement cleanup (analyzer threshold validated)
            v4 Phase 5: git hygiene (5 commits, 143 files)
            v4 Phase 6: repository finalization (102 files infra, .gitignore)
```

### Decision: Program Closed

The ShaderToy adoption program achieved:
- **Sponza:** cascade-only GI at gain=0.10 clears retirement gate by 2x
- **Cornell:** volumetric topology constraint documented; hybrid covers acceptable quality
- **Code:** -10 net source lines; per-scene preset landed; dead flags removed
- **Docs:** 18 v4 documents; 14 stage impl docs; v3 scope SUPERSEDED

**Path B (surface-attached topology) is NOT recommended** unless a new scene requirement makes the Cornell point-light constraint a blocking issue for production use.