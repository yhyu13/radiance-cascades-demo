# Reply to Review 30 — `phase6b_renderdoc_ai_plan.md` (Revisit)

**Date:** 2026-05-03  
**Review:** `30_phase6b_renderdoc_ai_plan_review_2.md`

All five findings accepted. The through-line is the same: the plan language has been
progressively updated but the framing has not kept up with (a) what is actually built,
(b) what end-of-frame snapshots can and cannot diagnose, (c) the scope growth introduced
by the GPU timing section, and (d) the stronger existing baseline. Each is addressed below
with the corresponding doc correction.

---

## Finding 1 — Still a forward design spec, not close to implementation (accepted, doc corrected)

The review is correct that none of the RenderDoc runtime infrastructure exists in the
codebase: no `renderdoc_app.h`, no `initRenderDoc()`, no capture lifecycle, no Python
script, no G-key path. The README entry already marks this "NOT YET IMPLEMENTED", but
the plan document itself reads as if implementation is imminent. It is not.

**Doc change:** Status line added to the plan header:

> **Status:** NOT YET IMPLEMENTED — design spec only. No C++ or Python code has been
> written for this phase. All code blocks are design intent, not deployed code.

This is the same pattern used in Phase 14b before it was implemented. Readers should not
need to cross-reference the README to know the implementation state.

---

## Finding 2 — Resource snapshot diagnostic reach still oversold (accepted, doc corrected)

The current limitation note correctly says this is resource snapshot analysis, not
event walking. But the opening Problem section still frames Phase 6b as enabling
"automated diagnosis of most pipeline faults." That framing does not follow from
end-of-frame state alone.

Bugs invisible to this approach:
- Transient intermediate corruption fixed by a later pass (e.g., a cascade bake writes
  garbage but the temporal blend overwrites it before end-of-frame)
- Pass-ordering errors where the wrong pass runs first (the final texture is correct
  but produced by the wrong route)
- Frame-N state that was correct at end-of-frame but wrong earlier (the sequence capture
  tool catches this better than a single-frame snapshot)

**Doc change:** The Problem section goal is narrowed:

> ~~"enables automated diagnosis of most pipeline faults"~~  
> → "catches **end-of-frame resource state errors**: wrong atlas content, missing SDF
> geometry, reduction output mismatch. It does **not** catch transient pass-internal
> corruption or pass-ordering bugs — those require event walking (future scope)."

The Limitation section is also expanded to explicitly call out the three invisible bug
classes above rather than just noting the snapshot vs. event-walk distinction abstractly.

---

## Finding 3 — GPU timing is scope expansion, should be named as such (accepted, doc corrected)

The GPU timing section added during Phase 14c was introduced as a natural extension but
treated in the doc as a minor addition. In practice it adds:
- a full recursive action walk
- per-event duration collection
- pass keyword matching
- a `format_perf_table()` function
- a `glObjectLabel(GL_PROGRAM, ...)` labeling requirement separate from texture labels

That is not a "minor addition." Combined with the resource snapshot and final-image
analysis, Phase 6b is now a multi-component GPU analysis framework, not the original
"press G, snapshot a few textures" diagnostic helper.

**Doc change:** Goal section updated:

> ~~"Press G → one GPU frame captured → Python extracts key textures and sends to Claude"~~  
> → "Phase 6b is a **two-component GPU analysis tool**: (A) resource snapshot — extract
> named 3D textures at end-of-frame for AI visual inspection; (B) GPU performance timing —
> walk all dispatches and draws, report per-pass GPU cost in µs. Component B was added
> during Phase 14c to address the sub-ms timing unreliability of the app-side
> `cascadeTimeMs` counter. The combined tool is substantially larger than the original
> snapshot helper."

A note is added that Component B (GPU timing) is the primary Phase 14c validation
deliverable and Component A (texture snapshot) is the original Phase 6b deliverable.
Implementers can ship Component A first and add Component B separately.

---

## Finding 4 — GL labeling is a maintenance burden, not just a prerequisite (accepted, doc corrected)

The plan treats labeling as a one-time prerequisite: "add glObjectLabel calls, then the
automation works." In practice, labeling is an ongoing synchronization requirement:

- renaming a shader file breaks the keyword match silently (timing rows revert to
  "Dispatch N")
- adding a new cascade level without a corresponding label produces an unlabeled row
- refactoring the shader loader may move the link point where labels must be applied
- the label strings in PASS_KEYWORDS and the glObjectLabel calls must stay in sync
  manually across refactors — there is no compile-time enforcement

If that discipline slips, the automation degrades without any explicit error: timing
rows appear with generic names, resource lookups silently return "Resource not found",
and the output looks plausible but is no longer trustworthy.

**Doc change:** "Prerequisites" section renamed "Labeling requirements and maintenance
cost." Added paragraph:

> Labels must be maintained across refactors. If a shader is renamed, the `PASS_KEYWORDS`
> dict in the Python script must be updated in the same commit. The Python script should
> print an explicit warning when a dispatch matches no keyword and falls back to the
> generic name, so labeling drift is visible rather than silent.

A concrete action item added: add a `[6b WARNING] Dispatch N unnamed` print in the
script when keyword matching fails.

---

## Finding 5 — Stronger baseline raises the bar for Phase 6b justification (accepted, doc corrected)

Since the original Phase 6b review the repo gained:
- single-image capture + AI analysis (6a)
- 3-mode burst capture with stats (12b)
- multi-frame sequence capture with temporal stability rating (14a)
- per-cascade probe statistics in JSON (12a/14c)

These cover a large fraction of what Phase 6b originally claimed. The remaining unique
value is narrow:

1. **Per-cascade GPU timing at µs precision** — the app-side `cascadeTimeMs` is
   unreliable below ~1ms; RenderDoc timestamps are not. This is the Phase 14c open
   question that probe_stats cannot answer.
2. **Pipeline-internal texture state** — e.g., inspecting whether the C1 probe atlas
   has populated directional bins across the full probe grid, not just the aggregated
   surfPct metric.
3. **Hypothesis validation that image-space cannot settle** — e.g., "is the atlas write
   for open-air probes writing zero or sky-sentinel alpha?" cannot be determined from the
   final rendered frame.

**Doc change:** "Goal" and "Problem" sections rewritten around these three specific
capabilities. The broad "automated diagnosis of pipeline faults" framing is dropped in
favour of:

> "Phase 6b addresses three gaps the current tooling cannot fill: (1) reliable per-pass
> GPU timing, (2) pipeline-internal texture inspection, (3) atlas-level hypothesis
> validation. It does not replace or subsume the sequence capture tool."

---

## Summary of doc changes

| Section | Change |
|---|---|
| Header | Status: NOT YET IMPLEMENTED added |
| Goal / Problem | Narrowed from "automated diagnosis" to three specific capability gaps |
| Limitation | Expanded: three invisible bug classes named explicitly |
| Goal | Scope growth acknowledged; Component A vs. B split made explicit |
| Prerequisites | Renamed to "Labeling requirements and maintenance cost"; ongoing burden described |
| Python script | Warning print added when dispatch falls back to generic name |

No code changes — Phase 6b remains unimplemented. Doc corrections only.
