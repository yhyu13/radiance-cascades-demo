# Reply to `01_audit_v3_status_and_gaps.md` + `02_correction_m1_flags_dead_code.md`

**Date:** 2026-05-28
**Reply to:** `doc/8_shadertoy/01_audit_v3_status_and_gaps.md`, `doc/8_shadertoy/02_correction_m1_flags_dead_code.md`
**Posture:** the audit is well-written and several findings are fair, but the auditor missed the entire Stage 1 + Stage 7-11d investigative chain (all dated 2026-05-27 / 2026-05-28). That chain already ran the A/B the audit calls "missing" and pivoted to a different root cause two stages ago. Detailed per-finding response below.

## Summary table

| Audit finding | Severity in audit | This reply | Why |
|---|---|---|---|
| F1: M1 #3/#6 flags exist but never A/B tested | HIGH (lowered to MEDIUM in `02_correction`) | **REJECTED — STALE** | The M1 #3/#6 2×2 matrix DID run at N=2048 on 2026-05-27. Verdict on disk: `delta3 = DEAD`, `delta6 = DEAD`, `both = DEAD`. See [v3_m1_stage1_delta36_matrix_impl.md](../7/v3_m1_stage1_delta36_matrix_impl.md). The flags went through the gated process; they failed the gate. |
| F2: M1 flag code pre-dates M0 Stage 1 baseline | HIGH | **REJECTED** | The flags were added BY Stage 1 (`v3_m1_stage0_delta36_impl.md` → `v3_m1_stage1_delta36_matrix_impl.md` chain). The matrix capture USED the M0 lock as its reference. The chronology is the opposite of what the audit claims. |
| F3: Delta #6 cone hardcoded for C0→C1 only | HIGH | **ACCEPTED (still true)** | Confirmed against current `src/demo3d.cpp` — `sinT = sin(0.75·π/2)` is applied to every dispatch. Per-cascade variation is unimplemented. However: given the `both=DEAD` verdict at this constant value, fixing the per-cascade variation is a low-priority follow-up — the structural #6 mechanism didn't work even at its best-case cascade. |
| F4: Sponza ratio 4.7× too bright is "FATAL FOR PATH A" | MEDIUM | **REJECTED — STALE** | The Sponza 4.7× was attributed to MB-feedback over-drive in Stage 8 and quantified in Stage 9. At `--multi-bounce-gain=0.10` Sponza `|p95|` drops from 4.53 to 0.253 — **clears the scope §7 retirement gate**. Stage 10 mode-0 RMS confirmed this is a real visual improvement (10× RMS reduction, 95% of cascade-vs-hybrid gap closed). The audit's framing assumes the only fix is the Delta #3/#6 merge knob; Stage 8+ identified a different, working lever. |
| F5: Sponza valid mask = 693 pixels (0.08%) | MEDIUM | **ACCEPTED (partial)** | The narrow valid mask is real. Stage 1 self-critique already flagged this (SC2). It is shared by every M1 stage that reused the M0 mask logic, so it's a uniform measurement floor, not a stage-specific defect. The Stage 8/9/10 findings reproduce on the same narrow mask, so the floor isn't bias-inducing for the relative-change measurements those stages report. |
| `02_correction` claim: M1 flags have no CLI | (correction body) | **REJECTED — FALSE** | CLI flags `--m1-delta3-gated-trilinear=0|1` and `--m1-delta6-geometric-cone=0|1` exist in `src/main3d.cpp:580-590`. They have been used by Stage 1 matrix captures, Stage 4-7 captures, Stage 8 `delta3_on` variant, and Stage 11d captures. The setters are reachable from CLI. Source-edit is NOT required. (UI checkboxes are not exposed, which is intentional — opt-in via CLI only.) |

## Per-finding response

### F1 — REJECTED, STALE (a.k.a. "the matrix already ran")

The audit's headline claim: *"M1 Delta #3 and Delta #6 are fully implemented as flag-gated toggles, but no A/B evaluation has been performed against the locked M0 baselines."*

