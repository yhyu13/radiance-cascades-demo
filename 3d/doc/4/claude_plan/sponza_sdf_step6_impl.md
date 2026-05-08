# Sponza SDF — Step 6: Implementation Notes (revised after codex 12)

**Date:** 2026-05-08 (revised after codex review `12_*` / reply `12_*`)
**Plan ref:** `C:\Users\XINDONG\.claude\plans\frolicking-yawning-moth.md`
**Status:** Implemented and verified. Build clean (0 errors, 37 project
warnings — Step 5 baseline preserved). Headless captures confirm the
qualitative win: Cornell-Original renders distinct red/green/white walls
plus a glowing light quad. Sponza-master loads all 262K faces and bakes
the full SDF; the uniform mid-gray appearance was predicted up-front
(its Kd is 0.4704 across all 25 materials — color comes from textures
we deliberately did not load).

**Changelog (vs first impl):** F1 — `resetCameraToScenePreset()` now
translates the 4-way `currentOBJPath` key to the 2-way kind before
calling `applyOBJViewPreset()` so the new variants reset to the right
preset (was: silently fell through to "unknown objKind" warning).
Runtime-verified via `--test-reset-helper` on both Cornell-Original and
Sponza-master. F2/F3 — corrected the Sponza density narrative: the old
`res/scene/sponza.obj` was replaced byte-for-byte with the
Sponza-master geometry at some prior point in this workspace, so the
"old=75K vs new=262K" comparison and "matches Step 4v2 baseline" claim
were both wrong. F4 — added a known-noise note about the pre-existing
`sdf_3d.comp` shader compile failure that appears in every runtime log.
F5 — fan triangulator now bounds-checks resolved indices before
emitting triangles. F6 — `[OBJLoader] Material …` log now distinguishes
"legacy fallback color" from true "default gray (no legacy match)" so
the legacy path doesn't read like a failure. F7 — `--load-obj=` CLI
comment updated to list all four accepted names.

---

## Summary

| Change | Where | Status |
|---|---|---|
| Real `.mtl` parser (`Kd` + `Ke` only) — `loadMTL()` | `OBJLoader::loadMTL` (new) | done |
| `mtllib` directive now resolves relative to .obj's directory | `OBJLoader::load` mtllib branch | done |
| `Ke` field added to `OBJMaterial`; `materials` map + `unknownMaterialsLogged` set | `OBJLoader` private members | done |
| `voxelize()` consults parsed materials first; legacy `getMaterialColor` is fallback | `OBJLoader::voxelize` | done |
| `Ke` baked into albedo voxel as `saturate(Kd + Ke / maxKe)` | `OBJLoader::voxelize` | done |
| **n-gon fan-triangulation in face parser** (forced by Cornell-Original quads) | `OBJLoader::load` f-line block | done |
| **Negative-index resolution** (forced by Cornell-Original `f -4 -3 -2 -1`) | same | done |
| 4-way `currentOBJPath` keys: `cornell` / `cornell_orig` / `sponza` / `sponza_master` | `Demo3D::loadOBJMesh` | done |
| 2 new ImGui buttons: Cornell-Original, Sponza-master | `Demo3D::renderUI` Scene panel | done |
| 4-way "Active OBJ" label | same | done |
| CLI: `--load-obj=cornell-orig` and `--load-obj=sponza-master` | `main3d.cpp` arg parser | done |

**Files touched (3):**
- `src/obj_loader.h` — material parser + n-gon/negative-index fix + Ke albedo boost
- `src/demo3d.cpp` — 4-way key, 2 new buttons, 4-way label
- `src/main3d.cpp` — extended CLI mapping

No shader changes, no header dependencies added, no build-system changes.

---

## The Critical Pre-existing Loader Bug

The plan predicted only a .mtl parser was needed. First headless capture
of Cornell-Original came back with **8 voxels filled** — three orders of
magnitude below the expected ~40K. Investigation showed Cornell-Original
uses two OBJ features the original parser silently dropped:

```
v  -1.01  0.00   0.99
v   1.00  0.00   0.99
v   1.00  0.00  -1.04
v  -0.99  0.00  -1.04
g floor
usemtl floor
f -4 -3 -2 -1
```

