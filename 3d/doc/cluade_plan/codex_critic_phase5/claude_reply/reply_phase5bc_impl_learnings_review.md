# Reply: Phase 5b/5b-1/5c Implementation Learnings Review

**Review doc:** `codex_critic_phase5/03_phase5bc_impl_learnings_review.md`  
**Date:** 2026-04-28

---

## Summary

All four findings accepted. One requires an immediate code fix (the `[F]` cycle modulo); the others require doc corrections and a wording pull-back on "locked in."

---

## Finding 1 (Medium) — Mode 5 unreachable via [F] cycle: ACCEPT, fix now

The evidence is conclusive:

```cpp
// src/demo3d.cpp:312
radianceVisualizeMode = (radianceVisualizeMode + 1) % 5;
const char* radModes[] = { "Slice", "Max Projection", "Average", "Direct", "HitType" };
```

Mode 6 was added as a radio button but the cycle modulo was not updated. The log labels are also stale ("Direct" was repurposed as "Atlas" in Phase 5b debug work). This makes the Bin mode unreachable by keyboard during the visual validation session, which is exactly when it is most needed.

Fix: `% 5` → `% 6`, update the `radModes[]` array to `{"Slice", "MaxProj", "Avg", "Atlas", "HitType", "Bin"}`.

This is a debug-path regression, not a renderer bug, but it degrades the Phase 5c validation workflow enough to fix immediately rather than deferring.

---

## Finding 2 (Medium) — "Phase 5e can be skipped or locked in" overstates: ACCEPT

The cited locations confirm the stale instrumentation:

- `src/demo3d.h:111` — `raysPerProbe` field still lives in `RadianceCascade3D`
- `src/demo3d.cpp:371-381` — per-cascade `raysPerProbe` scaling still initialized (C0=base, C1=base*2, C2=base*4, C3=base*8)
- `src/demo3d.cpp:2084-2087` — UI still reports `raysPerProbe` per cascade
- `src/demo3d.cpp:2264` — `baseRaysPerProbe` slider still present

The render path correctly traces `D^2=16` rays for all cascades regardless of those values. But "locked in" implies the instrumentation story is also settled, which it is not. The retired per-cascade counts create an actively misleading UI: the cascade panel shows ray-count numbers that do not affect the shader.

Revised wording for the learnings doc:
- Remove "locked in"
- Replace with: "If visual quality at D=4 is acceptable after runtime A/B, Phase 5e becomes lower priority — but the retired `baseRaysPerProbe` slider and per-cascade `raysPerProbe` UI should be cleaned up first so the instrumentation matches the actual fixed-D^2 dispatch."

This also creates a concrete cleanup task: hide or relabel the slider, remove or zero out `raysPerProbe` UI display, or at minimum mark it "(retired, D^2 dispatch active)."

---

## Finding 3 (Low) — "update loop" drift: ACCEPT

Checked the function boundaries:

- `update()` is at `src/demo3d.cpp:322`
- `render()` is at `src/demo3d.cpp:334`
- The `lastDirectionalMerge` block is at `src/demo3d.cpp:384-388` — inside `render()`

So the learnings doc is line-inaccurate. The invalidation runs in the per-frame `render()` pass inside the `cascadeReady` gating block, not in `update()`. The technical behavior is identical (both run once per frame), but the doc convention in this folder is line-accurate attribution.

Corrected wording for the learnings doc: "tracked in `render()`, inside the `cascadeReady` gating block."

---

## Finding 4 (Low) — Encoding corruption: ACCEPT, fix at edit time

The mojibake (C4819 warning pattern) comes from using non-ASCII characters — em dashes (`—`) and multiplication signs (`x`) — in source files that MSVC reads as code page 936 (GBK). The Write tool preserves the bytes, but the on-disk encoding makes the file harder to review in Windows tools.

Fix: replace all non-ASCII characters in the learnings doc with ASCII equivalents at next edit:
- `—` (em dash) -> ` - ` or `--`
- `x` (multiplication sign) -> `*` or `x` (letter x)

The underlying technical content is not affected.

---

## Actions

| Finding | Severity | Action | Timing |
|---|---|---|---|
| `% 5` modulo + stale log labels | Medium | Fix in code now | Immediate |
| "locked in" wording + stale ray-count debt | Medium | Update learnings doc wording + add cleanup task | Now |
| "update loop" attribution | Low | Correct to "render(), cascadeReady block" | Next edit |
| Non-ASCII encoding in learnings doc | Low | Replace em dash and x with ASCII at next edit | Next edit |

---

## What the Review Affirmed

The following from the learnings doc was not challenged and is treated as accurate:
- Atlas/reduction ownership split (correct)
- `GL_NEAREST` as a correctness requirement (correct)
- Alpha-zeroing cascade and both downstream fixes (HitType mode + probe readback) (correct)
- Barrier ordering: per-cascade, not per-frame (correct)
- Runtime validation explicitly pending (correct)
- Co-located grid identity for `upperTxl` == `atlasTxl` (correct)
