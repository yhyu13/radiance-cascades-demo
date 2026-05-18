# Critique: Phase 2.5d — Implementation Notes: Critic-11 Polish Bundle

**Document reviewed:** `visibility_phase2.5d_impl.md`
**Date:** 2026-05-15

---

## Strengths

1. **M1 empirical confirmation is excellent.** The histogram showing 99.2% of surface bins at α ≤ 0.1875, mean α = 0.088, directly proves the 2.5b failure mechanism. The forward walk from histogram-α to the smoothstep output (α ≈ 0.022) is mathematically transparent and closes the diagnostic loop. This is the kind of data-driven confirmation that should have been in Phase 2.5b originally — and now it is.

2. **Honest epistemic boundaries.** The "what this conclusion DOES NOT prove" section (critic 12 H1 integration) is unusually rigorous for an implementation doc. Explicitly stating that the geometric origin of small `sdfBefore / voxelSize` values is unexplained (naive expectation 0.5, observed 0.088 — 6× smaller) prevents future readers from over-interpreting the histogram data. Listing three candidate mechanisms without endorsing any is the correct stance.

3. **Two non-obvious bugs documented for posterity.** The wrong-uniform-names bug (copying `uVolumeMax` from raymarch.frag into radiance_3d.comp) and the RGBA16F denormal-flush bug are both real, subtle, and easy to re-introduce. Documenting them with root cause and fix saves future implementors the same debugging session.

4. **L5 shutdown warning is practical.** The "bake-leak test never fired" edge case is a silent failure mode that would have wasted user time. The warning message is actionable (tells the user exactly why and how to fix it). Good UX for a CLI tool.

5. **Bit-exact regression verification.** RMSE 0.000000 for default rendering confirms the polish items didn't accidentally change behavior. This is the correct verification standard for a "no behavior change" commit series.

---

## Weaknesses / Concerns

### W1 (HIGH) — The shader-compile-failed-silently bug is a real infrastructure problem, not just a M1 footnote

The document mentions that when `radiance_3d.comp` had wrong uniform names, the shader compile failed but the app continued running with garbage output. This is a **production-level reliability issue**: any future shader typo or syntax error in any compute shader will produce the same silent failure — the app runs, the scene renders wrong, and there's no obvious indication that a shader failed to load.

The document says "Filed: improve shader-load error path to abort. Not in this commit." But this filing is buried in the "Honest Residuals" section under a bullet about M1 bugs. This should be a **standalone actionable item** with severity tracking. A shader-load failure that silently produces garbage is worse than a crash — the user thinks the renderer is working but sees incorrect output. The priority should be higher than "not in this commit."

### W2 (MEDIUM) — M1's diagnostic clamp output is not the same as the failed 2.5b's actual α output, but the document's "forward walk" is imprecise about which smoothstep formula 2.5b used

The diagnosis section says:

```
Phase 2.5b actually wrote: mix(1e-3, 1.0, smoothstep(0, voxelSize, sdfBefore))
where sdfBefore / voxelSize = 0.088
→ smoothstep(0, 1, 0.088) ≈ 3·(0.088)² − 2·(0.088)³ ≈ 0.0212
→ mix(0.001, 1, 0.0212) ≈ 0.0223
```

This is correct math, but the smoothstep formula shown (`smoothstep(0, voxelSize, sdfBefore)` with `voxelSize` as the upper bound) is different from what Phase 2.5b actually implemented. The 2.5b impl doc should be cross-referenced here to confirm the exact formula, rather than reconstructing it from memory. If the smoothstep bounds were different (e.g., `smoothstep(0, voxelSize * k` for some constant `k`), the forward walk would produce a different α, and the 0.022 estimate would be wrong.

The document should cite the exact 2.5b shader line (or at least state "verified against 2.5b impl doc §X: the formula was exactly `mix(1e-3, 1.0, smoothstep(0, voxelSize, sdfBefore))`").

### W3 (MEDIUM) — The histogram bins are too coarse for the relevant range

The histogram uses 16 equal-width bins over [0, 1]. But 99.2% of the data falls in bins 0–3 (α ≤ 0.25). Within that range, bins are 0.0625 wide. The peak is bin 1 (0.0625–0.125) with 68.7%. This is useful, but the distribution's shape within [0, 0.25] is obscured by the coarse granularity. A 64-bin histogram (or a log-scale binning) over [0, 0.5] would give much more diagnostic resolution — for example, it could reveal whether the distribution has a secondary peak near 0.5 (head-on hits) that's currently invisible because bin 4 (0.25–0.3125) has only 1 count.

