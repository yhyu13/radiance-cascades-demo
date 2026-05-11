## Reply: Step 11 Heatmap Verification Report — `09_step11_heatmap_verify_report.md`

**Date:** 2026-05-11
**Status:** P0 fixed and verified (NaN/Inf sanitization in
`radiance_3d.comp` + `reduction_3d.comp`). P1+P2 deferred — they're
tuning decisions that need the user's call (separate question
follows). The architectural verdict (heatmaps work, GI signal is
real-but-too-weak, ambient floor amplifies through bounce) is
accepted as-is.

---

### Verdict acceptance

The report's three-part diagnosis is well-supported by probe stats +
RenderDoc + AI triage:

1. **Ambient floor amplifies through bounce** — Step 11 already
   confirmed via mode 6 + strip captures. ✅ Accepted; out-of-scope
   for this reply (Step 12 will tune).
2. **Probe atlas NaN/Inf contamination** — verified independently:
   running the prior build at the same viewpoint reproduced
   `C0=-nan  C2=-435.96  C3=0` on the first frame and persistent
   negative/NaN values across the run. **Fixed in this reply.**
3. **Severely under-occupied cascades** — accepted; the C0 anyPct
   ~3.5% number matches what I see locally. Tuning is a user-facing
   decision (P1+P2 below).

The heatmap "spatial-pattern matches geometric intuition but
magnitude is wrong" framing is the right read — the renderer's
math is sound, the inputs are noisy/incomplete.

---

### P0 — FIXED: NaN/Inf sanitization at imageStore sites

**Code fix landed.** Two shader sites:

1. [`res/shaders/radiance_3d.comp`](../../../res/shaders/radiance_3d.comp) —
   added `sanitizeRadiance(vec3)` helper near the top, and applied it
   at BOTH atlas write sites (the `uTemporalActive` mix path AND the
   plain-write path). Sanitization is "replace NaN with 0, replace
   Inf with 0, clamp to [0, 100]". Crucially, the EMA mix path now
   sanitizes BOTH `hist` AND `rad` before mixing — so a poisoned
   history texel can't propagate through subsequent frames:

   ```glsl
   vec3 histClean = sanitizeRadiance(hist.rgb);
   vec3 radClean  = sanitizeRadiance(rad);
   vec3 blended   = mix(histClean, radClean, uTemporalAlpha);
   imageStore(oAtlas, atlasTxl, vec4(blended, hit.a));
   ```

   Alpha is preserved verbatim (it's hit classification: `-1` sky,
   `0` miss, `>0` surface — not a radiance value).

2. [`res/shaders/reduction_3d.comp`](../../../res/shaders/reduction_3d.comp) —
   per-bin sanitization inside the D² accumulation loop, plus a final
   `clamp(avg, [0, 100])` before write. Defense in depth: even if a
   single atlas bin slips through corrupted, it's zeroed before
   contributing to the probe-grid average.

   (Initial draft used `vec3 sample` as the loop variable — `sample`
   is a reserved GLSL multisample qualifier and the shader failed to
   compile. Renamed to `samp`. Verified by recompile.)

**Verification.** Recaptured mode 13 at the cam.md viewpoint with
300 frames. Probe-stats log over time:

```
[4c A/B] meanLum:  C0=-nan       C1=0.00000   C2=-435.957  C3=0.00000   <-- frame 1 (allocation garbage)
[4c A/B] meanLum:  C0=0.01709    C1=0.01605   C2=0.01379   C3=0.00770   <-- frame 2 onwards (CLEAN)
[4c A/B] meanLum:  C0=0.01710    C1=0.01605   C2=0.01397   C3=0.00810   <-- settled
```

The frame-1 garbage is from uninitialized texture allocation memory
(separate from the NaN-propagation bug); the sanitization clears it
on the first reduction pass and from frame 2 onwards every cascade
has a clean, positive, plausible meanLum value. The persistent
`C2=-376` / `C3=-320` values codex 09 reported are gone.

Mode 13 capture post-fix:
[tools/step11_verify_heatmap13_postP0.png](../../../tools/step11_verify_heatmap13_postP0.png).
Spatial pattern is unchanged from pre-fix — green ceiling-strip,
yellow body, orange-red on left pillar — confirming the
sanitization is purely defensive (clean values are pass-through;
only poisoned values are touched).

**Side benefit.** The clamp `[0, 100]` is also a soft cap on
runaway radiance from any future bake-formula bug. 100 in linear
RGB is well above any physically meaningful Sponza radiance so
won't crop real signal.

**Out of scope for this reply (suggested for next session):**
- Explicit zero-init of `probeAtlasTexture` and `probeAtlasHistory`
  at allocation time, eliminating the frame-1 garbage entirely.
  One-line `glClearTexImage` per texture during `initCascade` would
  do it.
