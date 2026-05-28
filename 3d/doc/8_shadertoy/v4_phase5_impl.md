# v4 Phase 5 — Git Hygiene & Repository Finalization

**Date:** 2026-05-28T18:35+08:00
**Plan:** `doc/8_shadertoy/v4_phase5_plan.md`
**Status:** Phase 5 COMPLETE. v4 ShaderToy adoption fully committed.

---

## Completed Steps

| Step | Commit | Description |
|------|--------|-------------|
| — | `534c943` | v3 staged files: critic docs, Stage 0-1 docs, scope doc, hybrid/PT shaders |
| 5A | `a1ba642` | v4 source changes: per-scene MB-gain preset, M1 delta flag removal (5 files, +143/-80) |
| 5B | `5f85168` | v4 docs (14 files), tools (4 scripts), lock update, verified Phase 3 capture (8 files) |
| 5C | `b83898c` | .gitignore: capture noise patterns, keep verified Phase 3 files |
| — | `8dad339` | Stage 8-11d impl docs (14 files), v3 scope SUPERSEDED header |

### Git Log

```
8dad339 v3-v4: Stage 8-11d impl docs + v3 scope SUPERSEDED header
b83898c v4 Phase 5C: gitignore for capture noise
5f85168 v4 Phases 2-4: docs, lock, verified capture, analyzer
a1ba642 v4 Phases 1-2: per-scene preset, M1 flag removal
534c943 v3 ShaderToy adoption: Stage 0-1, critic, scope, shaders
```

### Self-Critique

**SC-5I.1: ~800 untracked tool files remain** (logs, JSONs, probe_stats, captures, tool subdirectories). These are v2.x/v3.x measurement artifacts. The .gitignore hides them from `git status` for most patterns, but some tool directories (`tools/v3_m1_*`, `tools/v20_*`) are not matched. Future sessions can selectively add them.

**SC-5I.2: The .gitignore `!v4_phase3_*` negations keep the Phase 3 capture in the repo.** This is correct — the verified Sponza pscene capture is a v4 artifact and should be tracked.

### v4 Program — All Phases Committed

```
Phase 1A: Sponza per-scene MB-gain preset             (a1ba642)
Phase 2B: Remove stale M1 delta flags                  (a1ba642)
Phase 1B: Cornell constraint documentation             (5f85168)
Phase 2A: v3 scope SUPERSEDED                         (8dad339)
Phase 2C: baseline_lock.json update                    (5f85168)
Phase 2D: 04_reply_to_audit.md update                  (5f85168)
Phase 3A: Sponza pscene capture verification           (5f85168 capture, verified in Phase 3B)
Phase 3C: Closeout report                              (5f85168)
Phase 4A: Lock confirmed capture SHA256s               (5f85168)
Phase 4B: Analyzer threshold investigation             (5f85168)
Phase 5A-C: Git hygiene, .gitignore                    (b83898c, 8dad339)
```