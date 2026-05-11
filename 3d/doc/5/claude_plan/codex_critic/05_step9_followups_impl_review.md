# Critic Review 05 - step9_followups_impl.md

Reviewed: 2026-05-10T15:29:16+08:00

Target: `doc/5/claude_plan/step9_followups_impl.md`

## Verdict

Both changes exist in the current source and match the document's structural description. The `addVoxelBox` rewrite is a genuine fix — the batched upload and corrected world-to-voxel math are both present and mirror `addVoxelSphere`. The name-based color hint pass also exists and runs at the end of `loadMTL` as described. The three referenced verification screenshots exist on disk.

The document has several factual inaccuracies and undocumented details that weaken its claims: a wrong codex reference number, an off-by-4 line number anchor, an undocumented `has("dif")` alternative in the curtain-blue pattern, overly broad single-character substring matches that the pattern table misrepresents as "variants", an inconsistent `[0.3, 0.7]` range description (the implementation uses strict `<` on the upper bound), and no discussion of write-ordering semantics for overlapping `addVoxelBox` calls or sub-volume memory scaling at higher resolutions. The document also does not acknowledge that the high-severity findings from codex 04 (GPU voxelizer rollback, GPU SDF toggle state bug) remain unfixed in this follow-up.

## Evidence Checked

- `doc/5/claude_plan/step9_followups_impl.md`.
- Current `src/demo3d.cpp` `addVoxelBox()` (lines 3285-3343), `addVoxelSphere()` (lines 3352+), and all `addVoxelBox` call sites (lines 3000-3264).
- Current `src/obj_loader.h` `loadMTL()` (lines 239-278), `applyNameBasedColorHints()` (lines 287-306), `sponzaMaterialHint()` (lines 310-339).
- `tools/step9_addvoxelbox_fix_cornell.png`, `tools/step9_sponza_hinted.png`, `tools/step9_cornell_orig_hint_check.png` — all exist on disk.
- Codex critic 00-04 for cross-reference consistency.

## What Looks Good

- The `addVoxelBox` rewrite is a real, present fix in source. The function body at `src/demo3d.cpp:3307-3342` matches the code snippet in the document. The batched `glTexSubImage3D` upload and `worldToVoxel` lambda are both there.
- The `addVoxelSphere` function (lines 3352+) uses the identical `worldToVoxel` lambda pattern, confirming the document's claim that the two helpers are now consistent.
- The color hint pass is wired correctly: `applyNameBasedColorHints()` runs at the end of `loadMTL()` (line 274), before the "Loaded N materials" log line (line 276). The `looksGray` guard correctly checks all three channels for near-gray and checks the value range. The `vec3(-1)` sentinel is correctly used to skip unmatched materials.
- The pattern ordering in `sponzaMaterialHint()` protects against some obvious overlaps: `has("ceiling")` at line 317 fires before `has("fabric") && has("c")` at line 327, so `fabric_ceiling` won't misfire on purple.
- The verification screenshots exist. The "21 of 25 materials hinted" claim and "0 hints for Cornell-Original" claim are plausible given the pattern table and the guard logic.
- The architecture note about hints staying inside `loadMTL` (not per-frame) is correct: the cache stores hinted Kd values, and both GPU `buildTriangles` and CPU `voxelize` consume the same `materials` map.

## Findings

### 1. Codex reference number is wrong

Severity: Low

The document says the `addVoxelBox` rewrite mirrors "the `addVoxelSphere` pattern (Step 8 codex 03 F3 + F4)" (lines 46 and 198-200). In the codex critic numbering, **codex 03** is `03_load_path_step9_plan_review.md` — the Step 9 plan review, not the Step 8 implementation review. The source code comment at line 3345-3351 says "Step 8 Phase 2b (codex 01 F3 + F4)", referencing **codex 01** (the Step 8 plan review).

The document should reference codex 01 or 02 (the Step 8 reviews), not codex 03 (the Step 9 plan review).

### 2. Line number anchor is off by 4

Severity: Low

The document's summary table says `src/demo3d.cpp:3289` for the `addVoxelBox` rewrite. In the current source, the function `void Demo3D::addVoxelBox(...)` starts at line 3285. Line 3289 is just the `bool emissive` parameter declaration. The fix body (the `worldToVoxel` lambda, the batched upload) starts at line 3307. The anchor should point to 3285 (function start) or 3307 (fix body start).

### 3. Undocumented `has("dif")` alternative in curtain-blue pattern

Severity: Medium

The document's pattern table says `curtain (with blue)` → `(0.20, 0.30, 0.65)` blue velvet. The source code at line 322 checks:

```cpp
if (has("curtain") && (has("blue") || has("dif")))
```

The `has("dif")` alternative is not documented anywhere in the document. A material named e.g. `curtain_dif` (which may be a shorthand for "curtain diffuse" in some naming conventions) would receive blue coloring without the document disclosing this behavior. This means the documented pattern coverage is incomplete — the actual match is broader than the table claims.

### 4. Single-character substring matches are far more broad than the "variant" framing suggests

Severity: Medium

