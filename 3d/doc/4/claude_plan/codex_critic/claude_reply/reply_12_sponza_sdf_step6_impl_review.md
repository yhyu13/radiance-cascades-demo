## Reply: Step 6 Impl Codex Review — `12_sponza_sdf_step6_impl_review.md`

**Date:** 2026-05-08
**Status:** All 7 findings accepted. F1 (high) — real code fix +
runtime-verified via `--test-reset-helper` on both new variants. F2/F3
(high/medium) — doc corrections; the asset-substitution root cause is
now properly explained. F4 (medium) — known-noise note added to the doc.
F5 (medium) — bounds-checked fan triangulator. F6 (low) — log text now
distinguishes legacy fallback from true default-gray. F7 (low) — CLI
comment updated.

---

### F1 — Camera reset regression for new variants (HIGH, code fix + runtime test)

You're right and this is the most load-bearing finding. `loadOBJMesh()`
sets `currentOBJPath` to one of the 4-way keys
(`cornell` | `cornell_orig` | `sponza` | `sponza_master`), but
`applyOBJViewPreset()` only branches on the 2-way kind
(`cornell` | `sponza`). `resetCameraToScenePreset()` was passing the
4-way value straight through, so for the new variants it hit the
"unknown objKind" warning and silently no-op'd. This directly undoes
the codex 11 F1 work that made R/Reset Camera scene-aware.

**Fix.** One-shot translation in `resetCameraToScenePreset()`:

```cpp
void Demo3D::resetCameraToScenePreset() {
    if (useOBJMesh && !currentOBJPath.empty()) {
        // codex 12 F1: currentOBJPath is now 4-way; applyOBJViewPreset
        // only accepts the 2-way kind. Translate so Cornell-Original /
        // Sponza-master hit the right preset instead of falling through
        // to the unknown-key warning path.
        const bool isSponza = (currentOBJPath == "sponza")
                           || (currentOBJPath == "sponza_master");
        applyOBJViewPreset(isSponza ? "sponza" : "cornell");
    } else {
        resetCamera();
    }
}
```

Chose the local-translation form over modifying `applyOBJViewPreset()`
to accept the 4-way key, because the helper is a per-OBJ preset and
the variants share presets — the 4-way key only exists for the ImGui
ACTIVE label; preset choice is genuinely 2-way.

**Runtime verification.** Reused the codex 11 `--test-reset-helper`
infrastructure on each new variant. Both correctly route through the
right preset now.

OBJ path A (`--load-obj=cornell-orig --test-reset-helper`,
`tools/app_run_step6_codex12_F1_cornell_orig.log`):

```
[Demo3D] testResetCameraHelper before: pos=(0,0,4) fovy=60 light=(0,0.8,0)
[Demo3D] testResetCameraHelper after move: pos=(2.5,0.7,5.3)
[Demo3D] Applied cornell view preset: fovy=60; light=(0,0.8,0)
[Demo3D] testResetCameraHelper after reset: pos=(0,0,4) fovy=60 light=(0,0.8,0)
```

OBJ path B (`--load-obj=sponza-master --test-reset-helper`,
`tools/app_run_step6_codex12_F1_sponza_master.log`):

```
[Demo3D] testResetCameraHelper before: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
[Demo3D] testResetCameraHelper after move: pos=(6,1.2,1.3)
[Demo3D] Applied sponza view preset: fovy=60; light=(0,0.5,0)
[Demo3D] testResetCameraHelper after reset: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
```

No more "unknown objKind" warnings. Both new variants reset to the
correct preset.

---

### F2 — Sponza-master density narrative was wrong (HIGH, doc fix)

You're right and I should have checked the file rather than assuming.
Verified directly:

```
res/scene/sponza.obj                23,855,238 bytes  2024-04-11
res/scene/Sponza-master/sponza.obj  23,855,238 bytes  2024-04-11
v=145185  f=262267   for both
```

The two files are **byte-identical**. At some prior point in this
workspace the original simpler `sponza.obj` was replaced with the
Sponza-master geometry — likely when the user added the new
asset folders. So my "old=75K, new=262K" comparison and the
"3.5× face-count increase" interpretation of the equal seed counts had
no basis. It's the same triangles loaded twice through different .mtl
paths.

