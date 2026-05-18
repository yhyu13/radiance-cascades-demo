# Critic Review 15 — `temporal_alpha_stability_impl.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-17
**Verdict:** **Cold-start measurements show the fix is working** (40× reduction confirmed on default Cornell AND cornell-orig OBJ; max per-pixel delta 2-3/255 at the worst pair; brightness stable to 4 decimals). **But the user reports the fix did not actually resolve the issue.** That gap is the substance of this critic — my measurement methodology may be miscalibrated to what the user perceives in continuous interactive use. **3 HIGH, 2 MEDIUM, 2 LOW.**

---

## HIGH severity

### H1 — I never confirmed the user is running the fix

The shader edits are runtime-loaded — fix lands in any restarted session. But **I have no evidence the user restarted after my fix.** If they kept running the session that was running when they reported the bug, they see the pre-fix behavior. Cold-start test screenshots prove the fix applies on a fresh launch; they prove nothing about the user's specific session state.

**Fix action**: explicitly verify with the user. Two quick checks:
1. Did the user fully exit and restart `RadianceCascades3D.exe` after the shader files were edited?
2. Have they checked the [3] log line for `WeightedSample` toggle and confirmed temporal toggles are at defaults?

If the answer to (1) is "no," the user has been comparing against pre-fix behavior; the fix may already be sufficient when they re-test.

### H2 — Cold-start screenshot A/B is not a fair proxy for "user observes flicker in interactive mode"

My methodology compares two completely independent runs (`--exit-frames=300` vs `--exit-frames=302`), both starting from cold history. Each run reaches a STEADY-STATE EMA convergence point at its specific exit frame, and the screenshot captures that.

But the user's interactive complaint is about **continuous single-session frame-to-frame variation** at 60 fps. The two regimes have different EMA transient behavior:
- **Cold-start at N=302**: history was empty at frame 0; 302 EMA blends later, alpha=0.05, history has converged toward the mean of all jitter positions seen so far. Captured frame = current bake into converged history. Quasi-stable.
- **Interactive at frame 1000+**: history is in steady-state. Each frame: new bake (at one jitter position) blends 5% into history. Displayed image = the history texture. Per frame the displayed image moves ~5% × (per-jitter-position bake delta) from its previous value.

If the per-jitter-position bake delta is large (because each new jitter position bakes a substantially different α/RGB), then 5% × large = small but visible. **The user perceives the visible delta at 60Hz as "swapping pattern."**

My cold-start screenshot measures the DELTA BETWEEN STEADY STATES OF TWO RUNS, not the per-frame motion within one running session. **These can have wildly different magnitudes.** My 0.0003 RMSE may reflect "the EMA converges to similar values from cold start regardless of last jitter position," not "the running session has stable per-frame display."

**Fix action**: instrument the running session to capture consecutive frames into a video / image sequence. The proper test is two consecutive frames at runtime, not two cold-start runs.

### H3 — `historyNeedsSeed` and `cascadeReady=false` cycles can bypass the EMA fix entirely