The document's pattern table says:

| Pattern | Color |
|---|---|
| `fabric` (variant g) | gold |
| `fabric` (variants c, e) | purple |

The source code at lines 326-327 checks:

```cpp
if (has("fabric") && has("g"))               return gold;
if (has("fabric") && (has("c") || has("e"))) return purple;
```

`has("g")` matches any fabric material whose **entire name** contains the letter 'g' anywhere — not just a "variant g" suffix. Examples of false positives:

- `fabric_green` → gold (0.70, 0.50, 0.20), not the green foliage color that `has("plant")` would produce
- `fabric_glass` → gold
- `background_fabric` → gold (contains "g" in "background")

Similarly, `has("c")` matches any name containing the letter 'c':
- `fabric_column` → purple, overriding the marble column color
- `fabric_ceiling` → would be purple if "ceiling" weren't checked first (the pattern ordering at line 317 saves this specific case)

The document acknowledges substring matching is "brittle" in general, but the "variant g/c/e" framing implies these are specific suffix patterns when they are actually single-character substring matches that will misfire on any asset whose naming conventions include these letters incidentally.

### 5. `[0.3, 0.7]` range description is inconsistent with the strict `<` implementation

Severity: Low

The document says the guard checks for "existing Kd is a near-gray in [0.3, 0.7]" (line 111). The source code at line 295 uses:

```cpp
mat.diffuse.r > 0.3f && mat.diffuse.r < 0.7f
```

The upper bound is strict `<` (0.7 is excluded), but `[0.3, 0.7]` in mathematical notation includes both endpoints. Either the guard should use `<= 0.7f`, or the document should say "[0.3, 0.7)" or "in (0.3, 0.7)".

This edge case doesn't affect Sponza's 0.4704, but a hypothetical material with Kd = (0.70, 0.70, 0.70) would be excluded from hints despite being a legitimate placeholder gray. The document's stated intent and the implementation's threshold behavior disagree.

### 6. No discussion of write-ordering for overlapping `addVoxelBox` calls

Severity: Low

When multiple `addVoxelBox` calls produce overlapping sub-volumes, the last call wins — each call uploads a solid RGBA buffer that overwrites whatever was previously in that region of `voxelGridTexture`. For analytic Cornell (boxes inside the room, walls at boundaries), this works fine because the boxes don't overlap the walls.

For other analytic scenes — Maze (up to 36 boxes), Procedural City (~17 boxes), Pillars Hall (~12 boxes) — the document claims the fix benefits them all but does not discuss whether any of these scenes have overlapping boxes where write order produces unintended boundary artifacts. The document should either confirm no overlaps exist in these scenes or note the last-write-wins semantics.

### 7. Sub-volume memory scaling not discussed

Severity: Low

The `addVoxelBox` rewrite allocates `size_t(dim.x) * dim.y * dim.z * 4` bytes on the CPU for each invocation. At 128³ resolution, a box spanning the full volume is ~8 MB. At 256³ it's ~64 MB. At 512³ (if the project ever scales up) it's ~512 MB — a transient allocation that could spike memory usage when switching analytic scenes with large boxes.

The prior per-voxel loop never allocated a contiguous buffer (it uploaded one voxel at a time), so this is a new memory pattern the document doesn't flag. For the current 128³ default this is negligible, but the scaling trajectory should be noted.

### 8. High-severity codex 04 findings remain unacknowledged

Severity: Informational

The document is a Step 9 follow-up but does not mention that the two high-severity findings from codex 04 (review 04, findings 1 and 2) are still present:

1. GPU voxelizer failure does not roll back to the prior scene (codex 04 F1).
2. Turning GPU SDF off after a GPU/GPU load strands CPU EDT without `meshVoxelData` (codex 04 F2).

These are not the follow-up's responsibility to fix, but a document claiming "Implemented and verified" status should at least acknowledge which known risks from the parent step remain open, especially when the follow-up touches the same code paths (the `addVoxelBox` rewrite modifies scene-switch paths; the color hints modify material loading paths that both CPU and GPU voxelizers consume).

## Verification Gaps To Add

- Add a test that loads Sponza-master through two equivalent paths (e.g., `res/models/sponza.obj` and `./res/models/sponza.obj`) and checks whether the cache produces one or two entries — related to codex 04 F3 but also relevant here because the color hints are baked into the cached `materials` map.
- Add a test for the `has("dif")` curtain pattern: load an asset with a material named `curtain_dif_green` and verify whether it receives blue or green coloring (the current code would give blue because `has("dif")` is checked before `has("green")`).
- Run the `addVoxelBox` rewrite at 256³ and 512³ resolutions to check sub-volume allocation sizes and wall time for analytic scene-switch.
- Verify that none of the other analytic scenes (Maze, City, Pillars) have overlapping `addVoxelBox` calls where write order matters.
- Confirm whether the `fabric_g` / `fabric_c` / `fabric_e` pattern matches are intended as single-letter substring matches or should be tightened to suffix/delimiter-bounded matches (e.g., `_g`, `_c`, `_e` or regex-like `\bg\b`).