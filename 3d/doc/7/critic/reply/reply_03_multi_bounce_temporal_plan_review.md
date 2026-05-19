# Reply: Multi-Bounce Temporal Plan Critic 03 — `03_multi_bounce_temporal_plan_review.md`

**Date:** 2026-05-18
**Status:** All 10 findings addressed. **H1+H2 forced reading `reduction_3d.comp`** — confirmed it's `Σ bin / D²` (simple average, no cosine, no α-gate). My rev 1 plan's "cosine_factor=0.5" was fabricated; the actual recurrence using isotropic feedback would amplify by `direct/(1-albedo)` = 10× for white walls. **H3 forced directional feedback from v1** — isotropic would produce muddy color bleed (no normal awareness). Plan rev 2 ports `sampleProbeDir`'s cosine-weighted hemisphere integration into the bake. M2 changed cascade-per-cascade feedback to shared-C0 feedback (faster convergence + higher resolution). M3 forced explicit l-blending analysis (feedback only fires in surface-hit branch's smoothstep zone — miss bins use upper's already-multi-bounced atlas). M4 added history-rejection clamp. **Scope grew to 3-4 days from 2.** Most importantly: v0.5 prototype-first added per critic-03 cross-cutting concern — measure actual gain before committing to final shader.

---

## How each finding was addressed

### H1 (HIGH) — Stability analysis used wrong recurrence

**Accepted, fundamental rewrite.** My rev 1 claimed "albedo × cosine_factor < 1, cosine_factor=0.5" without justification. Critic was right: the formula in the shader was `bake = direct + albedo × prev_irradiance`, which gives `bake_eq = direct / (1 - albedo)` if `prev_irradiance ≈ bake_prev`. For white-walled Cornell (albedo=0.9): `direct / 0.1 = 10× direct` → physically possible but unbounded for albedo→1.