1. **Quad faces.** The parser hardcoded `iss >> v1 >> v2 >> v3` —
   reading exactly 3 tokens, dropping the 4th vertex of every quad.
2. **Negative indices.** The parser did `face.v[0]--` (decrement to
   0-based), so `-4` became `-5` — a negative array index used to
   subscript `vertices[]` later. Undefined-behavior territory.

Both issues are valid OBJ features (the spec allows quads and n-gons
since OBJ 1.0; negative indices are a Wavefront convention dating from
the 1990s). The fix is small but load-bearing:

```cpp
// Read ALL face tokens, not just 3.
std::vector<int> fv, ft, fn;
std::string tok;
while (iss >> tok) {
    int v = 0, t = -1, n = -1;
    parseVertexIndex(tok, v, t, n);
    // OBJ allows negative indices = relative to current vertex count.
    if (v < 0) v = static_cast<int>(vertices.size()) + v + 1;
    if (t < 0 && t != -1) t = static_cast<int>(texcoords.size()) + t + 1;
    if (n < 0 && n != -1) n = static_cast<int>(normals.size()) + n + 1;
    fv.push_back(v - 1);
    ft.push_back(t == -1 ? -1 : t - 1);
    fn.push_back(n == -1 ? -1 : n - 1);
}
// Fan-triangulate (v0, vi, vi+1).
for (size_t i = 1; i + 1 < fv.size(); ++i) {
    OBJFace face;
    face.v[0] = fv[0];   face.t[0] = ft[0];   face.n[0] = fn[0];
    face.v[1] = fv[i];   face.t[1] = ft[i];   face.n[1] = fn[i];
    face.v[2] = fv[i+1]; face.t[2] = ft[i+1]; face.n[2] = fn[i+1];
    faces.push_back(face);
    faceMaterials.push_back(currentMaterial);
}
```

Cornell-Original 16 quads × 2 → 32 triangles → **39,648 voxels filled**
after the fix. The fan triangulation assumes convex polygons, which
holds for both Cornell-Original (axis-aligned quads) and Sponza-master
(tris and quads only — verified by capture).

This is now logged as a Do-Not-Repeat in `.wolf/cerebrum.md` so future
OBJ assets don't re-trigger the same investigation.

---

## .mtl Parser Design

Minimal, single-pass. Only what the radiance pipeline actually consumes:

```cpp
bool OBJLoader::loadMTL(const std::string& mtlPath) {
    std::ifstream f(mtlPath);
    if (!f.is_open()) {
        std::cout << "[OBJLoader] Material library not found: " << mtlPath
                  << " (will use default gray)" << std::endl;
        return false;
    }
    OBJMaterial cur;
    bool haveCur = false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string p;
        iss >> p;
        if (p == "newmtl") {
            if (haveCur) materials[cur.name] = cur;
            cur = OBJMaterial();
            iss >> cur.name;
            haveCur = true;
        } else if (p == "Kd" && haveCur) {
            iss >> cur.diffuse.x >> cur.diffuse.y >> cur.diffuse.z;
        } else if (p == "Ke" && haveCur) {
            iss >> cur.emissive.x >> cur.emissive.y >> cur.emissive.z;
        }
    }
    if (haveCur) materials[cur.name] = cur;
    return true;
}
```

Fields deliberately ignored: `Ns`, `Ka`, `Ks`, `illum`, `Ni`, `d`,
`map_Kd`, `map_Ka`, `map_Disp`, `map_d`. The pipeline has no spec or
shininess channel; ambient + specular are subsumed into the cascade
GI; texture maps are out of scope for Step 6.

The `mtllib` directive in `OBJLoader::load()` now resolves the .mtl
filename against the .obj's directory:

```cpp
std::string objDir;
{
    size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) objDir = filename.substr(0, slash + 1);
}
// ...
else if (prefix == "mtllib") {
    std::string mtlFile;
    iss >> mtlFile;
    if (!mtlFile.empty()) loadMTL(objDir + mtlFile);
}
```

So `res/scene/CornellBox-Original/CornellBox-Original.obj` →
`res/scene/CornellBox-Original/CornellBox-Original.mtl` automatically.

---

## Material Lookup with Triple Fallback

`voxelize()` now resolves a face's color in three tiers, in order:

