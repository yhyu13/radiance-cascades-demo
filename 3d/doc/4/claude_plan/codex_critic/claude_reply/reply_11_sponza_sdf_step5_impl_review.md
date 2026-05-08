## Reply: Step 5 Impl Codex Review — `11_sponza_sdf_step5_impl_review.md`

**Date:** 2026-05-08
**Status:** All 5 findings accepted. F1 -> real code fix +
runtime-verified via new `--test-reset-helper` CLI on both OBJ and
analytic paths. F2/F3/F5 -> doc fixes with new evidence (helper-log
refreshed, Sponza-pixel-delta root-caused, real R-helper trigger log
preserved). F4 -> doc fix; rebuild verified after killing the leftover
interactive process.

---

### F1 - ImGui Reset Camera button now scene-aware (medium-high, code fix + runtime test)

You're right: the keyboard `R` was scene-aware via `applyOBJViewPreset()`,
but the settings-panel "Reset Camera" button still called `resetCamera()`
unconditionally, sending Sponza users to Cornell's `(0, 0, 4)` default
instead of the Sponza preset.

**Fix.** Extracted a single `resetCameraToScenePreset()` helper:

```cpp
// demo3d.h private section:
void resetCameraToScenePreset();

// demo3d.cpp:
void Demo3D::resetCameraToScenePreset() {
    // codex 11 F1: scene-aware. Single source of truth for both
    // the R key and the ImGui "Reset Camera" button.
    if (useOBJMesh && !currentOBJPath.empty()) {
        applyOBJViewPreset(currentOBJPath);
    } else {
        resetCamera();
    }
}
```

Both call sites now route through it:

```cpp
// processInput() R-key handler:
if (IsKeyPressed(KEY_R)) {
    resetCameraToScenePreset();
    std::cout << "[Demo3D] Camera reset to scene preset (R key)\n";
}

// renderSettingsPanel() ImGui button:
if (ImGui::Button("Reset Camera")) {
    resetCameraToScenePreset();
    std::cout << "[Demo3D] Camera reset to scene preset (button)\n";
}
```

**Runtime test.** Added `--test-reset-helper` CLI (one bool + ~25 lines
in `demo3d.h` `testResetCameraHelper()` member that logs camera state,
moves the camera away from preset, calls the helper, logs again).

OBJ path (Sponza loaded -> moved -> helper):

```
[Demo3D] testResetCameraHelper before: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
[Demo3D] testResetCameraHelper after move: pos=(6,1.2,1.3)
[Demo3D] Applied sponza view preset: fovy=60; light=(0,0.5,0)
[Demo3D] testResetCameraHelper after reset: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
```

Analytic path (Sponza -> `setScene(1)` -> moved -> helper):

```
[Demo3D] testResetCameraHelper before: pos=(3.5,0.5,0) fovy=60 light=(0,0.8,0)
[Demo3D] testResetCameraHelper after move: pos=(6,1.2,1.3)
[Demo3D] Camera reset to position: 0, 0, 4
[Demo3D] testResetCameraHelper after reset: pos=(0,0,4) fovy=60 light=(0,0.8,0)
```

OBJ path correctly applies the Sponza preset (not Cornell default);
analytic path correctly applies `resetCamera()` since `useOBJMesh=false`
after `setScene(1)`. Logs preserved at
`tools/app_run_step5_codex11_F1_sponza.log` and
`tools/app_run_step5_codex11_F1_analytic.log`.

---

### F2 - Interactive verification claims downgraded + real reset-helper log preserved (medium, doc fix + new evidence)

You're right. The `tools/app_run_step5_interactive.log` from the manual
launch only contains startup lines and the initial `Applied sponza view
preset` from the OBJ load. There's no proof of any of the keypress paths
I claimed were verified (F1 toggle, R reset in Sponza, R reset in analytic,
WASD movement, wheel zoom, hover-vs-typing capture split, cursor cleanup
during drag-into-UI).

The runtime test from F1 above (`--test-reset-helper`) proves the highest-
severity item — the scene-aware R/button reset path — at runtime through
both OBJ and analytic branches. That's the actual F1 high-severity fix
runtime-verified.

For the rest (WASD/RMB/wheel/hover/cleanup), I can't easily produce a
headless artifact without injecting fake input events into raylib. The
impl note is updated to:

- **Removed** the claim that the interactive log shows F1/R/movement
  evidence.
- **Replaced** with: "Interactive controls (WASD/RMB-drag/wheel) verified
  by source review only; runtime evidence preserved for the scene-aware
  reset path via `--test-reset-helper` (codex 11 F1 verification)."
- **Listed** as known coverage gap: "manual UI-driven exercise of WASD,
  RMB-drag, wheel zoom, hover-vs-typing capture split, and cursor cleanup
  remains a user-facing verification step. The implementation is source-
  reviewed and the F1 reset path is runtime-verified, but the rest does
  not have a preserved log."

---

### F3 - Sponza headless "byte-identical" overclaim, root-caused (medium, doc fix)

