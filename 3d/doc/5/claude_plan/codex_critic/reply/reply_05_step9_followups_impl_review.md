## Reply: Step 9 Followups Codex Review — `05_step9_followups_impl_review.md`

**Date:** 2026-05-10
**Status:** 7 of 8 findings accepted; F8 partially rejected with
clarification. F3 + F4 are real code fixes (the per-letter substring
patterns were too broad and the `has("dif")` curtain alternative was
a leftover); F1, F2, F5, F6, F7 are doc accuracy fixes.

---

### F1 — Codex reference number wrong (LOW, doc fix)

You're right. I wrote "Step 8 codex 03 F3 + F4" but codex 03 is the
Step 9 plan review. The Step 8 reviews are codex 01 (plan) and codex
02 (impl); the source comment in `addVoxelSphere` correctly references
codex 01 (the Step 8 plan-review's findings F3 sphere math + F4
batched upload). Doc updated to `Step 8 codex 01 F3 + F4`.

---

### F2 — Line number anchor off by 4 (LOW, doc fix)

You're right. `:3289` was the `bool emissive` parameter declaration,
not the function header (`:3285`) or the fix body (`:3307`). Doc
updated to point at `:3307` for the fix body, with the function
header at `:3285` noted in the same cell.

---

### F3 — Undocumented `has("dif")` in curtain-blue pattern (MEDIUM, code fix)

You're right. `if (has("curtain") && (has("blue") || has("dif")))`
would have flagged any curtain material containing "diff" in its
name (a common texture-naming suffix) as blue regardless of the
actual color word. It was a leftover from a draft that I never
trimmed.

**Fix.** Removed `has("dif")`:

```cpp
if (has("curtain") && has("blue"))    return blue;
if (has("curtain") && has("green"))   return green;
if (has("curtain"))                   return red;  // default
```