Reading [demo3d.cpp:870-877](../../src/demo3d.cpp#L870):
```cpp
if (jitterHoldCounter >= jitterHoldFrames) {
    jitterHoldCounter = 0;
    probeJitterIndex = (probeJitterIndex + 1) % static_cast<uint32_t>(jitterPatternSize);
    if (useTemporalAccum)
        cascadeReady = false;  // <-- triggers re-bake on next eligible frame
}
```

Every `jitterHoldFrames=2` frames, when the jitter advances, `cascadeReady` is set false. This triggers cascade re-bake. Re-bake calls into the fused EMA path, where `fusedAlpha = historyNeedsSeed ? 1.0f : temporalAlpha`. **If `historyNeedsSeed` is also true at this moment, alpha=1.0 → mix(history, fresh, 1.0) = fresh. EMA bypassed.**

I never verified `historyNeedsSeed` stays false during steady-state interactive use. If something is keeping it true (e.g., camera input handler setting it on every mouse move), the user sees fresh α every frame regardless of my fix.

**Fix action**: add a debug counter "frames since last historyNeedsSeed=true" and print it every N frames during a 5-second interactive session. If it keeps resetting, that's the bypass.

---

## MEDIUM severity

### M1 — The "max per-pixel 2-3/255" measurement is below static-viewing JND, but possibly visible at 60Hz motion

Static JND for color brightness is ~3-5 luminance levels (out of 255). My measured max-per-pixel of 2-3/255 is just under static JND. But at 60Hz, flicker fusion threshold drops to ~1% Michelson contrast — meaning a 1% brightness change at 60Hz CAN be visible as faint shimmer.

If 2% of pixels change by 1% brightness every 2 frames (which my data shows), the user could perceive this as a subtle but persistent shimmer pattern. Not the gross flicker of pre-fix, but enough to be noticeable.

**Fix action**: if H1/H2/H3 don't account for the user's complaint, the residual shimmer may be REAL and require a deeper fix (lower jitter scale, finer EMA, or temporal blue noise instead of Halton).

### M2 — I didn't test scenarios that re-trigger `historyNeedsSeed=true`

A non-exhaustive list of code paths that set `historyNeedsSeed=true`:
- Demo3D constructor
- Toggling `useTemporalAccum`
- Camera position/target changes
- Scene reload
- Force-rebuild button
- Phase 3 `setUseWeightedSample` (my own setter does this)
- Toggling `useDirectionalMerge`, `useDirBilinear`, `useSpatialTrilinear`, `useWeightedSample` change-detect handlers
- Probably others

Some of these (camera changes) happen on every interactive movement. During those windows, the user sees fresh-α flicker. My fix only helps during STATIC periods.

**Fix action**: enumerate all `historyNeedsSeed=true` write sites; categorize as "warranted (scene structural change)" vs "spurious (transient interaction that doesn't need a full reseed)". Spurious ones could be removed.

---

## LOW severity

### L1 — temporal_blend.comp fallback path may not be reached on default

`doFusedEMA` is true when `useTemporalAccum && tb != shaders.end() && c.probeAtlasHistory != 0 && c.probeGridHistory != 0`. On default config, all three are true → fused path is used → my temporal_blend.comp edit is dead code in default operation. Not wrong; just not exercising both paths in my test.

**Fix action**: verify which path is actually hit in the user's config.

### L2 — Bake-leak metric numbers compared "pre-fix OFF" vs "post-fix OFF" showed 4373.5→4363.5 (10-unit shift). I attributed this to soft-α threshold drift but never verified

Possible alternative explanation: a real semantics drift in the upper-cascade's per-bin α (now time-averaged from binary to soft). If many "barely-opaque" bins drift below 0.001 (becoming "miss"-like) or "barely-miss" bins drift above 0.001 (becoming "opaque-like"), the count and sum will shift in complex ways. Not a critical issue but the 10-unit shift deserves a sanity-check.

---

## What this critic recommends as next actions

Ranked by likelihood of resolving the user's complaint:

1. **(H1) Verify the user restarted the binary after the fix.** If no, restart and re-test — fix may already be sufficient.
2. **(H2/H3) Instrument continuous-frame capture in a single session.** Cold-start screenshots are not a proxy. If frame-to-frame motion in one running session shows much larger oscillation than my N→N+2 cold-start tests, the EMA is being bypassed somewhere.
3. **(M2) Enumerate `historyNeedsSeed=true` sites.** If anything spurious is keeping it true, the EMA is bypassed despite my fix.
4. **(M1) If 1-3 don't explain the residual, consider:**
   - Reduce `probeJitterScale` from 0.06 → 0.03 (smaller per-frame variance)
   - Or `temporalAlpha` from 0.05 → 0.02 (slower EMA, smoother per-frame)
   - Or accept residual shimmer as cost of using probe jitter

---

## Honest position

My fix demonstrably works in the regime I tested (cold-start at fixed exit-frames). It demonstrably reduces consecutive-frame deltas 40× on that test. **What I have NOT demonstrated** is that it works in the user's specific interactive use case — that requires either user re-verification or continuous-frame instrumentation neither of which I've done. The doc's "verified" claim should be qualified as "verified on cold-start; interactive verification pending."
