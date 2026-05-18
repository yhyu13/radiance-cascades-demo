# Critic Review 12 — `visibility_phase2.5d_impl.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-15
**Verdict:** Bundle execution mostly clean (M1 produced a genuinely useful empirical result; L2/L3/L5 each ship as advertised). **Two HIGH severity issues**: M1's "diagnosis verified" claim is partially wrong (the smoothstep math is right but the conclusion ignores that the v2 clamp range `[1e-3, 0.999]` would have given the SAME under-darkening as the original `[1e-6, 1.0]` failed attempt — so the verification doesn't actually verify the claimed root cause), and the impl doc buries the "shader compile failed silently" infrastructure bug in Honest Residuals when it actually warrants a fix-now treatment (it cost a debug cycle and will cost more next time).

---

## HIGH severity

### H1 — M1's "diagnosis verified" is technically incomplete

The impl doc claims:

> **Diagnosis verified.** The Phase 2.5b smoothstep was `mix(1e-3, 1.0, smoothstep(0.0, voxelSize, sdfBefore))`. For the typical bin with α=0.088: smoothstep(0, 1, 0.088) ≈ 0.0212; mix(0.001, 1, 0.0212) ≈ 0.0223. ~98% of surface bins were rendered with α ≈ 0.022 — essentially fully transparent.

The math is correct, and the "98% near-zero α" conclusion does explain why Phase 2.5b dimmed. **But the claim "diagnosis verified" overreaches** in two ways:

1. **The diagnostic shader uses a DIFFERENT clamp than the failed 2.5b shader.** Phase 2.5b: `clamp(sdfBefore / voxelSize, 1e-3, 1.0)`. M1 diagnostic: `clamp(sdfBefore / voxelSize, 1e-3, 0.999)`. These produce nearly identical results, BUT — and here's the gotcha — Phase 2.5b's bake then ran the value through `mix(kSurfaceEps=1e-3, 1.0, smoothstep(0, voxelSize, sdfBefore))`. The M1 diagnostic SKIPS the smoothstep+mix and writes the clamp result directly. So the histogram measures `clamp(sdfBefore / voxelSize, ...)`, NOT `mix(1e-3, 1, smoothstep(0, voxelSize, sdfBefore))`.

   The two are related by a smoothstep, so the SHAPE of the distribution carries over (peak at 0.06–0.13 in the input → peak at smoothstep(0, 1, 0.06–0.13) ≈ 0.01–0.04 in the smoothstep output). The doc's math walks this through correctly. But the claim "the histogram measures what 2.5b wrote" is wrong — the histogram measures the smoothstep INPUT, and the doc back-derives what 2.5b would have written. That's a derivation, not a measurement of 2.5b's actual output.

2. **The unit interpretation of `sdfBefore / voxelSize` is potentially wrong.** Half-voxel-back from the hit, the SDF should be approximately `voxelSize/2` for a head-on hit (the SDF is roughly linear in the small region near the hit). So `sdfBefore / voxelSize ≈ 0.5` for head-on hits. But the histogram peak is at 0.06–0.13, well below 0.5. **Why?**

   Possibility A: hits aren't head-on; they're grazing, so SDF a half-voxel back is much less than voxel/2. But then "all hits are grazing" is itself surprising and would suggest different geometry analysis.

   Possibility B: `voxelSize` in the diagnostic was computed differently than I think. The shader uses `uGridSize.x / textureSize(uSDF, 0).x` which for a 4³ volume with 128³ SDF gives 0.03125. `sampleDist = max(hit.a - voxelSize * 0.5, 0.0) = hit.a - 0.0156`. For a typical Cornell hit at, say, hit.a = 0.5, sampleDist = 0.484. SDF at that point should be ~voxelSize/2 = 0.016 IF the geometry is exactly perpendicular at the hit. So `sdfBefore / voxelSize = 0.016 / 0.03125 = 0.5`. The observed mean of 0.088 is 6× smaller — meaning the SDF at sampleDist is 6× closer to zero than expected.

   **What this means**: either (a) hits are actually approaching walls at very shallow angles (most hits ARE grazing in Cornell-orig-alcove? unlikely), or (b) the SDF has a conservative-band offset (per cerebrum 2026-05-07 do-not-repeat: "EDT/JFA seeds aren't exact triangle SDF; subtract `voxelSz*sqrt(3)/2` for conservative band") that pulls the SDF closer to zero in a wider range than I expected.

   The conservative-band offset is `voxelSz * sqrt(3)/2 ≈ 0.027`. If the SDF gives "distance minus 0.027", then a hit at SDF=0 actually corresponds to a true distance of 0.027. The half-voxel-back point would have SDF = (true_dist - voxelSize/2) - 0.027. For true_dist near 0 (just past hit), SDF ≈ -0.043. After clamp to [1e-3, 0.999], that's 1e-3 — MOST hits would clamp to the floor.

   **But the histogram doesn't show that** — it shows mean 0.088, which is well above the 1e-3 floor. So the conservative-band-pulls-to-zero theory is also wrong.

   **The actual mechanism is unclear from the M1 data alone.** The histogram tells me the distribution, not the geometric cause of that distribution. The impl doc should acknowledge: "the why behind the distribution shape isn't clear from M1 alone; the verified claim is 'sdfBefore is small for ~all hits' not 'sdfBefore is small because the hits are grazing'."

