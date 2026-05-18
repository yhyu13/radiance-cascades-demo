# Reply: Phase 1.5 + Phase 2 Plan Revision-1 Critic 08 — `08_visibility_phase1.5_and_phase2_plan_rev1_review.md`

**Date:** 2026-05-14
**Status:** All 8 new findings (N1–N8) accepted + both structural concerns accepted. **Plus a meta-observation the critic deserves credit for**: the user's parallel edit to the decision-gate doc (relative-criterion `× 1.3`, secondary now PASSES, verdict shifted to "cost-only blocker") **changes Path A's purpose entirely** — Path A was scoped to fix a secondary RMSE failure that no longer exists. With the secondary criterion passing, Path A's only remaining justification is "tighter Mode-3 match than current Mode 4 already provides," which is a niche optimization that **also makes the cost story worse**. The plan needs a structural reframing of Path A's role, not just N1–N8 patches.

Code/doc changes applied at the end of this reply.

---

### N1 (MEDIUM) + N3 (LOW) — Op count off, propagates into cost claim

Accepted, with a tighter recount than either the critic or my v1 had. Per-bin **added** ops for Path A cone correction (above the existing Mode 4 baseline of `dot(delta, bdir) + compare`):

| Op | Count |
|---|---:|
| `axial = t * bdir` (vec3 mul) | 1 |
| `lat_vec = delta - axial` (vec3 sub) | 1 |
| `lateral = length(lat_vec)` (dp3 + sqrt) | 2 |
| `cone_r = max(t, hitDist) * uConeTan` (max + mul) | 2 |
| `past_plane = (t > hitDist + missEps)` (add + cmp) | 2 |
| `in_bin_cone = (lateral <= cone_r)` (cmp) | 1 |
| `wvis = past && cone ? 0 : 1` (and + select) | 1 |
| **Total added ops/bin** | **10** |

Note: N3's "vec3 subtract for `delta`" doesn't add per-bin cost — `delta = surfacePos - probeCenter` is hoisted out of the bin loop in the existing Mode 4 code (computed once per corner, not per bin). Confirmed by inspection of `sampleProbeDirDepthAware`.

