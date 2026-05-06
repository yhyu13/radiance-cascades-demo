# Reply to Review 17 — Phase 9b Implementation Detail Review

**Date:** 2026-05-02

All five findings accepted. One requires a code fix (Finding 1). Replies below.

---

## Finding 1 (High): `probeJitterIndex` is not a general rebuild counter — accepted, code fix required

The review is correct. `probeJitterIndex` only increments inside `if (useProbeJitter)`:

```cpp
if (useProbeJitter) {
    ...
    ++probeJitterIndex;
} else {
    ...
    probeJitterIndex = 0;
}
```

When temporal is ON but jitter is OFF, `probeJitterIndex` is always 0, so the UI shows
`Rebuilds: 0  Coverage: 0%` — which is actively misleading. History may be fully seeded
and accumulating, but the counter says zero.

**Root cause:** the counter was designed to serve dual duty (Halton index + rebuild counter)
but it is conditional on jitter, so it fails the rebuild-counter role.

**Fix: separate the two concerns.**

Add a dedicated `temporalRebuildCount` that increments every time `temporal_blend.comp`
actually fires — unconditionally, regardless of jitter. `probeJitterIndex` continues to
serve as the Halton sequence index only.

```cpp
// demo3d.h — Phase 9b section:
uint32_t temporalRebuildCount;  // increments every temporal blend dispatch, jitter or not

// demo3d.cpp constructor:
, temporalRebuildCount(0)

// update(), when temporal is newly enabled:
if (useTemporalAccum) {
    cascadeReady = false;
    historyNeedsSeed = true;
    probeJitterIndex = 0;
    temporalRebuildCount = 0;   // reset alongside Halton index
}

// updateSingleCascade(), after temporal blend dispatch fires:
// (only increment for one cascade per rebuild — use cascadeIndex == 0 as sentinel)
if (cascadeIndex == 0) ++temporalRebuildCount;

// UI: show temporalRebuildCount, not probeJitterIndex:
float coverage = 1.0f - std::pow(1.0f - temporalAlpha, (float)temporalRebuildCount);
ImGui::Text("Rebuilds: %u  Coverage: %.0f%%", temporalRebuildCount, coverage * 100.0f);
```

With this fix, `Rebuilds` and `Coverage` are honest when temporal is on and jitter is off.
`probeJitterIndex` remains the Halton index, displayed separately only when jitter is on.

---

## Finding 2 (Medium): Coverage formula tracks jitter-sample count, not EMA history state — accepted

The review is correct that even after the Finding 1 fix, `Coverage` is a settling
*heuristic*, not a true history-fill measurement. It estimates "how much of the final
converged EMA value is reflected in history" via `1 - (1-alpha)^N` — but it does not
account for:
- varying per-frame alpha (user can slide it mid-run)
- scene changes that partially invalidate history
- the actual content of history vs. its converged value

The label will be revised in the doc and UI tooltip to:

**Before:** "Coverage: %.0f%%"
**After:** "EMA fill: %.0f%% (heuristic)"

And the tooltip will clarify:

> `1 - (1-alpha)^N` — EMA settling heuristic. Counts how many blend steps have fired
> since temporal was last enabled, assuming constant alpha. Not a pixel-accurate readout.

This is a documentation/labeling fix, not a math fix.

---

## Finding 3 (Medium): Observability described too strongly — accepted

The review is correct. Step 1 added text readouts only. The doc used the word "debug
observability" which implies more than was delivered.

What Phase 9b actually added:
- `Rebuilds` counter (corrected to `temporalRebuildCount` per Finding 1)
- `Coverage` heuristic label
- `Jitter: (x, y, z)` text readout

What was NOT added and should be explicitly stated in the doc:
- No new render modes
- No history atlas or history grid viewer in the radiance debug panel
- No current-vs-history comparison or residual view
- No reset-history button

The doc section header "Debug observability" will be revised to "Debug text readouts"
and a "What this does NOT add" bullet list will be appended, following the pattern
established in the Phase 9 impl detail review reply.

---

## Finding 4 (Medium): D=8 angular step oversimplified — accepted as stated

The claim that D=4 gives "~45° angular steps" and D=8 gives "~22.5°" treats octahedral
bins as uniform solid-angle wedges, which they are not. The octahedral parameterization
stretches differently at poles vs. equator, so individual bins have varying angular extents.

The correct claim is:
- D=4 → 16 bins total, coarse directional quantization, visible color steps on matte faces
- D=8 → 64 bins total, finer quantization, steps reduced (but not to a simple 22.5°)
- D increase reduces directional banding — the magnitude is empirical, not cleanly predictable from bin count alone

The doc will drop the specific degree numbers and replace with: "D×D bins cover the full
sphere via octahedral mapping; doubling D quadruples bin count and substantially reduces
visible directional stepping, but the exact angular improvement depends on surface normal
and light direction geometry."

---

## Finding 5 (Low): Missing explicit "no new render mode" statement — accepted

The doc will be updated to include at the end of the debug observability section:

> **Phase 9b does NOT add:**
> - New render modes (render mode enum is unchanged)
> - History texture viewer in the radiance debug panel
> - Residual or error visualization

---

## Summary of actions

| Finding | Action | Type |
|---|---|---|
| 1: probeJitterIndex not a rebuild counter | Add `temporalRebuildCount`; fix UI to use it | Code + doc fix |
| 2: Coverage is a heuristic, not history-fill | Rename to "EMA fill (heuristic)" + tooltip clarification | Doc + UI label fix |
| 3: Observability overstated | Rename section; add "what was NOT added" bullet list | Doc fix |
| 4: D=8 angle oversimplified | Drop specific degree numbers; use qualitative language | Doc fix |
| 5: No explicit "no new render mode" | Add to doc | Doc fix |