**Fix**: tighten the impl doc's M1 conclusion. The histogram is real data; the explanation walked through the smoothstep math correctly; but "diagnosis verified" is too strong — the GEOMETRIC cause of the small-α distribution is itself not explained by M1.

### H2 — Shader-compile-failed-silently is filed in Honest Residuals but should be a fix-now

The impl doc admits:

> The shader-compile-failed-silently bug is a pre-existing infrastructure issue. When `loadShader` fails, the main loop continues with the broken shader. A "fail-fast on shader load failure" change would have caught my M1 bug immediately instead of producing garbage output that I then had to debug. **Filed**: improve shader-load error path to abort. Not in this commit.

This bug **already cost a debug cycle in this session** (~20 minutes of the M1 work was spent figuring out why the shader output was wrong, when a fail-fast would have surfaced the compile error immediately). Future shader work will hit this trap repeatedly. The fix is small (probably 5 lines in the shader-load error path).

**Filing it as "not in this commit" without a follow-up commit number is technical debt.** The impl doc should either:
- Apply the fix in this commit (small scope; would have prevented the M1 bug retroactively for future runs)
- Or file it with a concrete next-step (e.g., "Phase 2.5e: shader-load error abort") so it doesn't disappear into the residuals void

The Honest Residuals section is fine for things that genuinely need design work or are out of scope. Pure "infrastructure improvement that would prevent this exact debug cycle from happening again" should be done now, not filed.

---

## MEDIUM severity

### M1 — L3's atlas viewer label assumes orange is readable

The label uses `ImVec4(1.0f, 0.7f, 0.3f, 1)` (orange). On displays with dark themes (default ImGui), this should read fine. On light themes or for color-blind users, orange may be hard to distinguish from yellow/red. The fact this is only a NOTE (not a critical warning) softens the issue, but the doc claims "L3 verified by reading the patch" — which doesn't actually verify the label is visible.

**Suggested fix**: either run the GUI manually for 30 seconds and confirm the label renders readably, or use ImGui's standard "info" color (`ImVec4(0.5f, 0.8f, 1.0f, 1)` — light blue is the convention).

### M2 — L2's hard-coded camera coordinates won't scale

The alcove preset has `pos=(0.6, 1.0, 0.5) target=(0.6, 0.0, -0.5)` hard-coded inside main3d.cpp's CLI parser. This is fine for the cornell-orig-alcove scene specifically, but:

- The coordinates are tuned to halfExtent=1.0 (Cornell). If the scene normalizer ever changes Cornell's halfExtent (per cerebrum [Phase 5 Step 4]: "objKind stays 2-way (cornell vs sponza) since both Sponza variants share Sponza preset"), the preset would silently misalign.
- Adding a SECOND scene-specific preset means another hard-coded pair in main3d.cpp. Doesn't scale beyond 2-3 presets.

**Suggested**: not blocking, but a future "scene preset library" would centralize these in scene metadata or an asset config. Filed as future polish.

### M3 — L5's warning condition doesn't catch all failure modes

The L5 check is:

```cpp
if (demo->bakeLeakTestActive() && !demo->bakeLeakTestComplete()) {
    std::cerr << "[MAIN] WARN: --bake-leak-test was scheduled but never fired...";
}
```

This catches "scheduled-but-never-fired" cases. But it misses:
- The metric DID fire but failed silently (e.g., `glGetTexImage` returned garbage; the JSON wrote zeros). User would see "JSON written" but not realize the data is bad.
- The metric fired with diagAlphaMode=1 set; user thinks they got the leak baseline but actually got the diagnostic histogram.

The L5 fix is good but incomplete. The honest framing in the impl doc should note: "L5 catches the most common failure mode (insufficient frames). Other failure modes (silent JSON-with-bad-data) are not caught."

### M4 — M1's diagnostic infrastructure overloads the same JSON file as 2.5a.1

The impl doc itself notes:

> The diagnostic output overloads `bake-leak-test` JSON. When `--diag-alpha-mode=1`, the leak metric numbers in the same file are NOT comparable to the Phase 2 baseline.

