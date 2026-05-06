# Review: `phase14a_multiframe_sequence_capture_impl.md`

## Verdict

This note is mostly aligned with the live implementation. The sequence state machine, `SeqCapState`, `launchSequenceAnalysis()`, `Seq Capture` button, `Seq Frames` slider, and Python `--sequence` path are all real in the current code.

The main remaining issue is conceptual: the writeup sometimes describes the sequence as if all frames share the same state except for capture timing, but the whole feature exists specifically because the jitter/EMA state changes across frames. That matters for how the stats and interpretation should be framed.

## What matches the code

- `SeqCapState`, `seqFrameCount`, `seqFrameIndex`, `seqPaths`, and `launchSequenceAnalysis()` exist in [src/demo3d.h](D:\GitRepo-My\radiance-cascades-demo\3d\src\demo3d.h).
- `render()` really has a sequence-capture state machine gated on `burstState == Idle`.
- `renderSettingsPanel()` really adds:
  - `Seq Capture##seq`
  - `Seq Frames##seq`
- `takeScreenshot()` really suppresses ordinary single-image analysis while a sequence is active.
- `tools/analyze_screenshot.py` really implements `PROMPT_SEQUENCE`, `analyze_sequence()`, and `--sequence`.

## Main findings

### 1. The note overstates “shared state” across the captured frames

The document says:

- “All frames share timestamp `T` from `_f0`”
- “All frames share the same cascade/jitter state; one JSON is sufficient”

That second statement is too strong.

The sequence exists precisely because:

- probe jitter changes over time
- temporal EMA history changes over time
- the captured frames are meant to reveal those temporal changes

So while the sequence shares the same scene configuration and the same starting parameter set, it does **not** share the same runtime GI state frame-to-frame. That is the point of the feature.

A better phrasing would be:

- one JSON captures the initial parameter/context snapshot for the sequence
- but the rendered GI state intentionally evolves over the captured frames

### 2. The “stats are sufficient because they are captured once on f0” claim should be framed as a convenience, not a proof

Capturing stats only on `_f0` is a practical design choice and the code does exactly that. But the writeup currently makes it sound like that single JSON fully characterizes the whole sequence.

In reality:

- it captures the initial context
- it does not capture per-frame temporal evolution
- it is useful metadata, not a complete temporal state record

### 3. The note is stronger than the code on failure semantics for sequence output integrity

The implementation does a good job of clearing and validating `lastScreenshotPath`, and that is a real improvement. But the note still reads a bit more authoritative than the code in one area:

- `launchSequenceAnalysis()` trusts `seqPaths` once collected
- there is no extra validation that the sequence is contiguous beyond filename/tag checks during collection

That is probably fine, but the document sounds slightly more “fully sealed” than the actual state machine really is.

## Bottom line

This is a solid implementation note overall. The main correction is conceptual:

- the frames do **not** share the same runtime jitter/EMA state
- they share the same setup and starting snapshot, while the temporal GI state intentionally evolves across the sequence

That distinction should be made explicit because it is the core reason this feature exists.
