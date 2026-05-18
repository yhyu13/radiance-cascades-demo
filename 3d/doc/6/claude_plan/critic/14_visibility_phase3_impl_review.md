# Critic Review 14 — `visibility_phase3_impl.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-15
**Verdict:** Code is correct, default-OFF ship is right, build/smoke tier passes. **But the impl doc papers over one HIGH issue (the bit-exact assertion is logical, not measured) and undersells two MEDIUMs (the diagnosis is plausible but unverified; the leak-metric pivot is consequential but buried in §"What's next").** **2 HIGH, 3 MEDIUM, 2 LOW.**

---

## HIGH severity

### H1 — "Bit-exact regression with `uUseWeightedSample==0`" is asserted, not measured

The impl doc Tier 1 section says:

> **Pass implicitly.** The trilinear path now reads:
> ```glsl
> if (uUseWeightedSample != 0) sampleUpperDirWeighted(...)
> else sampleUpperDirTrilinear(...);
> ```
> Default OFF → never enters the new branch → identical instructions executed. **Not formally bit-tested with a binary diff** (would need a comparable golden image; postponed to verification follow-up if requested).

This is the same logic-vs-evidence trap critic 8 flagged on Phase 2's "trivially obvious" claims. The merge formula was changed from:

```glsl
rad = hit.rgb * l + upperDir.rgb * (1 - l);
```

to:

```glsl
rad = hit.rgb * l + upperDir.rgb * (1 - l) * upperDir.a;
```

**For default OFF, `upperDir.a == 1.0`** — IF the OFF branches always set `.a = 1.0`. Let me trace:

- Trilinear OFF branch (`uUseWeightedSample==0`): calls `sampleUpperDirTrilinear`, which does `mix()` over 8 corners' `.a`. **The 8 corners' `.a` come from the upper cascade's atlas — which can have α=0 for opaque surface bins** per the Phase 2 binary encoding. So `upperDir.a` is NOT necessarily 1.0; it's a trilinear blend of the upper cascade's per-bin α (0 or 1).
- The pre-Phase-3 merge formula `* upperDir.rgb` ignored `.a` entirely. The new `* upperDir.rgb * (1 - l) * upperDir.a` uses it.
- **Therefore default-OFF behavior IS NOT bit-exact** to pre-Phase-3 in the trilinear path. It changes the merge to gate the upper contribution by `(α-blended)` whenever the upper cascade has any opaque bin in the 8-corner neighborhood.

This is a real change, not just refactor. The "default OFF preserves Phase 2 exactly" claim is wrong for the trilinear path. It IS preserved for the OTHER three branches (single-probe, isotropic-bilinear, isotropic-texelFetch) because those explicitly set `.a = 1.0` before reaching the merge.

**Severity HIGH because**:
- The impl doc tells the user the default is safe; users may upgrade expecting no change.
- The render-side image-diff (RMSE 0.0069) was attributed to "Phase 3 ON having effect" — but some/all of that delta may be the OFF-mode's silently-changed merge formula.
- Without a TRUE bit-exact OFF gate, we can't separate "Phase 3 v1 works" from "the merge formula change ALONE has an effect."

**Fix paths**:
1. **Easy fix (revert the OFF-path semantics)**: in the merge formula, only apply `* upperDir.a` when `uUseWeightedSample != 0`. Adds one branch:
   ```glsl
   float aFactor = (uUseWeightedSample != 0 && uHasUpperCascade != 0) ? upperDir.a : 1.0;
   rad = hit.rgb * l + upperDir.rgb * (1 - l) * aFactor;
   ```
   Or equivalently, `sampleUpperDirTrilinear` could clamp its returned `.a` to 1.0, but that would silently zero useful upper-cascade `.a` info downstream.
2. **Or**: explicitly document that the OFF-mode merge formula CHANGED (and may have its own effect) and re-measure A/B with TRUE pre-Phase-3 build (git stash + measure).

