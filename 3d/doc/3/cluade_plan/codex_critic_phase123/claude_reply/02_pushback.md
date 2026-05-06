# Pushback and Clarifications

---

## Finding 3 — "Phase 2 is not really a radiance cascade" (HIGH) — PARTIALLY ACCEPTED

**The claim:** The implementation is a "single static probe volume," not a radiance cascade.

**Where I agree:** A single probe field with no hierarchy, no cascade-to-cascade merging, and no temporal accumulation is not a full radiance cascade implementation. The name is generous.

**Where I push back:** The Phase 2 spec was always "single 32³ cascade." The plan, from the start, said:

> Phase 2: Single 32³ probe grid fills the `uRadiance` sampler; toggling it changes the image visibly.

A single cascade level is the entry point of the cascade hierarchy — not a completed hierarchy. The naming is honest relative to the spec. Calling it "Phase 2 of a radiance-cascade implementation" is not the same as claiming the full cascade system is working.

**The real gap** is that the documentation sometimes uses the phrase "radiance cascade" when "probe field GI prototype" is more accurate. The codex critic is right to flag this. The fix is tighter language, not a different implementation.

**Correction to my docs:** Both `phase2_changes.md` and `PLAN.md` should use "single probe-grid GI prototype" in the description of what Phase 2 delivers, reserving "radiance cascade" for the multi-level system that Phase 3 would build.

---

## Finding 2 (shadow ray) — priority disagreement

**Not a pushback on correctness.** The critic is right that unshadowed direct light weakens the result.

**Pushback on priority:** The codex_plan_critic_critic explicitly says:

> At every point, prefer the option that keeps the project debuggable by one person in one evening.

Adding shadow rays to `radiance_3d.comp` requires:
- A second `raymarchSDF()` call from the hit point toward the light (full SDF march, up to 128 steps)
- For every probe ray that hits something: 32³ × 4 rays × 128 steps = up to 4M SDF samples per dispatch

At 0.125 world-units per probe cell and 4-unit volume, most hit points are within 1–2 world units of the light (which is at `(0, 0.8, 0)`). A capped 32-step secondary march is probably sufficient for a Cornell-box-scale scene.

**My revised position:** Shadow rays should be added alongside material colors (Phase 2.5), not deferred indefinitely. But they should not block the current visual smoke test. Do material colors first (visible in the toggle), do shadow rays second (improves quality of what you see).

---

## Finding 6 — Resource cleanup "still partially resolved" (MEDIUM) — CLARIFICATION

The critique cites `src/demo3d.cpp:216` (destructor) and `:1058` (destroyCascades) as having TODO markers.

**The actual state:** `destroyCascades()` at line 1058 iterates all cascades and calls `c.destroy()`, which was implemented in Phase 2: it deletes the probe texture and resets the handle. The loop itself is not a stub. The TODO comment above it is stale.

The destructor at line 216 (`// TODO: Implement destructor`) is also stale — the body already calls `destroyCascades()`, `destroyVolumeBuffers()`, deletes shaders, and deletes query objects. The TODO comment is lying.

**Fix:** Remove the stale TODO comments. The implementations exist; only the comments are wrong.

---

## Finding 7 — Probe quality intentionally minimal (LOW) — CONTEXT

**Accept the rating.** 32³ probes × 4 rays = low sample count. Expected output is noisy for fine detail, acceptable for a coarse GI toggle demonstration.

**Context the critic may not have:** The cascade dispatch now runs once per scene change, not every frame. Raising to 8 rays per probe (2× cost, one-time) is feasible and costs nothing at runtime. If the smoke test shows the toggle effect is too subtle, 8 rays is the first knob to turn.
