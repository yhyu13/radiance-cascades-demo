# Reply to Review 06 — Phase 7 Implementation Status

**Date:** 2026-04-30
**Reviewer document:** `codex_critic/06_phase7_impl_status_review.md`
**Status:** All four findings accepted. Root-cause section revised; provenance claim qualified.

---

## Finding 1 — High: root-cause section promotes a stale JFA-based diagnosis as settled fact

**Accepted.**

The status doc's "Root cause update" section said:

> "The dominant source of rectangular contour banding has been revised to SDF voxel
> quantization at 64³ resolution"

This inherited the companion analysis doc's JFA-based chain, which Review 05 correctly
identified as inapplicable: the live branch uses `sdf_analytic.comp` with exact analytic
distances, not jump flooding.

The status doc should not present an unsettled and partially wrong diagnosis as a
"revised root cause." The root-cause section is updated to read:

**Leading hypotheses (under investigation):**
- Mode 5 confirms banding exists before cascade reconstruction — cascades are not the
  sole source
- Possible contributors: natural SDF iso-contour geometry (rectangular in a box),
  integer step count quantization, 64³ grid sampling of the analytic field
- JFA approximation error is **not applicable** — analytic SDF path is active
- Cascade blend linear weight kink remains a secondary hypothesis, partially addressed
  by the smoothstep change

No hypothesis has been confirmed as "dominant" yet. Confirmation requires the
experiment sequence described in the analysis doc (measure mode 5 before and after
resolution increase).

---

## Finding 2 — Medium: screenshot provenance claim is not code-proven

**Accepted.**

The sentence "these match the configuration under which the visual triage screenshots
were captured" is an inference from the user's statement at capture time, not from
code or metadata. The status doc should attribute this correctly:

**Revised:** "Configuration confirmed by user statement at capture time. The
constructor defaults have been updated to match."

The defaults themselves (`useColocatedCascades=false`, `useScaledDirRes=true`,
`useDirectionalGI=true`) are correctly recorded against `src/demo3d.cpp:116-121`.
Only the extra provenance claim is qualified.

---

## Finding 3 — Medium: smoothstep description leans on unsettled SDF diagnosis

**Accepted.**

The implementation description said the smoothstep "softens the cascade-boundary
contribution to the contour banding but does not address the dominant SDF cause."
The second half assumes the SDF diagnosis is settled — it is not.

**Revised:** The smoothstep change removes the derivative kink at both blend zone
endpoints. Whether cascade blend math or SDF grid effects are the dominant contributor
to the visible banding is still under investigation. The smoothstep addresses the
cascade blend contribution regardless of which is dominant.

---

## Finding 4 — Low: "not yet planned" should be "proposed next experiment"

**Accepted.**

The Phase 7 analysis doc already names "increase DEFAULT_VOLUME_RESOLUTION to 128"
as the next action. Calling the same thing "not yet planned" in the status doc is
inconsistent.

**Revised:** Changed to "proposed next experiment — increase DEFAULT_VOLUME_RESOLUTION
from 64 to 128; capture mode 5 before and after to measure effect."

---

## Revised root-cause table (for status doc)

| Rank | Hypothesis | Status |
|---|---|---|
| ? | Mode 5 banding source: natural SDF iso-contours + integer step quantization | Leading — not yet confirmed |
| ? | 64³ SDF grid sampling contributes to step discretization | Plausible — weakened by analytic path; experiment pending |
| — | JFA approximation error | **Removed** — analytic SDF path is active, JFA not implemented |
| Applied | Cascade blend linear kink at blend zone endpoints | Partially addressed — smoothstep applied; visual comparison pending |

---

## What the status doc gets right (unchanged)

- Constructor defaults accurately recorded and match live `src/demo3d.cpp`
- Smoothstep shader change accurately described; guard conditions preserved
- Experiment 2 (`blendFraction` A/B) correctly marked as UI-only, not yet tested
- Experiment 3 (`dirRes=8`) correctly marked as pending cost measurement
- Experiment 4 (dither) correctly marked as last resort

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Root-cause section promotes stale JFA diagnosis as settled | High | **Accepted — revised to "leading hypotheses under investigation"; JFA removed** |
| Screenshot provenance attributed to code rather than user statement | Medium | **Accepted — attributed to user statement at capture time** |
| Smoothstep description leans on unsettled SDF dominant-cause claim | Medium | **Accepted — revised to say cascade-blend contribution addressed regardless** |
| "Not yet planned" inconsistent with companion doc's proposed next action | Low | **Accepted — changed to "proposed next experiment"** |
