# Critique: GUI Cleanup — Settings & Cascade Panels

**Document reviewed:** `gui_cleanup_impl.md`
**Date:** 2026-05-14

---

## Strengths

1. **Excellent scope discipline.** The "zero behavior change" constraint and explicit "what was NOT done" sections prevent scope creep and signal intent clearly.
2. **Before/after with concrete code.** The render-mode picker comparison (37 lines of RadioButtons vs 18 lines of Combo) is compelling and self-evident.
3. **Static_assert for array size.** Catching the "forgot to add a label" bug class at compile time is a genuinely good defensive practice — worth calling out.
4. **Phase suffix strategy.** Removing from primary labels but preserving in tooltips is the right tradeoff: surface declutters, archaeology survives.
5. **Tab grouping rationale.** Tied to usage patterns (banding → Sampling+Temporal, structure → Hierarchy) rather than arbitrary categorization.
6. **Honest verification gaps.** Explicitly stating that smoke test doesn't cover widget interaction is responsible.

---

## Weaknesses / Concerns

1. **`std::clamp(..., 0, 13)` is a smell.** The comment says "the existing setter already clamps, but the picker is a second line of defence." Double-clamping the same value in two places suggests the setter should be the *only* clamp point, and the picker should trust it. If someone later changes the range (e.g. adds mode 14), they must update both the setter clamp and the picker clamp — that's a coupling problem the static_assert doesn't catch. Consider: let the setter be authoritative, and have the picker `static_assert` the setter's range matches the array size instead.

2. **`kRenderModeLabels` as `static const char*[]` inside a method.** This is fine for now, but if render modes ever become configurable or shader-driven, this hardwired array becomes a maintenance bottleneck. Not a blocker, but worth a one-line "if modes become dynamic, refactor to shader-reflect" note in the out-of-scope section.

3. **Local `HelpMarker` alias lambda.** The document acknowledges this is cosmetic and exists to avoid sweeping ~30 call sites. Fair pragmatic choice, but it creates a naming inconsistency (`imHelpMarker` in settings panel, `HelpMarker` in cascade panel) that will confuse anyone reading the code without this doc. A single `replace_all` of `HelpMarker` → `imHelpMarker` would have been a 30-line mechanical change with zero risk — the document over-weights the "sweep" cost.

4. **Net-change claim is vague.** "net +20 / net −20 / ~13 small label edits" — is the total net change +0? Or +13? The phrasing makes it hard to understand the actual delta. A single number (or a diff stat like `+40 −27`) would be clearer.

5. **Verification grep patterns are imprecise.** `ImGui::(Text|Checkbox)\("[^"]*\(Phase` — this regex won't catch phase suffixes inside tooltip strings (which the doc says are *preserved*), but it also won't catch phase suffixes in `ImGui::SliderFloat`, `ImGui::Combo`, etc. The claim "zero matches remain" only proves those two widget types are clean, not all labels.

6. **No screenshot or visual diff.** For a GUI cleanup, before/after screenshots would be far more persuasive than code snippets. The document acknowledges runtime smoke doesn't cover interaction — a screenshot would at least prove the layout renders correctly.

7. **Cross-references are fragile.** Source-line links like `demo3d.cpp#L3432-L3440` will rot on any subsequent edit. Consider anchoring to symbolic names (function/variable) rather than line numbers.

---

## Minor

- The tab naming "Selectors & Stats" is ambiguous — "Selectors" could mean cascade selector or render-mode selector. Consider "Debug Selectors & Stats" to disambiguate.
- The `imHelpMarker` helper uses `ImGui::TextDisabled("(?)")` which adds a visual element — it's not just a hover tooltip helper, it's a widget. The doc should acknowledge this UX distinction more explicitly (it does briefly in out-of-scope, but the helper's own description understates it).

---

## Summary

A well-documented, disciplined cleanup with honest tradeoff analysis. The main actionable improvements are: eliminate the double-clamp, do the `HelpMarker` → `imHelpMarker` sweep, and add a screenshot.