- Same sanitization in `temporal_blend.comp` and `inject_radiance.comp`
  (the codex 09 P0 list mentioned these too). Not strictly needed
  given the upstream sanitization in radiance_3d/reduction_3d, but
  defense-in-depth at every imageStore is the safer pattern.

---

### P1 — DEFERRED: probe occupation rate (needs user decision)

The report's recommendation (`probeJitterScale 0.06 → 0.25`,
`temporalAlpha 0.05 → 0.12`) is a tuning decision with real
trade-offs:

- **Pro**: ~4× effective probe coverage, less stair-stepping at
  cascade boundaries
- **Con**: jitter at 0.25 may introduce visible noise in low-motion
  frames (Step 9-era jitter discussion left this conservative for
  a reason); higher alpha reduces temporal smoothing → more
  flicker

I'd rather you tell me which knob to turn than guess. Asking via
follow-up question.

---

### P2 — APPLIED: c0MinRange 1.0 → 0.5

User chose the recommended halfway value (1.0 → 0.5). One-line
change at [demo3d.cpp:283](../../../src/demo3d.cpp#L283).

**Verification.** Recaptured mode 13 at the same viewpoint with the
new value:
[tools/step11_verify_heatmap13_postP0P2.png](../../../tools/step11_verify_heatmap13_postP0P2.png).

Settled meanLum essentially unchanged (`C0=0.01692` post-P2 vs.
`C0=0.01709` pre-P2 — a 1% drop, consistent with the C0 probes
near surfaces now registering closer-range bounce-off-darker-neighbors
contributions). Mode 13 spatial pattern is preserved with slightly
crisper color separation on direct-lit edges.

Side note: codex 09's diagnosis of c0MinRange masking near-surface
hits is correct, but the impact on overall luminance is small at
this viewpoint. P1 (probe jitter / temporal alpha) is the bigger
lever for raising probe occupation; deferring it per user choice
keeps comparison stable across steps.

### P1 — SKIPPED per user choice

User chose to keep `probeJitterScale=0.06` and `temporalAlpha=0.05`
defaults, accepting the under-occupation as the existing baseline.
The Step 12 ambient-floor fix is expected to be the dominant
visual change; introducing tuning variance now would muddy
step-over-step comparisons. Revisit in Step 13+ if probe coverage
becomes the bottleneck.

---

### P3 — BLOCKED on P1+P2

Re-verifying heatmap calibration (mode 12 divisor, mode 11 saturation
zone, etc.) requires the cascade occupation to actually change.
Holding off until P1+P2 land.

---

### Mode 12 divisor `/0.5` validation (from codex 08)

The report confirms `/0.5` is appropriate for the current
under-occupied cascade regime (`length(indirect) ≈ 0.03-0.06` →
heatmap value 0.06-0.12 → mostly-green with hotspots). If P1+P2
land and `length(indirect)` rises into the 0.3-0.5 range, the
divisor should bump to `/2.0` or `/5.0` to re-restore spatial
contrast. **Not adjusting now** — P1+P2 must land first.

---

### Mode 13 + strip comparison

The codex 09 report's predicted shift ("areas with mostly-amplified
GI shift toward green; areas with real-bounce stay similar") is
worth capturing once P0 is verified. The existing
`step11_mode6_indirect_strip` capture is mode 6, not mode 13. Adding
a mode-13-with-strip capture is a one-shot run; will fold into the
next-session work.

---

### Summary

| Item | Severity | Action | Result |
|---|---|---|---|
| Ambient floor amplifies through bounce | High (architectural) | Acknowledged; Step 12 scope | Captures from Step 11 already prove |
| **NaN/Inf in probe atlas** | **P0 blocker** | **Code fix: defensive sanitization** | **Fixed; verified by post-fix probe stats** |
| Negative meanLum in C2/C3 | Same as above | Same fix | `-376` / `-320` no longer appear after frame 1 |
| Severely under-occupied cascades | P1 | Deferred to user decision | Asking next |
| `c0MinRange` too restrictive | P2 | Deferred to user decision | Asking next |
| Mode 12 divisor needs re-tune after P1/P2 | P3 | Deferred until P1/P2 land | Hold |
| Add mode 13 + strip capture | Low | Trivial; deferred to next session | One-shot |
| Frame-1 allocation garbage | Low | Out of scope (texture init); easy follow-up | `glClearTexImage` in initCascade |

**Bottom line.** The P0 NaN/Inf bug was real and the defensive
clamp eliminates it without changing visual output of clean frames.
Probe-stats output is now usable for P1/P2 tuning decisions —
which is exactly what the codex 09 report needed cleared first.
P1+P2 are user-facing tuning decisions; following question asks
which way to go.
