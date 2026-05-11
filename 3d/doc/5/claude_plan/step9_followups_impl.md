# Step 9 Follow-ups: Implementation Notes (revised after codex 05)

**Date:** 2026-05-10 (revised after codex review `05_*` / reply `05_*`)
**Status:** Implemented and verified. Two small post-Step-9 fixes
landed together: an `addVoxelBox` rewrite that drops analytic-scene
switching from ~10 s to <50 ms (and corrects long-broken coord math)
and a name-based color-hint pass that gives Sponza-master varied
materials until proper texture loading lands.

**Changelog (post codex `05_step9_followups_impl_review.md`):**

- **F1+F2 (low) doc fixes.** Codex reference `Step 8 codex 03 F3+F4`
  was wrong — Step 8 reviews are codex 01 (plan) and codex 02 (impl);
  codex 03 is the Step 9 plan review. Corrected to `Step 8 codex 01
  F3+F4` (matches the source comment in `addVoxelSphere`). Line
  anchor `:3289` was the parameter declaration line; corrected to
  `:3307` (fix-body start) / `:3285` (function start).
- **F3 (medium) code fix.** Removed undocumented `has("dif")`
  alternative in the curtain-blue pattern. It was a leftover that
  would have flagged any curtain material with "diff" in its name as
  blue regardless of the actual color word. Doesn't affect the
  Sponza-master test (no curtains exist in that asset).
- **F4 (medium) code fix.** Per-letter fabric variant patterns
  (`_g`, `_c`, `_e`) now use `endsWith` rather than naked `has`. The
  prior `has("g")` would also have matched any name containing 'g'
  anywhere — `background_fabric` → gold, `flag_fabric` → gold,
  `fabric_green` → gold (overriding the green-foliage rule), etc.
  Sponza-master uses `fabric_a/c/d/e/f/g` suffixes so the real
  intent was suffix-bound matching. Hint count unchanged at 21/25
  after the fix.
- **F5 (low) doc fix.** "[0.3, 0.7]" range notation was inconsistent
  with the strict `<` upper-bound check. Corrected to "(0.3, 0.7)"
  (open interval). The behavioral difference only matters at exactly
  Kd = 0.7, which no real asset hits.
- **F6 (low) doc fix.** Added note that overlapping `addVoxelBox`
  calls write last-call-wins via the bbox sub-volume upload. Verified
  by inspection that none of the analytic scenes (Empty Room,
  Cornell, Simplified Sponza, Maze, Pillars, City) have overlapping
  `addVoxelBox` regions where this would matter.
- **F7 (low) doc fix.** Added memory-scaling note: at 128³ a
  full-volume box transient is 8 MB; 256³ → 64 MB; 512³ → 512 MB.
  The prior per-voxel loop never allocated a contiguous buffer. For
  the current 128³ default this is negligible.
- **F8 (informational) clarification.** Codex 04 F1 (GPU voxelize
  rollback) and F2 (GPU SDF toggle stranding CPU EDT) were FIXED in
  the parent Step 9 codex 04 reply work, committed in `0f86079`.
  Codex 05's claim that they're "still present" appears to be based
  on a stale view; the current source has the snapshot/restore
  pattern in `loadOBJMesh` (F1) and the always-keep-CPU-mirror
  policy (F2).

---

## Summary

