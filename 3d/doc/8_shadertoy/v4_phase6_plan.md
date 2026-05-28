# Phase 6 — Repository Finalization & Project Retrospective Plan

**Date:** 2026-05-28T19:10+08:00
**Predecessor:** v4 Phase 5 (git hygiene, all source/docs committed)
**Status:** The ShaderToy adoption program is closed. Path B is deferred. This phase finalizes the repository and documents the program's endpoint.

---

## 1. Honest Assessment

All executable v4 phases (1A, 2B, 3-5) are shipped and committed. The only remaining v4 scope item is Phase 3 (deferred): Path B — surface-attached topology rewrite. The v4 closeout report recommends against it:

> *"Do NOT proceed to Path B unless a new scene requirement makes the Cornell point-light constraint a blocking issue. The current solution (cascade for open/directional + hybrid for enclosed/point) covers all tested configurations at acceptable quality."*

There is no other "next phase" within the ShaderToy adoption program. The program is **closed**.

---

## 2. Remaining Untracked Files

From `build/s` (git status), ~800 untracked files remain in `tools/`:
- `tools/v3_m1_*/` — Stage 8-11d capture tooling (scripts, JSONs, EXRs)
- `tools/v20_*/` — v2.x convergence measurement tooling
- `tools/phase*` — Phase 2.x diagnostic logs and metrics
- `tools/probe_stats_*` — Probe attribution JSONs
- `res/scene/` — Scene OBJ files and textures
- `lib/tinyexr/`, `include/exr_writer.h`, `src/exr_writer.cpp` — EXR writer infrastructure

These are historical artifacts. They should either be:
a) Committed as project infrastructure (tooling, scene assets, EXR writer)
b) Added to .gitignore (logs, JSON dumps, captures)
c) Deleted (if truly obsolete)

---

## 3. Steps

### Step 6A — Commit Remaining Infrastructure

```
# Scene assets and EXR writer (small, needed for any future capture)
git add res/scene/ lib/tinyexr/ include/exr_writer.h src/exr_writer.cpp

# Stage 8-11d capture tooling (scripts + small JSONs, exclude large EXRs)
git add tools/v3_m1_*/*.ps1 tools/v3_m1_*/*.py tools/v3_m1_*/*.json
git add tools/v20_convergence/*.ps1 tools/v20_convergence/*.py
git add tools/v20_pre_measurement/
git add tools/v3_baseline/*.ps1 tools/v3_baseline/sponza_*_metrics.json

git commit -m "Phase 6A: remaining tooling, scene assets, EXR writer infrastructure"
```

### Step 6B — Extend .gitignore for Remaining Noise

```
# Append to .gitignore
tools/*.log
tools/*.err  
tools/phase*.json
tools/probe_stats_*.json
tools/temporal_*.log
tools/rdoc_*.log
tools/captures/
tools/hybrid_validation/
tools/__pycache__/
tools/.env

git add .gitignore
git commit -m "Phase 6B: extended gitignore for remaining diagnostic logs and JSON dumps"
```

### Step 6C — Write Project Retrospective

One-page summary of the entire ShaderToy adoption effort (v2.0 → v4):
- What was attempted (31-commit v2.x correction, v3 delta port, v4 diagnostic chain)
- What was learned (volumetric constraint, MB-gain scene asymmetry, ShaderToy topology mismatch)
- What was shipped (per-scene preset, dead code removal, 14 docs)
- What remains open (Path B, Cornell point-light, valid mask)
- Recommendation: close the program, move to new work

---

## 4. Self-Critique

### SC-P6.1: This "phase" is a formality

The program is already closed. Committing remaining untracked files and writing a retrospective is cleanup, not new work. But it's the honest next step — there's nothing else to do within the ShaderToy adoption scope.

### SC-P6.2: Scene assets are large

`res/scene/` contains Sponza OBJs (~23MB) and Cornell OBJs. Committing them to git adds ~50MB to the repo. If bandwidth is a concern, scene assets should be in a submodule or LFS. For now, they're small enough to commit directly.

### SC-P6.3: No user decision to make

Path B is the only open item and the recommendation is "don't do it." Unless the user overrides this, the ShaderToy adoption program ends here. The retrospective should make this explicit so the next person reading the docs knows the program concluded, not stalled.