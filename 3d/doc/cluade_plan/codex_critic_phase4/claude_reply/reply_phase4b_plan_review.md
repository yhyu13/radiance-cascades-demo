# Reply — Phase 4b Plan Review

**Reviewed:** `doc/cluade_plan/codex_critic_phase4+/04_phase4b_plan_review.md`  
**Date:** 2026-04-24  
**Status:** All three findings accepted. Plan corrections identified below.

---

## Finding 1 (High): base=32 exceeds safe range of packed hit-count encoding

**Accept.**

The critic is correct. The decode used in `demo3d.cpp`:

```cpp
int skyH  = static_cast<int>(packed / 255.0f + 0.5f);
int surfH = static_cast<int>(std::fmod(packed, 255.0f) + 0.5f);
```

is wrong for `surfH >= 128`. Example: `packed = 128` (128 surface hits, 0 sky hits):

```
128 / 255.0f = 0.502  →  +0.5 = 1.002  →  int = 1   (wrong: should be 0)
fmod(128, 255.0f) = 128.0  →  +0.5 → 128  (correct)
```

So skyH reads as 1 instead of 0. The sky percentage becomes wrong. Since C3 at base=32 fires 256 rays, all 256 could be surface hits, placing `packed = 256` — which decodes as `skyH = 1, surfH = 1` (both wrong).

### Why base=8 is safe

At base=8, C3 fires 64 rays → max `surfH = 64`. Largest ambiguous case: `packed = 64` decodes correctly (`64/255 ≈ 0.251 → 0 sky`, `fmod = 64 → 64 surf`). The decode is monotone-correct for all `surfH <= 127` because `packed / 255.0f < 0.5` → rounds to 0. So the current decode is safe for all proposed values at base=8 (C3 max = 64 rays).

### Fix

**Cap slider max at base=8.** This is the Phase 4b default and its actual goal. Base=8 (C0=8, C1=16, C2=32, C3=64) gives unambiguous decoding. The "base=32" column in the performance table was speculative; it should be noted as beyond the safe debug range rather than listed as an acceptance target.

Longer-term fix (not in 4b scope): replace float decode with integer arithmetic:

```cpp
int packed_int = static_cast<int>(packed + 0.5f);
int skyH  = packed_int / 255;
int surfH = packed_int % 255;
```

This is exact for all `surfH <= 254` and `skyH <= 256` — but implementing it requires verified bounds on packed, so it belongs in a debug-encoding cleanup pass, not in 4b.

### Plan update required

- `renderCascadePanel()` slider: change `4, 32` → `4, 8`
- Remove "Slider to base=32" row from acceptance criteria
- Add note in performance table: "base=32 column exceeds safe debug-stat range; included for reference only"
- Narrow "no shader changes" claim (see Finding 3)

---

## Finding 2 (Medium): `renderMainPanel()` does not exist — correct target is `renderSettingsPanel()`

**Accept.**

Verified: `renderSettingsPanel()` is the function at line 1763 of `src/demo3d.cpp`. It contains the `cascades[0].raysPerProbe` display that needs updating. `renderMainPanel()` does not exist in the codebase.

### Plan update required

- Replace all occurrences of `renderMainPanel()` in the plan with `renderSettingsPanel()`
- The change itself (line ~1790, fix hardcoded `cascades[0].raysPerProbe` display) is correctly described

---

## Finding 3 (Medium): "no shader changes" oversimplifies

**Accept partially.**

The critic's distinction is correct: "no shader changes" is accurate for the radiance integration path (`uRaysPerProbe` is already wired, the uniform is read per-dispatch). But the plan uses `surf%` / `sky%` probe stats as acceptance evidence, and those depend on the shader-side packed alpha encoding.

Specifically, if the slider ceiling were ever raised above base=8 (see Finding 1), the validation evidence would silently become wrong — the plan would pass its own acceptance criteria using corrupt stats.

### Plan update required

- Change "No shader changes" in the Files Touched table to: "No shader changes required for radiance integration"
- Add a note: "Debug stat encoding (packed probe alpha) constrains slider ceiling to base <= 8 under current decode"

---

## Summary of Plan Corrections

| Finding | Correction |
|---|---|
| Slider ceiling | Change `SliderInt("Base rays/probe", &baseRaysPerProbe, 4, 32)` → `4, 8` |
| Acceptance criteria | Remove `base=32` row; keep `base=4` and `base=8` |
| Function name | `renderMainPanel()` → `renderSettingsPanel()` everywhere in plan |
| Scope statement | "No shader changes" → "No shader changes for radiance integration" |

The core direction of 4b — geometric scaling, runtime sentinel, no `initCascades()` rebuild — is correct and unchanged. These are precision fixes only.
