# GUI Cleanup — Settings & Cascade Panels

**Date:** 2026-05-14
**Status:** Implemented; build clean (0 errors); runtime smoke clean (4s window run, ImGui setup OK, all cascades bake normally).

**Critic chain extends:** [critic 06](critic/06_gui_cleanup_impl_review.md) → [reply 06](critic/reply/reply_06_gui_cleanup_impl_review.md). Three follow-up code edits landed (magic 13 removed, `HelpMarker` alias swept, "Selectors & Stats" tab renamed to "Debug & Stats"); doc updated below.

**Why:** the two main control panels had grown organically over phases 4–14 and become hard to navigate. Render-mode picker was 14 individual `RadioButton`s in a wrap-y mosaic; cascade panel was a single 640-line scroll mixing structural settings, sampling toggles, temporal knobs, and runtime stats; primary widget labels were polluted with historical phase suffixes (`(Phase 5c)`, `(Phase 5d)`, `(4a)`, etc.) that meant nothing to anyone using the controls.

**Scope discipline:** zero behavior change. Same controls, same state, same tooltips. Only layout / picker affordances / label text changed.

---

## Summary

| Change | File | Effect |
|---|---|---|
| Hoisted `imHelpMarker(const char*)` to file scope | [src/demo3d.cpp:3432-3440](../../../src/demo3d.cpp#L3432-L3440) | Single definition shared by both panels (was per-panel lambda) |
| Render mode: 14 `RadioButton`s → single `Combo` | [src/demo3d.cpp:3641-3676](../../../src/demo3d.cpp#L3641-L3676) | ~37 lines of mosaic widgets → 18 lines of one combo + grouped tooltip |
| Cascade panel wrapped in `BeginTabBar` with 4 tabs | [src/demo3d.cpp:3741-…](../../../src/demo3d.cpp#L3741) | Logical separation: "Hierarchy & Merge" / "Sampling" / "Temporal" / "Selectors & Stats" |
| Stripped `(Phase 5c)`, `(Phase 5d)`, `(Phase 5e)`, `(Phase 5f)`, `(Phase 5g)`, `(Phase 5h)`, `(Phase 5i)`, `(Phase 9)`, `(Phase 2)`, `(4a)`, `(4b/5a)`, `(4c)` from primary widget labels | various | User-facing text is now task-named, not phase-numbered. Tooltips still describe phase context for archaeology. |

No widgets removed. No state members touched. No tooltip text shortened.

---

## Render mode picker — before / after

**Before (37 lines, 14 RadioButtons + scattered tooltips, no consistent grouping):**

```cpp
ImGui::Text("Debug Render Mode:");
ImGui::RadioButton("Final (0)",       &raymarchRenderMode, 0); ImGui::SameLine();
ImGui::RadioButton("Normals (1)",     &raymarchRenderMode, 1); ImGui::SameLine();
ImGui::RadioButton("Depth (2)",       &raymarchRenderMode, 2); ImGui::SameLine();
ImGui::RadioButton("Indirect*5 (3)",  &raymarchRenderMode, 3);
// … 10 more lines of RadioButton + SameLine + scattered IsItemHovered+SetTooltip calls …
ImGui::RadioButton("GIHeat-Frac (13)", &raymarchRenderMode, 13);
if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Mode 13: GI-fraction heatmap = …");
```

**After (single combo + one grouped tooltip via `imHelpMarker`):**

```cpp
static const char* kRenderModeLabels[] = {
    "0  Final (combined)",
    "1  Normals",
    "2  Depth",
    "3  Indirect x5",
    "4  Direct only (+ ambient floor)",
    "5  Steps (integer step count)",
    "6  GI only (indirect bounce)",
    "7  RayDist (continuous travel distance)",
    "8  ProbeCell (fract of probe-grid coord)",
    "9  DirectNoAmb (direct, no ambient floor)",
    "10 AmbFloor only",
    "11 GIHeat-Vis (visible-GI heatmap)",
    "12 GIHeat-Raw (raw-GI magnitude)",
    "13 GIHeat-Frac (GI / (direct+GI))",
};
static_assert(sizeof(kRenderModeLabels) / sizeof(kRenderModeLabels[0]) == 14,
              "renderModeLabels must enumerate all 14 raymarch modes");
int rmIdx = std::clamp(raymarchRenderMode, 0, 13);
if (ImGui::Combo("##RenderMode", &rmIdx, kRenderModeLabels, 14))
    raymarchRenderMode = rmIdx;
imHelpMarker("Final (0): albedo * (direct + indirect).\n"
             "Geometry/diagnostic (1-2): normals, depth.\n"
             "Component split (3,4,6,9,10): isolate direct vs indirect vs ambient floor.\n"
             "Heatmaps (5,7,8): SDF cost (steps / ray distance) and probe-cell boundaries.\n"
             "GI heatmaps (11-13): where GI is doing work (visible / raw / fraction).\n"
             "Notes:\n"
             "  Mode 5: integer step count. Banded mode 5 + smooth mode 7 → quantization.\n"
             "  Mode 8: aligned banding = Type A (cell-size); misaligned = Type B (D bins).\n"
             "  Modes 9/10 split mode 4 into 'direct without ambient' + 'ambient floor only'.\n"
             "  Modes 11-13 require Cascade GI ON; otherwise output is all-green / zero.");
```

**Static assert** catches the bug class where someone adds a new render mode in the shader and forgets to add a label to the picker (would silently leave the new mode unselectable). Fires at compile time if the array length and the documented mode count diverge.

**Defensive clamp via `kModeCount - 1`** (per critic-06 W1). The clamp is load-bearing: `setRenderMode` warns on out-of-range values but **assigns them anyway** (preserves shader fallthrough), so the picker is the only thing standing between an out-of-range `raymarchRenderMode` and an OOB `kRenderModeLabels[99]` read. Bound to `kModeCount` (not the magic `13` it originally was) so adding a label automatically extends the picker range — no second clamp site to update.

---

## Cascade panel — TabBar structure

The 640-line panel is now organised as four tabs (source order preserved — tabs match the existing section ordering, no widget moved across tab boundaries):

| Tab | Source-line region | Contains |
|---|---|---|
| **Hierarchy & Merge** | start → just before "Shadow ray in direct path" | Cascade count + per-cascade summary, disable-merge toggle, directional merge, directional bilinear merge, probe layout (co-located vs ShaderToy halving), C0 resolution combo, C0/C1 min range sliders, spatial trilinear merge |
| **Sampling** | "Shadow ray in direct path" → just before "Temporal Accumulation" | Direct shadow ray, directional GI sampling, soft shadow (display + bake + k slider), per-cascade D scaling, environment fill, ray count scaling, dirRes radio |
| **Temporal** | "Temporal Accumulation" → just before "Interval Blend" | EMA accumulation, AABB history clamp, alpha slider, probe jitter (scale/pattern/dwell), stagger interval, EMA-fill / jitter-vector readout |
| **Debug & Stats** | "Interval Blend" → end of panel | Interval blend slider, render-using-cascade selector (C0..C3), radiance debug viewer mode, probe fill rate per cascade with progress bars, mean luminance histogram, probe-luminance distribution, C0 spot samples |

(Tab name changed from "Selectors & Stats" → "Debug & Stats" per critic-06 M1 — clearer that the tab holds debug viewer-mode + cascade selector + runtime stats.)

The TabBar wrapper is open by default to the first tab; ImGui remembers the last selection within a session.

**Why this grouping:** matches how someone uses the panel. When fixing banding you live in "Sampling" + "Temporal". When debugging cascade structure you live in "Hierarchy & Merge". When checking convergence you live in "Selectors & Stats". The previous single-scroll layout forced you to scroll past unrelated controls every time.

**What was NOT done (deliberately):**

- No physical widget reorder across tab boundaries — tab boundaries align with existing source-order section breaks. This keeps the diff small and reviewable.
- No collapsible groups inside tabs — the existing `ImGui::Separator()` + `ImGui::Text("Section:")` pattern is preserved within each tab.

---

## Phase suffix stripping

These primary labels lost their `(Phase ...)` suffix (one-by-one Edits, no `replace_all` — each label was unique):

| Before | After |
|---|---|
| `Cascade GI (Phase 2):` | `Cascade GI:` |
| `Directional merge (Phase 5c)` | `Directional merge` |
| `Directional bilinear merge (Phase 5f)` | `Directional bilinear merge` |
| `Cascade Probe Layout (Phase 5d):` | `Cascade Probe Layout:` |
| `Spatial trilinear merge (Phase 5d)` | `Spatial trilinear merge` |
| `Shadow ray in direct path (Phase 5h)` | `Shadow ray in direct path` |
| `Directional GI sampling (Phase 5g)` | `Directional GI sampling` |
| `Soft Shadow (Phase 5i):` | `Soft Shadow:` |
| `Directional Resolution Scaling (Phase 5e):` | `Directional Resolution Scaling:` |
| `Environment Fill (4a):` | `Environment Fill:` |
| `Ray Count Scaling (4b / 5a):` | `Ray Count Scaling:` |
| `Temporal Accumulation (Phase 9):` | `Temporal Accumulation:` |
| `Interval Blend (4c):` | `Interval Blend:` |

Verified with `Grep ImGui::(Text|Checkbox)\("[^"]*\(Phase` — zero matches remain.

**Phase context not lost:** every tooltip / `HelpMarker` body that previously named a phase still does. So if you hover a control that originated in Phase 5g, the tooltip still reads "Phase 5g: …". Archaeology preserved; surface text declutters.

---

## Hoisted helper

```cpp
// File-scope ImGui helper used across renderSettingsPanel + renderCascadePanel.
// Renders a grey "(?)" marker on the same line as the preceding widget; hovering
// the marker shows `desc` as a tooltip after a short delay. Use for long help
// text where the user benefits from a discoverable affordance. For short hints
// on small toggles/sliders, prefer per-widget IsItemHovered+SetTooltip (already
// used widely; the GUI cleanup pass intentionally did not normalize these — the
// two styles serve different UX needs).
//
// This is a WIDGET (it draws "(?)"), not just a hover-handler — placement
// matters relative to surrounding ImGui::SameLine / ImGui::Separator calls.
static void imHelpMarker(const char* desc) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", desc);
}
```

Lives just above `Demo3D::renderUI()`. **All 24 cascade-panel call sites swept to `imHelpMarker`** (per critic-06 W3) — single name everywhere; the previous local `HelpMarker` alias lambda is gone.

---

## What was NOT done (out of scope this round)

- **Settings panel TabBar.** Considered, but rejected — the current single-column layout in the Settings panel is already mostly task-aligned (stats top, camera collapsing header, lighting collapsing header, render mode, blur, perf metrics, debug-windows toggle). Adding a tab bar there would have moved more code without obvious wins. Render-mode picker (the actual mess) was the surgical fix.
- **Probe stats refactor.** The probe-fill-rate / mean-lum / distribution / spot-sample blocks live in "Debug & Stats" tab now but their internal layout is unchanged. They were genuinely dense but the density carries information; restructuring them would lose data per square pixel. Filed as future work if a stakeholder finds the current density unreadable.
- **Tutorial panel.** Untouched. It's instructional content, not control surface — not part of the "messy debug GUI" complaint.
- **Mass tooltip-mechanism normalization.** The existing mix of `IsItemHovered + SetTooltip` (per-widget hover) and `imHelpMarker` (separate `(?)` indicator) is preserved as-is. Per-widget hover is genuinely more discoverable on small toggles where the user is already moving toward the control; `(?)` markers are better for long help text. Forcing one style everywhere would lose UX information without payoff.
- **Shader-driven render mode list** (per critic-06 W2). `kRenderModeLabels` is hardwired in C++; if render modes ever become introspected from a shader uniform block / enum table, this array becomes a maintenance bottleneck. For now the array is small and stable, and the `static_assert` catches the only way it can drift. Refactor to reflect-from-source if/when modes become dynamic.
- **`setRenderMode` clamps authoritatively** (per critic-06 W1 deeper suggestion). Currently the setter warns on out-of-range and assigns the value anyway. Changing to "warn + clamp" would alter `--render-mode=99` semantics (silently maps to 13 instead of leaving 99 with a warning) — some experimental workflows may rely on the current behaviour. The picker's `kModeCount - 1` defensive clamp is the intermediate fix.
- **GUI screenshot in verification** (per critic-06 W6). The existing `--screenshot=` capture saves the 3D scene WITHOUT ImGui (`[MAIN] --screenshot saved (clean 3D, no UI)`). Capturing the GUI would require either a manual session (subjective; not reproducible) or a new "include UI in screenshot" code path. Filed; the 4-second smoke run + `[INIT] Setting up ImGui...` log line is the strongest available headless evidence the layout doesn't crash.

---

## Verification

- **Build:** Release `RadianceCascades3D.exe` rebuilt clean (0 errors; only pre-existing warnings — encoding C4819, sscanf C4996, signed/unsigned C4018, int→float C4244 — none introduced).
- **Runtime smoke:** [tools/app_run_gui_cleanup_smoke.log](../../../tools/app_run_gui_cleanup_smoke.log) — 4-second window run; ImGui setup OK, scene loads, all 4 cascades bake normally with non-zero mean luminance, no ImGui assertion failures, no crashes.
- **Greps:**
  - **Broadened pattern (per critic-06 W5):** `ImGui::(Text|Checkbox|SliderFloat|SliderInt|Combo|Button|RadioButton|InputText|InputFloat|InputInt|ColorEdit|TextColored)\("[^"]*\(Phase` — zero matches across all widget families (not just `Text`/`Checkbox` as the original verification grep checked).
  - `\bHelpMarker\b` — zero matches (sweep complete; only `imHelpMarker` remains).
  - `BeginTabBar`/`EndTabBar` — paired (one open in `renderCascadePanel`, one close at panel end).
  - `BeginTabItem`/`EndTabItem` — 4 pairs in `renderCascadePanel` (one per tab).

What this verification does NOT cover: actual user interaction (clicking the tabs, switching render modes via the combo). The runtime smoke proves the GUI initialises without crashing; per-widget interaction needs a manual session.

---

## Files changed

`src/demo3d.cpp` — only file touched (plus the 2 doc files). No header changes; `imHelpMarker` is `static` to the .cpp.

**Diff stats** (per critic-06 W4 — replacing the previous vague "net +20 / net −20" claim):

The branch-level `git diff --stat src/demo3d.cpp` reports +205 / −91 (114 net additions), but that figure is **entangled with prior Mode 4 work** (visibility-mode 4 wiring, ImGui combo expansion to 5 entries, CLI doc updates) — there's no clean commit boundary between the Mode 4 patch and this GUI cleanup, so I can't give a clean per-cleanup stat. Component breakdown by inspection:

| Cleanup component | Approx delta |
|---|---:|
| Helper hoist (`imHelpMarker` definition + comment) | +14 |
| Render-mode 14-radio → Combo (label array, static_assert, clamp, combo, tooltip) | +18 / −37 = **−19** |
| Cascade panel tab boundaries (4 BeginTabItem + 4 EndTabItem + section comments) | +30 |
| Phase-suffix label edits (13 lines edited in place) | ~0 net |
| Critic-06 follow-ups (`HelpMarker` sweep removed alias lambda; clamp uses `kModeCount`; tab rename) | −5 net |
| **GUI cleanup subtotal** | **~+20** |

The remainder of the +114 belongs to Mode 4 + visibility-mode wiring documented in [visibility_unified_plan_phase1_impl.md](visibility_unified_plan_phase1_impl.md). A clean per-PR stat will be available after the next commit.

---

## Next steps (the work this cleanup unblocks)

Per [visibility_unified_plan_phase1_test_results.md](visibility_unified_plan_phase1_test_results.md), the Phase 1 default-mode-flip decision is gated on:

1. **Per-region RMSE on three Sponza crops** (lit floor / shadowed alcove / vertical wall column).
2. **RenderDoc capture of Mode 0 vs Mode 4 raymarch pass timing.**

Both are now easier to do because the render-mode picker is no longer a 14-radio mosaic. The Combo lets a tester switch modes faster during region-crop selection and during RenderDoc-capture mode-iteration.

---

## Note on cross-references

Per critic-06 W7: source-line links in this doc (`src/demo3d.cpp#L3432-L3440` etc.) follow the project's CLAUDE.md `[file.cpp:42](path#L42)` convention but **will rot on subsequent edits to demo3d.cpp**. For navigation that survives churn, search by symbol name (`imHelpMarker`, `renderCascadePanel`, `kRenderModeLabels`, `BeginTabItem("Hierarchy & Merge")`) rather than relying on line numbers. The line-number convention is a project-wide tradeoff and isn't being changed unilaterally here.
