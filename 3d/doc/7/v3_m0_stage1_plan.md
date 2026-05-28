# M0 Stage 1 — Plan (baseline captures)

**Date:** 2026-05-27.
**Predecessor:** [v3_m0_stage0_closeout_impl.md](v3_m0_stage0_closeout_impl.md) (Stage 0 closeout shipped — 8 scope patches + 3 sanity + critic 08 patches applied).
**Goal:** capture the M0 reference + cascade + hybrid baselines for both **cornell** and **sponza** at signed-off N, snapshot to `tools/v3_baseline/baseline_lock.json`, and hand off to M1 Stage 0.

Stage 1 is **GPU-execution + scripting work** — different from Stage 0 (analytical) and the Stage 0 closeout (doc-only). The Stage 1 outputs are EXR captures + a JSON lock file, not new analytical findings.

---

## Scope (in / out)

**In scope:**
- Verify (or re-capture) the cornell cascade-OFF baseline at cornell/cam0/MB-ON g=1.0/hybrid-OFF/mode-17/N=2048.
- Capture cornell hybrid-ON baseline at the same scene/cam/N config (the only diff is `--use-hybrid=1`).
- Restore / re-stage the camera-config JSONs (`cameras.json`, `sponza_cam.json`) — both are absent from disk and untracked in git per `git status`.
- Create the v3 baseline capture script `tools/v3_baseline/sponza_capture.ps1` (forked from `cv1_capture_leaksupp.ps1` — `cv1_capture.ps1` no longer on disk).
- Run Sponza PT convergence verification per contingency ladder: try N=2048; if PT clearly unconverged (`mean_pt` drifts >5% vs N/2 or visually noisy), escalate to N=4096; if still inadequate, fall back to provisional bands (document the fall-back, do not block on convergence).
- Capture Sponza cascade-OFF (hybrid-OFF) baseline + Sponza hybrid-ON baseline at the signed-off N.
- Snapshot all 4 captures + the 3 Stage 0 audit-doc verdicts into `tools/v3_baseline/baseline_lock.json` (schema below).