1. **Parsed `.mtl` materials map** (Step 6 addition — primary path)
2. **Legacy hardcoded `getMaterialColor()`** (kept for old `cornell_box.obj`
   that uses names like `BloodyRed`, `Light`, `Khaki`)
3. **Default `(0.8, 0.8, 0.8)` gray** (last resort; logged once per
   material name via `unknownMaterialsLogged` set)

```cpp
glm::vec3 kd, ke;
auto mit = materials.find(matName);
if (mit != materials.end()) {
    kd = mit->second.diffuse;
    ke = mit->second.emissive;
} else {
    kd = getMaterialColor(matName);  // legacy hardcoded names
    ke = glm::vec3(0.0f);
    if (!matName.empty() && unknownMaterialsLogged.insert(matName).second) {
        std::cout << "[OBJLoader] Unknown material '" << matName
                  << "' -> default color " << kd.x << "," << kd.y << "," << kd.z
                  << std::endl;
    }
}
```

The `unknownMaterialsLogged` is `mutable` because `voxelize()` is `const`.
Per-name dedup avoids spamming hundreds of thousands of lines for
Sponza-style meshes.

---

## Ke as Albedo Boost

The user picked "Kd + Ke as albedo boost" over auto-relocating
`lightPosition`. The lighting model stays as-is — Ke just paints the
voxel brighter so geometry that the .mtl marked emissive *looks* lit.

```cpp
glm::vec3 color = kd;
float maxKe = std::max({ ke.x, ke.y, ke.z, 1.0f });
if (maxKe > 1.0f) {
    color = glm::clamp(kd + ke / maxKe, glm::vec3(0.0f), glm::vec3(1.0f));
}
```

For Cornell-Original's `light` material (`Kd 0.78 0.78 0.78`,
`Ke 17 12 4`):
- maxKe = 17
- ke / 17 ≈ (1.0, 0.706, 0.235)
- Kd + ke/maxKe ≈ (1.78, 1.49, 1.02) → saturate → `(1, 1, 1)` warm-white

For all non-emissive materials (`Ke 0 0 0`), maxKe stays at 1.0 and the
boost branch is skipped → identical behavior to a Kd-only path. Sponza
walls and Cornell colored walls are unchanged.

---

## 4-way OBJ Key

`currentOBJPath` is now a 4-way string discriminator computed from the
filename, while `objKind` (used by `applyOBJViewPreset` and the
per-OBJ `halfExtent`) stays 2-way:

```cpp
const bool isSponza = (filename.find("sponza") != std::string::npos)
                   || (filename.find("Sponza") != std::string::npos);
const std::string objKind = isSponza ? "sponza" : "cornell";
std::string objKey;
if (filename.find("Sponza-master") != std::string::npos)            objKey = "sponza_master";
else if (filename.find("CornellBox-Original") != std::string::npos) objKey = "cornell_orig";
else if (isSponza)                                                  objKey = "sponza";
else                                                                objKey = "cornell";
```

Both Sponza variants share the Sponza preset (halfExtent=1.9, camera
(3.5, 0.5, 0), light (0, 0.5, 0)); both Cornell variants share the
Cornell preset. The 4-way key only exists so the ImGui ACTIVE indicator
distinguishes the new variants from the old:

```cpp
const char* objName = "Cornell Box (OBJ)";
if      (currentOBJPath == "sponza")        objName = "Sponza (OBJ)";
else if (currentOBJPath == "sponza_master") objName = "Sponza-master (OBJ+MTL)";
else if (currentOBJPath == "cornell_orig")  objName = "Cornell-Original (OBJ+MTL)";
ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active: %s", objName);
```

---

## CLI Surface

`--load-obj=` now accepts four names:

| CLI flag                 | Resolved path                                              |
|---|---|
| `--load-obj=cornell`         | `res/scene/cornell_box.obj`                              |
| `--load-obj=cornell-orig`    | `res/scene/CornellBox-Original/CornellBox-Original.obj`  |
| `--load-obj=sponza`          | `res/scene/sponza.obj`                                   |
| `--load-obj=sponza-master`   | `res/scene/Sponza-master/sponza.obj`                     |

Unknown names print a clear error listing the four valid options and
exit before window construction.

---

## Verification Results

### Build (Release, `cmake --build . --config Release --clean-first`)