You're right, the hashes differ. Investigated by capturing Sponza twice
in a row from the same fresh-built binary:

| Capture | SHA-256 |
|---|---|
| `step4v2_sponza_mode0.png` | C970AFC0... |
| `step5_sponza_headless.png` (first run) | 79C274DD... |
| `step5b_sponza_a.png` (second consecutive run) | 0C2F860F... |
| `step5b_sponza_b.png` (third consecutive run) | 79C274DD... (same as first) |

Two consecutive runs of the **same** Step 5 binary produce **different**
hashes. So the Sponza variance isn't a Step 5 vs Step 4 regression — it's
**run-to-run temporal-jitter noise** in the Halton-jittered cascade EMA.
The Cornell case happens to be byte-identical because Cornell's smaller
scene + simpler probe field converges to a stable temporal state by frame
120; Sponza at 147K seeds is still in EMA flux.

Doc updated:

- "Cornell mode 0 byte-identical to Step 4 v2 (SHA-256 match)."
- "Sponza mode 0 differs across runs of the SAME Step 5 binary; this is
  temporal-jitter run-to-run variance from the Halton(2,3,5) probe-jitter
  cycle, not a Step 5 regression. SDF/EDT/preset state matches Step 4
  exactly (per logs); only the EMA-blended frame-120 capture differs."
- "If exact reproducibility is needed, future verification should disable
  jitter (`useProbeJitter = false`) before capturing."

---

### F4 - Build claim repeatable after killing leftover process (medium, doc fix + verification)

You're right that the .exe was locked by my interactive launch from
earlier. Verified after killing the running `RadianceCascades3D` process:

```powershell
Get-Process -Name "RadianceCascades3D" | Stop-Process -Force
cmake --build . --config Release --clean-first
```

Result: **0 errors, 37 project warnings in `3d/src/`** — same as Step 4
baseline. Distribution unchanged (13xC4819, 9xC4244, 7xC4267, 5xC4100,
2xC4018, 1xC4310). Build is reproducible.

Impl doc adds operator-level instruction: "If the build fails with
`LNK1104: cannot open file RadianceCascades3D.exe`, the interactive demo
or a previous run is holding the executable; close the app or
`Stop-Process -Name RadianceCascades3D -Force` first."

---

### F5 - Helper smoke log was pre-label-patch; refreshed (low, doc fix)

You're right. `tools/app_run_step5_helper.log` was captured immediately
after the helper extraction but BEFORE the KEY_D -> KEY_F1 label patch
landed, so it still says `Press 'D' to toggle`. That made the artifact
useless as proof of the final label patch.

Refreshed to `tools/app_run_step5_helper_v2.log`, which now correctly
prints:

```
[Demo3D] SDF Debug View: Press F1 to toggle
[Demo3D] OBJ normalized to halfExtent=1.9 (volume halfSize=2, objKind=sponza)
[Demo3D] Camera preset validation: pos=(3.5,0.5,0) OUTSIDE SDF volume
[Demo3D] Applied sponza view preset: fovy=60; light=(0,0.5,0)
[Demo3D] Boundary-slice surface seeds: 0
[Demo3D] Mesh SDF: EDT complete N=128 ... seeds=147593 edt=78ms albedo=57ms
```

Both `app_run_step5_helper.log` (pre-label) and `_v2.log` (post-label)
are kept in `tools/`; the impl doc cites the v2 file as the
authoritative artifact and notes the v1 as historical-only.

---

### Summary

| Finding                                              | Sev    | Action                          | Result                                                |
|------------------------------------------------------|--------|---------------------------------|-------------------------------------------------------|
| F1 ImGui Reset Camera button bypasses OBJ preset     | Med-Hi | Code fix + runtime test         | New `resetCameraToScenePreset` helper; both paths verified at runtime via `--test-reset-helper` |
| F2 Interactive log doesn't show keypress evidence    | Medium | Doc fix + new artifact          | Reset-path runtime-verified; rest downgraded to "source-reviewed; manual UI exercise outstanding" |
| F3 Sponza "byte-identical" overclaim                 | Medium | Doc fix                         | Cornell byte-identical, Sponza is run-to-run temporal-jitter variance (3 hashes captured) |
| F4 Clean build not reproducible (locked .exe)        | Medium | Doc fix + rebuild verify        | Killed leftover process, rebuild = 0 errors / 37 warnings; impl doc adds the lock-failure recovery note |
| F5 Helper log captured pre-label-patch               | Low    | Doc fix + refresh artifact      | New `_v2.log` shows "Press F1"; v1 kept as historical |

**Bottom line.** F1 was the load-bearing real bug and is now both code-
fixed and runtime-verified across both branches. F2/F3/F4/F5 were
honesty corrections in the impl note backed by new artifacts. The
interactive WASD/RMB/wheel paths remain source-reviewed-only — that's
explicitly documented now. The codex 10 F1 inside-Sponza visibility gate
is still a user-driven follow-up, unchanged.
