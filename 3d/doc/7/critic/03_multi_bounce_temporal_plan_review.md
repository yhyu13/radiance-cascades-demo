# Critic Review 03 — `multi_bounce_temporal_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-18
**Verdict:** Plan is structurally clean (small change, well-scoped, opt-in default OFF). **But three HIGH issues will cause v1 to either malfunction or under-deliver if not addressed**: (H1) the stability analysis is wrong — `albedo × cosine_factor < 1` is NOT the convergence criterion for our formula; using `probeGridHistory` directly gives `bake = direct + albedo × hist`, not `bake = direct + albedo × cosine × hist`. (H2) the existing `probeGridTexture` is already cosine-weighted irradiance (per Phase 5b-1 reduction comment) — so multiplying it by `albedo` AND the bake-formula's `cosine_at_next_bounce` would double-cosine. Need to verify what the reduction actually produces. (H3) the v1 isotropic feedback ignores the SURFACE NORMAL at the bake hit — a corner near a colored wall gets the same indirect contribution as the wall's center, defeating the purpose of having directional probes. Plus 4 MEDIUM, 3 LOW.

---

## HIGH severity

### H1 — Stability analysis uses the wrong recurrence

The plan §3 says:
> In equilibrium: `bake_radiance = direct + albedo × bake_radiance_prev × gain × cosine_factor`.
> Solving: `bake_eq = direct / (1 - albedo × gain × cosine_factor)`.
> For white walls (albedo=1.0, cosine=0.5, gain=1.0): feedback = 0.5 < 1, stable. Equilibrium = 2× direct.

This is the rendering equation's steady-state form for a path tracer — but our cascade doesn't compute it that way. The shader code in §4.1 does:

```glsl
indirectColor = albedo * prevIrradiance * uMultiBounceGain;
color = directColor + indirectColor;
```

No explicit `cosine_factor` term. The `prevIrradiance` from `probeGridTexture` is *already* the cosine-weighted hemisphere integral (per Phase 5b-1 reduction). So the cosine is already baked in — the formula becomes:

```
bake = direct + albedo × prev_irradiance × gain
```

In steady state: `bake_eq = direct + albedo × bake_eq × gain` (assuming prev_irradiance ≈ bake from previous frame at the same point).

→ `bake_eq = direct / (1 - albedo × gain)`.

For white walls (albedo=1.0, gain=1.0): `1 - 1×1 = 0` → **division by zero, unbounded amplification**.

**This is the opposite of stable.** The plan's "cosine_factor = 0.5" is invented out of thin air; the actual factor depends on what the reduction shader produces and how it relates to "irradiance at a surface with a given normal."

**Fix paths**:
- (A) Verify what `probeGridTexture` actually contains. If it's the average of all bins (no cosine weighting), the recurrence is `bake = direct + albedo × isotropic_avg × gain`, and `isotropic_avg = ∫L(ω) dω / ∫dω = mean_radiance`. The effective gain in equilibrium is then `albedo × gain × (mean_irradiance / direct_irradiance)` which is scene-dependent but typically <1 for moderate albedos. Still need explicit verification.
- (B) Read [reduction_3d.comp] to confirm exact formula.
- (C) Add a hard energy clamp regardless of theoretical stability (e.g., `min(indirectColor, vec3(maxRadiance))`). Belt-and-suspenders.

The plan ABSOLUTELY MUST do (B) before shipping. The stability analysis as written is fiction.

### H2 — Double-cosine concern: is `probeGridTexture` irradiance or radiance?

Same root cause as H1. The plan asserts:
> `probeGridTexture` stores per-probe AVERAGE IRRADIANCE (Phase 5b-1 reduction).

