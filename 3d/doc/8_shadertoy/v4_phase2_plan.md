# v4 Phase 2 — Cleanup & Closeout Plan

**Date:** 2026-05-28  
**Scope doc:** `doc/8_shadertoy/v4_shadertoy_adoption_scope.md`  
**Predecessor:** `doc/8_shadertoy/v4_phase1_impl.md` (Phase 1 complete)  
**Goal:** clean up stale documentation, update baseline lock, close v4 ShaderToy adoption narrative

---

## 1. What Phase 2 Does (And Does NOT Do)

Phase 2 is a **documentation hygiene pass** + **lock update**. No engine source changes. No shader changes. No capture re-runs. The behavioral correctness of the cascade pipeline is unchanged from Phase 1.

| Step | What | Lines changed | Risk |
|------|------|--------------|------|
| 2A | Mark v3 scope doc SUPERSEDED, add cross-ref header | ~5 added | None |
| 2B | (SKIPPED — already done in Phase 1) | — | — |
| 2C | Update baseline_lock.json: v4 tag, new build hash, Sponza gain=0.10 preset entry | ~30 changed | Low (JSON update only) |
| 2D | Update 04_reply_to_audit.md: note Phase 2B flag removal | ~5 appended | None |
| 2E | Self-critique pass on Phase 2 changes | — | — |

---

## 2. Step-by-Step

### Step 2A — Mark v3 Scope Doc SUPERSEDED

**File:** `doc/7/v3_shadertoy_adoption_scope.md`

Prepend a SUPERSEDED header block immediately after the title line:

```markdown
# v3 — ShaderToy RC adoption: scope, milestones, gates

> ⚠️ **SUPERSEDED by [v4 ShaderToy Adoption Scope](../../doc/8_shadertoy/v4_shadertoy_adoption_scope.md)**
> **Date superseded:** 2026-05-28
> **Why:** The v3 M1 plan (port Deltas #3/#6/#4) was abandoned after the 2×2 matrix
> returned DEAD on 2026-05-27. The subsequent 11-stage diagnostic chain (Stages 2–11d)
> found the real constraints: Sponza MB-gain=0.10, Cornell point-light topology limit.
> The v4 plan documents the actual findings and the new scope.
> **This document is preserved for historical reference only.** Read v4 for the
> current plan.
```

**Do NOT delete or modify any other content in the file** — the v3 scope doc is the historical record of how the pivot was originally conceived. The SUPERSEDED header makes it clear this is not the current plan.

### Step 2C — Update baseline_lock.json

**File:** `tools/v3_baseline/baseline_lock.json`

Changes:

1. **Update `_note`:** from `"M0 Stage 1 baseline lock for v3 ShaderToy adoption"` → `"v4 ShaderToy adoption Phase 2 closeout lock"`
2. **Update `_locked_at`:** to current timestamp
3. **Update `_engine_build.exe`:** new SHA256 after Phase 1/2B build (the exe changed because M1 delta flags were removed from C++)
4. **Update `_engine_build.build_id`:** from `"3d_v3.0 + EXR capture plumbing..."` → `"3d_v4.0 Phase 2 closeout (M1 delta flags removed; per-scene MB-gain preset added)"`
5. **Add new capture entry:** `sponza_cam0_cascade_off_g010_pscene` — the Sponza gain=0.10 preset baseline. Status: `"planned"` (not yet captured — differs from Phase 1 because we want a clean lock entry pointing to the expected config)
6. **Add `_supersedes` field:** pointing to the v3 lock note

**Self-critique catch (SC-P2.1):** The `_engine_build.exe.sha256` CHANGED from Phase 2B because the M1 delta flag removal removed dead code from `demo3d.cpp` and `main3d.cpp`. The compiled binary is different. The lock should reflect this. However, the behavioral output is identical since the removed code paths were always `false`. The SHA256 change is a binary size/content change, not a behavioral one.

**Self-critique catch (SC-P2.2):** The Sponza gain=0.10 preset entry should NOT have a SHA256 for its EXRs — we have Stage 9 metrics (|p95|=0.25, ratio=1.04) but those were captured with `--multi-bounce-gain=0.10`, not with `--mb-gain-per-scene`. The two CLI paths produce the same gain value (0.10), so the EXR output should be identical, but we should re-capture to confirm. Mark the entry as `"status": "pending_capture"` rather than `"complete"` if we can't confirm.

### Step 2D — Update 04_reply_to_audit.md

**File:** `doc/8_shadertoy/04_reply_to_audit.md`

