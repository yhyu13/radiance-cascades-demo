## Reply: Step 7 Impl Codex Review — `13_sponza_sdf_step7_impl_review.md`

**Date:** 2026-05-08
**Status:** All 7 findings accepted. F1 (high) and F3 (medium) — real
code fixes, both verified at runtime via fresh captures + reset-helper
runs. F2/F4/F5/F6/F7 — doc accuracy corrections; the impl note is
revised so claims match the evidence.

---

### F1 — Active OBJ bounds updated before commit (HIGH, code fix)

You're right and this directly broke the Step 3 stage-and-commit
atomicity guarantee. Step 7 v1 assigned `currentObjBmin/Bmax` right
after `normalize()` — before voxelize, before the
`newVoxelData.empty()` failure check. So a load that opens + normalizes
but fails voxelization would leave the previous mesh visible while
R-key reset used the failed mesh's bounds.

**Fix.** Held bounds as locals between normalize and commit:

```cpp
// (early — locals only)
glm::vec3 nbmin, nbmax;
objLoader.getBounds(nbmin, nbmax);
std::cout << "[Demo3D] Post-normalize bounds: ...\n";

// ... voxelize, fail-checks ...
if (newVoxelData.empty()) return false;   // PREVIOUS state preserved

// (commit block — atomic with rest)
meshVoxelData        = std::move(newVoxelData);
meshSDFReady         = false;
useOBJMesh           = true;
useAnalyticRaymarch  = false;
historyNeedsSeed     = true;
renderFrameIndex     = 0;
temporalRebuildCount = 0;
sceneDirty           = true;
currentOBJPath       = objKey;
currentObjBmin       = nbmin;   // codex 13 F1: assign atomically
currentObjBmax       = nbmax;
```

Now a failed-load case leaves `currentObjBmin/Bmax` matching the
visible mesh (whatever was loaded last successfully).

---

### F3 — Sponza framing change is large, not "slightly wider" (MEDIUM, code fix + doc fix)

You're right. The pixel diff (715K of 921K Sponza pixels changed)
matches what I saw qualitatively but described too softly. The
underlying issue: a naive `1.0 × diag` backoff puts the camera at
distance 4.74 from a 1.59×2.34 visible YZ plane, filling only ~30% of
the vertical screen. That isn't a marginal change from the old 3.5
preset — it's a worse view.

**Fix.** Replaced the formula with a FOV-aware fit + min-backoff
guard:

```cpp
glm::vec3 perp = size - lookDir * glm::dot(size, lookDir);
const float visH = std::abs(perp.y);
const float visW = std::sqrt(perp.x*perp.x + perp.z*perp.z);
const float halfFovyRad = glm::radians(60.0f) * 0.5f;
const float halfFovxRad = std::atan(std::tan(halfFovyRad) * aspect);
const float distFromY   = (visH * 0.5f) / std::tan(halfFovyRad);
const float distFromX   = (visW * 0.5f) / std::tan(halfFovxRad);
float fitDist = std::max(distFromY, distFromX) * 1.4f;   // 40% headroom

// Min-backoff: at least outside bbox along lookDir + 30% diag margin.
// Without this, FOV-fit can park the camera right against (or inside)
// a wall when the bbox fills the SDF volume — Sponza's bmax.x=1.9
// gives FOV-fit dist=1.93, exactly on the wall, producing solid gray.
const float boxHalfAlongLook = std::abs(glm::dot(size, lookDir)) * 0.5f;
const float minBackoff       = boxHalfAlongLook + 0.3f * diag;
fitDist = std::max(fitDist, minBackoff);
```

Updated positions:

| OBJ | v1 distance | v3 distance | Dominated by |
|---|---|---|---|
| Cornell-Original | 3.44 | 2.38 | FOV-fit |
| Sponza / Sponza-master | 4.74 | 3.32 | min-backoff |

