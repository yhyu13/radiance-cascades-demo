# Critic Review 07 — `visibility_phase1.5_and_phase2_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-14
**Verdict:** Plan is **structurally sound** (the A/B sequencing decision tree is genuinely the right framing) but has **several substantive technical errors and verification gaps** that would silently produce wrong results or wasted work if executed as written. **Three findings are HIGH severity** (algorithmic bug in Path A's lateral test logic; ill-defined cone-radius scaling at large `t`; bake-leak test scene assumption is unverified). Several smaller findings tighten the cost predictions, the decision tree, and the verification protocols.

---

## HIGH severity

### H1 — Path A's `past_plane && inside_cone` logic is wrong at the wall plane itself

The algorithm spec:

```glsl
bool past_plane  = (t > hitDist + missEps);
bool inside_cone = (lateral <= cone_r);
wvis = (past_plane && inside_cone) ? 0.0 : 1.0;
```

Walking through the corner cases:

- `t < 0` (surface behind probe relative to bdir) → `past_plane = false` → `wvis = 1.0` ✅
- `0 ≤ t ≤ hitDist` (surface between probe and wall) → `past_plane = false` → `wvis = 1.0` ✅
- `t > hitDist + missEps`, lateral ≤ cone_r (surface past wall, within wall's lateral extent) → `wvis = 0.0` ✅
- `t > hitDist + missEps`, lateral > cone_r (surface past wall but lateral to wall) → `wvis = 1.0` ✅

OK the on-paper logic is correct. **But** there's a subtler bug: the `cone_r = hitDist * uConeTan` formula gives the cone radius **at the wall plane**, not at the surface point's projection along bdir. The lateral test should compare against the cone radius **at the surface's `t`**, not at `hitDist`. If the surface is well past the wall (large `t`), the cone widens — and the surface might still be "inside the cone" geometrically. Conversely, a near-wall surface might be technically inside the cone-at-wall-plane but well outside the cone-at-its-own-t.

The geometrically correct test: "is the surface within the cone the bin's solid angle defines, FROM THE PROBE?" That's `lateral / t ≤ tan(θ_half)`, i.e., `lateral ≤ t * tan(θ_half)`. The wall plane test `past_plane` is independent — it checks whether the surface is past where the wall geometry actually is.

So the right combined test is:

```glsl
bool past_plane  = (t > hitDist + missEps);
bool in_bin_cone = (lateral <= max(t, hitDist) * uConeTan);  // cone widens with distance
wvis = (past_plane && in_bin_cone) ? 0.0 : 1.0;
```

`max(t, hitDist)` ensures the cone radius is large enough to cover the surface point if it's past the wall plane (use `t`) but doesn't shrink below the wall-plane cone radius if the surface is at `t < hitDist` (which can't happen if `past_plane` is true, but defensive).

**Severity:** the existing spec under-occludes when surface is past wall but moderately lateral — produces leaks the cone correction was supposed to retain occlusion for. Quality test would fail in the same lit_floor region for a different reason.

### H2 — `θ_half` derivation is wrong for octahedral mapping

The plan states:

> The bin covers solid angle ~4π/D² steradians (octahedral mapping). The half-angle of the equivalent cone is approximately:
>     θ_half = acos(1 − 2/D²)

This is the half-angle of a spherical cap with area 4π/D² — correct for a uniform hemisphere partition into D² cells. But **octahedral mapping is not uniform-area**: bins near the equatorial fold (z = 0 in the octahedral projection) cover **less** solid angle per UV unit than bins at the octahedron's vertices, and the mapping introduces a 2× area variation across the hemisphere.

Using a single global `θ_half` will over-occlude in some directions and under-occlude in others. The right answer is either:

- **Per-bin θ_half:** compute the actual solid angle of each (dx, dy) bin from the octahedral Jacobian. Adds compute per bin (maybe a small LUT).
- **Conservative single value:** use the largest bin's solid angle (over-occludes least) or smallest (under-occludes least) depending on which failure mode is worse.

The plan implicitly assumes a uniform-area mapping. The verification protocol's "tunable `uConeTan` slider" can absorb some of this error empirically, but the spec should acknowledge the assumption.

**Severity:** the actual quality outcome of Path A depends on whether the mapping non-uniformity matters in practice. May be small (the variation is within ~2×, and the test is binary so small lateral-distance errors don't matter), but the plan's "approximate" framing under-states the issue.

### H3 — Bake-leak test scene is asserted to exist but isn't verified

Phase 2 verification §4:

> Use a closed-room test scene (Cornell-orig with all walls opaque, lit only from the front).
> Capture the directional atlas at a probe deep inside an occluded region (behind the back wall).

**Cornell-orig has no "behind the back wall" region** — it's a closed box. Any probe placed past the wall would be **outside the geometry**, in space the SDF treats as exterior. There is no "occluded region" inside an opaque-walled Cornell.

To do this test we need either:
- A scene with a closed alcove inside a larger room (geometry the existing OBJ assets don't have)
- A modified Cornell with an extra interior partition (need to author or hack)
- An analytic-SDF scene (e.g., two parallel walls forming an alcove)

The plan's verification §4 will **fail to execute** as written. Need to either:
- Spec a new test scene as a Phase 2 prerequisite (with cost/time estimate), OR
- Replace the test with one using existing assets (e.g., Sponza alcove behind a column — but then "occluded region" is harder to define quantitatively).

**Severity:** the bake-leak question is the **gating factor** between "Path B is mandatory" and "Path B is optional cleanup." If the test can't be executed, this gate isn't decidable — and the plan's recommendation to defer Path B may be wrong.

---

## MEDIUM severity

### M1 — Path A cost estimate is hand-wavy

The plan writes:

> Total per pixel: 8 corners × D² × 6 ops ≈ 3000 extra ops/pixel. At 921k pixels = 2.8B ops. At GPU throughput ≈ 1 TFLOP, that's ~2.8 ms. Predicted raymarch cost: ~18 ms.

The "1 TFLOP" is a guess; this machine's actual throughput on this specific shader (with its existing memory traffic) was already measured: Mode 4's per-bin work was estimated at ~2.4B ops and the actual cost was +5.1 ms over Mode 0. So the **measured** ops/ms ratio is ~470M ops/ms (~0.47 TFLOP effective in this shader, half the headline figure). Path A's 2.8B extra ops would then add ~6 ms, not ~3 ms.

**Honest revised expectation:** raymarch ~20–22 ms after Path A (vs Mode 4's 15.4 ms, Mode 0's 10.2 ms). That's **+95–115% over Mode 0**, not +75%. The cost story for Path A is even worse than the plan states.

This matters because the decision tree branch "user accepts +75% raymarch cost" is calibrated to the wrong number. Should be calibrated to "user accepts ~+100% raymarch cost," which most users would reject.

### M2 — Path B cost prediction "≤ Mode 0 + 5%" is too optimistic

The plan claims Phase 2 will be "near Mode 0 cost ± 5%" because:

> The only added op is the α multiply, which is essentially free.

But Phase 2 **adds α to the per-bin weight** (`w = wcos * a.a`). Mode 0 currently does `w = wcos`. The addition of one multiply is cheap, but the consequence is that **bins with a.a = 0 still contribute 0 to the sum** — which means renormalization (`num/wsum`) becomes necessary again, which Mode 0 doesn't do today.

Actually wait — re-reading Mode 0: it does NOT renormalize; it just computes a weighted sum and divides by wsum at the end of `sampleProbeDir`. So Phase 2 inherits the same divide. OK, the renormalization claim is not added cost.

But **branch divergence** from `wcos > 0` early-exit (existing in `sampleProbeDirPerBinOccluded` and `sampleProbeDirDepthAware`) doesn't exist in `sampleProbeDir`. Mode 0 always reads all D² bins. Phase 2 inherits Mode 0's structure. So no divergence concern.

**Revised honest prediction:** Phase 2 raymarch cost ≈ Mode 0 ± single-digit %. The plan's "±5%" is plausible. **But it's not measured; it's a prediction.** The plan should hedge more explicitly — call it "expected to match Mode 0 cost; verify in Step 5" rather than asserting "predicted: ≤ Mode 0 + 5%".

### M3 — Decision tree's Q2 ("user accepts +50% raymarch cost") is forced-choice without data

The user has **never been asked** whether +50% raymarch cost is acceptable. The plan presents it as a yes/no branch but provides no information for the user to decide:

- What does +50% raymarch cost mean for **frame rate** at the resolutions/scenes the user actually uses? (At 1280×720 in Sponza, raymarch is ~10 ms of a ~52 ms frame. +5 ms is ~10% frame increase. At higher resolution where raymarch dominates, it's worse.)
- What does +50% raymarch cost mean for **GPU power/heat** (if the user runs on a laptop)?

The Q2 branch needs a richer prompt than "accept yes/no". Suggestion: add a "Q2 prep" step that quantifies the cost in user-meaningful terms (target framerate at target resolution).

### M4 — Decision tree assumes the user has tried Mode 4 at "any viewpoint"

Q1: "Is bake-time leak materiality already proven (user reports leaks in static scenes at any viewpoint, even with Mode 4 enabled)?"

But the user's only experience with Mode 4 is the captures **we** generated at cam.md. They haven't actually been told to try it manually at other viewpoints. Q1 is asking the user a question they can't answer.

Fix: split Q1 into two parts:
- **Q1a (bake-leak hypothesis check):** Does the user have prior knowledge of bake-time leaks in this scene at any viewpoint? (Most likely answer: no.)
- **Q1b (empirical leak test):** Run a 30-min manual A/B session — load Sponza/Cornell, navigate to alcove and corner viewpoints, toggle Mode 4 on/off, look for cross-wall light bleed. **This becomes a prerequisite to Path A vs Path B sequencing.**

### M5 — Path A's cone test doesn't address `glDrawElements` or `gi_blur` cost overheads

The Phase 1 timing data showed:
- `gi_blur`: +17% under Mode 4 (suspicious — likely noise but unexplained)
- `glDrawElements`: +6% under Mode 4

Path A doesn't touch these. If they're real (not noise), Path A's verification needs to call out that these cross-mode timing differences exist independent of the visibility mode change. Otherwise a future reader will see Path A's "expected raymarch +18ms" and not realize there are other ~0.5ms shifts they should investigate.

**Fix:** add a verification note: "if gi_blur cost varies by >5% across Path A captures, file as a separate timing-noise question; don't conflate with the Path A change."

### M6 — Phase 2 doesn't say what happens to existing Mode 4 in the decision tree

The decision tree branches don't explicitly handle: "Path A passes, Path B is later scheduled — what does Mode 4-with-cone do during the Path B development?" Is it the new default? Does it stay as default until Phase 2 ships? What happens to the existing `--visibility-mode=N` users who have scripts that set mode 4?

Fix: add an explicit "during Path B development, Mode 4-with-cone is the default; CLI/scripts continue to work; Phase 2C cleanup deletes them with the deprecation grace period documented in the unified plan."

---

## LOW severity

### L1 — `θ_half` formula uses `acos` but the shader will use `tan(θ_half)` directly

The plan derives `θ_half = acos(1 - 2/D²)` then `tan(θ_half)`. For D=8, `1 - 2/64 = 0.96875`, `acos(0.96875) ≈ 0.2515 rad`, `tan(0.2515) ≈ 0.257`. The roundtrip through `acos`/`tan` is unnecessary — `tan(acos(x)) = sqrt(1-x²)/x`, so `tan(θ_half) = sqrt(1 - (1-2/D²)²) / (1-2/D²)`. For D=8: `sqrt(1 - 0.93848) / 0.96875 = sqrt(0.06152) / 0.96875 ≈ 0.248/0.969 ≈ 0.256`. Matches.

Computing this CPU-side once per cascade and uploading as a uniform avoids any shader trig. The plan implies this but the spec snippet shows the GLSL uniform `uConeTan`, which is correct. Just a documentation polish: state explicitly that `uConeTan` is computed CPU-side.

### L2 — Verification protocols don't specify the binary or branch state

When Path A and Path B captures are run, what's the build state? Released main with the impl applied, or a feature branch? Plan should say: "captures taken from Release build of feature branch `phase1.5` / `phase2`, head of branch must be the impl commit referenced in the impl doc."

### L3 — "Files this plan would produce" lists doc paths that may collide

`visibility_phase1.5_impl.md` and `visibility_phase2_impl.md` are reasonable but inconsistent with the existing convention `visibility_unified_plan_phase1_impl.md`. Either follow the convention (`visibility_unified_plan_phase1.5_impl.md`) or break from it deliberately and explain why.

### L4 — Out-of-scope list omits an important deferral

The plan omits **per-cascade α** as out-of-scope. The current Phase 2 spec stores the same `α = 0.0/1.0` across all cascades. But cascades sample at different distance scales — a wall that's transparent to a far-cascade probe (because its hit distance is much less than the cascade's interval) may be opaque to a near-cascade probe at the same world position. The current spec doesn't distinguish.

Add to out-of-scope: "Per-cascade α-derivation logic — initial Phase 2 uses the same hit/miss/sky classification rule per cascade; cross-cascade α-coherence is a future Phase 2.5 if quality issues emerge near cascade boundaries."

### L5 — Cone correction doesn't acknowledge depth-test failure mode

If `t < 0` (surface behind probe) AND `lateral` is large (surface far off-axis), the current logic returns `wvis = 1.0` (visible). But geometrically, a surface that's far behind the probe AND lateral is way outside the bin's cone — the probe's sample for that bin direction has nothing to do with that surface. Treating it as visible is fine (over-includes minor noise), but **the renormalization weight doesn't compensate** if many such bins all contribute their irrelevant radiance.

Mostly a non-issue (the cosine weighting `wcos = max(0, dot(bdir, normal))` already excludes back-facing bins), but worth a verification note: "test that surfaces facing away from a probe don't pick up cross-volume radiance through the new cone test."

---

## Editorial / framing

### E1 — TL;DR claims Path B "may be faster than Mode 0" without measurement caveat

Section's first paragraph: "May also be faster than Mode 0 at render time (no per-bin visibility ops, no outer probeVisibility, no renormalize)." **Mode 0 also has no outer probeVisibility and no renormalize today.** The "no renormalize" comparison is to Mode 4, not Mode 0. The TL;DR over-sells Path B's performance by mixing baselines.

Fix: "May match Mode 0 cost (no added per-bin visibility test; cost change is the α multiply only — essentially free) and is **strictly faster than Mode 4** which has the per-bin compare."

### E2 — Section §3 says "this re-derives Phase 2 from the unified plan, source of truth"

This creates two source-of-truth docs for Phase 2: this combined plan AND `visibility_unified_plan.md`. Future readers will not know which is current. Fix: add an explicit "When this plan is approved, the unified plan's Phase 2 section becomes historical; refer to this plan §3 for the current spec."

### E3 — Section §4 decision tree presents 3 questions but only 2 have crisp criteria

Q1 has a yes/no (leaks-reported-or-not). Q2 has a yes/no (cost-acceptance). Q3 is "want default-flip blocked on secondary?" — phrased as preference, not decision. The plan should either:
- Convert Q3 into a measurable criterion (e.g., "is Mode-3-quality match required for default-on, or is Mode-4-quality acceptable?"), or
- Drop Q3 entirely and let Q1+Q2 decide. Q3 muddies the tree.

### E4 — Sections §6 and §7 partially overlap

§6 ("Open risks") includes "Bake-time leak test scene doesn't exist yet" which is a planning gap. §7 ("Files this plan would produce") doesn't list the test scene as a deliverable. Either §7 should list it, or §6 should escalate it from "risk" to "prerequisite work." Currently it's filed in both places without clear ownership.

### E5 — No explicit "what happens if everything goes well" timeline

Plan describes paths and decision gates but doesn't say "if Path A passes today + Path B ships in 2 weeks, here's the order of events the user will see." Users care about milestones and dates, not just decision branches. Add a "Happy-path timeline" subsection at the end of §4.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | Path A's cone-radius scaling is wrong at large t; uses cone-at-wall-plane instead of cone-at-surface-projection |
| H2 | HIGH | `θ_half` derivation assumes uniform-area octahedral bins (false; varies up to 2×) |
| H3 | HIGH | Phase 2 bake-leak test scene asserts "Cornell-orig has occluded region" — it doesn't (closed box, no inside-the-wall) |
| M1 | MEDIUM | Path A cost estimate uses "1 TFLOP" guess instead of the measured 0.47 TFLOP-effective from Phase 1 |
| M2 | MEDIUM | Phase 2 cost prediction "≤ Mode 0 + 5%" is asserted, not measured |
| M3 | MEDIUM | Decision tree Q2 asks user to accept +50% cost without quantifying frame-rate impact |
| M4 | MEDIUM | Decision tree Q1 asks user a question they have no data to answer (no manual A/B at other viewpoints) |
| M5 | MEDIUM | Path A doesn't address why gi_blur/glDrawElements timings shifted in Phase 1 captures |
| M6 | MEDIUM | Decision tree doesn't say what happens to Mode 4 + CLI users during Path B development |
| L1 | LOW | `θ_half` derivation uses unnecessary trig roundtrip |
| L2 | LOW | Verification doesn't specify build state / branch |
| L3 | LOW | Doc filename inconsistency with existing `visibility_unified_plan_*` convention |
| L4 | LOW | Per-cascade α not in out-of-scope list |
| L5 | LOW | Cone correction's behind-probe + far-lateral case unspecified |
| E1 | EDIT | TL;DR over-sells Path B perf vs Mode 0 (mixes baselines with Mode 4) |
| E2 | EDIT | Two source-of-truth docs for Phase 2 (this plan + unified plan) |
| E3 | EDIT | Q3 is preference-shaped, not measurable; muddies the tree |
| E4 | EDIT | §6/§7 ownership of "test scene needed" is unclear |
| E5 | EDIT | No happy-path timeline |

---

## Top 5 actions for the plan revision

1. **Fix H1 — Path A cone test logic.** Use `lateral ≤ max(t, hitDist) * uConeTan`, not `lateral ≤ hitDist * uConeTan`. This is a correctness bug in the spec.
2. **Fix H3 — Phase 2 bake-leak test scene.** Either spec a new scene as Phase 2 prerequisite work (with cost), or replace the test with one usable on existing assets. Cornell-orig as written can't be used.
3. **Fix M1 — Path A cost estimate.** Recalibrate against the measured ~0.47 TFLOP-effective from Phase 1 captures. Honest expectation: ~+95–115% raymarch cost, not +75%.
4. **Fix M3+M4 — Decision tree.** Quantify Q2 in user-meaningful terms (frame rate at target resolution); restructure Q1 as a prerequisite empirical test rather than a question the user can't answer.
5. **Fix H2 — `θ_half` derivation.** Acknowledge octahedral non-uniformity; either add per-bin θ derivation or document the conservative single-value choice with rationale.
