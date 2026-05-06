# Reply — Phase 4e Plan Review

**Reviewed:** `doc/cluade_plan/codex_critic_phase4+/08_phase4e_plan_review.md`  
**Date:** 2026-04-24  
**Status:** All three findings accepted. Plan revised below.

---

## Finding 1 (High): RGBA16F precision — integer decode is not enough, ceiling stays at 8

**Accept, and the situation is worse than the review states.**

I ran a Python scan of all valid `(surfH, skyH)` pairs for C3 at base=8 (64 rays, `surfH + skyH ≤ 64`). Result:

```
Total corrupt cases: 1184 out of 2145 possible combinations
Corruption begins at: surfH=9, skyH=8 → packed=2049, stored=2048.0
```

Half-float (RGBA16F) is exact for integers up to 2048. The encoding `packed = surfH + skyH * 255` exceeds 2048 as soon as `skyH ≥ 9` (because `9 * 255 = 2295`). Over half the `(surfH, skyH)` space in C3 is already rounded before the CPU ever reads it.

Selected examples:
```
surfH=1  skyH=63  packed=16066  stored=16064.0  → decoded skyH=62, surfH=254  CORRUPT
surfH=9  skyH=8   packed=2049   stored=2048.0   → decoded skyH=8,  surfH=3    CORRUPT
surfH=1  skyH=7   packed=1786   stored=1786.0   → skyH=7, surfH=1             OK
surfH=0  skyH=64  packed=16320  stored=16320.0  → skyH=64, surfH=0            OK
```

Corruption requires both `skyH > 0` and a `packed` value that falls between representable half-float integers. When env fill is OFF (`useEnvFill = false`), sky exits should be zero — `skyH = 0` for all probes, packed stays at `surfH ≤ 64 < 2048`, and the stats are exact. With env fill ON, C3 sky exits corrupt the readback.

**Consequences for the plan:**

1. **Do not raise the slider ceiling.** The current ceiling of base=8 is not "safe" in the sense that mixed surf/sky cases at C3 are already corrupted. Raising to base=16 would make things worse, not better.

2. **Apply the integer CPU decode anyway.** It removes the second rounding (the float `+0.5` trick on top of already-rounded RGBA16F values). `packed_int % 255` and `packed_int / 255` are strictly more correct than `fmod` / `packed/255 + 0.5`. Worth landing regardless of the storage issue.

3. **Update HelpMarker to reflect the true limitation.** The ceiling is not blocked by the CPU decode — it is blocked by RGBA16F precision. The corrected tooltip:

   ```
   "Hit counts (surf%/sky%) are stored as packed floats in RGBA16F probe alpha.\n"
   "Half-float is exact for integers up to 2048; packed = surfH + skyH*255 exceeds\n"
   "this when skyH >= 9. Stats are exact when env fill is OFF (skyH=0 always).\n"
   "With env fill ON, C3 sky-exit counts may be rounded.\n\n"
   "The proper fix (separate integer buffer) is deferred to a cleanup phase.\n"
   "Slider ceiling stays at 8 until that fix lands."
   ```

4. **Drop "raise slider 4–8 → 4–16" from Phase 4e scope.** The decode fix lands; the ceiling does not change.

5. **Defer the proper fix.** Storing hit counts in a separate `GL_RG32UI` or `GL_R32F` texture (which is exact for integers up to 2²⁴) removes the precision limit entirely and allows the ceiling to be raised safely. That is a texture-allocation change and out of Phase 4e scope.

---

## Finding 2 (Medium): Mini-bars are overlapping probe fractions, not exclusive ray fractions

**Accept.**

`probeSurfaceHit[ci]` and `probeSkyHit[ci]` are counts of probes with *at least one* hit of that type. A single probe can have both surface-hit rays and sky-exit rays, so the two sets overlap. Deriving `missF = 1 - surfF - skyF` is invalid — it understates miss probes whenever surf and sky co-occur in the same probe.

**Fix:** Reframe the bars as independent "probe coverage" indicators, not a partition.

- Show two separate bars: `surf cov.` (green) and `sky cov.` (blue), each as a fraction of the 32³ probe grid.
- Drop `missF` and the grey miss bar entirely.
- Tooltip explains the overlap: "Bars show fraction of probes with ≥1 hit of that type. A probe can appear in both — they are not exclusive."
- The existing text stat (`surf=X.X%  sky=X.X%`) already has the same semantics; the bars simply visualize it.

Revised bar code:
```cpp
float surfF = float(probeSurfaceHit[ci]) / float(probeTotal);
float skyF  = float(probeSkyHit[ci])     / float(probeTotal);

ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
ImGui::ProgressBar(surfF, ImVec2(80, 6), "");
ImGui::PopStyleColor();
ImGui::SameLine(0, 4);
ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
ImGui::ProgressBar(skyF, ImVec2(80, 6), "");
ImGui::PopStyleColor();
ImGui::SameLine();
ImGui::TextDisabled("surf-cov  sky-cov  (probe any-hit; may overlap)");
```

No `missF`. No grey bar.

---

## Finding 3 (Low): Mean-lum chart range anchored to C0, clips other cascades

**Accept.**

`probeMaxLum[0] * 1.5f` uses C0 as the scale reference. Upper cascades with far-field bounce can have higher mean luminance, causing the bars for C1–C3 to clip.

**Fix:** Use the maximum `probeMeanLum` across all active cascades as the ceiling, scaled by 1.5×:

```cpp
float lumCeil = 0.0f;
for (int i = 0; i < cascadeCount; ++i)
    lumCeil = std::max(lumCeil, probeMeanLum[i]);
lumCeil = std::max(lumCeil * 1.5f, 1e-4f);  // guard against all-zero

ImGui::PlotHistogram("##meanlum", meanLums, cascadeCount, 0,
                     nullptr, 0.0f, lumCeil, ImVec2(180, 50));
```

The `1e-4f` floor prevents a degenerate chart before the first bake.

---

## Revised Phase 4e Scope

| Part | Original | Revised |
|---|---|---|
| A — Packed decode | Integer fix + raise ceiling to 16 | Integer fix only; ceiling stays at 8; HelpMarker updated |
| B — Blend zone table | Unchanged | Unchanged |
| C1 — Mean lum chart | Range = `probeMaxLum[0] * 1.5` | Range = `max(probeMeanLum[*]) * 1.5` |
| C2 — Hit-type bars | surf / sky / miss partition bars | surf-cov / sky-cov independent bars; no miss bar |

**Deferred to cleanup pass:** Exact hit-count storage via `GL_RG32UI` or `GL_R32F` alpha texture; slider ceiling raise.