- Errors: **0**
- Project warnings (`3d/src/`): **37** — exact Step 5 baseline preserved
- Distribution unchanged: 13×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018, 1×C4310
- Step 6 added zero new warnings

### Headless captures

| Capture                                        | Outcome |
|---|---|
| `tools/step6_cornell_orig_mode0.png`           | Distinct red, green, white walls + glowing warm-white light quad. Two boxes visible inside (with expected 128³ voxel-staircase artifacts). |
| `tools/step6_sponza_master_mode0.png`          | Uniform mid-gray Sponza geometry (predicted: Sponza-master Kd=0.4704). Same camera framing as old Sponza. |
| `tools/step6_sponza_master_mode5.png`          | SDF step-count heatmap — full red/yellow gradient confirms all 147,593 SDF seeds populated correctly. |
| `tools/step6_sponza_old_mode0.png`             | Visually similar to `step4v2_sponza_mode0.png` but **not pixel-identical** (~450K of 921K pixels changed; codex 12 F3). Run-to-run temporal-jitter EMA accounts for some of this; the rest is the F2 asset substitution (see below) — Step 4 v2 was captured against a different file. Treat this as a qualitative not regression-grade comparison. |
| `tools/step6_cornell_old_mode0.png`            | **Pixel-identical** (codex 12 verified) to the Step 5 cornell capture — red wall via legacy `BloodyRed`, white walls via legacy `Khaki`/`White`. Right wall stays white because `DarkGreen` is not in the legacy table (pre-existing limitation, NOT a Step 6 regression). This is the strong-evidence regression check; old-Sponza is qualitative only. |

### Bake stats (codex 12 F2 corrected)

| OBJ              | Faces   | Materials  | Seeds   | EDT (ms) | Albedo (ms) |
|---|---|---|---|---|---|
| `cornell`        | 32      | 0 (no .mtl) *(see note)* | 40,878  | 62.7     | 28.3        |
| `cornell_orig`   | 32      | 8           | 39,648  | 68.9     | 27.7        |
| `sponza`         | 262,267 | 0 (no .mtl) | 147,593 | ~67      | ~30         |
| `sponza_master`  | 262,267 | 25          | 147,593 | 66.5     | 26.4        |