Disk evidence to the contrary:

- [doc/7/v3_m1_stage1_delta36_matrix_plan.md](../7/v3_m1_stage1_delta36_matrix_plan.md) — the 2×2 matrix plan.
- [doc/7/v3_m1_stage1_delta36_matrix_impl.md](../7/v3_m1_stage1_delta36_matrix_impl.md) (dated 2026-05-27) — N=2048 captures + analyzer + verdict table:

  | Condition | Cornell | Sponza | Combined verdict |
  |---|---|---|---|
  | `delta3` | DEAD | MISSING (empty mask) | DEAD |
  | `delta6` | DEAD | MARGINAL | DEAD |
  | `both`   | DEAD | DEAD              | DEAD |

- Tooling: `tools/v3_m1_delta36/analyze_matrix.py`, `tools/v3_m1_delta36/capture_matrix.ps1`.
- Captures: `tools/v3_m1_delta36/captures_cornell_{baseline,delta3,delta6,both}/`, same for Sponza.

The gated process the audit calls "skipped" was actually run, produced DEAD on every condition, and the resulting lesson — "do not promote the current #3/#6 changes; audit transport contract mismatch instead" — drove the entire subsequent Stage 2-11d sequence. The audit appears to have not read the Stage 1 impl doc or the matrix tooling directory.

### F2 — REJECTED (chronology is opposite of the claim)

Audit claim: *"The M1 flag implementation pre-dates M0 Stage 1 baseline completion, meaning the flags were coded BEFORE there was a lock.json to measure against."*

Actual chronology:

| Date | Event | Doc |
|---|---|---|
| 2026-05-26 | M0 Stage 0 closeout shipped (P1-P8 patches) | `v3_m0_stage0_closeout_impl.md` |
| 2026-05-27 | M0 Stage 1 baselines complete (lock.json) | `v3_m0_stage1_impl.md`, `v3_m0_stage1_sponza_ladder_impl.md` |
| 2026-05-27 | **M1 Stage 0**: added `--m1-delta3-gated-trilinear=` and `--m1-delta6-geometric-cone=` CLI flags | `v3_m1_stage0_delta36_impl.md` |
| 2026-05-27 | **M1 Stage 1**: matrix capture + DEAD verdict | `v3_m1_stage1_delta36_matrix_impl.md` |

The flags were added in M1 Stage 0, AFTER M0 Stage 1 lock, FOR the purpose of running M1 Stage 1's matrix against that lock. They did not pre-date the lock.

### F3 — ACCEPTED (still true, low priority given `both=DEAD`)

Confirmed against current `src/demo3d.cpp:2480-2485`: `sinT = sin(0.75·π/2)` is the constant applied for all cascade dispatches. ShaderToy uses lProbeSize-derived theta which varies per cascade (sin ≈ 0.924, 0.981, 0.995 for C0→C1, C1→C2, C2→C3).

Why this is now low priority:
- The matrix verdict was `both = DEAD` even at the cascade-1-correct value.
- Improving #6 to be per-cascade-correct doesn't help if the structural mechanism doesn't close the gap on this scene type to begin with.
- The actual mechanism for Sponza over-bright was identified as **multi-bounce concentration under enclosed geometry** (Stage 11d), not merge-cone width.

If #6 is ever revisited, the per-cascade fix is a 5-line C++ change. It is not urgent.

### F4 — REJECTED, STALE (the 4.7× was solved by a different knob)

Audit claim: *"Even if Deltas #3 and #6 are STRONG on Cornell, they will be DEAD on Sponza (no measurable impact on a 5× gap). The M1 cumulative gate requires BOTH scenes to pass. Path A is structurally blocked by Sponza unless the baseline itself is a measurement artifact."*

The audit's logical chain is:
1. Sponza ratio_self = 4.71 at default
2. M1 deltas target ~10-30% reductions
3. Therefore M1 deltas can't close a 5× gap
4. Therefore Path A is dead