But [demo3d.cpp:2429](../../src/demo3d.cpp#L2429) calls reduction_3d.comp which probably computes the average across D² bins — that's **average radiance**, not irradiance. Irradiance requires cosine-weighted integration: `E = ∫ L(ω) cos(θ) dω`.

If it's mean radiance (no cosine), then `albedo × mean_radiance × π` would be roughly irradiance (assuming uniform hemisphere), and `albedo × mean_radiance / π × cos × wcos × area` would be exiting radiance for the BRDF integral. The factor we need depends on convention.

Without reading reduction_3d.comp, we don't know:
- Whether the value is radiance, irradiance, or some other quantity
- Whether it has units of W/m² or W/(m²·sr)
- Whether the bake should multiply by π, 1/π, or 1

**Fix**: read reduction_3d.comp. Read raymarch.frag's `sampleDirectionalGI` to see what it computes. The bake's feedback formula must match the convention.

The plan §4.1 hand-waved this with "probeGridTexture stores cosine-weighted hemisphere average." That claim is unsupported and may be wrong.

### H3 — Isotropic feedback at a surface ignores surface normal — defeats the purpose of having directional probes

The cascade renderer's whole value proposition is that probes have *directional* bins (D² per probe). At a surface point with normal `n`, the correct indirect contribution is a cosine-weighted sum over the hemisphere oriented around `n`:

```
indirect(p, n) = ∫_{Ω+(n)} L(ω) × max(0, dot(ω, n)) dω / π
```

Display path (raymarch.frag's `sampleProbeDir`) does this — sums forward-facing bins weighted by `wcos`.

v1 plan uses `probeGridTexture` directly (no normal awareness). At a surface in a corner of the Cornell box, both the floor (normal up) and the back wall (normal forward) get the **SAME** feedback value. That's physically wrong — the floor sees light coming from above, the wall sees light coming from in front.

**Quality impact**: indirect color bleeding (e.g., red wall's tint on the floor) will be WRONG because the floor's "indirect from red wall direction" is averaged with "indirect from green wall direction" and others. Net: indirect is a muddy gray rather than directional color bleed.

This is the kind of failure mode the user would IMMEDIATELY notice on Cornell box. v1 ships looking subtly wrong on the most-tested scene.

**Fix**: at least use the directional atlas with normal weighting. Cost is `D²` extra texture lookups per hit, but D=4 in C0 = 16 samples = negligible. v1 should NOT be "isotropic only" — should be directional from day 1.

OR: accept the muddier result for v1, document explicitly, plan v2 for proper directional. But the plan currently presents isotropic as "acceptable for diffuse scenes" which is not true for color-bled corners.

---

## MEDIUM severity

### M1 — `historyNeedsSeed` interaction not fully thought through

The plan §4.2 says:
> `bool hasFeedback = useMultiBounce && feedbackTex != 0 && !historyNeedsSeed;`

But `historyNeedsSeed` is set true on EVERY cascade rebuild trigger (scene change, light move, etc.). After ANY change, multi-bounce is OFF for 1 frame, then ON. This produces a visible "brighten by 7-10% over 10 frames" transient after every interaction. Acceptable for static views, jarring for interactive use.

**Fix**: either (a) seed the feedback texture by running ONE iteration of the bake with feedback OFF (current behavior), then turn feedback ON for subsequent frames — same as current `historyNeedsSeed` does for RGB; (b) document the transient explicitly as expected behavior.

### M2 — Stagger interaction with multi-bounce unverified

Plan §7 acknowledges:
> Coupling to cascade staggering: with stagger=8, C3 only updates every 8 frames. Multi-bounce in C3 would use 8-frame-old data.

The plan calls this "verify experimentally" but doesn't budget time for it. With our default `staggerMaxInterval=8`, the lowest cascade updates infrequently. Multi-bounce feedback at C3 will be sluggish — color changes in the scene take ~80 frames to propagate fully through the cascade chain.

**Fix paths**:
- Force-bake all cascades every frame while multi-bounce is ON (defeats stagger's perf benefit)
- Use a SHARED feedback source (always C0) for all cascades' feedback — converges at C0's rate (1 frame stagger)
- Document the latency

Recommendation: use shared C0 history for all cascades' feedback. Same texture binding regardless of which cascade is baking. Simpler + faster convergence + better quality (C0 highest resolution).

### M3 — Phase 3 WeightedSample composition needs explicit verification, not just hope

Plan §3 says:
> Phase 3 v3 (WeightedSample) attenuates upper-cascade contribution at bake by visibility fraction. Orthogonal to multi-bounce — they compose cleanly.

This is asserted, not verified. Specifically:
- Multi-bounce adds `albedo × prev_irradiance` to the hit color (before the upper-cascade merge).
- Phase 3's `* aFactor` then modifies the (1-l) × upper term at merge time.
- They touch DIFFERENT parts of the formula but their interaction at l=0 (far hit) or l=1 (near hit) needs analysis.

At `l=1` (close hit): `rad = hit.rgb × 1 + upper × 0 × aFactor = hit.rgb` = direct + indirect_feedback (multi-bounce works as designed; Phase 3 inert).

At `l=0` (far hit): `rad = hit.rgb × 0 + upper × 1 × aFactor = upper × aFactor`. Multi-bounce feedback is GATED OUT because l=0 zeros the hit.rgb. **So multi-bounce only contributes in the smoothstep zone and at l=1 hits.** For miss bins (l undefined, third branch), there's no hit.rgb to add feedback to.

This means **most bins get no multi-bounce contribution** — the feedback only applies where the ray hits geometry IN the cascade's interval (small portion of bins for C0 in open scenes).

Quality impact: multi-bounce gain may be less than the predicted 7-10%, because the feedback only fires on a subset of bins.

**Fix**: rewrite the algorithm spec to account for the l-blending. Probably the right formula is to add feedback to `rad` regardless of branch (direct, miss, or smoothstep), not just at hit-time. But that means feedback at miss-bins comes from `prev_irradiance(L_far + tMax × rayDir)` — a different sample point than the bake's hit.

This needs more careful design. Possibly worth a v0.5 prototype to measure actual gain before committing to a final shader.

### M4 — Energy-clamp `gain` slider isn't enough; need history-rejection clamp too

If a sudden scene change introduces a much brighter direct light, `probeGridHistory` still has the OLD (dim) values. After 1 frame, atlas has new direct + old × albedo × gain ≈ new direct. After many frames, it converges to `new_direct / (1 - albedo × gain × factor)`.

But if a sudden DARKENING happens (light goes off), atlas history is BRIGHT, multi-bounce keeps adding the old brightness for several frames → "ghosting" / "after-image" effect.

Standard TAA fix: history rejection via neighborhood AABB (we already have `uClampHistory`). Multi-bounce should also use this — clamp feedback to "what the local neighborhood currently has."

**Fix**: gate feedback through history clamp (similar to existing fused EMA clamp).

---

## LOW severity

### L1 — "Multi-bounce contribution: ~7-10% of total brightness" estimate could be off

The motivation section cites PT@2 (0.393) vs PT@∞ (0.421) = +7% brightness as "multi-bounce contribution." But our multi-bounce ALSO bakes direct + indirect, and the cascade currently does direct + single-indirect. PT@2 means "direct + 1 bounce" — but the indirect bounce in PT samples a DIFFERENT surface and adds its DIRECT light. Cascade's "single bounce" is reading probes, which contain only direct.

So PT@2 already includes "indirect from surfaces lit by direct," and PT@4 adds "indirect from surfaces lit by indirect-from-direct," etc.

Multi-bounce in cascade would add "feedback from previous frame's atlas = previous frame's direct + previous frame's indirect" — this gives multi-bounce in TIME, not in single-frame depth. Equivalent in equilibrium to PT@∞ if convergent.

**Implication**: with multi-bounce ON in cascade, we should expect cascade brightness to increase to ~PT@∞ × (1 - integration_loss). Not just +7%. Could be much more.

The plan undersells the expected impact. Real gain might be 15-30% on cornell-orig, not 7-10%.

### L2 — "Cost: 1 extra texture sample per hit (isotropic)" doesn't account for the bbox check

Plan §4.1 shows the feedback code:
```glsl
vec3 uvw = (pos - uGridOrigin) / uGridSize;
if (all(...) && all(...)) { ... texture(uPrevFrameRadiance, uvw).rgb; }
```

The bbox check itself is ~6 ALU ops. Trivial but worth noting that "1 texture sample" isn't quite right; it's "6 ALU + 1 texture sample, gated on a branch."

### L3 — `tools/compare_cascade_pt.py` doesn't exist yet; v1 success criteria depend on it

Plan §6 references the script. Plan §9 success criteria depends on RMSE measurement. But the script needs to be written. Day 2 budget for "validation + tuning" should explicitly include writing the comparison tool (or reusing one from Phase 7).

---

## Cross-cutting: scope creep risk

The plan is ostensibly "small" (~12 lines of shader). But the issues above suggest:
- H1+H2: need to read reduction_3d.comp + raymarch.frag's sampleDirectionalGI to know the convention before writing the formula
- H3: directional feedback adds ~D² per hit complexity — should probably be v1 not v2
- M3: l-blending interaction means the algorithm needs to be reworked
- M4: history rejection needs to compose with existing temporal clamp

Realistic effort: 3-4 days, not 2. Or scope down to "directional feedback only at l=1 hits" as a clear v1 with simpler validation.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | Stability analysis uses wrong recurrence; "cosine_factor=0.5" is invented |
| H2 | HIGH | What `probeGridTexture` actually contains is unverified; double-cosine risk |
| H3 | HIGH | Isotropic feedback ignores normal → muddy color bleed; should be directional from v1 |
| M1 | MEDIUM | `historyNeedsSeed` interaction not designed; transient brighten after every interaction |
| M2 | MEDIUM | Stagger interaction unverified; recommend shared C0 history for all cascades |
| M3 | MEDIUM | l-blending in bake formula means feedback only fires on subset of bins; algorithm needs rework |
| M4 | MEDIUM | History rejection needed alongside gain clamp for dynamic scenes |
| L1 | LOW | Estimated +7-10% gain may be wrong (could be larger) — multi-bounce-in-TIME is different than PT@N |
| L2 | LOW | Cost analysis under-counts (bbox check) |
| L3 | LOW | Validation depends on `tools/compare_cascade_pt.py` which doesn't exist yet |

---

## Top actions for plan revision

1. **Fix H1+H2**: read `reduction_3d.comp` and `sampleDirectionalGI`. Write the EXACT recurrence based on what the textures actually contain. Re-derive convergence.
2. **Fix H3**: switch v1 to directional feedback (sample D² bins around the surface normal, like display does). The cost is small (~D² = 16 lookups at C0). The quality win is large (correct color bleed).
3. **Fix M3**: rework the algorithm to handle the l=0/l=1/miss branches consistently. Feedback should fire on miss bins too (their continuation point at hit_pos + tMax × rayDir is where we'd want to sample previous-frame indirect).
4. **Fix M1, M2, M4**: documented designs for history-seed, shared-C0 feedback, and history rejection.
5. **Adjust scope**: 3-4 days realistic, not 2.
6. **Fix L3**: write or reuse comparison tooling before claiming validation passes.

Then ship a v0.5 prototype to MEASURE the actual gain (vs the speculative 7-10%) and only after that commit to the final algorithm.