This is documented but not enforced. A user could pass `--bake-leak-test=tools/phase2.5_bake_leak_baseline.json --diag-alpha-mode=1` and OVERWRITE the v2 baseline JSON with diagnostic-mode data. **The fix is small**: when `diagAlphaMode != 0`, refuse to write to a path containing "baseline" in the name, OR (cleaner) refuse to share output paths between the two modes.

Or even simpler: just add a stderr warning when both flags are combined: "Note: diag mode 1 will produce diagnostic data in the bake-leak-test JSON; this is NOT a Phase 2 baseline measurement."

### M5 — The "two non-obvious bugs found" section reads as a worked example, but doesn't change the bug-prevention pattern

The impl doc has a great section about the two debug-cycle bugs (wrong uniform names; RGBA16F denormal flush). But it doesn't propose changing the WORKFLOW to prevent them. For RGBA16F flush specifically: the project doesn't have a shared "GLSL precision constants" header that could codify "always use ε >= 1e-3 in RGBA16F." Adding such a header would prevent recurrence.

This is more of a follow-up suggestion than a critic finding. Acknowledged.

---

## LOW severity

### L1 — Histogram bucket width is fixed at 1/16

16 buckets across [0, 1] gives bucket width 0.0625. Most data clustered in the bottom 4 buckets means the histogram is over-binned in the upper range and under-binned where the action is. A log-scale histogram (e.g., buckets at 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64, 1.0) would show more detail near zero. Not blocking; the linear histogram answered the question adequately.

### L2 — The impl doc lists "M1, L2, L3, L5 closed" but the conversation context shows there were also H1, H2, M2, M3, L4 in critic 11 — those should be referenced as "closed in critic 11 reply"

The impl doc says "M1, L2, L3, L5 closed; H1, H2, M2, M3, L4 closed in the prior critic-11 reply; nothing else from critic 11 outstanding." This is correct but reads as if I'm taking credit for closing them. Could clarify which ones landed in 2.5d (M1/L2/L3/L5) vs which ones landed in the critic-11 reply (H1/H2/M2/M3/L4).

### L3 — M1's `setDiagAlphaMode` triggers a full cascade rebuild, but only when the value CHANGES

The setter does `cascadeReady = false; forceCascadeRebuild = true; renderFrameIndex = 0; historyNeedsSeed = true;` unconditionally. If the user sets diagAlphaMode=1, then sets diagAlphaMode=1 again (e.g., re-applies the CLI flag programmatically), this triggers a redundant rebuild. Defensive but wasteful. Add: `if (m == diagAlphaMode) return;` before the rebuild side effects.

### L4 — The shader's clamp range `[1e-3, 0.999]` is good but isn't named

`1e-3` and `0.999` are magic numbers in the shader. A `const float kDiagAlphaMin = 1e-3;` and `kDiagAlphaMax = 0.999` would document intent and make adjustment easier if RGBA16F precision changes (e.g., RGBA8 atlas later). Trivial fix.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | M1 "diagnosis verified" is overstated; histogram measures clamp output not 2.5b's actual mix-with-smoothstep output; geometric cause of the small-α distribution itself unexplained |
| H2 | HIGH | Shader-compile-failed-silently filed in Residuals; should be fixed now (already cost a debug cycle this session) |
| M1 | MEDIUM | L3 label color assumed readable; not actually GUI-verified |
| M2 | MEDIUM | L2 hard-codes alcove coordinates in main3d.cpp; doesn't scale beyond 2-3 presets |
| M3 | MEDIUM | L5 catches scheduled-but-never-fired; misses silent-JSON-with-bad-data + accidental diag-mode-overwriting-baseline |
| M4 | MEDIUM | M1 diagnostic JSON shares filename pattern with 2.5a.1 baseline; could silently overwrite |
| M5 | MEDIUM | "Two bugs" worked-example doesn't propose workflow changes to prevent recurrence |
| L1 | LOW | Histogram is linear; data clusters in low buckets; log-scale would show more |
| L2 | LOW | "Closed" attribution mixes 2.5d items with critic-11 reply items |
| L3 | LOW | setDiagAlphaMode triggers rebuild even when value unchanged |
| L4 | LOW | Shader's clamp range magic numbers should be named |

---

## Top actions for revision

1. **Fix H2**: apply the fail-fast-on-shader-load fix in this session. Small scope; prevents the same debug cycle from recurring.
2. **Fix H1**: tighten the M1 "diagnosis verified" wording to acknowledge what the histogram does (and doesn't) prove. Don't claim the geometric cause is verified when only the distribution shape is.
3. **Fix M4**: add a stderr warning when `--bake-leak-test` and `--diag-alpha-mode=1` are combined ("output JSON will contain diagnostic data, not a baseline").
4. **Fix L3**: add the early-return in `setDiagAlphaMode` to avoid redundant rebuilds.
5. **Fix L4**: name the diagnostic clamp constants in the shader.