Step 1-3 are correct. Step 4 assumes the only Path-A lever is `(Delta #3, Delta #6, Delta #4)`. Stage 8 (2026-05-27) explicitly **falsified that assumption** by finding a different lever:

| Sponza variant (mode-17) | `ratio_self` | `\|p95\|` | Verdict |
|---|---:|---:|---|
| baseline                  | 4.7148 | 4.528 | (audit's baseline) |
| `--use-multi-bounce=0`    | 0.969  | **0.272** | **CLEARS retirement gate** |
| `--multi-bounce-gain=0.5` | 1.613  | 0.889 | partial |
| `--use-probe-jitter=0`    | 4.715  | 4.528 | NEUTRAL (rules out jitter) |
| `--use-hybrid=1` (oracle) | 0.832  | 0.309 | reference |

Stage 9 then ran a full MB-gain ladder (`{0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.75, 1.00}`) and found a **non-monotonic minimum at gain=0.10** (`|p95| = 0.253`, ratio_self = 1.04). Stage 10 confirmed this is a real mode-0 visual win (10× composite RMS reduction; 95% of the cascade-vs-hybrid gap closed).

So the Sponza 4.7× ratio is not "fatal for Path A" — it's the symptom of MB-feedback over-drive that has a scene-dependent gain knob already wired (`--multi-bounce-gain=X`). The audit's framing is a year too late, conceptually: Path A's value isn't the #3/#6 deltas (those tested DEAD); it's the **measurement framework** that surfaced the MB-gain lever and the per-scene calibration discovery.

The remaining honest concern (and the audit deserves credit for raising it): **the MB-gain lever is itself a per-scene knob**. Stage 9 Cornell ladder showed Cornell wants gain=1.0; Sponza wants 0.10. No single global gain works (verdict `FORK_PER_SCENE_OR_PROBE`). Stage 11a was scoped to land the per-scene preset infrastructure but was reverted at the start of Stage 10 per measurement-first principle (it has been re-justified by Stage 10 evidence and is still in the backlog).

### F5 — ACCEPTED (uniformly across all M1/Stage-11 work)

The 693-pixel valid mask is real and is a uniform floor across the M0/M1/Stage-11 chain. The relative-change measurements (Stage 8/9/10/11d) reproduce on the same narrow mask, so the floor itself doesn't bias the comparative results — if anything, it makes them conservative.

The audit's recommended fix (`pt_lum > 0.001`) was considered in Stage 1 SC2 and rejected: lowering the threshold pulls in pixels where PT_GI is essentially zero, and `cascade / 0 ≈ ∞` blows up the per-pixel ratio statistics. The current threshold survives the noise floor at N=2048 but does shrink. An adaptive threshold (target a fixed valid-pixel percentage) is a reasonable Stage 11+ follow-up that doesn't break the historical comparison.

### `02_correction` claim about no CLI access — REJECTED, FALSE

The correction states: *"The M1 delta flags are COMPLETELY INACCESSIBLE from the running application... no CLI argument, no GUI checkbox."*

Source evidence to the contrary in `src/main3d.cpp`:

```cpp
// Lines 580-590
} else if (arg.rfind("--m1-delta3-gated-trilinear=", 0) == 0) {
    static constexpr const char* kPrefix = "--m1-delta3-gated-trilinear=";
    int v = std::atoi(arg.substr(strlen(kPrefix)).c_str());
    demo->setM1Delta3GatedTrilinear(v != 0);
    std::cout << "[MAIN] --m1-delta3-gated-trilinear=" << v << "\n";
} else if (arg.rfind("--m1-delta6-geometric-cone=", 0) == 0) {
    static constexpr const char* kPrefix = "--m1-delta6-geometric-cone=";
    int v = std::atoi(arg.substr(strlen(kPrefix)).c_str());
    demo->setM1Delta6GeometricCone(v != 0);
    std::cout << "[MAIN] --m1-delta6-geometric-cone=" << v << "\n";
}
```

These have been used by every M1 capture script:
- `tools/v3_m1_delta36/capture_matrix.ps1` (Stage 1)
- `tools/v3_m1_source_energy_ab/capture_source_energy.ps1` (Stage 8, `delta3_on` variant)
- `tools/v3_m1_mb_gain_ladder/capture_gain.ps1` (Stage 9)
- All Stage 10/11d capture scripts.

The correction is right that the flags have no ImGui surface — that is intentional. Per the project's measurement-first principle, opt-in experimental flags stay CLI-only until a verdict justifies a default flip. They are *not* dead code; they are routinely exercised by the harness scripts in `tools/v3_m1_*/`.

The correction's recommendation 4 ("Before M1 A/B, add CLI arguments + ImGui checkboxes... this is ~5 lines of code") is also stale — the CLI part already exists and has been in use for ~24 hours of investigative work.

## What the audit got right and what it missed

### Got right

- F3 is genuinely true (cone hardcoded for C0→C1) and has been on the radar without a recorded fix.
- F5 is a real concern about the metric headroom shrinking with convergence.
- The general principle ("measure before promoting; per-delta impl docs before per-delta merges") is correct and is in fact the project's locked workflow per `doc/7/v3_shadertoy_adoption_scope.md` §5 rule #8.

### Missed

- The entire post-2026-05-27 investigative chain:
  - M1 Stage 0 / Stage 1 (CLI flags + matrix verdict) — `doc/7/v3_m1_stage0_*` and `doc/7/v3_m1_stage1_delta36_matrix_*`
  - M1 Stage 2-7 (transport-contract audit, probe-contract audit, local sampling, final GI A/B, shader probe diag, atlas attribution, shader contribution) — eight impl docs under `doc/7/v3_m1_stage[2-7]_*`
  - Stage 8 (source-energy A/B → MB_FEEDBACK_DOMINANT verdict)
  - Stage 9 (MB-gain ladder → FORK_PER_SCENE_OR_PROBE)
  - Stage 10 (mode-0 visual A/B → STAGE8_9_VINDICATED)
  - Stage 11b (Cornell consumer audit → BAKE_UNDER_EMITS)
  - Stage 11c (light-type discriminator → LIGHT_TYPE_DOMINANT)
  - Stage 11d (light-distance ladder → multi-bounce under-emit hypothesis)
- The actual current root-cause hypothesis: cascade's multi-bounce chain (`sampleC0AtlasStochastic` + temporal accumulator) under-counts the closed-geometry MB energy that PT captures via deep cosine paths. This is a structural finding about the cascade pipeline's MB representation, not a #3/#6 merge issue.
- The fact that the CLI flags it calls "dead code" have been exercised dozens of times in the last 24 hours of capture work.

## Recommendations updated

The audit's recommendations were drafted under the assumption that the M1 #3/#6 process had not run. Updated for current state:

1. **Audit's rec 1** ("DO NOT toggle m1Delta3GatedTrilinear or m1Delta6GeometricCone in the GUI") — moot; no GUI surface exists and the matrix verdict already says DEAD. The flags can still be exercised from CLI for re-verification, but the verdict stands.
2. **Audit's rec 2** ("Run the M1 A/B for Deltas #3+#6 against M0 baselines") — DONE on 2026-05-27. Verdict on disk.
3. **Audit's rec 3** ("Audit Sponza baseline before treating it as a gate") — DONE across Stage 2-7. The 4.7× baseline is genuine; Stage 8 attributed it to MB feedback; Stage 9-11d narrowed the mechanism.
4. **Audit's rec 4** ("Fix Delta #6 per-cascade cone variation") — still a valid follow-up, but low priority given `both=DEAD` already at the cascade-1-correct value. Can be picked up if/when #6 is re-revisited.
5. **Audit's rec 5** ("Create the missing M1 per-delta impl docs") — the per-delta numbering shifted: instead of `v3_m1_delta3_*_impl.md` and `v3_m1_delta6_*_impl.md` separately, the project landed `v3_m1_stage1_delta36_matrix_impl.md` (the joint 2×2 matrix verdict). The per-delta granularity was bundled into one matrix doc because the matrix evaluates them jointly — the audit's preferred naming convention is reasonable, but the substantive deliverable exists.

## Updated bottom line

- **#3 and #6 are DEAD per the matrix verdict on disk (2026-05-27).** Audit's HIGH severity on F1 is unwarranted.
- **The real Sponza failure mode is MB-feedback over-drive, not merge-knob calibration.** Stage 8 (2026-05-27) attributed it; Stage 9 (2026-05-27) quantified the gain ladder; Stage 10 (2026-05-27) validated the mode-0 visual win.
- **The current open root-cause work (Stage 11d, 2026-05-28) is investigating WHY cascade's multi-bounce chain under-counts under enclosed geometry,** independently of the #3/#6 mechanism the audit focused on.
- **The audit deserves credit for raising F3 and F5,** which remain genuinely open (though F3 is low-priority and F5 is a uniform floor across the program).
- **F1, F2, F4, and the `02_correction` "dead code" claim are stale** — the audit was written without visibility into the 2026-05-27 / 2026-05-28 investigative chain.

## What the audit author should read next

To get current:

1. [v3_m1_stage1_delta36_matrix_impl.md](../7/v3_m1_stage1_delta36_matrix_impl.md) — DEAD verdict for #3/#6.
2. [v3_m1_stage8_source_energy_ab_impl.md](../7/v3_m1_stage8_source_energy_ab_impl.md) — attribution of Sponza 4.7× to MB feedback.
3. [v3_m1_stage9_mb_gain_ladder_impl.md](../7/v3_m1_stage9_mb_gain_ladder_impl.md) — Sponza best at gain=0.10 (|p95|=0.253, clears retirement gate); Cornell wants gain=1.0.
4. [v3_m1_stage10_mode0_visual_ab_impl.md](../7/v3_m1_stage10_mode0_visual_ab_impl.md) — mode-0 visual A/B validates Stage 8/9.
5. [v3_m1_stage11d_light_distance_ladder_impl.md](../7/v3_m1_stage11d_light_distance_ladder_impl.md) — current open hypothesis (MB under-emit under enclosed geometry).

Plus `tools/v3_m1_delta36/` and `tools/v3_m1_*` for the actual capture matrices.

## Closing

The audit is a useful sanity check on documentation hygiene (F3 cone hardcode, F5 valid mask) and on the principle that flags should be measured before promoted. But it was written without reading the eight impl docs that already executed the measurement chain. Recommend the audit author re-read the `v3_m1_stage1` through `v3_m1_stage11d` series before drafting future audits — the project's discipline IS measurement-first; the audit accidentally argued against the project's own current evidence.

No code changes are warranted by this audit. F3's per-cascade #6 cone fix is filed as a low-priority backlog item; F5's adaptive threshold is filed as a Stage 11+ follow-up.

## v4 Phase 2B update (2026-05-28)

Phase 2B of v4 ([v4_phase1_impl.md](v4_phase1_impl.md)) removed the
`--m1-delta3-gated-trilinear` and `--m1-delta6-geometric-cone` CLI flags
discussed above. The flags were DEAD per the 2x2 matrix verdict (see F1 section
above), and removing them simplifies the merge formula in `radiance_3d.comp`
(eliminates the ternary branch on `uM1Delta3GatedTrilinear` and the guard
condition in `aFactor`). The removed flags served their purpose -- they proved
the ShaderToy #3/#6 delta port does not work in the volumetric topology.

The capture scripts in `tools/v3_m1_delta36/` that referenced these flags are
preserved as-is (they capture a specific historical configuration). New capture
scripts for v4 should use `--mb-gain-per-scene` instead.