The fix-path-1 change is ~2 lines and restores the "default OFF is bit-exact" property that the doc claims. Strongly recommended.

### H2 — The Tier 3 leak-metric verdict was filed as "fail"; the diagnosis was filed as "metric is wrong" — these can't both be true without verification

The impl doc says:

> Per the plan's gate, this is Tier 3 → revert to 3a-only ship.

then later:

> The render-side image-diff (76% pixels affected, 2% global darkening, RMSE 0.0069) is currently a **more reliable signal that Phase 3 is doing real work** than the bake-leak metric.

These are contradictory positions: either the metric is the success criterion (in which case Tier 3 fail → revert) or the metric is unreliable (in which case the Tier 3 verdict is meaningless — what's the actual ship/no-ship gate?).

The impl doc resolves this by shipping default-OFF, which is operationally fine but logically muddy. **A proper resolution requires committing to one of**:
- "v1 works; the metric was wrong; ship default-on" (defensible only if you can produce a metric Phase 3 actually moves)
- "v1 doesn't work; revert" (defensible only if you accept the leak metric's verdict at face value)
- "v1 inconclusive; ship default-OFF; defer pending better metric" — what we did, but **the impl doc should call this out as the actual position**, not file as "Tier 3 fail" then quietly disagree with itself.

**Severity HIGH because**: this is the kind of muddied verdict that comes back to bite later — a future developer reading the doc sees "Tier 3 fail" and assumes Phase 3 was a dead end, missing that the metric itself was suspect.

**Fix**: rewrite the "Honest assessment" + "What's next" sections to commit to "v1 INCONCLUSIVE pending revised metric. Default-OFF ship preserves optionality. Tier 3 verdict is contingent on the current metric being sensitive to Phase 3's leverage, which the diagnosis says it isn't."

---

## MEDIUM severity

### M1 — Diagnosis (1)/(2)/(3) is plausible but not verified

The impl doc lists three explanations for why v1 didn't move the leak metric:
1. Most leak bins at `l == 1.0` → Phase 3 has no leverage
2. Leak dominated by `hit.rgb` Lambertian, not upper contribution
3. Cone correction over-permissive at high `lProbeRayDist`

**None of these are verified.** They are reasonable hypotheses but the impl doc files them as the explanation. A 30-min instrumented test would distinguish:
- Add a debug uniform `uDiagWeightedSample` that, when set, writes `(1 - l)` or `upperDir.a` to a debug atlas channel.
- Re-run leak metric with the debug channel; compare distribution.
- Confirms which hypothesis (or which combination) actually dominates.

Without this, the v2/v3 plan ("rethink the leak metric"; "tune the cone correction") is shooting in the dark.

**Severity MEDIUM** (not HIGH because the conclusion — "v1 is inconclusive on the metric, ship default-OFF" — is robust to which hypothesis is right). **Fix**: file the verification as an explicit follow-up before any v2/v3 work.

### M2 — The `upperProbeWorld` derivation in the shader uses `uUpperProbeCellSize`, not `uUpperCellSize`

The impl doc's "Plumbing notes" section says:

> The shader can use `uGridOrigin + (vec3(cornerPos) + 0.5) * uUpperProbeCellSize` directly. Net plumbing cost: 1 new uniform (the cone sin), not 3.

The variable is named `uUpperProbeCellSize` (existing); the plan called it `uUpperCellSize`. The impl uses the existing name correctly, but the impl doc (in the algorithm spec section, if I had reproduced it) and the plan should be reconciled — future readers tracing plan→impl may be confused. Minor naming mismatch.

**Fix**: footnote or note in the impl doc: "Plan §3 named this `uUpperCellSize`; impl reuses existing `uUpperProbeCellSize` (same value). No plan revision needed; just record the alias."

### M3 — The cone correction `sin(theta_half) = 0.248` for D=8 is the AVERAGE-area value but the impl uses it for the look-back bin, not the average

The cone-half-angle derivation (`cos(θ_half) = 1 - 2/D²`) gives the half-angle of an EQUAL-area spherical cap covering 1/D² of the sphere. This is the ASYMPTOTIC average. For real octahedral bins the per-bin solid angle varies — bins near the octahedral "corners" have different shapes than bins on the "faces."

Critic 7 H2 flagged this. The plan's v2 fallback addresses it (per-bin LUT). But the impl doc doesn't note that **v1 uses the average; the per-bin variation may already be ±20-30% from the average**, which could flip "barely visible" corners on either side. This may be partially responsible for the v1 result.

**Severity MEDIUM** (not HIGH because it's already documented as v2 work). **Fix**: add a brief note in the diagnosis section that "(3) over-permissive cone" specifically may be CHURN from per-bin variance the average doesn't capture — making the case for v2 work clearer.

---

## LOW severity

### L1 — The CLI flag accepts `0`/`1` but the impl uses `v != 0` semantics; values 2+ are silently treated as ON

The CLI handler:

```cpp
int v = std::atoi(arg.substr(22).c_str());
demo->setUseWeightedSample(v != 0);
```

`--use-weighted-sample=2` quietly enables the toggle. Probably fine but inconsistent with `--diag-alpha-mode=N` which validates `N` against an enum. Tiny.

**Fix**: optional warn-on-other for forward-compat (in case v2 introduces multiple modes, e.g., `=2` for per-bin LUT).

### L2 — The GUI checkbox "(needs non-colocated + trilinear)" disabled-state message is correct but the `imHelpMarker` tooltip doesn't repeat the requirement up-front

A user clicking the checkbox in co-located mode sees nothing happen (it's disabled). The tooltip lists the requirement on line 2. Minor UX. **Fix**: lead the tooltip with "REQUIRES non-co-located + spatial trilinear (other paths can't carry per-corner info)."

---

## Severity summary

| ID | Severity | Issue | Action |
|---|---|---|---|
| H1 | HIGH | Default-OFF NOT bit-exact: merge formula change applies even when uUseWeightedSample==0; trilinear-OFF path now picks up `* upperDir.a` from upper cascade's α encoding | Gate `* upperDir.a` on `uUseWeightedSample != 0` in the merge formula |
| H2 | HIGH | Tier 3 verdict + "metric is wrong" diagnosis are contradictory; impl doc doesn't commit to a clear ship position | Rewrite "Honest assessment" to call this out as INCONCLUSIVE; add explicit follow-up for revised metric |
| M1 | MEDIUM | Diagnosis (1)/(2)/(3) is plausible but not verified | File debug-channel verification as explicit follow-up before v2/v3 |
| M2 | MEDIUM | Plan/impl naming mismatch (uUpperCellSize vs uUpperProbeCellSize) | Footnote in impl doc |
| M3 | MEDIUM | Cone average-area value ignores per-bin variance | Add note in diagnosis pointing at per-bin LUT (already in v2 plan) |
| L1 | LOW | CLI accepts v ≥ 2 silently as ON | Optional warn-on-other |
| L2 | LOW | GUI tooltip requirements buried | Lead with requirement |

---

## Top actions for impl revision

1. **Fix H1**: gate `* upperDir.a` on `uUseWeightedSample != 0`. Restores default-OFF bit-exactness as claimed.
2. **Re-measure A/B** after H1 fix: render diff likely DROPS significantly; confirms how much of the 76%-pixels-changed signal was from H1 vs from genuine Phase 3 effect.
3. **Fix H2**: rewrite the verdict to "INCONCLUSIVE pending revised metric." Don't claim Tier 3 fail when the diagnosis says the metric is suspect.
4. **Fix M3**: 1-line note in diagnosis tying (3) to per-bin variance.
5. **Defer L1, L2**: trivial; do if convenient or leave.
6. **File explicit follow-up**: "before any v2/v3 work, instrument the bake to write a debug channel that quantifies Phase 3's actual leverage per bin (expected to be `(1-l) * (1 - upperDir.a)`); revise the leak metric accordingly."