**codex 12 F2 correction.** The first revision of this doc claimed old
`sponza.obj` was ~75K faces and treated the equal seed count between the
two Sponza paths as evidence of voxel saturation under a 3.5× face
density increase. That was wrong. The two files in this workspace
(`res/scene/sponza.obj` and `res/scene/Sponza-master/sponza.obj`) are
**byte-identical** (both 23,855,238 bytes, both 145,185 verts /
262,267 faces, both dated 2024-04-11). At some prior point in this
workspace the original simpler Sponza file was replaced with the
Sponza-master geometry. So the Step 6 comparison is really
**same-mesh-loaded-twice**: once without finding adjacent .mtl (the
`res/scene/sponza.obj` path has no neighbouring `sponza.mtl`), once
with the Sponza-master directory's .mtl resolved. Identical seed counts
are therefore expected (it's the same triangles). The voxel-saturation
hypothesis has no support from the Step 6 data — it would need an
asset that's actually distinct.

**`cornell_box.obj` "no .mtl" note.** That file declares
`mtllib cornell-box.mtl` in its header, but `res/scene/cornell-box.mtl`
doesn't exist. The loader correctly reports
`Material library not found: ... (will use default gray)` and falls
through to the legacy `getMaterialColor()` table for known names like
`BloodyRed`/`Light`/`Khaki`.

EDT/albedo bake time is similar across all four — once the seed grid is
built, the Felzenszwalb separable EDT is O(N³) regardless of how the
seeds were generated.

### Logs preserved

- `tools/app_run_step6_cornell_orig.log`
- `tools/app_run_step6_sponza_master.log`
- `tools/app_run_step6_sponza_master_m5.log`
- `tools/app_run_step6_sponza_old.log`
- `tools/app_run_step6_cornell_old.log`
- `tools/app_run_step6_codex12_F1_cornell_orig.log` (reset-helper, codex 12 F1 verification)
- `tools/app_run_step6_codex12_F1_sponza_master.log` (reset-helper, codex 12 F1 verification)
- `tools/app_run_step6_codex12_F6_log.log` (legacy/default log distinction, codex 12 F6 verification)

### Known log noise (codex 12 F4)

Every Step 6 runtime log includes the pre-existing
`res/shaders/sdf_3d.comp` compile failure:

```
[gl_helpers] ERROR: shader compile failed: ... imageLoad ... overload mismatch
```

This shader is **not used** by the active CPU-EDT path (Step 2
replaced it). The cerebrum has it logged as
"Pre-existing broken: `res/shaders/sdf_3d.comp` ... unused, replaced
by CPU EDT path" since Step 4. Step 6 inherits the same noise
unchanged. The runtime continues normally and all subsequent screenshots
are captured. Do not treat the line as a Step 6 regression.

---

## Architecture Notes

**`materials` is per-OBJLoader-instance, not static.** Each `load()`
call clears it. If a future scene loads two OBJs with overlapping
material names, they don't collide.

**Default-gray fallback is preserved at every level.** `loadMTL`
failure → empty map → legacy `getMaterialColor()` → default gray. No
crash path in any of the three layers. This is what kept the old
`cornell_box.obj` and `sponza.obj` paths byte-compatible.

**The fan triangulator assumes convex polygons.** All OBJs we ship
satisfy this — Cornell-Original is axis-aligned quads, Sponza-master
mixes tris and convex quads. If a future asset has concave polygons
the fan will produce overlapping/missing triangles. Not worth fixing
preemptively; document the assumption and add ear-clipping if/when
needed.

**Bounds-checked face indices (codex 12 F5).** Resolved face indices
are now validated against `vertices.size()` before pushing the triangle.
Out-of-range indices skip the triangle and log up to 8 warnings per
load (then suppress further) plus a final dropped-count. `voxelize()`
can no longer hit `vertices[<negative>]` or `vertices[>=size()]`
through this path, even on malformed OBJs. Both new assets clean — 0
dropped triangles.

**Ke is not a light source.** It's purely cosmetic — the surface
*looks* bright. The cascade probes still gather radiance from the
single `lightPosition` uniform. A future step could harvest Ke voxels
to seed `inject_radiance.comp` with multi-bounce, but that's a
substantially bigger change.

**Sponza-master visual parity.** Same camera, same volume bounds, same
seed count → same render to within temporal-jitter-EMA noise. The
denser mesh helps SDF accuracy at sub-voxel scale (better surface
sampling) but the 128³ grid is the bottleneck, not face count.

---

## Known Open Items (Step 6 boundary → later)

| Item | Where to land it |
|---|---|
| Sponza .mtl Kd is uniform 0.4704 → no color variation | requires .tga texture loader (Step 7?) |
| Cornell `light` Ke could drive `lightPosition` automatically | future "auto-light from emissive" step |
| Concave n-gons would break fan triangulation | ear-clipping in `obj_loader.h` if asset hits this |
| Old `cornell_box.obj` `DarkGreen` material → default gray | already broken pre-Step-6; fixable by adding to legacy table or shipping a .mtl. Codex 12 F6 made the log distinguish "legacy fallback" from "true default gray" so this is now visible in the log. |
| Old `res/scene/sponza.obj` is byte-identical to Sponza-master (codex 12 F2) | If a smaller/simpler Sponza is wanted for A/B comparison, restore the original ~75K-face variant from git history or re-export. Pre-existing condition; outside Step 6 scope. |
| 128³ voxel resolution limits sub-voxel detail in Sponza-master | future higher-res cascade step |

---

## Why Step 6 Grew (and Why That's Fine)

Plan estimated three small changes (.mtl parser, 4 path sites, button
add). Reality required a fourth: the OBJ parser's quad/negative-index
gap. That's not scope creep — the new asset *needs* it; without the
fix, Cornell-Original is unrenderable.

The fix itself is contained (one block in `OBJLoader::load`), well-tested
(both new OBJs hit it; both old OBJs are unaffected), and now lives in
`.wolf/cerebrum.md` Do-Not-Repeat so future OBJ work won't rediscover it.

The user-visible win — Cornell-Original's distinct colored walls and
glowing light — is exactly what the plan promised. Sponza-master's
uniform-gray result was also predicted (its .mtl has no color
information without textures) and accepted up-front in the plan's
context section.