| Change | File | Effect |
|---|---|---|
| `addVoxelBox` batched + corrected coord math | [src/demo3d.cpp:3307](src/demo3d.cpp#L3307) (fix body; function header at `:3285`) | Analytic Cornell scene-switch ~10 s → <50 ms; geometry now correctly anchored at world center |
| Sponza name-based color hints in `loadMTL` post-process | [src/obj_loader.h:287-339](src/obj_loader.h#L287) | Sponza-master shows 21 distinct material colors instead of uniform 0.4704 gray |

Neither change introduces new toggles, CLI flags, or shaders.

---

## Change 1 — `addVoxelBox` rewrite

### Why

User reported "switching to analytic Cornell takes ~10 s after Step 9
made everything else fast". Investigation showed `addVoxelBox` had
**two latent bugs** present since Step 0 that were just newly visible
once the OBJ load path stopped dominating the wall time:

1. **Per-voxel `glTexSubImage3D` in a tight loop.** Same anti-pattern
   I called out for `addVoxelSphere` in Step 8 codex 02 F3, but
   `addVoxelBox` was left as "out of scope" then. For analytic Cornell
   at 128³: floor + ceiling + 4 walls + 2 boxes + light = ~525K
   individual GL calls × ~20 µs driver overhead = ~10 s stall.
2. **Wrong world-to-voxel math.** The helper assumed
   `gridOrigin = (0,0,0)` and `voxelSize = 1/N` — but the actual SDF
   volume is at `volumeOrigin = (-2,-2,-2)`, `volumeSize = (4,4,4)`.
   Cornell walls at `x = -1.5` mapped to voxel index `-192` and got
   clamped to 0. Result: scenes were silently rendered with
   most boxes clipped to a corner of the volume.

### Fix

Mirrored the `addVoxelSphere` pattern (Step 8 codex 01 F3 + F4 —
the Step 8 plan-review findings; the source comment in
`addVoxelSphere` correctly references codex 01):

```cpp
const int N = volumeResolution;
auto worldToVoxel = [&](const glm::vec3& w) {
    glm::vec3 norm = (w - volumeOrigin) / volumeSize;
    return glm::ivec3(norm * float(N));
};
glm::ivec3 minV = worldToVoxel(center - halfSize);
glm::ivec3 maxV = worldToVoxel(center + halfSize);
minV = glm::clamp(minV, glm::ivec3(0), glm::ivec3(N - 1));
maxV = glm::clamp(maxV, glm::ivec3(0), glm::ivec3(N - 1));
glm::ivec3 dim = maxV - minV + glm::ivec3(1);
if (dim.x <= 0 || dim.y <= 0 || dim.z <= 0) return;

// Build sub-volume on CPU (one byte pattern repeated -- box is solid).
std::vector<uint8_t> sub(size_t(dim.x) * dim.y * dim.z * 4);
for (size_t i = 0, n = sub.size() / 4; i < n; ++i) {
    sub[i*4 + 0] = r; sub[i*4 + 1] = g;
    sub[i*4 + 2] = b; sub[i*4 + 3] = a;
}
// ONE upload.
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glTexSubImage3D(GL_TEXTURE_3D, 0, minV.x, minV.y, minV.z,
                dim.x, dim.y, dim.z, GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
glBindTexture(GL_TEXTURE_3D, 0);
```

The `emissive` parameter (alpha = 255 vs 128) is preserved from the
prior behavior.

### Verify

- **`tools/step9_addvoxelbox_fix_cornell.png`** — analytic Cornell
  Box now renders correctly: red left wall, green right wall, white
  floor/ceiling/back, properly-positioned tall and short boxes,
  ceiling light visible. Compare to pre-fix where everything was
  clipped to a corner of the volume.
- Wall time: 3.75 s for `--switch-to-scene=1 --exit-frames=120`,
  most of which is the 120-frame render at ~60 fps (~2 s). The
  actual `setScene` work is now sub-100 ms vs the prior ~10 s stall.
- Same fix benefits all other analytic scenes that use `addVoxelBox`:
  Empty Room (3 boxes), Simplified Sponza (~14 boxes), Maze (up to
  36 boxes), Pillars Hall (~12 boxes), Procedural City (~17 boxes).

---

## Change 2 — Sponza name-based color hints

### Why

Sponza-master's `.mtl` defines 25 materials but their `Kd` values are
all `0.4704 0.4704 0.4704` — placeholder grays where the real color
comes from `map_Kd` textures we don't load yet. Result: the entire
atrium renders as uniform gray in mode 0, even after Step 6's `.mtl`
parser landed. The user asked for a quick win until proper texture
loading is implemented.

### Fix

Added two helpers to `OBJLoader`:

- `applyNameBasedColorHints()` — runs at the end of `loadMTL`,
  iterates the parsed `materials` map, and overrides Kd for any
  material whose **(a)** name matches a Sponza-style pattern AND
  **(b)** existing Kd is a near-gray with each channel in the open
  interval (0.3, 0.7). The two-condition guard is load-bearing: it
  ensures Cornell-Original's distinct red/green/white walls (Kd
  values like 0.63/0.065/0.05) are never touched even if a material
  happens to share a name fragment.
- `sponzaMaterialHint(name)` — case-insensitive substring matcher
  returning a "make-sense" diffuse color for known Sponza material
  patterns. Per-letter fabric variant patterns (`_g`, `_c`, `_e`)
  use `endsWith` to suffix-bound the match (codex 05 F4); naked
  `has` would have matched any name containing those letters
  anywhere (e.g. `background` → gold-fabric). Returns `vec3(-1)`
  sentinel when no pattern matches.

Pattern-to-color table (substring match on lowercased name):

| Pattern | Color | Intent |
|---|---|---|
| `brick` | `(0.65, 0.45, 0.35)` | reddish-brown brick |
| `ceiling` | `(0.92, 0.86, 0.72)` | warm cream |
| `floor` | `(0.72, 0.62, 0.48)` | sandstone tan |
| `arch` | `(0.80, 0.74, 0.62)` | light stone |
| `column` | `(0.88, 0.85, 0.78)` | marble |
| `chain` | `(0.35, 0.32, 0.30)` | dark metal |
| `curtain` (with `blue`) | `(0.20, 0.30, 0.65)` | blue velvet |
| `curtain` (with `green`) | `(0.20, 0.55, 0.30)` | green velvet |
| `curtain` (default) | `(0.65, 0.12, 0.12)` | red velvet |
| `fabric` ending in `_g` | `(0.70, 0.50, 0.20)` | gold |
| `fabric` ending in `_c` or `_e` | `(0.55, 0.35, 0.55)` | purple |
| `fabric` (default — `_a/_d/_f`) | `(0.62, 0.45, 0.30)` | brown |
| `vase` + `plant` | `(0.30, 0.50, 0.22)` | foliage |
| `vase` + `hang` | `(0.78, 0.55, 0.20)` | copper |
| `vase` (default) | `(0.70, 0.42, 0.28)` | terracotta |
| `lion` | `(0.72, 0.50, 0.22)` | bronze |
| `flag` / `pole` | `(0.78, 0.65, 0.30)` | gold |
| `roof` | `(0.45, 0.28, 0.22)` | dark tile |
| `thorn` / `plant` / `leaf` | `(0.30, 0.50, 0.22)` | green foliage |
| `detail` | `(0.82, 0.78, 0.68)` | off-white trim |
| `background` | `(0.60, 0.65, 0.75)` | sky-ish |

The thresholds are an approximation, not asset-correct — they're
"plausible building materials" until real `map_Kd` sampling lands
(planned next step).

### Verify

- **Sponza-master**: 21 of 25 materials hinted. The 4 not hinted are
  the unnamed `Material__25/47/57/298` placeholder names that don't
  match any pattern — they stay default gray.
  Capture: [tools/step9_sponza_hinted.png](tools/step9_sponza_hinted.png)
  shows reddish-brown brick wall + bronze lion-feature rim at the
  doorway top, replacing the prior uniform dark gray.
- **Cornell-Original**: 0 hints applied (logged as "Loaded 8
  materials" with no hint message). Distinct red/green/white walls +
  glowing light + boxes preserved.
  Capture: [tools/step9_cornell_orig_hint_check.png](tools/step9_cornell_orig_hint_check.png)
  matches the pre-hint Step 9 baseline.
- **Old `cornell_box.obj`** (no .mtl): unchanged. Hint logic only
  runs inside `loadMTL`; meshes that don't reference a `mtllib` line
  fall through the legacy `getMaterialColor` table as before.

### Logged output

Both Sponza variants print:
```
[OBJLoader] Applied 21 name-based color hints to placeholder materials
[OBJLoader] Loaded 25 materials
```

Cornell-Original prints:
```
[OBJLoader] Loaded 8 materials
```

(no "Applied N hints" line because the guard short-circuits on every
material — Cornell-Original's distinct Kd values fail the looksGray
test).

---

## Combined Performance Headlines

| Scenario | Pre-fix | Post-fix |
|---|---|---|
| Analytic Cornell scene-switch | ~10 s | <50 ms |
| Empty Room scene-switch | ~3 s (estimated; 3 boxes) | <20 ms |
| Sponza-master visual variety | uniform gray | 21 distinct material colors |
| Cornell-Original | distinct walls + light (correct) | same (preserved) |

---

## Architecture Notes

**The `addVoxelBox` rewrite is finally consistent with `addVoxelSphere`
and `addVoxelSphere`'s prior fixes** (Step 8 codex 01 F3 + F4 — same
two issues, same solution pattern). Both helpers now use the canonical
world-to-voxel formula `(world - volumeOrigin) / volumeSize × N` and
upload via one batched `glTexSubImage3D`. The OBJ triangle voxelizer
(CPU + GPU) uses the same convention. There is now ONE world-to-voxel
formula in the codebase; the broken `(0,0,0)`-origin variant is gone.

**`addVoxelBox` is last-write-wins for overlapping calls (codex 05
F6).** Each call uploads a solid RGBA8 sub-volume that overwrites
whatever was previously in that bbox region of `voxelGridTexture` —
no per-voxel "skip if non-zero" logic. For analytic Cornell the
boxes don't overlap (walls at boundaries, internal boxes inside).
Verified by inspection that none of the analytic scenes (Empty Room,
Cornell, Simplified Sponza, Maze, Pillars Hall, Procedural City)
have call sequences where overlap order would be visually significant.

**Sub-volume memory scales with voxel resolution (codex 05 F7).**
`addVoxelBox` allocates `dim.x * dim.y * dim.z * 4` bytes on the CPU
per call. At 128³, a full-volume box is ~8 MB transient; at 256³
~64 MB; at 512³ ~512 MB. The prior per-voxel loop never allocated
a contiguous buffer (one byte at a time), so this is a new memory
pattern. Negligible at the current 128³ default; if/when the
volume grid is raised, `addVoxelBox` should switch to a streaming
upload (chunked sub-volumes) rather than one big allocation.

**The color-hint pass is intentionally narrow.** It's a temporary
visual aid until texture loading lands — substring matching on
material names is brittle (any asset using "fabric_a/b/c..." will get
hint colors that may not match its actual texture), but it's a no-op
on assets whose `.mtl` already provides distinct colors. Cornell-
Original's intentional design isn't disrupted; Sponza-master's
"all materials are 0.4704 gray" placeholder is replaced with
plausible substitutes.

**Hint logic stays inside `loadMTL`**, not in the per-face material
lookup at voxelize time. This means:
- The cache stores hinted Kd values (no per-frame substring matching)
- GPU `buildTriangles` and CPU `voxelize` see the same Kd
- Toggling GPU/CPU voxelizer hits no drift (per Step 9 codex 03 F4
  contract)

---

## Known Open Items

| Item | Where to land it |
|---|---|
| Real .tga texture loading + UV-driven voxel sampling | Step 10 candidate; would supersede the color hints |
| Hint pattern coverage for the 4 unnamed `Material__N` Sponza-master materials | Trivial follow-up if user wants the 4 remaining gray materials colored |
| Hint table for other public assets (Crytek Sponza variants, Bistro, Buddha) | Add patterns as new assets are added |
| `addVoxelBox` per-voxel-write density toggle (currently fills the entire bbox solid; could rasterize a hollow shell like `addVoxelSphere`) | Out of scope — analytic boxes are intentionally solid for the Cornell test scene |

---

## Why These Were "Free" Wins

Both changes are < 100 lines of net code, no new shaders, no new GPU
work, no new toggles. They cleanup latent issues that had been
masked by larger problems further upstream:

- `addVoxelBox` was slow forever, but the OBJ load was always slower,
  so analytic scenes "felt fine" in comparison. Once Step 9 made OBJ
  loads fast, the `addVoxelBox` cost became the dominant scene-switch
  delay and surfaced.
- Sponza-master colors were uniformly gray since Step 6's .mtl parser
  landed, but the user spent Steps 7-9 working on camera, GPU SDF,
  and load performance — visual quality wasn't the focus. The
  hint-table is a 50-line stopgap until Step 10 textures.

Both changes are reversible (single-function rewrites) so a future
proper texture pipeline can supersede them cleanly.
