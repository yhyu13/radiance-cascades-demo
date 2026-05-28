# v4 Phase 5 — Git Hygiene & Repository Finalization Plan

**Date:** 2026-05-28T18:30+08:00
**Predecessor:** v4 Phases 1-4 (all ShaderToy adoption work complete)
**Goal:** Commit the v4 work, organize untracked files, finalize repository state

---

## 1. State Analysis

From `build/s` (git status):

| Category | Files | Action |
|----------|-------|--------|
| **Staged** (new files from v3 era) | critic/*, stage docs, shaders | Leave staged — separate v3 commit batch |
| **Unstaged modified** | CMakeLists.txt, radiance_3d.comp, raymarch.frag, demo3d.cpp/h, main3d.cpp | Commit as v4 source changes |
| **Untracked — v4 docs** | doc/8_shadertoy/* (14 files) | Add + commit |
| **Untracked — v4 captures** | v4_phase3_sponza_pscene.* (8 files) | Add + commit |
| **Untracked — v4 tools** | verify_phase3.py, verify_phase4.py, analyzer changes | Add + commit |
| **Untracked — noise** | 1000+ PNGs, EXRs, RDC files, .kilo/, .wolf/ | .gitignore |

---

## 2. Step-by-Step

### Step 5A — Commit v4 Source Changes

```
git add CMakeLists.txt res/shaders/radiance_3d.comp \
        res/shaders/raymarch.frag src/demo3d.cpp src/demo3d.h \
        src/main3d.cpp
git commit -m "v4 Phase 1-2: per-scene MB-gain preset, remove stale M1 delta flags"
```

### Step 5B — Commit v4 Documentation & Tools

```
git add doc/8_shadertoy/
git add tools/v3_baseline/baseline_lock.json
git add tools/v3_baseline/analyze_baselines.py
git add tools/v3_baseline/verify_phase3.py
git add tools/v3_baseline/verify_phase4.py
git add v4_phase3_sponza_pscene.png v4_phase3_sponza_pscene_*_gi.exr \
        v4_phase3_sponza_pscene_pt_*.exr
git commit -m "v4 Phase 2-4: documentation, lock update, verified capture, analyzer threshold"
```

### Step 5C — Add .gitignore Entries

Append to `.gitignore`:
```
# v4 capture noise
*.png
*.exr
*.rdc
*.cap
!v4_phase3_sponza_pscene.png
!v4_phase3_sponza_pscene_cascade_gi.exr
!v4_phase3_sponza_pscene_pt_full.exr
!v4_phase3_sponza_pscene_pt_direct.exr
!v4_phase3_sponza_pscene_gbuffer.exr
!v4_phase3_sponza_pscene_probe_bin.exr
!v4_phase3_sponza_pscene_probe_contrib.exr
!v4_phase3_sponza_pscene_probe_diag.exr
tools/app_run*.log
tools/app_run*.err
tools/phase*_rdoc_extract.*
tools/frame_*.png
tools/frame_*.md
```

Commit the .gitignore:
```
git add .gitignore
git commit -m "v4 Phase 5C: gitignore for capture noise, keep verified Phase 3 files"
```

---

## 3. Self-Critique

### SC-P5.1: The .gitignore is aggressive — excludes ALL PNGs/EXRs

The project root has ~500 .png screenshots from v2.x captures. Adding `*.png` to .gitignore will untrack all of them. The `!v4_phase3_*` negations keep the Phase 3 verified capture. The v2.x captures are noise — they were never committed. If they need to be preserved, they should be under a separate directory or committed separately.

### SC-P5.2: The doc/7/ files are already staged (from v3 era)

The v3 stage docs, critic files, and scope doc are already in the index (staged). They should be committed as a separate batch before the v4 changes. The recommended commit order:
1. First commit: v3 staged files (critic + stage docs + scope doc)
2. Second commit: v4 source changes (Phase 1-2)
3. Third commit: v4 docs + tools + verified capture
4. Fourth commit: .gitignore

### SC-P5.3: The project has no .gitignore at root level

Need to check if .gitignore exists. If not, create it. If it exists, append to it.