**Out of scope (deferred):**
- M1 Stage 0 work (per-delta impl docs for #3+#6 bundle, #4 formulation-comparative, etc.) — handoff is to M1 Stage 0, not implementation here.
- Re-running the existing cornell cascade-OFF capture if file-state shows the artifacts at N=2048 are present, valid EXRs, and the metric JSON is consistent. (Re-capture only if Stage 0 closeout invalidated the inputs — it didn't.)
- M2 Path B mechanism scoping doc (per closeout impl P8 precondition).
- Anything that requires shader edits or engine recompile (this is a measurement phase against the locked Hybrid v1.3.1 / MBRC v2.0-postfix build).

### Patch / step categories (per critique 08 I1 lesson, applied to Stage 1)

Carrying the mechanical-vs-judgment split into Stage 1 work classification (impacts budget):

| Category | Steps | Description |
|----------|-------|-------------|
| **Mechanical** | restore cameras.json, restore sponza_cam.json, verify existing cornell cascade-OFF artifacts, snapshot to baseline_lock.json | Copy or recover content; check file existence; serialize known metrics |
| **Script-fork** | sponza_capture.ps1 from cv1_capture_leaksupp.ps1 | Disciplined diff: scene swap + camera-file swap + flag adjustments. Not analytical, but requires care to match the cornell capture's flag set so the cross-scene comparison is valid. |
| **GPU-execution** | cornell hybrid-ON capture, Sponza PT/cascade/hybrid captures | Long-running but unattended. Time risk, not judgment risk. |
| **Judgment** | PT convergence verdict (N=2048 vs 4096 vs provisional), sign-off N selection per-scene | Reading the convergence numbers and deciding whether to escalate. Cannot be pre-scripted. |

Budget reflects this split: mechanical at ~5 min each, script-fork at ~15 min, GPU-execution time-bound by frame counts (see Risk + budget), judgment at ~10 min per call.

---

## Work breakdown

### Step 0 — Pre-impl audit (~10 min, per critique 08 I3 + I4 pattern)

**0a — File-existence + writability check** for everything Stage 1 will touch:

| File / dir | Expected state | If missing | Notes |
|-----------|----------------|------------|-------|
| `build/RadianceCascades3D.exe` | Present, post-Hybrid-v1.3.1 build (mtime ≥ 2026-05-26) | STOP — surface to user; cannot capture without engine | Verified present, mtime 2026-05-26 |
| `tools/v20_pre_measurement/cameras.json` | Present, contains cam0/cam1/cam2 entries for cornell | Restore from git `6baa004:3d/tools/v20_pre_measurement/cameras.json` | **Missing on disk; restore via `git cat-file -p` redirect** |
| `tools/v20_pre_measurement/sponza_cam.json` | Present, contains sponza_cam_md entry | Restore from git `388de0a:3d/tools/v20_pre_measurement/sponza_cam.json` | **Missing on disk; restore via `git cat-file -p` redirect** |
| `res/scene/sponza.obj` | Present (engine maps `--load-obj=sponza` → this path) | STOP — Sponza captures impossible | Verified present |
| `tools/v20_convergence/captures_cv1_postfix/cv1_cornell_cam0_mbon_g100_hyb0_N2048_m17_postfix_cascade_gi.exr` | Present, valid EXR | Re-capture cornell cascade-OFF | Verified present, ~810 KB, valid |
| `tools/v20_convergence/captures_cv1_postfix/cv1_postfix_results.json` | Present, contains N=2048 metrics for `pre` and `post` keys | Re-analyze if missing | Verified — `pre.2048` has ratio 0.65, p95 1.28, bright% 5.44 |
| `tools/v3_baseline/` | Directory exists with delta audit docs | mkdir if missing | Verified present, 3 audit docs |
| `cv1_capture.ps1` (in tools/v20_convergence/) | Originally referenced by closeout handoff item #3 | **GONE — only `_leaksupp` variant remains.** Fork from `_leaksupp` instead, accept the DM=1/ST=1/WS=1 flag set or strip them down to bare Default. | **DECISION below.** |

**0b — Decision: which existing script to fork.** `cv1_capture_leaksupp.ps1` is the only template available. Its flag set forces leak-suppression preset (DM+ST+WS=1), which is **not** the cornell Default capture used to produce the existing `cv1_postfix` baseline at N=2048. The existing cornell baseline used the Default flag set (no DM/ST/WS=1 forcing). For cross-scene comparability, the Sponza capture must match the **same flag set as the existing cornell cascade-OFF baseline** — i.e., not force-on the leak-suppression flags.

**Decision:** fork `cv1_capture_leaksupp.ps1` and **strip** `--use-directional-merge=1`, `--use-spatial-trilinear=1`, `--use-weighted-sample=1` (let engine defaults govern), keeping the rest. This matches the Default Path A configuration used in the existing cornell baseline. Document the strip explicitly in the fork's header comment.

**0c — Stale-language grep on this plan doc and on cv1_capture_leaksupp.ps1** for terms that would mislead a future reader ("leak-suppressed", "DM=1", "ST=1", "WS=1") — confirm the forked script's header is unambiguous about being a Default-flag capture, not a leak-suppressed one. (This step runs after the fork in Step 5; included here for traceability.)

### Step 1 — Restore camera JSONs (~5 min, mechanical)

```bash
git cat-file -p 6baa004:3d/tools/v20_pre_measurement/cameras.json > tools/v20_pre_measurement/cameras.json
git cat-file -p 388de0a:3d/tools/v20_pre_measurement/sponza_cam.json > tools/v20_pre_measurement/sponza_cam.json
```

Verify each by reading first 5 lines to confirm `"cameras"` key and at least one camera entry. Do NOT auto-stage to git — these are workspace-local config files that the user has historically kept untracked.

### Step 2 — Verify existing cornell cascade-OFF baseline (~5 min, mechanical)

Read `tools/v20_convergence/captures_cv1_postfix/cv1_postfix_results.json` `pre.2048` entry. Expected values per Stage 0:

| Metric | Expected (~) | Actual | Pass? |
|--------|--------------|--------|-------|
| ratio_self (mean_casc / mean_pt) | ~0.65 | 0.6499 | ✓ |
| abs_p95 | ~1.28 | 1.2776 | ✓ |
| bright_pct | ~5.4% | 5.44% | ✓ |
| dim_pct | ~28.6% | 28.60% | ✓ |

If actual diverges by > 5% from expected, re-capture (engine state may have shifted post-Hybrid-v1.3.1 build). If consistent, **reuse** as-is; do not re-render.

### Step 3 — Capture cornell hybrid-ON baseline (~10 min, GPU-execution)

Single-config capture (no N-ladder needed — N=2048 is the locked sign-off N for hybrid retirement criterion per §7 of scope doc). Same scene/cam/seed as Step 2, only `--use-hybrid=1` differs.

Tag format: `cv1_cornell_cam0_mbon_g100_hyb1_N2048_m17_postfix`.

Output dir: `tools/v3_baseline/captures_cornell_hybon/`.

Files expected: `.png`, `_cascade_gi.exr`, `_pt_full.exr`, `_pt_direct.exr`.

Sanity check after capture: cornell hybrid-ON's |p95| should be tighter than cascade-OFF's 1.28 (hybrid pulls toward PT-ref). If not, surface to user — the build may not have hybrid active. Expected band: |p95| ∈ [0.6, 1.0].

### Step 4 — Fork cv1_capture_leaksupp.ps1 → tools/v3_baseline/sponza_capture.ps1 (~15 min, script-fork)

Diffs from the source:
- Scene: `--load-obj=cornell` → `--load-obj=sponza`
- Camera file: `tools/v20_pre_measurement/cameras.json` → `tools/v20_pre_measurement/sponza_cam.json`
- Camera index: `$cam=0` stays (sponza_cam.json has one camera in slot 0)
- Strip flags per Step 0b: remove `--use-directional-merge=1`, `--use-spatial-trilinear=1`, `--use-weighted-sample=1`
- Output dir: `tools/v3_baseline/captures_sponza_default/`
- Tag prefix: `cv1_leaksupp` → `v3base_sponza_default`
- Header comment: explicit "Default flag set; not leak-suppressed; mirrors cornell cv1_postfix baseline for cross-scene comparison"
- Frame list: same as cv1 (`@(128, 256, 512, 1024, 2048)`) — needed for PT-convergence ladder verdict in Step 5
- Add a `--use-hybrid` parameter to the function so the same script can produce both cascade-OFF and hybrid-ON captures (Step 6 reuses)

Verification: run the script with `-WhatIf` semantics by adding a dry-run flag, OR launch a single N=128 capture first and inspect the output before queuing N=2048.

### Step 5 — Sponza PT convergence ladder (~20–60 min, GPU-execution + judgment)

Run `tools/v3_baseline/sponza_capture.ps1` with `--use-hybrid=0` for all 5 N values (128 → 2048).

**Convergence verdict criteria (judgment step):**
1. Compute `delta = abs(mean_pt[N] - mean_pt[N/2]) / mean_pt[N/2]` for N=256, 512, 1024, 2048.
2. **CONVERGED:** `delta[2048]` ≤ 5%. → Lock N=2048 as Sponza sign-off N.
3. **MARGINAL:** `delta[2048]` ∈ (5%, 10%]. → Escalate: capture N=4096 (single value, not full ladder); recompute delta. If `delta[4096]` ≤ 5% → lock N=4096. If still > 5%, fall to PROVISIONAL.
4. **PROVISIONAL:** `delta[N]` > 10% at the highest tested N. → Lock at highest-N captures with explicit "provisional bands; PT reference unconverged at tested budget" tag in baseline_lock.json. Do NOT block Stage 1.

Per closeout handoff: "PROVISIONAL bands" is an accepted outcome. Stage 1 ships either way.

### Step 6 — Sponza hybrid-ON capture (~10 min, GPU-execution)

Single-config capture at the Stage 5 sign-off N. Reuses `sponza_capture.ps1` with `--use-hybrid=1`.

Tag: `v3base_sponza_hybon_N{N}_m17`.
Output dir: `tools/v3_baseline/captures_sponza_hybon/`.

Sanity check: similar to Step 3 — hybrid-ON should tighten |p95| vs cascade-OFF.

### Step 7 — Build baseline_lock.json (~10 min, mechanical)

Schema:

```jsonc
{
  "_note": "M0 Stage 1 baseline lock — locked artifacts for v3 ShaderToy adoption M1+ comparison anchor.",
  "_locked_at": "2026-05-27T<HH:MM>Z",
  "_engine_build": {
    "exe_mtime": "<from build/RadianceCascades3D.exe>",
    "build_id": "Hybrid v1.3.1 / MBRC v2.0-postfix"
  },
  "captures": {
    "cornell_cam0_cascade_off": {
      "tag": "cv1_cornell_cam0_mbon_g100_hyb0_N2048_m17_postfix",
      "dir": "tools/v20_convergence/captures_cv1_postfix",
      "metrics": { "ratio_self": ..., "abs_p95": ..., "bright_pct": ..., "dim_pct": ..., "mean_casc": ..., "mean_pt": ... },
      "N": 2048,
      "source": "reused from v20 cv1_postfix (no re-capture; verified consistent)"
    },
    "cornell_cam0_hybrid_on": { ... new this stage ... },
    "sponza_cam0_cascade_off": { ... new this stage ... , "convergence_verdict": "CONVERGED|MARGINAL|PROVISIONAL" },
    "sponza_cam0_hybrid_on": { ... new this stage ... }
  },
  "audits": {
    "delta3_alpha_audit": { "doc": "tools/v3_baseline/delta3_alpha_audit.md", "verdict": "<from Stage 0>" },
    "delta5_ceiling_estimate": { "doc": "tools/v3_baseline/delta5_ceiling_estimate.md", "verdict": "< 3% magnitude leverage (algebraic Jensen)" },
    "delta7_offset_audit": { "doc": "tools/v3_baseline/delta7_offset_audit.md", "verdict": "CONFORMANT" }
  },
  "sign_off": {
    "cornell_N": 2048,
    "sponza_N": <Step 5 verdict>,
    "sponza_convergence_verdict": "<CONVERGED|MARGINAL|PROVISIONAL>"
  }
}
```

This file is the M1+ comparison anchor — every M1 delta capture references back to these tags.

### Step 8 — Self-critique pass on impl + dump impl summary doc (~20 min)

Apply the **5-item self-critique checklist (per critique 08 I9):**

1. Does each capture match its spec (flags, N, scene, cam, output dir)?
2. Are there any stale references in the forked script or baseline_lock.json?
3. Does the baseline_lock.json round-trip parseable + correctly mirror EXR file paths?
4. Were any file-state assumptions violated (cameras.json existence, exe presence, capture-dir writability)?
5. Did any step require judgment beyond "execute known script with known flags"? (PT convergence verdict in Step 5 explicitly does — flag in impl doc Strategic Changes section.)

Create `doc/7/v3_m0_stage1_impl.md` mirroring closeout impl structure: Strategic changes section (if any) → patches/steps table → bookkeeping → self-critique → verification → handoff to M1 Stage 0 → housekeeping.

---

## Cerebrum / memory entries (exact wording)

### Entry 1 — Sponza PT convergence verdict (project-level decision)

Destination: `.wolf/cerebrum.md` Decision Log.

> **Sponza PT reference sign-off N = {Stage 5 verdict}.** Convergence ladder ran N=128 → N=2048 (and N=4096 if MARGINAL) at cornell-camera-parity flag set (Default Path A). Verdict: {CONVERGED/MARGINAL/PROVISIONAL}. M1+ Sponza comparisons reference this N as the locked PT reference; any deviation requires re-running the ladder and updating baseline_lock.json.

### Entry 2 — Camera/script restoration (project-level note)

Destination: `.wolf/cerebrum.md` Key Learnings.

> **`tools/v20_pre_measurement/` is untracked in git.** `cameras.json` and `sponza_cam.json` exist only in past commits (`6baa004`, `388de0a`). M0 Stage 1 restored both via `git cat-file -p`. Future capture-script sessions in this workspace MUST verify these files exist before launching captures — they're not on the .gitignore + sticky-recovery path that protects other dev files.

### Entry 3 — `cv1_capture.ps1` no longer exists (workflow note)

Destination: `.wolf/cerebrum.md` Do-Not-Repeat.

> **Don't reference `cv1_capture.ps1` in docs as if it's a current artifact.** It's gone from disk; only `cv1_capture_leaksupp.ps1` remains. The Stage 0 closeout handoff (item #3) referenced the base script as a fork source; the actual fork source for Stage 1 was `cv1_capture_leaksupp.ps1` with DM/ST/WS flags stripped to recover Default semantics. Anatomy.md still lists `cv1_capture.ps1` (stale entry as of 2026-05-27; clean up in Stage 1 housekeeping).

---

## Risk + budget

**Budget:** ~90 min wall-clock (Step 0: 10, Step 1: 5, Step 2: 5, Step 3: 10 [GPU], Step 4: 15, Step 5: 20–60 [GPU + judgment, depends on convergence ladder depth], Step 6: 10 [GPU], Step 7: 10, Step 8: 20). GPU time dominates if Sponza needs the N=4096 escalation.

**Risks:**

- **R1 — Sponza PT divergence forces PROVISIONAL.** Sponza is geometrically complex; PT may not converge at N=4096. **Mitigation:** PROVISIONAL is an accepted outcome (per closeout handoff). Document explicitly in baseline_lock.json; do NOT spend extra budget chasing convergence at this stage.
- **R2 — Forked sponza_capture.ps1 carries hidden semantic drift.** If the strip of DM/ST/WS flags doesn't actually produce Default-equivalent semantics (e.g., one of those flags has a default-on path), the Sponza measurement won't be cross-comparable to the cornell baseline. **Mitigation:** verify by running cornell through the forked script too (single N=128 sanity test); compare metrics to the existing cornell cv1_postfix N=128 entry. If they match within numeric noise, the strip is semantically equivalent.
- **R3 — Engine crashes on Sponza at high N.** Sponza is the heaviest scene; PT shader at N=2048+ may hit driver TDR or OOM. **Mitigation:** start Sponza ladder at N=128; if any single capture fails, the script logs the failure and the impl doc captures the partial state. Lower-N captures still feed PROVISIONAL bands.
- **R4 — Camera-JSON content drift between git commits.** `cameras.json` was edited multiple times in history (cam1/cam2 reframing in `6baa004`). Restoring from `6baa004` gives the LATEST framing; this is correct, but a future reader may wonder why coords differ from older docs that reference the pre-`6baa004` version. **Mitigation:** Step 1 documents the source commit explicitly.
- **R5 — Hybrid-ON capture diverges unexpectedly.** If cornell hybrid-ON's |p95| does NOT tighten vs cascade-OFF, the build's hybrid pipeline may be broken. **Mitigation:** Step 3 sanity check is explicit; failure surfaces to user before continuing.

**Stop conditions:**
- If `build/RadianceCascades3D.exe` is missing or older than 2026-05-26 mtime, STOP and surface to user.
- If Sponza PT capture crashes the engine at any N ≥ 256, STOP and surface (the |p95| ladder verdict can fall back to PROVISIONAL with N=128 only, but engine crashes need user attention).
- If sponza_capture.ps1 sanity-test on cornell N=128 diverges from cv1_postfix N=128 metrics by > 5% on `mean_pt`, STOP and surface (the flag strip is broken).
- If patches/steps accumulate beyond ~10 (because new follow-up edits emerge), STOP and re-scope rather than absorb silently.

---

## Acceptance

Stage 1 is complete when:
1. `tools/v3_baseline/baseline_lock.json` exists, validates against the schema in Step 7, and points to readable EXR files for all 4 captures.
2. Cornell hybrid-ON capture exists in `tools/v3_baseline/captures_cornell_hybon/` (4 files per the EXR convention).
3. Sponza cascade-OFF + hybrid-ON captures exist in `tools/v3_baseline/captures_sponza_*` at sign-off N.
4. Sponza PT convergence verdict is recorded (CONVERGED / MARGINAL / PROVISIONAL).
5. `tools/v3_baseline/sponza_capture.ps1` exists, runs, and its header explains the flag strip from `_leaksupp`.
6. `doc/7/v3_m0_stage1_impl.md` summarizes the captures + verdicts + handoff to M1.
7. The 5-item self-critique checklist runs clean (or any flagged item is documented + dispositioned in impl).

After acceptance: **M0 closed.** M1 Stage 0 (per-delta impl docs for #3+#6 bundle) can begin.

---

## Self-critique pass on this plan (SC1–SC5)

Initial draft reviewed:

- **SC1 — Step 0b decision was hidden in the audit table.** The decision to strip DM/ST/WS flags from the fork has substantive impact on Sponza measurement validity (R2). **Fixed** by promoting it to its own paragraph + adding the R2 mitigation cross-test. Without this fix, a future reader might miss that the fork is NOT a literal copy of the leaksupp script.
- **SC2 — PT convergence verdict criteria was ill-specified in first draft.** Original wording said "if not converged, escalate" without numeric thresholds — the kind of conditional that bit Stage 0 (S2/S3 lesson). **Fixed** in Step 5 with explicit 5% / 10% / PROVISIONAL thresholds. A future engineer reading the plan can apply the verdict mechanically.
- **SC3 — File-existence check missed `res/scene/sponza.obj`.** Initial Step 0a only checked tooling files; if the scene OBJ is missing, all Sponza work fails. **Fixed** by adding the scene OBJ to the Step 0a table.
- **SC4 — Sponza hybrid-ON's `--use-hybrid=1` doesn't necessarily route through the same code path as cornell on Sponza.** If `--use-hybrid=1` requires a roughness texture lookup that Sponza's materials don't populate, the hybrid contribution may be degenerate. **Partial fix** — added R5 to surface unexpected divergence; full validation is part of Step 8 self-critique item 1, not pre-runtime.
- **SC5 — baseline_lock.json schema didn't include EXR file paths**, only tags. M1 consumers need explicit paths to read EXRs. **Fixed** by adding `"dir"` field per capture entry. Note: bake-time merging happens on the cascade side in the EXR (`_cascade_gi.exr`); PT-ref is `_pt_full.exr` (full bounce-included). M1 metric scripts must read these specific files, not the `.png`.

No issues found that would require restructuring the plan or expanding scope beyond the 9-step breakdown.

### Self-critique checklist applied (per critique 08 I9, repurposed for plan-time)

1. ✓ Each step has a target deliverable, flag set, expected metric.
2. ✓ Stale-language grep added as Step 0c (forked-script header is checked).
3. ✓ Plan reads coherently end-to-end (re-read pre-publish).
4. ✓ File-existence check covers all assumed-present files (Step 0a, includes the unexpected `cv1_capture.ps1` absence).
5. ✓ Judgment steps explicitly flagged (Step 5 convergence verdict, Step 0b fork strategy) — both will get callout in Stage 1 impl Strategic Changes section.
