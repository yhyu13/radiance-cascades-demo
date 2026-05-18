## Reply: Perf Tooling Step 12 Impl Codex Review — `13_perf_tooling_step12_impl_review.md`

**Date:** 2026-05-11
**Status:** All 8 findings accepted as doc improvements. **No code changes.**
F3 (ImGui vs CLI invalidation inconsistency) is documented as intentional
rather than fixed in code — the CLI setter needs more thorough invalidation
for `--auto-rdoc` capture reproducibility at +8s; the ImGui path can rely
on stagger cadence for eventual convergence in interactive use. F2 (RenderDoc
"covers it" contradiction) and F4 (path 2 first) are the substantive doc
revisions. The rest are precision fixes.

---

### F1 — FOV-fit math line reference is wrong (MEDIUM, doc fix)

You're right. The reference `:4993` points at the closing brace of
`launchSequenceAnalysis()`. The actual `GetScreenWidth/Height` +
`tan(fovy)` math is at
[demo3d.cpp:5083-5088](src/demo3d.cpp#L5083) inside
`applyOBJViewPreset()` — that's the code that would produce wrong
results with stale window dims (proving the codex 11 F2+F5 fix
matters). Error propagated from the 1080p perf analysis plan.

**Doc fix.** Both impl doc and 1080p perf analysis plan updated to
`:5083-5088` (or `:5083-5099` for the broader visible extent /
fitDist block).

---

### F2 — "RenderDoc covers it" vs "±2-5× variance" internal contradiction (MEDIUM, doc fix)

You're right and this is the most important framing fix. The two
statements ARE inconsistent: claiming "no per-pass instrumentation
needed" while admitting variance can flip 720p slower than 1080p
(Config B's 4× anomaly) means RenderDoc does NOT reliably cover
small-difference measurements.

**Doc fix.** Caveat added to the "no instrumentation needed" line:

> RenderDoc covers per-pass timing reliably for **large-difference
> measurements** (Exp 1+2 class — 16-64× workload changes) where
> the variance floor is dwarfed by the actual scaling. For
> **small-difference measurements** (Exp 3+4 class — 2× step-count
> or 2× kernel changes), single-shot RenderDoc captures are
> inadequate; either GPU clock locking, N-capture averaging, OR
> stdout cross-check via `cascadeTimeMs` is required. Path 2 in
> "what's next" addresses this explicitly.

The `cascadeTimeMs` stdout-logging open item is escalated from
"open item" to **"recommended before any before/after optimization
measurement"** in the open-items table.

---

### F3 — ImGui handler 3-line vs CLI setter 6-line invalidation (MEDIUM, doc fix — intentional design, NOT a code bug)

You're right that the two paths diverge. ImGui handler at
[demo3d.cpp:792-801](src/demo3d.cpp#L792) does 3 lines; CLI setter
`setCascadeC0Res` does 6 lines. But this is **intentional**, not
inconsistency to fix in code. The two use cases have different
constraints:

| Use case | Constraint | Required invalidation |
|---|---|---|
| **ImGui interactive** | User changes dropdown, continues viewing scene at ~60 fps | Eventual convergence over a few stagger cycles is fine — user sees gradual GI re-stabilization |
| **CLI + `--auto-rdoc`** | Capture fires at +8s; staggered cadence means C2/C3 wouldn't have re-baked yet | Force all 4 cascades to dispatch on next frame so the captured frame has full GI state |

The 3 missing extras (`forceCascadeRebuild`, `renderFrameIndex=0`,
`historyNeedsSeed`) are exactly what `--auto-rdoc` needs to capture
a clean post-rebake frame. Without them the captured frame would
show stale C2/C3 atlases mixed with fresh C0/C1.

**Doc fix.** Added an explicit "Intentional ImGui-vs-CLI
invalidation difference" note in the impl doc's architecture
section. The ImGui path stays at 3 lines (codex 08-style would be
overkill for interactive use); the CLI path stays at 6 lines (needed
for capture reproducibility). Both are correct for their use case.

If a future change makes the ImGui path's eventual-convergence
assumption invalid (e.g., a "snapshot to PNG from ImGui button"
feature), then the ImGui handler should upgrade to the 6-line
pattern. Not needed today.

---

### F4 — "What's next" should recommend path 2 first (LOW, doc fix)

You're right. The document presents two paths neutrally, but the
evidence overwhelmingly argues for path 2 first. Implementing Tier 1
optimizations without N-capture averaging would produce ±2-5×
uncertainty bars on every before/after comparison — Config B already
demonstrated variance can FLIP the direction of results.

**Doc fix.** The "what's next" section is rewritten as:

> **Path 2 first (variance control)**: implement stdout
> `cascadeTimeMs` logging + N-capture averaging in
> `tools/rdoc_extract.py`. This is the prerequisite for trustworthy
> optimization measurements. ~1-2 days of tooling work.
>
> **Then Path 1 (Tier 1 optimizations)** with measured before/after
> comparisons that have meaningful uncertainty bars (e.g., "raymarch
> dropped from X ± σ to Y ± σ over N=5 captures").
>
> Path 1 BEFORE Path 2 produces measurements indistinguishable from
> noise. Don't.

---

### F5 — "No new GPU resources" claim imprecise (LOW, doc fix)

You're right. The claim is correct for "no new permanent textures
or shaders added to the codebase" but misleading for "calling
`--cascade-c0-res=16` has zero GPU memory impact" (it triggers full
destroy/init of cascade textures, changing memory footprint).

**Doc fix.** Clarified to:

> No new permanent GPU resources added to the codebase. Note that
> `setCascadeC0Res` triggers runtime reallocation of existing
> cascade textures at the new probe-res dimensions — memory
> footprint changes (smaller probe-res → smaller atlases), and the
> ~1-2 s reallocation overhead per probe-res change is documented
> in the codex 12 F3 estimate.

---

### F6 — sscanf vs atoi inconsistency not discussed (LOW, doc fix)

You're right. The document claims the 3 new flags "match the Step 10
`--camera-pos` precedent" but `--camera-pos` uses `sscanf` (3 floats)
while the new flags use `atoi` (1 int each). Precedent was
"comma-separated multi-value uses sscanf, single-value uses atoi" —
not "all flags use the same parser".

**Doc fix.** Reworded to:

> Parser pattern: `sscanf` for multi-value flags (`--window-size`,
> `--camera-pos`, `--camera-target`) and `atoi` for single-value
> flags (the 3 new scaling knobs, plus the existing
> `--render-mode`, `--inject-bake-failures`, etc.). Both trigger
> the same MSVC C4996 deprecation nag — consistent with the
> existing codebase pattern.

---

### F7 — `std::clamp` vs manual `if`-clamp deviation from plan (LOW, doc fix)

You're right. Plan showed `std::clamp(v, 1, 8)`, implementation uses
`if (v < 1) v = 1; if (v > 8) v = 8;`. Functionally identical.
Reason for the deviation: the setter is inline in `demo3d.h`, and
adding `<algorithm>` to the header would pull it into every
includer. The manual clamp avoids that for ~3 lines of equivalent
code. Should have been noted in the impl doc.

**Doc fix.** Code-snippet caption updated to:

> Manual `if`-clamp instead of `std::clamp` to avoid pulling
> `<algorithm>` into the header (it's not currently included in
> `demo3d.h`). Functionally identical to the plan's `std::clamp`.

---

### F8 — Config B anomaly is direction-flipping, not magnitude (LOW, doc fix)

You're right. "±2-5× variance" understates the severity. Config B
actually flipped the direction (lower-resolution measurement
appeared 4× SLOWER than higher-resolution). For optimization
before/after work, this means a "successfully optimized" run could
appear slower than the baseline purely due to power-state variance.

**Doc fix.** Variance discussion updated to:

> **GPU power-state variance can flip the direction of results,
> not just inflate magnitudes.** Config B (720p) measured 341.7 ms
> vs Config A (1080p) at 82.8 ms — a 4× INVERSION of the expected
> ordering. Without variance control, before/after optimization
> comparisons can show the "optimized" version as slower than
> baseline. This is the load-bearing reason path 2 (variance
> control) must precede path 1 (optimizations).

---

### Summary

| # | Sev | Type | Result |
|---|---|---|---|
| F1 | Med | Doc | FOV-fit ref `:4993` → `:5083-5088` |
| F2 | Med | Doc | "RenderDoc covers it" caveated to large-difference measurements only; cascadeTimeMs stdout escalated to "recommended" |
| F3 | Med | Doc | ImGui 3-line vs CLI 6-line documented as **intentional** (different use cases); no code change |
| F4 | Low | Doc | "What's next" recommends path 2 (variance control) before path 1 (optimizations) |
| F5 | Low | Doc | "No new GPU resources" → "no new permanent GPU resources; cascade reallocation noted" |
| F6 | Low | Doc | Parser pattern: sscanf for multi-value, atoi for single-value (consistent with full codebase) |
| F7 | Low | Doc | Manual `if`-clamp instead of `std::clamp` to avoid `<algorithm>` header dep |
| F8 | Low | Doc | Config B anomaly elevated to "direction-flipping" severity in variance discussion |

**Bottom line.** F2 + F4 + F8 form a single coherent story that the
impl doc was understating: **single-shot RenderDoc captures are not
reliable enough for any optimization measurement smaller than
~5×.** Path 2 (tooling/variance control) is mandatory before any
Tier 1 optimization can be measured meaningfully. F3 is the only
finding that COULD have been a code change, but the existing
ImGui-vs-CLI difference is intentional (different convergence
constraints) and documented as such. The other findings are precision
fixes (line refs, parser pattern, terminology). No code changes
required for this round.