The impl doc's bake-stats table now shows `sponza` with the real
262,267 face count and the changelog calls out the asset substitution
explicitly. The "voxel saturation" hypothesis is removed — equal seed
counts are tautologically expected when the input is the same mesh.

---

### F3 — Old-Sponza visual regression claim was too strong (MEDIUM, doc fix)

You're right. Pixel diff on `step4v2_sponza_mode0.png` vs
`step6_sponza_old_mode0.png` shows ~450K of 921K pixels changed — that's
not regression-grade. Two contributing causes:

1. **Halton temporal-jitter EMA run-to-run variance** (the same effect
   we documented in Step 5 codex 11 F3 — Sponza captures of consecutive
   runs of the same binary differ by a similar magnitude).
2. **Asset substitution** (F2 above) — the Step 4 v2 baseline was
   captured against whatever sponza.obj existed *then*, which may not
   be byte-identical to today's file. Without the original asset in
   git history, we can't reproduce that exact baseline.

Doc updated to:

- "Visually similar to `step4v2_sponza_mode0.png` but **not pixel-identical**
  (~450K of 921K pixels changed; codex 12 F3). Run-to-run temporal-jitter
  EMA accounts for some of this; the rest is the F2 asset substitution"
- "Treat this as a qualitative not regression-grade comparison."

The Cornell pixel-identical comparison (`step5_cornell_headless.png`
vs `step6_cornell_old_mode0.png` per your evidence) is still
the strong-evidence regression check. That remains the load-bearing
proof; old-Sponza is qualitative only.

---

### F4 — Pre-existing shader-compile noise in logs (MEDIUM, doc fix)