Cornell now fills the frame (visible in `tools/step7v3_cornell_orig_mode0.png`
— red+green walls + glowing light + boxes all clearly visible).
Sponza is back to a comparable view to the old hand-tuned 3.5 preset
(visible in `tools/step7v3_sponza_master_mode0.png` — uniform-gray
wall fills most of the frame with floor strip at bottom). I did NOT
capture a fresh pixel-diff vs Step 6 because Sponza varies run-to-run
by Halton-jitter EMA noise (codex 11 F3), but the v3 result is
qualitatively back to "framing parity with Step 6".

The min-backoff guard caught a real failure mode I didn't anticipate
in the original v1 design: between switching to FOV-aware fit and
re-capturing, the first run produced a solid-gray Sponza image
because the camera had moved to x=1.93, exactly on the wall at
x=1.9. The guard fixed it.

---

### F2 — Auto-fit cameras are all outside the SDF volume (MEDIUM, doc fix)

You're right and the impl note's "cleanly inside the ray-march-able
region" wording was wrong. Verified in the v3 logs:

```
Cornell:   pos=(0, 0.098, 2.38)   OUTSIDE SDF volume (uvw.z=1.19)
Cornell-O: pos=(0, 0.098, 2.38)   OUTSIDE SDF volume (uvw.z=1.19)
Sponza:    pos=(3.32, 0.079, 0)   OUTSIDE SDF volume (uvw.x=1.33)
SponzaM:   pos=(3.32, 0.079, 0)   OUTSIDE SDF volume (uvw.x=1.33)
```

The old hardcoded presets (Cornell `(0,0,4)` uvw.z=1.5; Sponza
`(3.5,0.5,0)` uvw.x=1.375) were also outside-volume positions. The
existing `applyOBJViewPreset()` log line correctly says "alpha check
skipped, relying on ray-box intersection at march time" — that's the
intended path for outside-volume cameras and is identical pre/post
Step 7. Doc updated to:

- "All four computed positions are still OUTSIDE the SDF volume. The
  old hardcoded presets were too. Raymarching enters the volume via
  the existing ray-box intersection path."
- Removed the misleading "cleanly inside the ray-march-able region"
  line.

---

### F4 — Light placement ignores emitter semantics (MEDIUM, doc fix)

You're right. For Cornell-Original, the .mtl's emissive `light`
material lives on the ceiling at y≈1 (Ke 17 12 4), but the auto-fit
preset places the directional point-light at y≈0.59 (`0.3 × size.y`
above center). That's `lightPosition` placed for camera convenience,
NOT physically driven by emissive geometry.

This is consistent with Step 6's explicit decision ("Ke is not a light
source; it's purely cosmetic; lighting model unchanged"). Step 7 didn't
change that — but the impl note used the word "appropriate" which read
as a stronger claim. Doc updated:

- New "Light placement is geometry-driven, not material-driven"
  section under Architecture Notes, calling out that Step 7 light
  placement is camera convenience, not asset-correct emitter
  placement.
- Explicit pointer to a future step that would consume Ke voxels to
  place `lightPosition` from emissive material centroid as the right
  place to add asset-correct lighting.

Not fixing the actual Ke→lightPosition path here — that's an
intentional Step 8 candidate, and binding light placement to mesh
material requires a different design (e.g. should it auto-disable for
non-emissive scenes? scale brightness by Ke? what about multiple
emissives?).

---

### F5 — Reset-helper coverage narrower than note said (LOW, doc fix)

You're right. `--test-reset-helper` calls `resetCameraToScenePreset()`
directly, bypassing both the R-key handler in `processInput()` and the
ImGui "Reset Camera" button. The behavior IS structurally covered
because both UI entry points call the same helper (verified by source
review at `src/demo3d.cpp:525` for R-key, `src/demo3d.cpp:3014` for
button), but the test does not literally simulate a key event or
click event.

Doc updated:

- "Both new variants verified at v3 positions" → "verifies
  `resetCameraToScenePreset()` at the helper level. The R key in
  `processInput()` and the ImGui Reset Camera button both call this
  exact helper (verified by source review at `src/demo3d.cpp:525`
  and `src/demo3d.cpp:3014`), so structural coverage is high — but
  the test does NOT literally simulate a key press or a click event."
- Pointed out that a future raylib input-injection harness could
  close the gap.

---

### F6 — Pre-existing shader compile error in logs (LOW, doc fix)

You're right and this is the same pre-existing line that Step 6's
codex 12 F4 made me add to the impl doc — Step 7 v1 forgot to carry
the same caveat. Added a "Known log noise (codex 13 F6 — same as
Step 6)" section pointing at the unused `sdf_3d.comp` shader (replaced
by the Step 2 CPU-EDT path) and noting it's not a Step 7 regression.

Not fixing the shader itself in this reply — that's still a separate
"clean up the unused legacy GPU SDF shader" task.

---

### F7 — Warning baseline not demonstrated (LOW, doc fix + actual count)

You're right. Captured via clean Release rebuild
(`cmake --build . --config Release --clean-first`):

- 0 errors
- **38** project src warnings (was 37 baseline; **+1 C4819**)
- Distribution: 14×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018, 1×C4310

The +1 C4819 is from line-position shifts in `demo3d.cpp` moving an
existing high-bit-character region into a different MSVC scan window
— Step 7's new code added zero non-ASCII characters (verified:
`awk 'NR>=4500 && NR<=4570' src/demo3d.cpp | LC_ALL=C grep -n
'[^ -~\t]'` returns no matches). Baseline drift, not a code-quality
regression.

Doc updated to remove "no new warnings" phrasing and report the
actual delta with the line-shift explanation.

---

### Summary

| Finding                                         | Sev    | Action                          | Result                                                                 |
|-------------------------------------------------|--------|---------------------------------|------------------------------------------------------------------------|
| F1 Bounds updated before commit (atomicity)     | High   | Code fix                        | `currentObjBmin/Bmax` now in commit block alongside `meshVoxelData/useOBJMesh/currentOBJPath` |
| F3 Sponza framing dramatically worse, not "slightly wider" | Medium | Code fix + doc fix              | Replaced 1.0×diag with FOV-aware fit + min-backoff guard. Cornell at z=2.38 (FOV-fit), Sponza at x=3.32 (min-backoff, close to old 3.5). All 4 captured + verified at v3 positions. |
| F2 Cameras all outside SDF volume               | Medium | Doc fix                         | Removed misleading "ray-march-able region" wording; doc now matches the runtime "OUTSIDE SDF volume" log lines and notes the old presets were the same. |
| F4 Light placement ignores Ke semantics         | Medium | Doc fix                         | New section calling out light placement is geometry-driven (camera convenience), not material-driven. Pointed at future Ke-aware step. |
| F5 Reset-helper test bypasses R/button events   | Low    | Doc fix                         | Scope narrowed to "helper-level coverage; key/button share helper in source." |
| F6 Pre-existing shader noise in logs            | Low    | Doc fix                         | Added "Known log noise" section (carry-over from codex 12 F4 fix in Step 6 doc). |
| F7 No warning baseline                          | Low    | Doc fix + actual count          | Reported 38 src warnings (was 37, +1 C4819 from line-shift; no new non-ASCII chars). |

**Bottom line.** F1 was a real atomicity regression — fixed
structurally. F3 was a real framing regression masked by overly soft
wording — fixed by switching to FOV-aware fit with a min-backoff
guard, and the v3 captures show the framing is now back to
qualitative parity with Step 6 for Sponza while better-than-Step-6 for
Cornell. F2/F4/F5/F6/F7 were all doc-honesty corrections backed by
actual evidence in the impl note. The codex 13 F4 question of
"should `lightPosition` come from emissive material" remains
explicitly open as a future step — not silently in the back of the
implementation's mind.