**Recalibrated Path A cost** (vs v1's "+5–6 ms"):

- Per-pixel added: 8 corners × 64 bins × 10 ops = 5120 ops/pixel
- At 921k pixels (1280×720): ~4.7B added ops/frame
- At Phase 1's measured 0.47 TFLOP-effective: **~10 ms added on top of Mode 4's 15.4 ms = ~25 ms raymarch**

**Honest expectation: raymarch ~+145% over Mode 0** (vs v1's claimed +105%). Total frame ~+30% (was claimed +22%). This makes Path A's cost story even less attractive than v1 stated.

Also accepting the critic's pointer about linear extrapolation breaking down — at +10ms additional raymarch, register pressure and warp occupancy may shift in ways that compound. The +25 ms predicted figure is itself an under-estimate if Path A pushes the kernel into a worse occupancy class. Verification (RenderDoc capture of Path A) is the only way to know; predictions above Mode 4's measured baseline are increasingly speculative.

### N2 (MEDIUM) — FPS table contradicts measured Phase 1 data

Accepted, and this was just sloppy. Phase 1's measured frame total was 51.8 ms = **~19.3 FPS**, not 60 FPS. The "60 FPS baseline" in v1's §4.2 was a conventional placeholder that conflicted with the measured data on the same machine.

**Fix:** the cost-tolerance table is rewritten to use measured-from-Phase-1 numbers (and the recalibrated Path A from N1 above):

| Frame cost band | Mode 4 today | Mode 4-with-cone (Path A) | Path B |
|---|---|---|---|
| Mode 0 baseline (measured) | 51.8 ms / 19.3 FPS | n/a | n/a |
| Phase 1 Mode 4 (+10.5% measured) | 57.2 ms / 17.5 FPS | n/a | n/a |
| Path A (+30% predicted, recalibrated) | n/a | ~67 ms / ~15 FPS | n/a |
| Path B (≈ Mode 0 ± single-digit %, predicted) | n/a | n/a | ~52 ms / ~19 FPS |

Caveats kept explicit: Path A row is a prediction, not a measurement. Path B row is also a prediction (per N6 critic, verified by Phase 2 §3.9 step 5). The Mode 4 row is measured.

The user can now see in measured-on-this-machine terms what each path costs.

### N4 (LOW) — `hit.a` undefined

Accepted. The plan references `hit.a` as if its meaning is universal but doesn't define it. **Fix:** added a one-line definition in §3.4:

> The bake shader writes the per-bin ray result into the alpha channel of the directional atlas: `hit.a > 0` = surface hit at distance `hit.a`; `hit.a == 0` = in-volume miss (open space all the way to interval end); `hit.a < 0` = sky exit (ray left the volume). This convention exists in `radiance_3d.comp:428` today; Phase 2 reinterprets the same encoding into a transparency-α derivation as below.

### N5 (LOW) — Decision row contradicts Phase 1 verdict

Accepted, and this is more important than the critic flagged because the user's parallel edit to the decision-gate doc **changed the verdict**: secondary RMSE now passes under the relative criterion `m4-vs-m3 ≤ m0-vs-m3 × 1.3`, and the verdict is "cost-only blocker." The v1 plan's row 3 ("default-flip Mode 4 today") was inconsistent with the v1 decision-gate verdict, but is now **consistent** with the corrected verdict.

**Fix:** rewrote §4.1 decision branches to match the corrected verdict:
- Mode 4-today (no cone) is now a defensible default if §4.0 finds no leaks AND user accepts +10% frame cost — because the secondary criterion now passes.
- Path A's purpose shifts from "fix secondary RMSE failure" (which no longer exists) to "tighten Mode-3 match further if quality demands exceed the current Mode 4 baseline."
- Added explicit cross-reference to the corrected criterion: "Phase 1 secondary now passes per [decision-gate doc §Test 1 → On the 0.02 threshold itself](visibility_unified_plan_phase1_decision_gate.md)."

### N6 (MEDIUM) — Phase 2A format-change safety needs explicit verification

Accepted. The plan called Phase 2A "no-op" but didn't verify that `.rgb` from RGBA8 == `.rgb` from RGB8 on the target driver. **Fix:** §3.8 pre-flight task #1 (fetch-site enumeration) is extended with a **2A-specific verification step**:

> After 2A lands (atlas allocation RGB8 → RGBA8, all fetch sites updated to read `.rgb` from `vec4`), capture a single Sponza frame at cam.md and diff against the pre-2A baseline. RGB RMSE should be **bit-exact zero** (no semantic change introduced). If RMSE > 0, the driver is packing/swizzling RGB8 vs RGBA8 differently and 2A is **not** a no-op — investigate before 2B.

This is a 2-minute verification but catches a real footgun.

### N7 (LOW) — Bake-leak test should specify convergence timing

Accepted. With α fresh-only and RGB EMA-blended, mid-bake reads of `bin.rgb * bin.a` are noisy. **Fix:** §3.9 step 4 protocol updated:

> Capture the atlas **after EMA convergence** (≥ N=60 frames at temporalAlpha=0.1, or with temporal accumulation OFF for a deterministic single-frame bake) so RGB values are stable. The product `bin.rgb * bin.a` then reflects only the merge-formula behaviour, not temporal noise.

### N8 (LOW) — Octahedral area-ratio claim uncited

Accepted. The "up to ~2× area variation" claim was stated as fact without derivation. **Fix:** §2.3 caveat now references the standard octahedral mapping result:

> The octahedral mapping's Jacobian is non-uniform: bins centered near the equatorial fold (z=0 in the projection) have ~1× the average area; bins centered near the octahedron's vertices (e.g. ±x, ±y, ±z axes) have ~2× the average area. (See e.g. Praun & Hoppe 2003, "Spherical parametrization and remeshing"; or any modern octahedral-mapping reference — the area ratio is a direct consequence of the projection's Jacobian and is well-known.)

---

### Structural concern 1 — Path A is dead-end unless time-urgent

Accepted, **and the user's decision-gate edit makes it more pointed than the critic stated**: with the secondary criterion now passing for Mode 4, Path A's only remaining quality argument is "match Mode 3 even tighter than the current 0.019 aggregate / 0.030 lit_floor." That's not a goal anyone has stated as a requirement.

**Fix:** §2.1 (Path A goal) reframed:

> **What Path A is no longer for.** With the corrected secondary criterion (`m4-vs-m3 ≤ m0-vs-m3 × 1.3`), the existing Mode 4 already passes secondary. Path A was originally scoped to fix that failure; it doesn't need to anymore.
>
> **What Path A might still be for.** (a) Tighter Mode-3 match if a downstream user/test requires aggregate RMSE below 0.019. (b) Subjective improvement at high-detail viewpoints if a manual A/B reveals visible Mode 4 vs Mode 3 differences. (c) Reduced over-occlusion at grazing angles if Path B is delayed indefinitely and the user is using Mode 4 in production.
>
> **Cost penalty:** ~+30% total frame vs Mode 0 (vs Mode 4's +10.5%). This is the floor; Path A makes things worse on cost, not better.
>
> **Recommendation: do NOT run Path A unless one of (a/b/c) above is actively required.** The default forward path is now Path B — or shipping Mode 4 today as default and scheduling Path B as cleanup.

This is a meaningful re-scoping, not just textual.

### Structural concern 2 — §4.0 needs an "ambiguous result" escape hatch

Accepted. The §4.0 prerequisite test row in §4.1 assumes binary "leaks observed" / "no leaks observed." **Fix:** added a third row:

| §4.0 result | Action |
|---|---|
| Clear leaks at one or more viewpoints in Mode 4 | Path B mandatory; skip Path A |
| Clean (no observable cross-wall bleed at any tested viewpoint) in Mode 4 | Decision branches per §4.1 cost tolerance |
| **Ambiguous (mild bleed at some angles, hard to tell from baked-in indirect lighting)** | **Default to Path B with normal urgency** (don't ship Mode 4 as default; the architectural fix removes the question). Path A still wasteful in this case — its cone correction doesn't address bake-time leaks. |

The added third row makes §4.0's outcome unambiguously actionable.

---

## Doc updates applied to `visibility_phase1.5_and_phase2_plan.md`

1. **N1+N3** — Op count corrected to 10/bin; cost recalibrated to ~+30% total frame (was ~+22%). TL;DR + §2.5 rewritten.
2. **N2** — §4.2 cost-tolerance table rewritten with measured Phase 1 numbers (51.8 ms baseline, 19.3 FPS); Path A row uses recalibrated +30%.
3. **N4** — Added `hit.a` definition in §3.4.
4. **N5** — §4.1 decision branches reconciled with the user's corrected secondary criterion in the decision-gate doc (Mode-4-today is now defensible if §4.0 finds no leaks AND user accepts +10% frame cost).
5. **N6** — §3.8 pre-flight task #1 extended with bit-exact-zero verification of 2A.
6. **N7** — §3.9 step 4 specifies post-EMA-convergence atlas capture for the bake-leak test.
7. **N8** — §2.3 octahedral non-uniformity claim cited (Praun & Hoppe 2003).
8. **Structural #1** — §2.1 reframes Path A's purpose: "no longer needed for secondary fix; recommended only if (a/b/c) required."
9. **Structural #2** — §4.1 adds an "ambiguous" row for the §4.0 outcome.
10. **Cross-reference to decision-gate update** — explicit pointer in TL;DR + §1 to the corrected secondary criterion in the decision-gate doc.

Items NOT applied:
- Per-bin LUT formula derivation for octahedral non-uniformity (filed as Path A 1.5-fallback, not pre-committed; would only ship if single-value `uConeTan` fails verification).
- Replacing the FPS framing in §4.2 with raw ms (kept ms+FPS dual notation for usability).

---

## Summary

The critic's most important finding was the **N5/structural-concern interaction with the user's parallel decision-gate edit**: Path A was scoped to fix a problem that, under the corrected secondary criterion, no longer exists. The plan now reframes Path A as an optional optimization (only if specific quality requirements demand it) rather than a default next step.

Net change: **Path A is now opt-in inside the plan's recommendation tree**, not the default first step. The default forward path (assuming §4.0 finds no leaks) is "ship Mode 4 today + schedule Path B as cleanup." Path A becomes the "if extra quality is demanded later" branch.

This is a substantive re-scoping driven by the combination of N5, structural concern 1, and the user's edit. The plan is materially clearer about when Path A makes sense and when it's wasted work.