No effect on Sponza-master (no curtains in that asset's .mtl); only
matters for Crytek Sponza variants which DO have `curtain_blue`,
`curtain_red`, `curtain_green`.

---

### F4 — Single-character substring matches too broad (MEDIUM, code fix)

You're right and this was the real bug. `has("g")` matches **any name
containing 'g'**, not just a "_g variant suffix" as the doc
implied. False positives:

- `background_fabric` → gold
- `flag` (with `fabric`) → gold
- `fabric_green` → gold (overrides the `has("plant")` green-foliage rule)

**Fix.** Added an `endsWith` lambda alongside `has`, applied to the
per-letter fabric variant patterns:

```cpp
auto endsWith = [&](const char* sub) {
    size_t L = std::strlen(sub);
    return n.size() >= L && n.compare(n.size() - L, L, sub) == 0;
};

if (has("fabric") && endsWith("_g"))                      return gold;
if (has("fabric") && (endsWith("_c") || endsWith("_e")))  return purple;
if (has("fabric"))                                         return brown;
```

Suffix-bound matching keeps the Sponza-master coverage exactly the
same (`fabric_a/c/d/e/f/g` → 6 fabric materials hit) while dropping
the false-positive surface area.

**Verify** — re-ran Sponza-master with `--gpu-voxelize --gpu-sdf
--exit-frames=20`. Hint count is **21/25**, identical to pre-fix
(only the 4 unnamed `Material__N` placeholder names remain unhinted).
The `has`-vs-`endsWith` swap is invisible on this asset because it
has no false-positive material names; the change protects against
future assets and is a strict improvement.

---

### F5 — `[0.3, 0.7]` range notation inconsistent (LOW, doc fix)

You're right. Code uses strict `<` on both bounds (`mat.diffuse.r >
0.3f && mat.diffuse.r < 0.7f`). Mathematically that's the open
interval `(0.3, 0.7)`, not the closed `[0.3, 0.7]` I wrote. Doc
updated to `(0.3, 0.7)` (open interval).

The behavioral difference only matters at exactly Kd = 0.7, which
no real asset hits. I considered widening the bound to `<= 0.7` to
match the doc instead, but chose the doc fix because the strict
upper bound is intentional — Kd = 0.7 is plausibly a real "light
gray" wall color (e.g. Phase 0 analytic primitives use gray
0.7-0.8 ranges).

---

### F6 — Write-ordering for overlapping `addVoxelBox` calls (LOW, doc fix)

You're right. `addVoxelBox` is **last-write-wins** for overlapping
calls (each invocation uploads a solid RGBA8 sub-volume; no
per-voxel "skip if non-zero" logic). Verified by inspection that
none of the analytic scenes have visually-significant overlap order:

- **Cornell**: walls at boundaries; floor + ceiling don't overlap
  walls; tall + short box are inside the room interior, no overlap
  with each other or the walls.
- **Empty Room**: 3 boxes (floor, ceiling, back wall) at boundaries;
  no overlap.
- **Simplified Sponza**: floor/ceiling spans the full corridor;
  pillars are inside the corridor, no overlap.
- **Maze**: 1×1 cells, walls placed in distinct cells per the maze
  array, no overlap.
- **Pillars Hall** + **Procedural City**: same — pillars/buildings
  are placed in distinct slots.

Doc updated to call out the last-write-wins semantic and confirm
no scene currently relies on overlap order.

---

### F7 — Sub-volume memory scaling not discussed (LOW, doc fix)

You're right. The new `addVoxelBox` allocates `dim.x * dim.y *
dim.z * 4` bytes per call. At 128³ a full-volume box = 8 MB; at
256³ = 64 MB; at 512³ = 512 MB. The prior per-voxel loop never
allocated a contiguous buffer (one byte at a time).

Doc updated to call out the scaling pattern and note that if the
volume grid is raised, `addVoxelBox` should switch to chunked
streaming uploads. Negligible at the current 128³ default.

---

### F8 — Codex 04 high-severity findings unacknowledged (INFORMATIONAL, partial reject)

You're right that the followups doc didn't mention codex 04 F1+F2,
but those findings are **NOT still present** — both were FIXED in
the parent Step 9 codex 04 reply work, committed in `0f86079`:

- **codex 04 F1** (GPU voxelize rollback) — fixed by snapshotting
  7 prior fields pre-commit and restoring them on
  `voxelizeOBJ_GPU()` failure. See `loadOBJMesh` at the
  `struct PriorScene` block in `src/demo3d.cpp` and the
  `if (!voxelizeOBJ_GPU()) { ... rolling back ... }` branch.
- **codex 04 F2** (GPU SDF toggle stranding CPU EDT) — fixed by
  always populating `meshVoxelData` from the GPU `glGetTexImage`
  readback, even on the GPU/GPU path. See the
  `meshVoxelData = cm.voxelBytes;` line in the cache-hit branch
  and the `// codex 04 F2` comment in the cache-populate block.

Both fixes are runtime-verified (`--toggle-gpu-sdf-off-after-load`
test hook passes). Your review may have been against an older view
of the source — the current `src/demo3d.cpp` has both fixes.

That said, **your point about the followups doc not acknowledging
risks from the parent step is fair.** The followups doc mention
adding "X is fixed in `0f86079`" references would have made the
review easier. Doc updated with an F8 changelog entry that
explicitly states the two findings' resolution status + commit
reference.

---

### Summary

| # | Sev | Action | Result |
|---|---|---|---|
| F1 | Low  | Doc fix  | "codex 03" → "codex 01" (correct Step 8 review reference) |
| F2 | Low  | Doc fix  | Line anchor `:3289` → `:3307` (fix body) / `:3285` (function start) |
| F3 | Med  | Code fix | Removed undocumented `has("dif")` from curtain-blue pattern |
| F4 | Med  | Code fix | Per-letter fabric patterns now `endsWith("_g")`, etc. — no incidental-letter false positives |
| F5 | Low  | Doc fix  | "[0.3, 0.7]" → "(0.3, 0.7)" (open interval; matches strict `<` impl) |
| F6 | Low  | Doc fix  | Documented last-write-wins for overlapping `addVoxelBox`; verified no scene relies on order |
| F7 | Low  | Doc fix  | Memory scaling note: 128³=8MB, 256³=64MB, 512³=512MB transient per full-volume box |
| F8 | Info | Clarify  | Codex 04 F1+F2 already FIXED in commit `0f86079` (parent Step 9); doc now references them explicitly |

**Bottom line.** F4 was the only real bug the user could observe —
once a future asset uses material names like `background_*` or
`fabric_g_dark`, the broad `has` pattern would have produced wrong
colors. F3 was a code-cleanup leftover (no asset hits it currently
but it had no business being there). F8 was a confusion: the
findings are fixed, just not mentioned in this followup doc. The
rest are honest doc-code mismatch corrections.
