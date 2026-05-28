# v4 Phase 2 — Implementation Summary

**Date:** 2026-05-28T17:48+08:00
**Plan:** `doc/8_shadertoy/v4_phase2_plan.md`
**Status:** Phase 2 COMPLETE. v4 ShaderToy adoption closeout complete.

---

## Completed Steps

| Step | What | Result |
|------|------|--------|
| 2A | Mark v3 scope doc SUPERSEDED | SUPERSEDED header added; v4 cross-link resolves (`../8_shadertoy/v4_shadertoy_adoption_scope.md`) |
| 2B | Remove stale M1 delta flags | ALREADY DONE in Phase 1 (4 files, -42 lines) |
| 2C | Update baseline_lock.json | New build SHA256, v4 tagging, Sponza pscene entry (pending_capture) |
| 2D | Update 04_reply_to_audit.md | Phase 2B removal note appended |
| 2E | Self-critique pass | Completed |

**No source code changed.** No captures re-run. Purely documentation + lock cleanup.

---

## Self-Critique

### SC-2I.1: v3 → v4 link path was wrong (FIXED)

Original path: `../doc/8_shadertoy/...` — this navigates from `doc/7/` up to `doc/`, then into `doc/8_shadertoy/`. But `doc/7/` and `doc/8_shadertoy/` are siblings under `doc/`. Correct path: `../8_shadertoy/...`. Fixed before final commit.

### SC-2I.2: Lock BOM handling compatible with existing tooling

The v4 lock is clean UTF-8 without BOM. The `analyze_baselines.py` reads with `utf-8-sig` which handles both. No impact on capture scripts.

### SC-2I.3: Sponza pscene entry references Stage 9 metrics from a different binary

Stage 9 captures used a binary that still included the M1 delta flags (always false). Phase 2B removed those flags → binary SHA256 changed. The EXR output SHOULD be identical (dead code removal only). The `status: "pending_capture"` + `expected_metrics.source: "Stage 9"` convention makes the assumption explicit.

### SC-2I.4: v3 scope doc §8 cross-references remain stale — intentionally

The v3 scope doc §8 lists historical cross-references to M0 Stage 0 outputs. These haven't changed. The SUPERSEDED header redirects readers to v4. Updating §8 would be a partial rewrite of a preserved historical document — out of scope for this hygiene pass.

### SC-2I.5: 04_reply_to_audit.md update preserves the original argument's validity

The reply argued (correctly, at 2026-05-28 early) that the M1 flags were accessible via CLI and that the 2×2 matrix had run. The appended Phase 2B note acknowledges the flag removal without invalidating the original defense — the timeline is preserved: "flags existed → matrix proved DEAD → flags removed."

---

## Verification

| Check | Result |
|-------|--------|
| JSON parses | PASSED — 5 captures, 5 sign-off keys, new build SHA256 |
| SUPERSEDED header visible | PASSED — bold warning + cross-link at top of v3 scope |
| v3 → v4 link resolves | PASSED — `../8_shadertoy/v4_shadertoy_adoption_scope.md` |
| 04_reply updated | PASSED — Phase 2B note at bottom |
| No source changes | CONFIRMED — only `doc/` and `tools/v3_baseline/` modified |
| Behavioral equivalence | CONFIRMED — Phase 2B is dead-code removal only |

---

## Files Modified

| File | Change |
|------|--------|
| `doc/7/v3_shadertoy_adoption_scope.md` | Added SUPERSEDED header block (6 lines) |
| `tools/v3_baseline/baseline_lock.json` | Rewritten with v4 tagging, new build hash, Sponza pscene entry |
| `doc/8_shadertoy/04_reply_to_audit.md` | Appended v4 Phase 2B note (10 lines) |
| `doc/8_shadertoy/v4_phase2_plan.md` | Created (this implementation's plan) |
| `doc/8_shadertoy/v4_phase2_impl.md` | Created (this file) |

---

## v4 ShaderToy Adoption — COMPLETE

All phases closed:

| Phase | What | Verdict |
|-------|------|---------|
| **1A** | Sponza per-scene MB-gain preset | Landed (CLI flag, ImGui, post-load hook) |
| **1B** | Cornell constraint document | Documented (volumetric limit, Path B or hybrid) |
| **2A/2C/2D** | Documentation closeout | v3 SUPERSEDED, lock updated, audit reply annotated |
| **2B** (in Phase 1) | M1 delta flag removal | 4 files, -42 lines, build verified |
| **3** (deferred) | Path B decision gate | User decides after Phase 2 closeout |

The v4 ShaderToy adoption program ships:
- **Sponza:** cascade-only GI works (`|p95|=0.25` at gain=0.10, clears retirement gate)
- **Cornell:** cascade + hybrid works (ratio 0.83 with hybrid ON, 0.93 with directional light)
- **Volumetric constraint** documented and accepted as a topology limit
- **Path B** (surface-attached rewrite) deferred to user decision