Append a note at the bottom acknowledging Phase 2B:

```markdown
## v4 Phase 2B update (2026-05-28)

Phase 2B of v4 removed the `--m1-delta3-gated-trilinear` and `--m1-delta6-geometric-cone`
CLI flags discussed above. The flags were DEAD per the 2×2 matrix verdict (see §F1 above),
and removing them simplifies the merge formula in `radiance_3d.comp` (eliminates the
ternary branch and `aFactor` guard condition). The removed flags had served their purpose
— they proved the ShaderToy #3/#6 delta port does not work in the volumetric topology.

The capture scripts in `tools/v3_m1_delta36/` that referenced these flags are preserved
as-is (they capture a specific historical configuration). New capture scripts for v4
should use `--mb-gain-per-scene` instead.
```

### Step 2E — Self-Critique Pass

After all changes, re-read each modified file to verify:
- No broken links (check that `../../doc/8_shadertoy/v4_shadertoy_adoption_scope.md` resolves)
- No stale wording ("v3", "M1 Stage 1" without qualification)
- The SUPERSEDED header is visible and unambiguous

---

## 3. Self-Critique on the Plan (Before Implementation)

### SC-P2.1: The lock SHA256 will change from flag removal but shouldn't

The M1 delta flag removal in Phase 2B changed source files (`demo3d.cpp`, `main3d.cpp`) → compiled binary changed → SHA256 changed. But the flags were ALWAYS FALSE, so the behavioral output is identical. The SHA256 mismatch between the old lock and the new build is expected and correct — the binary DID change.

**Improvement:** Add a `_behavioral_equivalence` note in the lock stating that the Phase 2B binary produces bit-identical EXR output to the Phase 1/pre-Phase-2B binary for the same CLI flags (M1 delta flags were always OFF). This prevents future confusion if someone compares old EXRs against the new binary and finds the lock SHA256 doesn't match.

### SC-P2.2: Sponza gain=0.10 entry should be "pending" not "complete"

We have Stage 9 metrics for Sponza at gain=0.10, captured with `--multi-bounce-gain=0.10`. The Phase 1A per-scene preset uses the same gain value but a different CLI flag (`--mb-gain-per-scene`). The EXR output SHOULD be identical (same gain, same scene, same seed), but we haven't confirmed this with a capture. Better to mark `"status": "pending_capture"` and note the expected metrics from Stage 9.

### SC-P2.3: The cerebrum.md shouldn't be manually updated

`cerebrum.md` is an OpenWolf-managed file. Per CLAUDE.md, OpenWolf hooks auto-append learnings. I should NOT manually edit it. The Phase 1 learnings (per-scene gain, volumetric constraint) are already documented in `v4_shadertoy_adoption_scope.md` §6. OpenWolf will pick them up on its next scan.

**Decision:** Do not touch cerebrum.md. The v4 scope doc §6 is the canonical source for the learnings.

### SC-P2.4: The 04_reply_to_audit.md update is now partially self-contradictory

Phase 2B removed the flags this doc defended. The appended note (Step 2D) explains why — the flags were DEAD, removal simplifies the code. This creates a document that argues for the flags' existence AND acknowledges their removal. This is acceptable — the reply was written at a specific point in time (before Phase 2B) and the appended note adds the post-Phase-2B context.

### SC-P2.5: Missing step — update the v3 scope doc's §8 cross-references

The v3 scope doc §8 lists "On disk:" cross-references to other docs. These are still valid as historical references. The SUPERSEDED header already redirects readers to v4. Updating §8 would be scope creep (Phase 2 is documentation hygiene, not a full rewrite of the v3 doc). Leave §8 as-is — it's part of the preserved historical record.

---

## 4. Acceptance Gates

| Check | Method | Expected |
|-------|--------|----------|
| v3 scope doc header | Read the file | SUPERSEDED block visible immediately after title |
| v3 → v4 link resolves | Click / grep | `../../doc/8_shadertoy/v4_shadertoy_adoption_scope.md` exists |
| lock.json parses | `python -c "import json; json.load(open('...'))"` | No parse errors |
| lock SHA256 is new | Compare with git diff | Different from Phase 1 lock |
| 04_reply updated | Read the file | Phase 2B note at bottom |
| No source changes | `git diff -- src/ res/` | Empty (only doc + lock changed) |

---

## 5. Rollback

If any step introduces a broken link or corrupts a file:
1. Revert that file to pre-Phase-2 state via git
2. Fix the issue in the plan
3. Re-apply

No code is changed in Phase 2. Rollback is simple git revert.