**Fix in plan rev 2 §3**: re-derived the recurrence based on what `probeGridTexture` actually contains (confirmed by reading [reduction_3d.comp:39](../../../res/shaders/reduction_3d.comp#L39): `avg += samp; avg /= D²`). With directional feedback (cosine-weighted hemisphere), the recurrence is `bake_eq = direct / (1 - albedo × gain × hemi_factor)` where `hemi_factor` is the spatial coupling. For Cornell with effective albedo ~0.6 and gain=0.7 default: stable, equilibrium ~1.5-1.7× direct (matches PT@∞ observation).

**Defaults changed**: `multiBounceGain = 0.7` (was 1.0) for stability margin. Hard `sanitizeRadiance` clamp at 100 (existing) provides additional safety. History-rejection clamp (per M4) provides per-frame bound.

### H2 (HIGH) — `probeGridTexture` semantics unverified (double-cosine risk)

**Accepted, read reduction shader.** `probeGridTexture` is `Σ bins / D²` — **simple arithmetic mean of all atlas bins**, no cosine weighting, no α-gating. This is "average radiance over full sphere" — including back-facing bins.

Using this directly in the bake would: (a) ignore surface normal (per H3); (b) include back-facing bins that physically shouldn't contribute to a Lambertian surface; (c) compound the H1 amplification.

**Fix**: v1 algorithm now uses **directional sampling via `sampleC0AtlasIrradiance`** which mirrors raymarch.frag's `sampleProbeDir` (cosine-weighted forward hemisphere, α-gated). No `probeGridTexture` lookup at all. Direct read of the C0 atlas with proper hemisphere integration.

### H3 (HIGH) — Isotropic feedback ignores surface normal

**Accepted, escalated isotropic to directional in v1.** Original v1 plan had "isotropic for v1, directional for v2." Critic correctly pointed out this would produce muddy color bleed (floor and wall at the same point get same feedback). On Cornell-orig — the most-tested scene — this would be immediately visible.

**Fix**: v1 now uses directional from day 1. Cost: ~D² × 8 lookups per hit (~128 at C0's D=4, ~512 if D=8 at higher cascades). ~5-30 ms additional bake cost — acceptable; cascade bake is already ~16.5 ms.

For early v1, restrict feedback to C0 hits only (skip higher cascades) to control cost; v2 enables for all cascades.

### M1 (MEDIUM) — historyNeedsSeed transient

**Accepted, redesigned.** Original plan gated feedback on `!historyNeedsSeed` — every interaction would cause a "brighten over 10 frames" transient.

**Fix**: removed the gate. On first frame, history texture is current atlas (zero-initialized or whatever the freshly-allocated state is). Feedback reads zero on first frame → graceful degradation to single-bounce. Subsequent frames have real history → multi-bounce kicks in smoothly. No visible transient.

### M2 (MEDIUM) — Stagger interaction unverified

**Accepted, switched to shared C0 history.** Critic was right that per-cascade feedback would have stagger-induced latency (C3 with stagger=8 updates every 8 frames → multi-bounce in C3 uses 8-frame-old history → low-frequency oscillation).

**Fix**: all cascades' multi-bounce feedback samples C0's atlas history (single shared texture). C0 always updates every frame → fastest convergence. Bonus: C0 has highest spatial resolution → best quality feedback.

### M3 (MEDIUM) — l-blending interaction

**Accepted, explicit analysis added.** The bake's merge formula has three branches (sky / surface-hit / miss); feedback added to hit.rgb only fires in the surface-hit branch, and at `l=0` (smoothstep) it's gated out.

**Fix in plan rev 2 §3**: explicit per-branch analysis. v1 scope decision: feedback ONLY fires at surface-hit bins (where `hit.a > 0`), and only at `l=1` cleanly; smoothstep zone gets partial feedback via the `l` blend. Miss bins don't get feedback directly — but they DO inherit multi-bounce via cascade chaining (upper cascade's atlas, baked first, already has multi-bounce; lower cascade reads it via the existing miss-branch `rad = upperDir`).

So multi-bounce reaches miss bins indirectly through cascade chaining, not direct feedback at the miss point. Correct geometric behavior.

### M4 (MEDIUM) — History rejection clamp

**Accepted, added `sampleC0AtlasNeighborhoodMax` helper.** Without history clamp, a sudden scene darkening (light off) would cause multi-bounce to keep adding the old brightness for several frames (after-image / ghosting effect).

**Fix in plan rev 2 §3**: feedback clamped to `min(albedo × hemi × gain, neighborhood_max × 1.5)` where `neighborhood_max` is the channel-wise max over a 3³ probe neighborhood around the hit. The 1.5× allows headroom for legitimate brightening; clamps runaway amplification.

Composes with the existing temporal-α EMA fix (which clamps RGB to neighbor-AABB during temporal blending). Two layers of protection.

### L1 (LOW) — "+7-10% gain" speculative

**Accepted, language softened + prototype-first added.** Updated to "7-22% (revised from 7-10% — multi-bounce in TIME differs from PT@N in DEPTH)." Added critic's "must measure with prototype before final shader" recommendation as the **Day 1 v0.5 gate** in the sequencing.

Day 1 v0.5 has explicit pass criteria: brightness ratio cascade/(PT cascade-match) improves by ≥ 5%. If not, write up findings and stop. Don't sink Day 2-3 into a feature that doesn't deliver. This avoids the trap of "spec'd N days, finished N days, oops the feature didn't help" that previous plans (e.g., Phase 3 v1's GI-killing variant) sometimes hit.

### L2 (LOW) — Cost analysis under-counts bbox check

**Accepted, doc note.** Bbox check is 6 ALU per hit, trivial vs the 128+ texture fetches. Doc reflects "~5-30 ms additional bake cost" range rather than "trivial."

### L3 (LOW) — `tools/compare_cascade_pt.py` doesn't exist

**Accepted, added to Day 3 budget.** The Python comparison tool needs writing (or extending Phase 7's). Day 3 explicitly includes "write `tools/compare_cascade_pt.py`" as part of the validation work. Without it, success criteria can't be measured.

---

## Cross-cutting: scope grew from 2 → 3-4 days

Accepted. Critic-03 cross-cutting note ("realistic effort: 3-4 days") was correct. Rev 2 sequencing:
- Day 1 v0.5 prototype (cheap, measurable, gate)
- Day 2 hardening (algorithm cleanup + GUI + invalidation)
- Day 3 validation + Phase 3 composition + comparison tool
- Day 4 buffer

v0.5-first structure (per critic-03 cross-cutting) is the most important change. If the v0.5 gate fails (multi-bounce contribution < 5% gap closure), we save 2-3 days by stopping early.

---

## Summary

| Critic 03 ID | Severity | Action |
|---|---|---|
| H1 | HIGH | Re-derived stability after reading reduction_3d.comp; default gain=0.7 (was 1.0); equilibrium = direct/(1 - albedo × gain × hemi_factor) |
| H2 | HIGH | `probeGridTexture` is simple-avg confirmed; v1 doesn't use it — directly samples C0 atlas with cosine-weighted hemisphere |
| H3 | HIGH | **Escalated isotropic to directional from v1.** Mirror raymarch.frag's sampleProbeDir |
| M1 | MEDIUM | Removed historyNeedsSeed gate; first-frame history is zero → graceful degradation |
| M2 | MEDIUM | **All cascades share C0 history for feedback** (not per-cascade) — single texture binding, fastest convergence, best resolution |
| M3 | MEDIUM | Explicit per-branch l-blending analysis; miss bins inherit multi-bounce via cascade chaining |
| M4 | MEDIUM | History-rejection clamp added (`sampleC0AtlasNeighborhoodMax`); composes with temporal-α EMA |
| L1 | LOW | Speculative gain estimate updated; prototype-first gate added on Day 1 |
| L2 | LOW | Cost analysis updated (5-30 ms range vs "trivial") |
| L3 | LOW | `tools/compare_cascade_pt.py` added to Day 3 budget |

**The biggest changes from rev 1 to rev 2**:
1. **Algorithm rewrite** (H1+H2+H3+M3): isotropic→directional, shared-C0, l-blending analysis, recurrence corrected after reading reduction_3d.comp
2. **v0.5-first sequencing** (critic-03 cross-cutting): measure before committing; explicit fail-gate on Day 1
3. **Scope honest at 3-4 days** (was 2)

**Critic value**: H1/H2 caught a fabricated stability analysis that would have produced runaway amplification on white walls. H3 caught an "isotropic for v1" decision that would have shipped looking visibly wrong on the most-tested scene. M2 caught a stagger-latency issue I hadn't thought through. Round well-earned.