The document acknowledges this implicitly ("the three candidate metrics from critic 11 remain valid Phase 2.6 starting points") but doesn't note that the histogram resolution limits what can be inferred about the hit-angle distribution. If future work wants to distinguish "mostly grazing hits" from "SDF gradient is nonlinear near walls," finer bins are needed.

### W4 (MEDIUM) — L2 hard-codes scene-specific coordinates in main3d.cpp

The `--cam-preset=alcove` flag embeds `(0.6, 1.0, 0.5) → (0.6, 0.0, -0.5)` in C++ code. This is scene-specific — if someone loads Sponza with `--cam-preset=alcove`, they'll get a nonsensical camera position. The flag's name doesn't indicate scene dependency.

The "Honest Residuals" section acknowledges this: "could refactor when scene metadata becomes a thing." But this is the same pattern as critic 11's L4 (alcove filter constants hard-coded in demo3d.cpp). The plan now has **two hard-coded scene-specific constants** in two different files with no cross-reference. At minimum, the flag should be renamed to `--cam-preset=cornell-alcove` or validated against the loaded scene (warn if scene doesn't match).

### W5 (LOW) — The RGBA16F denormal-flush discovery is significant but buried in a bullet

The finding that `1e-6` clamped values flush to zero in half-float is a GPU precision constraint that affects **any future work storing small values in RGBA16F atlas textures**. This is not just a 2.5d bug — it's a hardware limitation that anyone writing diagnostic or soft-α values to the atlas must respect. The document mentions it in the "two non-obvious bugs" section but doesn't highlight it as a **project-wide constraint**. Consider adding a note to AGENTS.md or a project-level precision reference: "RGBA16F atlas: values below ~6.1e-5 may flush to zero; always clamp diagnostic/soft-α values to ≥ 1e-3."

### W6 (LOW) — The diagnostic output overloads `--bake-leak-test` JSON format

When `--diag-alpha-mode=1` is combined with `--bake-leak-test`, the same JSON file contains both leak metrics and diagnostic histogram data — but the α values are different (diagnostic vs binary), making the leak numbers incomparable with the Phase 2 baseline. The document warns: "don't compare `phase2.5_bake_leak_baseline_v2.json` against `phase2.5d_sdf_distribution_alcove.json`." This is correct, but the **format design** should have separated these: either a different JSON key namespace (e.g., `diag_*` prefix vs `leak_*` prefix) or a separate `--diag-output=path` flag. The current design invites accidental comparison.

### W7 (LOW) — L3 verification is "code inspection, not GUI testing"

The document says "no GUI test framework but the conditional text is straightforward." This is reasonable for a text-only ImGui insertion, but the label claims "shows raw atlas RGB; ignores α. Bake-time leaks visible here are expected" — which is a **semantic claim about the debug viewer's behavior**, not just a UI label. If the debug viewer's atlas mode has changed since Phase 2 (e.g., now reads RGBA and displays α in some way), the label would be wrong. The verification should at least confirm that the atlas debug viewer mode 3 still renders RGB-only (ignoring α), not just that the label text renders correctly.

### W8 (LOW) — M1's implication section claims "SDF doesn't carry information about hit angle" — this is imprecise

The document says "SDF doesn't carry information about hit angle." Strictly, the SDF gradient **is** the surface normal at the hit point — which is the hit angle relative to the sampling direction. The problem is that Phase 2.5b used `sdfBefore` (the SDF value at a half-voxel-back point), not the SDF **gradient** at the hit point. The gradient carries hit-angle information; the scalar value at a nearby point doesn't (at least not directly). The document should say "the SDF scalar value at a nearby point doesn't carry hit-angle information" — the SDF itself (via its gradient) does.

---

## Summary

A thorough polish-bundle document that closes four critic-11 deferred items with empirical data and practical fixes. The main concerns:

1. **W1 (HIGH):** Shader-compile-failed-silently is a production reliability bug that should be tracked as a standalone high-priority item, not buried in M1 residual notes.
2. **W2 (MEDIUM):** The forward-walk from histogram-α to 2.5b α should cite the exact 2.5b shader formula for verification, not reconstruct from memory.
3. **W3 (MEDIUM):** 16-bin histogram is too coarse for the relevant [0, 0.25] range; finer resolution would support future hit-angle diagnosis.
4. **W4 (MEDIUM):** `--cam-preset=alcove` hard-codes scene-specific coordinates without scene validation or name scoping.
5. **W5 (LOW):** RGBA16F denormal-flush constraint is project-wide, not just a 2.5d bug — should be documented at project level.