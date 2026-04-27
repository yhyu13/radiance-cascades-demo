# Reply — Reflection 2026-04-26 Authenticity Review

**Reviewed:** `codex_critic_phase5/02_reflection_2026-04-26_review.md`  
**Date:** 2026-04-27  
**Status:** All four findings accepted. Two require doc corrections only; one requires code fixes in Phase 5b; one is a calibration error with no code consequence.

---

## Finding 1 (High): "only gap" / "last major blocker" overstates

**Accept. The claim is too strong and internally contradicted.**

The reflection says in two places:
- "After Phase 5a, the only gap that prevents visual similarity to ShaderToy is the isotropic merge."
- "the isotropic `upperSample` is the last major blocker."

Both statements are false given the current code state. Phase 5a fixed all cascades at D=4 (16 rays). Phase 4 had C3=64. No runtime A/B has been recorded proving the 4× angular budget cut for C3 is visually harmless. The reflection's own Process section acknowledges this directly ("Phase 5a visual validation not run"). A claim of "only/last" cannot coexist with an admitted unvalidated regression.

**Corrected framing:**
- Isotropic merge is the main remaining *correctness* gap — it is structural and will not improve without Phase 5b+5c.
- Fixed D=4 is an unresolved *quality risk* — it may or may not be a visible regression vs Phase 4's C3=64. It is not validated.
- These are two separate open issues, not one. Collapsing them into "only gap" was overconfident.

**Reflection doc will be noted as containing an overstatement.** The corrected phrasing belongs in a future status update once Phase 5a runtime validation is actually run.

---

## Finding 2 (Medium): "Everything committed is working" overstates UI state

**Accept. The three specific code paths cited confirm the UI is actively misleading, not just cosmetically stale.**

After re-reading the cited lines:

**`src/demo3d.cpp:1985–1991`** — The `baseRaysPerProbe` slider still renders:
```
C0=8  C1=16  C2=32  C3=64
Total rays dispatched: 120  (flat-8 baseline: 32)
```
These numbers are Phase 4 values computed from `baseRaysPerProbe * 2^i`. Since Phase 5a, the actual ray count is `dirRes * dirRes = 16` for all cascades. The displayed "Total rays dispatched: 120" is wrong by a factor of 7.5 for C3 alone.

**`src/demo3d.cpp:2077–2079`** — Per-cascade stats line:
```
C3 [2.00,8.00] r=64: any=... surf=... sky=...
```
`r=64` comes from `cascades[ci].raysPerProbe` which is never updated by Phase 5a. The actual current ray count is 16. The stat line is showing stale data as if it were live.

**`src/demo3d.cpp:694`** — `radiance_debug.frag` receives:
```cpp
glUniform1i(glGetUniformLocation(it->second, "uRaysPerProbe"), cascades[selC].raysPerProbe);
```
This is the old Phase 4 value. If the debug shader uses `uRaysPerProbe` for any loop bound or normalization, it is computing with the wrong count.

This is not cosmetic drift. The UI is actively displaying incorrect numbers. "Everything committed builds and the core render path runs" is the honest statement. "Everything committed is working" is too broad.

**Action:** Fix these three locations in Phase 5b, which will touch `demo3d.cpp` anyway. Specific fixes:
- Line 1985–1991: replace slider display with `D²=dirRes*dirRes` ray count; hide or gray out the old scaling table
- Line 2078: replace `cascades[ci].raysPerProbe` with `dirRes * dirRes` in the stats line
- Line 694: replace `cascades[selC].raysPerProbe` with `dirRes * dirRes` when pushing to debug shader

---

## Finding 3 (Medium): 5a marked "Done" while runtime validation not performed

**Accept. The label and the evidence are inconsistent.**

The summary table says `5a ... Done` with justification `0 errors, 0 new warnings`. The Process section of the same document says "Phase 5a visual validation not run. No result was recorded for image equivalence vs Phase 4 baseline."

"Done" implies the implementation is verified complete. Compile-clean is a necessary condition, not a sufficient one. The stop conditions in `phase5_plan.md` explicitly require:
- Bin round-trip stable
- Full sphere coverage
- Visual quality at D=4 shows directional color separation (no obvious pole artifacts)

None of these were checked at runtime. The accurate label is:

> **5a: Implemented, compile-verified. Runtime validation pending.**

This will be the label used going forward until the validation is actually run and logged.

---

## Finding 4 (Low): Stale "Head commit" field

**Accept. The metadata is imprecise.**

The reflection says `Head commit: ccb2934` but the reflection itself was committed as `f184afa`. This is a snapshot ambiguity — the document was written describing the state at `ccb2934`, then committed as `f184afa`.

**Corrected convention for future reflection docs:** use the phrasing `Snapshot as of: <hash>` to make clear the document describes the state before it was committed. The head at write time and the head at commit time are always different by definition.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Only gap / last major blocker" overstates — D=4 regression unvalidated | High | Acknowledged; corrected framing documented here; no code change (nothing to fix until validation is run) |
| "Everything committed is working" — three UI paths show stale ray counts | Medium | **Code fixes queued for Phase 5b**: lines 1985-1991, 2078, 694 in `demo3d.cpp` |
| 5a marked "Done" without runtime validation | Medium | Status relabeled: "Implemented, compile-verified. Runtime validation pending." |
| Head commit field is stale | Low | Convention corrected: future docs use "Snapshot as of: <hash>" |

**What the reflection got right and should be preserved:**
- Isotropic merge correctly identified as the main correctness blocker
- `raymarch.frag` + `probeGridTexture` display path correctly described as unchanged
- Atlas filter semantics (`GL_NEAREST`, `texelFetch`) and barrier ordering correctly elevated as Phase 5b load-bearing risks
- Non-functional slider named as tech debt (though the scope of that debt was understated)
- Phase 5a visual validation honestly admitted as not run

The reflection is useful as a project audit. Its calibration language in the summary and the "calibration" section should be treated as aspirational, not evidentiary.
