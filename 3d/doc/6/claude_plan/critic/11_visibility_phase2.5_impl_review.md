# Critic Review 11 — `visibility_phase2.5_impl.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-14
**Verdict:** Plan execution honest about the soft-α failure (Tier 3 fail correctly triggered the revert), but **two HIGH and three MEDIUM findings** identify real issues with the bake-leak metric implementation, the audit completeness, and the cleanup choices. Most importantly: **the bake-leak baseline numbers are partially compromised** because (a) the metric was run with the default Phase 2 binary-α encoding which counts both surface-α=0 AND sky-α=0 bins (they're indistinguishable in current encoding), inflating the numbers by including legitimate sky radiance — the impl doc acknowledges this for Sponza but NOT for the alcove scene which it claims is rigorous.

---

## HIGH severity

### H1 — Bake-leak baseline INCLUDES SKY-EXIT bins, not just leaked-through-wall bins

The metric filter (impl + doc):

> If `α < 1e-3` (occluded — surface OR sky terminal), accumulate `length(rgb)`.

Per Phase 2's encoding:
- Surface hit → α = 0 ✓ (this is what we want to measure — leaked wall radiance)
- **Sky exit → α = 0 ✗** (this is sky-fill radiance, NOT a leak — we don't want to count it)
- In-volume miss → α = 1 (correctly excluded)

Both surface-hit and sky-exit have α=0 in current Phase 2 encoding (by design — both terminate radiance composition). The metric can't distinguish them.

The impl doc **acknowledges this for Sponza** ("legitimate sky-bin radiance gets counted") but explicitly claims rigor for cornell-orig-alcove:

> The bake-leak metric only rigorously measures cornell-orig-alcove.

But cornell-orig-alcove ALSO has sky-exit bins at the volume boundary. The Cornell box's geometry occupies `x ∈ [-1.02, 1.0]` but the SDF volume is `[-2, 2]`. **Probes near the alcove side (x > 0.30) but outside the box (x > 1.0) ARE sky-exit territory.** Their bins would be classified as α=0 (sky), and `length(rgb)` would include the sky env-fill color.

**Effect on baseline numbers:** the C0 leak=4937.8 figure is INFLATED by sky-exit contributions from probes at the volume edge. The actual "wall-leak radiance" is some smaller subset.

**Fix options:**
- (a) Restrict the alcove filter further: `0.30 < world.x < 1.0` (only inside the Cornell box). Most rigorous.
- (b) Add hit/sky discrimination to the bake (Option B encoding from 2.5a.3 — but ε=1e-3 wasn't actually applied; Phase 2's binary α=0 was kept). Without the encoding change actually landing, the metric can't disambiguate.
- (c) Re-run with the Option B encoding partially landed (just the sky=strict 0 / surface=ε for the binary case; no soft α). This was effectively what the failed 2.5b had — and the encoding worked there even though the soft-α didn't.

The impl doc claims rigor it doesn't have. **Either the metric needs the alcove-plus-Cornell-box filter, OR the Option B encoding needs to land minimally (sky=0, surface=ε) so the metric can distinguish.**

### H2 — `reduction_3d` audit was done BY READING THE SHADER, not by running multi-frame timing

The impl doc says:

> No code change. Audit doc not separately written; this section IS the audit doc.

The audit found no α-coupled code path — fine, that's the right verdict for a code audit. But the plan §2.5a.2 explicitly called for **N=3 averaged timing captures** to confirm the +42% is noise vs real. Code audit answers "is there a code bug?"; multi-run timing answers "does the +42% persist?" These are different questions.

The impl skipped the timing measurement entirely, then concluded "+42% is GPU scheduling noise." That conclusion isn't supported by the actual audit work — it's a guess that becomes plausible because no code bug was found, but isn't measured.

**Honest fix:** either (a) actually run N=3 averaged captures and report numbers, or (b) state in the impl doc that timing was NOT measured, only the code was audited.

The current "GPU scheduling noise" claim in the impl doc is unjustified. Could equally well be a real GPU-side perf characteristic (cache pressure from new α distribution, branch divergence change, anything driver-side that no shader audit could catch).

---

## MEDIUM severity

### M1 — 2.5b failure analysis missed a simpler diagnosis

The impl doc identifies the failure root cause as "SDF half-voxel before hit doesn't capture head-on vs grazing." Correct as far as it goes, but a simpler diagnosis: **the smoothstep range `[0, voxelSize]` is too aggressive regardless of what the input metric is.**

If the SDF-before-hit value for typical hits is, say, half-voxel (0.5 × voxelSize), then `smoothstep(0, voxelSize, 0.5 × voxelSize) = 0.5` (the smoothstep midpoint). That gives α=0.5 for typical hits — not "near zero" as the doc claims. So MOST surface bins would be at ~0.5 α, not ε.

But the result was clearly much darker than that would predict. So either:
- The typical SDF-before-hit value is well below 0.5 × voxelSize (close to 0), giving α near ε for most hits.
- Or the smoothstep is being applied to NaN / Inf values (the SDF sample at `worldPos + rayDir * sampleDist` could exit the volume for grazing hits, giving sentinel values).

**The impl doc didn't actually diagnose WHICH of these is true.** The "SDF doesn't capture head-on vs grazing" framing is a conceptual diagnosis without measurement. A proper post-mortem would print the SDF values for a few representative hits and check the distribution.

### M2 — Encoding decision (Option B with ε=1e-3) was PINNED but never APPLIED

Plan §2.5a.3 picks Option B. Impl doc claims it was "applied to the failed 2.5b attempt." But after 2.5b reverted, **the Option B encoding was reverted along with it** — the bake still uses Phase 2's pure binary α=0 for both surface and sky. The encoding decision is filed but not active in shipped code.

This means:
- The bake-leak metric (H1) can't distinguish sky from surface — Option B WOULD have helped if it had landed.
- Phase 2.6 (soft α retry) will need to land Option B AGAIN as a prerequisite.

**The impl doc should clarify**: "Option B is the chosen encoding when next needed; not currently active in the bake." Currently the doc reads as if Option B is in effect.

### M3 — Cleanup of `--visibility-mode=N` may break user scripts unannounced

The plan §4.1 said "Phase 2's deprecation grace period was 'one release.' Phase 2.5 ships in the same release." The cleanup deleted the flag.

But Phase 2 already shipped to the user (per the `phase2_followup_v40_default_flip.md` decision-gate doc that landed before Phase 2.5). The user MAY have scripts that pass `--visibility-mode=N`. Pre-2.5c those scripts got a deprecation warning + continued. Post-2.5c they get NOTHING — the flag is silently slipped through (the impl doc confirms: "no warning, no behavior").

A silent failure mode is worse than the previous noisy one. If a user's script depends on the flag behavior (even just for "set mode 4 explicitly"), that script now silently does the default — no error message tells them why.

**Better:** keep the deprecation stub for one MORE release (now that we know external users may exist), OR have main3d.cpp print "WARN: unknown argument: --visibility-mode=4" when it encounters unrecognized flags.

The impl took the aggressive cleanup path; the plan justified it by "Phase 2.5 ships in the same release as Phase 2." But the actual sequence was: Phase 2 shipped; Mode 4 became default; user did A/B testing; Phase 2 was the apparent release. Phase 2.5 is a follow-up. The "same release" justification is shaky.

---

## LOW severity

### L1 — Phase 2.6 filing has no concrete derivation candidate

The impl doc lists three candidates for a future soft-α derivation (ray-normal dot, cell-boundary distance, hit-distance-fraction-within-interval). All three are bullet-pointed without any analysis. A real Phase 2.6 plan would need to evaluate each. Filing is fine; no action needed now.

### L2 — Cornell-orig-alcove camera viewpoint deferral is a concrete usability gap

The plan called for an alcove preset. Impl doc says "auto-fit works 'well enough'" and documents the manual `--camera-pos=...` flags. But anyone running the bake-leak test in the future has to read the impl doc to find the right camera. Pre-defined `--cam-preset=alcove` would have been ~5 lines of code. Skipped for "scope reasons" but it's small enough that it could have shipped.

### L3 — Atlas debug viewer label deferral compounds with Phase 2.5a.1 visibility

Now that the bake-leak baseline is measurable (and large — 4937.8 units of leaked C0 radiance), the existing atlas debug viewer mode will display this leakage prominently. Without a label, a user toggling that mode might think the renderer is wrong. The plan called for "Quick: add a tooltip." Took ~5 minutes; skipped.

### L4 — The metric's "alcove filter" hard-codes `world.x > 0.30`

The partition is at x=0.30 in the cornell-orig-alcove .obj. The metric's hard-coded `0.30` couples the metric code to the asset. If the .obj is ever modified (partition moved), the metric silently produces wrong numbers. Should at minimum be a named constant or read from the scene metadata.

### L5 — Per-frame trigger increments only on `cascadeReady` — could deadlock

The trigger logic:

```cpp
if (bakeLeakTestPending && !bakeLeakTestDone) {
    if (cascadeReady) ++bakeLeakElapsedFrames;
    if (bakeLeakElapsedFrames >= bakeLeakTestFramesAfter) {
        computeBakeLeakMetric();
    }
}
```

If `cascadeReady` is never set true (e.g., a scene that fails to bake — unlikely but possible), `bakeLeakElapsedFrames` never increments, the trigger never fires, the JSON never writes. The user sees "frames quit at 300" but no `[bake-leak]` output and no JSON. They'd have no easy way to diagnose what went wrong.

Mitigation: add a "frames elapsed without cascade ready" warning at the end if `bakeLeakTestPending && !bakeLeakTestDone` when render exits.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | Bake-leak baseline INCLUDES sky-exit bins; "rigorous for cornell-orig-alcove" claim is wrong because the alcove probes at volume edge include sky-exits |
| H2 | HIGH | reduction_3d audit only read shader; never ran the N=3 timing the plan called for; "GPU scheduling noise" is unjustified |
| M1 | MEDIUM | 2.5b failure analysis is conceptual; never measured the actual SDF-before-hit distribution to confirm root cause |
| M2 | MEDIUM | Option B encoding was pinned in 2.5a.3 but never landed in shipped code; the impl doc reads as if it's active when it isn't |
| M3 | MEDIUM | --visibility-mode=N silently slips post-cleanup; users with scripts get no error |
| L1 | LOW | Phase 2.6 filing has no concrete derivation evaluation |
| L2 | LOW | Cornell-orig-alcove camera preset skipped for ~5 lines of work |
| L3 | LOW | Atlas viewer label skipped for ~5 minutes of work |
| L4 | LOW | Alcove filter hard-codes x=0.30; brittle to .obj changes |
| L5 | LOW | Bake-leak trigger could silently deadlock if cascadeReady never sets |

---

## Top actions for revision

1. **Fix H1**: tighten the alcove filter to `0.30 < x < 1.0` (inside the Cornell box only) AND/OR land the Option B encoding's sky=strict 0 / surface=ε change so the metric can distinguish. Re-run the baseline; report corrected numbers.
2. **Fix H2**: either run the N=3 timing campaign and report results, or change the impl doc's "GPU scheduling noise" claim to "code audit found no α-coupled bug; timing not re-measured."
3. **Fix M2**: clarify in the impl doc that Option B encoding is filed-not-active. Phase 2.6 (or whatever revisits soft α) needs to land it as a prerequisite.
4. **Fix M3**: restore a deprecation warning for `--visibility-mode=N` (since the user already had Phase 2 in their hands), OR add a "WARN: unknown argument" handler to main3d's parser to catch silent-typo cases generally.
5. **Fix L4**: replace the hard-coded `0.30` in `computeBakeLeakMetric` with a named constant or argument so the metric isn't silently coupled to the .obj geometry.
