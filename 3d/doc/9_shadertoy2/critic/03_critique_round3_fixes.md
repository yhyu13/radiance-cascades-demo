# Critique 03 — Round-3 Response Verification

**Sources reviewed:**
- [doc/9_shadertoy2/critic/reply/02_reply_to_critique_phase2a_ring_packed_index_debug.md](reply/02_reply_to_critique_phase2a_ring_packed_index_debug.md)
- [res/shaders/surface_ring_debug.comp](../../../res/shaders/surface_ring_debug.comp) (updated)
- [src/surface_rc.cpp:230-264](../../../src/surface_rc.cpp#L230) (updated)
- [doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md](../phase2_impl_ring_packed_index_debug.md) (updated)
- [tools/phase2a_visual/ring_m{0..4}.png](../../../tools/phase2a_visual/) (new evidence)

**Date:** 2026-05-29
**Scope:** Verify Critique-02 fixes landed correctly and identify any residual issues.

---

## 0. Verdict by ID

| ID | Verdict | Notes |
|---|---|---|
| H1 | ✅ **Fixed** | Wall TBN matches ShaderToy verbatim; mode-4 world-position mapping correctly updated |
| M1 | ⚠️ **Partial** | Screenshots exist; the visual gates in plan §4 are still not actually verified — pixel-pattern metric is too weak |
| **M2** | ❌ **Not fixed** | New `ringStripe` fires for *every* texel (always-on) instead of never. The dead-code bug inverted, not resolved. **The critique 02 suggestion was wrong; the impl correctly inherited the error.** |
| M3 | ✅ **Correctly carried forward** | cornell_box.obj confirmed has no front_wall; chartActive[6] queued for Phase 2B |
| L1 | ✅ **Fixed** | Aspect-preserving overlay verified at both 1024×768 (→ 384×575) and 320×240 (→ 160×239) viewports |
| L3 | ✅ **Fixed** | `probeCoord / probePositions` gives `[0, 1)` at all cascades |
| L2 / L4 / P1-P3 | ✅ **Acknowledged in reply doc** | Reasonable handling for low-severity process items |

**Net:** 5 of 7 fixes landed correctly; 1 unverified; 1 incorrectly fixed. The incorrect fix is a critique-02 author error inherited verbatim by the impl — credit, not blame, to the impl side.

---

## 1. H1 — TBN restoration verified

ShaderToy [CubeA.glsl:82-106](../../../shader_toy/CubeA.glsl#L82) vs new [surface_ring_debug.comp:72-111](../../../res/shaders/surface_ring_debug.comp#L72):

| Wall | ShaderToy gTan / gBit / gNor | Impl tangent / bitangent / normal | Match |
|---|---|---|---|
| left  (id=3) | (0,1,0) / (0,0,1) / (1,0,0)  | (0,1,0) / (0,0,1) / (1,0,0)  | ✓ |
| right (id=4) | (0,1,0) / (0,0,1) / (-1,0,0) | (0,1,0) / (0,0,1) / (-1,0,0) | ✓ |
| back  (id=5) | (0,1,0) / (1,0,0) / (0,0,1)  | (0,1,0) / (1,0,0) / (0,0,1)  | ✓ |
| front (id=6) | (0,1,0) / (1,0,0) / (0,0,-1) | (0,1,0) / (1,0,0) / (0,0,-1) | ✓ |

Mode-4 world-position mapping updated to match:
- Walls now use `probeUVChart.x → Y axis` (tangent), `probeUVChart.y → Z or X` (bitangent).
- Specifically [surface_ring_debug.comp:167-170](../../../res/shaders/surface_ring_debug.comp#L167): id=3/4 use `mix(min.y, max.y, probeUVChart.x)` for Y, `mix(min.z, max.z, probeUVChart.y)` for Z. id=5/6 use `mix(min.x, max.x, probeUVChart.y)` for X (bitangent), `mix(min.y, max.y, probeUVChart.x)` for Y (tangent).

Consistent. Phase 2B can build `probeDir = dx*tangent + dy*bitangent + dz*normal` and get ShaderToy-equivalent ray directions per atlas texel.

**No Phase 1 chart shader consistency issue:** [surface_cornell_debug.comp](../../../res/shaders/surface_cornell_debug.comp) does not define tangent/bitangent variables (verified via grep — no matches for `tangent|bitangent|gTan|gBit`). Phase 1 was normal-only, so no inconsistency carries between Phase 1 and Phase 2A.

---

## 2. M2 — `ringStripe` "fix" is incorrect (my error)

### The bug, restated

`probeThetai = max(abs(probeRel.x), abs(probeRel.y))` with `probeRel = probeUV - probeSize*0.5` and `probeUV = dirCoord + 0.5`. Therefore `probeRel ∈ {±0.5, ±1.5, ±2.5, ...}`, and **`probeThetai ∈ {0.5, 1.5, 2.5, ...}` is always exactly half-integer**.

### Critique 02's suggested replacement (wrong)

```glsl
float ringStripe = fract(probeThetai + 0.5) < 0.15 ? 1.0 : 0.0;
```

Evaluate at every possible probeThetai:
- probeThetai = 0.5: `fract(0.5 + 0.5) = fract(1.0) = 0.0` → `0.0 < 0.15` is **true** → stripe = 1
- probeThetai = 1.5: `fract(1.5 + 0.5) = fract(2.0) = 0.0` → **true** → 1
- probeThetai = 2.5: `fract(2.5 + 0.5) = fract(3.0) = 0.0` → **true** → 1
- ... every half-integer hits an integer when shifted by 0.5

**The stripe fires for every texel.** The old condition fired for none; the new one fires for all. Neither marks ring boundaries.

Used here:
```glsl
rgb = mix(vec3(ringNorm, 1.0 - ringNorm, float(cascade) / 5.0), vec3(1.0), ringStripe * 0.35);
```
With `ringStripe ≡ 1`, every texel in mode 3 is mixed 35 % toward white — the entire image is washed brighter. The `ringNorm` gradient is still visible, but the explicit per-ring marker that the plan §4 mode-3 gate requires is absent in a *different* way than before.

### Why critique 02 missed this

Critique 02 §3 wrote "fires near integer boundaries" while suggesting the replacement, but probeThetai is never near an integer — it's always exactly half-integer, equidistant from two integers. The replacement was algebraically incorrect. The impl correctly transcribed the suggestion verbatim; the error is on the critique side, not the implementation side.

### Correct fix

To mark transitions between successive discrete rings (`floor(probeThetai)`), use the integer ring index directly:

```glsl
float ring = floor(probeThetai);              // 0, 1, 2, ... per probe
float stripe = step(0.5, mod(ring, 2.0));     // alternate rings shaded
```

Or, to highlight only the outermost ring of each probe tile:

```glsl
float stripe = (probeThetai > probeSize*0.5 - 0.6) ? 1.0 : 0.0;
```

Both will produce visible per-ring structure that survives the half-integer constraint. Pick one and re-run the mode-3 screenshot to confirm rings appear.

### Severity

Medium. Mode 3 is debug-only; the broken stripe doesn't affect correctness of any future radiance code. But the plan §4 gate "ring/theta index" remains unverifiable until the stripe actually marks boundaries, and the issue is easy to fix (one line).

---

## 3. M1 — Screenshots captured, gates still unverified

### What was produced

5 PNGs at `tools/phase2a_visual/ring_m{0..4}.png`, 1024×768. Pixel-pattern summary:
```
m0=98  m1=142  m2=234  m3=94  m4=289
```

This is one number per mode — the count of unique RGB colors after 4-bit quantization.

### Why this doesn't verify the gates

Plan §4 declares four visual contract gates. Mapping each to what the metric proves:

| Gate | What the unique-color count tells you |
|---|---|
| "six cascade bands visible" | Nothing — would also fire for 4 or 8 bands |
| "probe coordinate density halves per cascade" | Nothing — would fire for any density pattern |
| "direction/ring pattern grows with probeSize" | Nothing — would fire for uniform random colors |
| "final mode-0 volumetric path unchanged" | Nothing — out of scope for this metric |

The metric proves only that *some* per-mode variation exists, which is also true for accidentally-correct or accidentally-broken output. Mode-3's count (94) is even *lower* than mode-0's (98), which would be surprising if the ringStripe fix were producing new structure — but in fact ringStripe is washing the image toward uniform white-tint, which compresses rather than expands the color count. This is consistent with the M2 bug above.

### The screenshots do exist

That's progress relative to round-2 (smoke-test only). A human viewing the PNGs offline can verify the gates directly. The remaining gap is just the lack of automated structure checks — and the M2 bug means mode-3 will fail one of the visual checks when actually inspected.

### Suggested closure

Either:
1. **Human inspection** — open each PNG, confirm 6 horizontal bands (m0), halving density (m1), growing tiles (m2), ring boundaries (m3 *after* M2 fix), smooth gradient (m4). Record one sentence per mode in impl §6.
2. **Structure-aware metric** — e.g. for mode 0, count horizontal runs of yellow pixels (`(1,1,0)` ± ε) within the overlay region; assert == 6. Five such checks (one per mode) would be ~50 lines of PowerShell with `System.Drawing`, much stronger evidence than the unique-color count.

For Phase 2A specifically, option 1 is cheaper and probably sufficient.

---

## 4. M3 — Correctly forwarded

The reply's evidence is exactly what was needed: `cornell_box.obj` named objects listed, no `front_wall` present. Chart 6 is debug-reserved, not a real surface.

The recommended `chartActive[6]` mask is the right choice — preserves the ShaderToy-like 1024-wide layout while preventing Phase 2B's radiance/feedback from sampling a non-existent surface. Adding it as the first task of Phase 2B (before any tracing) is the right order.

No critique on this item.

---

## 5. L1 — Aspect-preserving overlay verified

[surface_rc.cpp:239-248](../../../src/surface_rc.cpp#L239):

```cpp
debugH = std::min(576, viewport[3]);
debugW = std::min(384, std::min(viewport[2], int(float(debugH) * aspect)));
debugH = std::max(1, int(float(debugW) / aspect));
```

At 1024×768 viewport: `debugH = 576`, `debugW = min(384, min(1024, 384)) = 384`, `debugH = int(384 / 0.667) = 575`. Final 384×575, aspect 0.668. ✓

At 320×240 viewport: `debugH = 240`, `debugW = min(384, min(320, 160)) = 160`, `debugH = int(160 / 0.667) = 239`. Final 160×239, aspect 0.669. ✓

Both match atlas aspect 1024/1536 = 0.667 to within rounding. Fix verified.

---

## 6. L3 — Probe-coordinate normalization verified

[surface_ring_debug.comp:152](../../../res/shaders/surface_ring_debug.comp#L152):

```glsl
rgb = vec3(probeCoord / max(probePositions, vec2(1.0)), float(cascade) / 5.0);
```

- C0, gRes=256: probePositions=128, probeCoord ∈ [0, 128), normalized ∈ [0, 1). ✓
- C5, gRes=256: probePositions=4, probeCoord ∈ [0, 4), normalized ∈ [0, 1). ✓
- C5, gRes=128: probePositions=2, probeCoord ∈ [0, 2), normalized ∈ [0, 1). ✓

No overshoot at any cascade/chart combination. Fix verified.

---

## 7. Process — Critique loop is healthy

The full chain now exists and is being honored:

```
01_critique → reply/01 → phase2_plan + phase2_impl
02_critique → reply/02 + code patches + visual sweep
03_critique → (this doc)
```

Each cycle has narrowed the scope of remaining issues. Round-3 fixes 5/7 items correctly, leaving:
- 1 partial (M1 — screenshots exist, gates need manual eyeball)
- 1 incorrectly fixed (M2 — needs the corrected fix from §2 above)
- 1 deliberately deferred (M3 — Phase 2B prerequisite)

This is the kind of asymptotic convergence the critique loop is supposed to produce. The error rate on each round is dropping. Recommend continuing the pattern through Phase 2B.

---

## 8. Pre-Phase-2B Updated Action List

Compared to critique 02 §10, the remaining work is small:

| Action | Where | Effort |
|---|---|---|
| Re-fix M2 with correct stripe condition | [surface_ring_debug.comp:157](../../../res/shaders/surface_ring_debug.comp#L157) | 1-line change + re-run mode-3 screenshot |
| Eyeball-verify the 5 PNGs at `tools/phase2a_visual/` | manual | ~2 min, write one sentence per mode |
| Add `chartActive[6]` plumbing | [surface_rc.h](../../../src/surface_rc.h), [surface_ring_debug.comp:102-112](../../../res/shaders/surface_ring_debug.comp#L102), Phase 2B radiance shader | First task of Phase 2B |
| Carry forward stop-loss policy into Phase 2B plan | doc/9_shadertoy2/phase2b_*_plan.md | Plan-writing task |

Nothing here is a major refactor or new design decision. Phase 2A is essentially done modulo M2 and human inspection of the PNGs.

---

## 9. Owning the M2 Error

Critique 02 §3 confidently asserted that `fract(probeThetai + 0.5) < 0.15` "fires near integer boundaries." That assertion was wrong — probeThetai is never near an integer, so the condition either always fires (current behavior) or never fires (the old behavior), depending on the offset. The impl correctly transcribed the suggestion as written.

This matches the [[feedback_analytical_doc_qa]] memory rule: *"For any claim of the form 'X is wider/tighter/larger than Y,' look up the actual numerical values."* Critique 02 reasoned from formula shape instead of plugging in the actual half-integer values, exactly the same failure mode the memory warns against. Logged here as a recurrence; the corrected fix in §2 above closes the loop.

---

## 10. Cross-References

- [doc/9_shadertoy2/critic/02_critique_phase2a_ring_packed_index_debug.md](02_critique_phase2a_ring_packed_index_debug.md)
- [doc/9_shadertoy2/critic/reply/02_reply_to_critique_phase2a_ring_packed_index_debug.md](reply/02_reply_to_critique_phase2a_ring_packed_index_debug.md)
- [doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md](../phase2_impl_ring_packed_index_debug.md)
- [shader_toy/CubeA.glsl:82-106](../../../shader_toy/CubeA.glsl#L82) — wall TBN reference (H1 verification)
- [feedback_analytical_doc_qa] memory — direction-sign / numerical-substitution rule that M2 re-violated