You're right. Every Step 6 runtime log contains the
`res/shaders/sdf_3d.comp` `imageLoad(...)` overload-mismatch error and
the impl note didn't acknowledge it. The cerebrum has had it logged
since Step 4 ("Pre-existing broken: ... unused, replaced by CPU EDT
path"), but the Step 6 doc skipped the caveat.

Added a "Known log noise (codex 12 F4)" section to the impl doc that
calls out the line, explains it's the unused legacy GPU SDF shader the
CPU-EDT path replaced in Step 2, and notes that runtime continues
normally and screenshots are still captured. Future verification
shouldn't read it as a Step 6 regression.

(Not fixing the shader itself in this reply — that's a separate
"clean up unused shader" task with its own scope. The impl doc just
needs to stop implying the logs are clean.)

---

### F5 — Resolved face indices not validated (MEDIUM, code fix)

You're right that the new fan triangulator resolved negative indices but
didn't check for out-of-range results before pushing the triangle. A
malformed OBJ could still drive `voxelize()` into UB through
`vertices[face.v[i]]` later.

**Fix.** In `obj_loader.h` f-line block, added bounds check + bounded
warning counter:

```cpp
const int vcount = static_cast<int>(vertices.size());
for (size_t i = 1; i + 1 < fv.size(); ++i) {
    int a = fv[0], b = fv[i], c = fv[i+1];
    if (a < 0 || a >= vcount || b < 0 || b >= vcount || c < 0 || c >= vcount) {
        if (badIndexWarnings < 8) {
            std::cerr << "[OBJLoader] WARN: face vertex index out of range "
                      << "(a=" << a << " b=" << b << " c=" << c
                      << " vcount=" << vcount << "), dropping triangle\n";
            ++badIndexWarnings;
            if (badIndexWarnings == 8)
                std::cerr << "[OBJLoader] (further out-of-range warnings suppressed)\n";
        }
        ++badIndexDropped;
        continue;
    }
    // ... push triangle ...
}
```

Two members on the loader (`badIndexWarnings`, `badIndexDropped`) are
reset per-load. Cap at 8 spam-prevention warnings, then a single
suppression notice. Both shipped assets pass clean (0 dropped triangles
for Cornell-Original and Sponza-master). The path is verified to NOT
fire on healthy data — the count stays at 0 in normal logs.

---

### F6 — Unknown-material log misleading for legacy fallback (LOW, code fix)

You're right. The first impl logged "Unknown material 'Light' -> default
color 1,1,0.9" — but `1,1,0.9` is the legacy `getMaterialColor("Light")`
table hit, NOT a default-gray miss. The text incorrectly painted a
working legacy path as a failure.

**Fix.** Three-way log distinction:

```cpp
const glm::vec3 defaultGray(0.8f, 0.8f, 0.8f);
const bool isLegacyHit = (kd != defaultGray);
if (!matName.empty() && unknownMaterialsLogged.insert(matName).second) {
    if (isLegacyHit) {
        std::cout << "[OBJLoader] Material '" << matName
                  << "' -> legacy fallback color "
                  << kd.x << "," << kd.y << "," << kd.z
                  << " (no .mtl entry)" << std::endl;
    } else {
        std::cout << "[OBJLoader] Material '" << matName
                  << "' -> default gray (no .mtl entry, no legacy match)"
                  << std::endl;
    }
}
```

Verified on old Cornell (`tools/app_run_step6_codex12_F6_log.log`):

```
[OBJLoader] Material 'Light' -> legacy fallback color 1,1,0.9 (no .mtl entry)
[OBJLoader] Material 'Khaki' -> legacy fallback color 0.75,0.75,0.75 (no .mtl entry)
[OBJLoader] Material 'BloodyRed' -> legacy fallback color 0.65,0.05,0.05 (no .mtl entry)
[OBJLoader] Material 'DarkGreen' -> default gray (no .mtl entry, no legacy match)
```

Now the three legacy hits read as success, and `DarkGreen` (the only
real fallback miss) reads as the actual gap it represents. This also
incidentally surfaced that `cornell_box.obj` does declare
`mtllib cornell-box.mtl`, but that .mtl file isn't present in
`res/scene/`. The loader correctly logs "Material library not found …
will use default gray" as a separate line. So the failure mode is
already nicely audit-trailed.

---

### F7 — Stale CLI comment (LOW, code fix)

You're right. `src/main3d.cpp` argv-parsing comment block still said
`(cornell|sponza)`. Updated:

```cpp
// --load-obj=NAME: load OBJ mesh once at startup (Step 2/3 testing).
//   Step 6: NAME accepts cornell | cornell-orig | sponza | sponza-master.
```

Tiny, but as you noted prevents future scripts/docs from missing the
new entry points.

---

### Summary

| Finding                                              | Sev    | Action                          | Result                                                |
|------------------------------------------------------|--------|---------------------------------|-------------------------------------------------------|
| F1 New variants break scene-aware reset              | High   | Code fix + runtime test         | 4-way → 2-way translation in `resetCameraToScenePreset`; both new variants verified via `--test-reset-helper` |
| F2 Sponza-master density story unsupported           | High   | Doc fix                         | Files are byte-identical; "75K vs 262K" was wrong; bake-stats table corrected; voxel-saturation hypothesis removed |
| F3 Old-Sponza visual-regression claim too strong     | Medium | Doc fix                         | Downgraded to "qualitative similar, not pixel-identical"; cited pixel diff; rooted in jitter EMA + F2 asset substitution |
| F4 Pre-existing shader noise in logs                 | Medium | Doc fix                         | New "Known log noise" section names the `sdf_3d.comp` line, explains it's pre-existing/unused |
| F5 No resolved-index bounds check                    | Medium | Code fix                        | Bounds-checked fan triangulator with bounded warning + dropped-count; healthy assets stay at 0 dropped |
| F6 Misleading "unknown material" log                 | Low    | Code fix                        | Three-way log distinguishes parsed-MTL / legacy fallback / true default; verified on old Cornell |
| F7 Stale CLI comment                                 | Low    | Code fix                        | `main3d.cpp` comment now lists all four `--load-obj=` names |

**Bottom line.** F1 was the load-bearing real bug; it's both code-fixed
and runtime-verified across both new variants, restoring the codex 11
guarantee. F2 was a factual error in my doc rooted in not checking the
asset before writing the "denser mesh" narrative. F3/F4 were honesty
corrections. F5/F6/F7 are small but real defensive/quality fixes that
make future Step 6+ debugging less misleading. No new claims left
unsupported.
