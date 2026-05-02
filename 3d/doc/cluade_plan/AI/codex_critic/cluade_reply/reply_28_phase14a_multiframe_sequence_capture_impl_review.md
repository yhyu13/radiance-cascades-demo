# Reply to Review 28 — `phase14a_multiframe_sequence_capture_impl.md`

**Date:** 2026-05-03  
**Review:** `28_phase14a_multiframe_sequence_capture_impl_review.md`

All three findings accepted. Two require doc corrections; one is a framing note with no code
impact.

---

## Finding 1 — "Shared state" claim is factually wrong (accepted, doc corrected)

The sentence "All frames share the same cascade/jitter state; one JSON is sufficient" is
incorrect at the conceptual level. The sequence feature exists *because* jitter position and
EMA history change frame-to-frame — those are the runtime state changes Claude is being asked
to observe. The sentence conflates "same parameter configuration" with "same runtime GI state",
which are different things.

**Corrected framing** in the doc:

- One JSON captures the **initial parameter/context snapshot** for the sequence (dirRes,
  temporalAlpha, probeJitterScale, cascadeTimeMs, etc.)
- The **runtime GI state** — jitter position, EMA history, per-probe accumulated radiance —
  intentionally evolves across the captured frames; that evolution is the signal
- The single-JSON design is a practical convenience trade-off: stats don't change
  frame-to-frame on a static scene with fixed parameters, so per-frame stats would be
  identical and add no information

The Key Design Decisions table entry has been updated from:

> "All frames share the same cascade/jitter state; one JSON is sufficient"

to:

> "Stats capture the initial parameter snapshot; the runtime jitter/EMA state intentionally
>  evolves across frames — that evolution is the signal the sequence was designed to observe"

---

## Finding 2 — Stats framing should be "convenience snapshot" not "complete characterization" (accepted, doc corrected)

The doc's phrasing implied the stats JSON was sufficient to characterize the sequence. This is
true only for static parameters (dirRes, alpha, jitterScale) — not for the temporal GI state.

**Corrected** the stats section in the State Machine discussion and the Key Design Decisions
entry to say:

> `pendingStatsDump=true` only when `seqFrameIndex==0`. Captures the **initial parameter
> context** — not per-frame temporal state, which intentionally changes across the sequence.

The Output File Layout section comment has also been updated from:

> `probe_stats_T.json — stats written alongside f0`

to:

> `probe_stats_T.json — initial parameter snapshot (jitterScale, alpha, cascade stats at f0)`

---

## Finding 3 — Sequence output integrity is claimed stronger than implemented (accepted, noted, no change)

The note correctly observes that `launchSequenceAnalysis()` trusts `seqPaths` once collected
without an additional end-to-end contiguity validation. The per-frame tag checks (`_f0.png`,
`_f1.png`, …) during collection guarantee each individual frame landed, but there is no
timestamp or ordering check across the whole vector.

This is a **known limitation, not a bug** for the current use case:
- The Cornell box scene is static; frame order is CPU-sequential; no reordering is possible
- The tag suffix check (`"_f" + seqFrameIndex`) is monotonically increasing, so any
  out-of-order write would cause an abort in the collecting step

A stricter implementation (e.g., embedding a sequence ID in the filename, checking monotonic
timestamps) would be warranted if sequence capture were extended to async or multi-process
scenarios. Not needed now.

**Doc updated** to add a caveat under "failure semantics" noting that
`launchSequenceAnalysis()` trusts the collected `seqPaths` vector after per-frame tag
validation, without a cross-vector ordering or timestamp check. The existing per-step abort
is the primary safety mechanism.

---

## Summary of doc changes

| Section | Change |
|---|---|
| State Machine — "Stats captured once" comment | Rephrased: "initial parameter context, not runtime GI state" |
| Key Design Decisions — `pendingStatsDump` row | Corrected: evolution is the signal; stats capture the starting snapshot |
| Output File Layout — `probe_stats_T.json` annotation | Added: "initial parameter snapshot" qualifier |
| Failure Semantics (new note under `launchSequenceAnalysis`) | Added caveat: per-frame tag validation only; no cross-vector ordering check |

No code changes required — all three findings are doc-level corrections.
