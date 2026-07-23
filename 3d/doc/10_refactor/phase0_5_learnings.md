# Phase 0–5 Validation Learnings

**Date:** 2026-07-23
**Status:** Living document, updated as gates advance
**Scope:** Methodology and technical lessons from building the strangler-shell
refactor and the ShaderToy parity kernel through Phase 5 (G0–G6).

---

## 1. Validation Methodology That Worked

### Independent golden fixtures are the backbone

Every gate derives expected values from a double-precision Python port of
`shader_toy/Common.glsl` / `CubeA.glsl`, checked in as JSON plus a generated
C++ header. A CPU oracle agreeing with itself is not evidence. The effective
chain is:

```text
Python golden (double, independent derivation)
    -> CPU oracle (float32, production-mirror math)
    -> GLSL shader (float32, production code)
```

Each layer must match the one above it within recorded tolerances. The Python
generator is checked in and regenerable; fixtures are never edited by hand.

### Machine-readable gates, never screenshots

Every phase writes a JSON report with an explicit gate name, metrics, and a
`mismatches` array (context / field / expected / actual, capped). Process exit
codes are nonzero on failure so runners can chain. The full chain is:

```powershell
@(
  "tools\10_refactor\phase0_baseline\run_phase0_baseline.ps1",
  "tools\10_refactor\phase1_shell\run_phase1_shell_parity.ps1",
  "tools\10_refactor\phase2_reference_scene\run_phase2_reference_scene.ps1",
  "tools\10_refactor\phase3_layout\run_phase3_layout.ps1",
  "tools\10_refactor\phase4_transport\run_phase4_transport.ps1",
  "tools\10_refactor\phase5_merge\run_phase5_merge.ps1"
) | ForEach-Object {
  powershell -NoProfile -ExecutionPolicy Bypass -File ".\$_"
  if ($LASTEXITCODE -ne 0) { throw "FAILED: $_" }
}
```

### Legacy output is protected by bitwise evidence, not vibes

Phase 1 proves the strangler seam by running the same workload through
`legacy-direct` and the `App3D`-wrapped adapter and requiring identical
backend selection, scene/shader revisions, and **byte-identical screenshots**
(SHA-256). Any Phase that touches shared startup code reruns this.

### Deterministic sampling beats random sampling

All coverage sets use seeded LCG generators, so a failure reproduces exactly
on every machine and every run. Reports are keyed by UTC timestamp, commit,
and a GUID; they record GPU strings and shader SHA-256 records.

---

## 2. Gate-Driven Discoveries (Bugs the Fixtures Caught)

### 2.1 Direction index couples to texels through probe positions

The reference coupling is angular resolution `probeSize²` times spatial
density `(gRes/probeSize)²`. Direction `(dx, dy)` lives at atlas texel
`(dx*probePositions + 0.5, dy*probePositions + 0.5)`, **not** at
`(dx + 0.5, dy + 0.5)`. The first fixture generation enumerated a single
probe cell and produced degenerate self-agreeing fixtures. Hemisphere-sum
fixtures (which integrate over the true direction cell) exposed it.

**Lesson:** fixtures that sum/integrate over a domain catch enumeration bugs
that point fixtures hide.

### 2.2 Fold constants to avoid CPU/GPU float reassociation drift

The interior-page physical mapping `256 + y - (1536 + 256c)` produced ~1e-4
differences between CPU and GPU evaluation of the same formula, because the
GLSL compiler may reassociate float expressions. The folded form
`y - (1280 + 256c)` is a single subtraction that both sides evaluate
identically. The golden values (double precision) are unaffected by the form.

**Lesson:** when CPU and GPU must agree bit-closely, write the mathematically
equivalent form with the fewest rounding steps and no reassociation freedom.

### 2.3 Band-sample comparisons must snap to texel centers

The GPU evaluates one probe per texel at its half-texel center. Comparing a
CPU expectation at an arbitrary float UV against the GPU texel containing it
fails with small distance deltas and occasional sky/hit flips. Snap sample
UVs with `floor(uv) + 0.5` before comparing.

### 2.4 The reference room is fully enclosed — lit points must be found, not assumed

`Common.glsl`'s Cornell variant is enclosed; direct sun reaches surfaces only
through the animated ceiling exclusion blades. Hand-picked "obviously lit"
floor/wall points were actually ceiling-shadowed. The transport fixture
generator now scans deterministically for genuinely lit and occluded points
and asserts each category exists.

**Lesson:** never hand-write fixture expectations that depend on global
illumination reasoning; derive them from a trace.

### 2.5 The ShaderToy merge leaves chart-edge look-backs unclamped — a policy decision was required

`CubeA.glsl:35` uses the raw candidate coordinate for the look-back distance
fetch; only the radiance fetch clamps. At chart edges the literal source reads
into a neighboring chart or cascade band. The parity port applies the plan's
safer policy: **candidate coordinates are clamped to the owning chart before
both look-back and radiance addressing**. This is recorded in the fixture
metadata as the approved edge policy.

**Lesson:** where the captured source and the plan conflict, decide
explicitly, encode the decision in fixtures, and document it. Silent
"improvements" invalidate parity claims; so does silently copying a bug.

### 2.6 WeightedMerge's visibility alpha comes from a distinct address

The look-back distance texel and the four radiance-bin texels are different
addresses with different roles. G6 fixtures deliberately put contradictory
alpha in radiance bins; the visibility decision must only read the look-back
address.

### 2.7 Source quirks are parity requirements

Preserved verbatim, by contract:

- theta literal `3.14192653` (typo in the reference; distinct from the PI
  literal `3.141592653` used for azimuth/weighting);
- `t > -0.5` acceptance (hits slightly behind the origin are valid);
- the `+sqrt` cylinder root (far-side hits, back-facing normals);
- strict `<` in visibility tests (boundary equality rejects);
- `max(0.01, visibleWeight)` normalization floor;
- one-sided `sign(norDot*pDot) < -0.5` plane tests with chart-specific plane
  normals that differ from shading normals (ceiling, z0, interior back).

Cleaning any of these up is a deliberate post-parity divergence and must not
happen silently.

---

## 3. Environment and Tooling Lessons

- **Default `python` is Python 2.7** on this machine; all generators must run
  through `py -3` (Python 3.13). The legacy `analyze_screenshot.py` failure
  seen in Phase 0 logs is the same root cause.
- **`active` is a reserved GLSL keyword**; struct members must be renamed
  (e.g., `isActive`).
- **`glClearTexImage` needs GL 4.4**; the runtime accepts 3.3 contexts with
  extensions, so all clears have an upload fallback.
- **MSVC chokes on nested-brace `std::array` aggregate tables**; plain C
  arrays avoid it.
- **Readback after image writes needs barriers**:
  `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT` between
  compute passes; add `GL_TEXTURE_UPDATE_BARRIER_BIT` before `glGetTexImage`.
- Console mojibake (`鲁` for `³`) is a code-page artifact, not a rendering
  bug.

---

## 4. What Each Gate Actually Proves (and Does Not)

| Gate | Proves | Does not prove |
|---|---|---|
| G0 | Legacy baseline is reproducible; shader bytes are known | any visual correctness |
| G1(shell) | Strangler seam is behavior-identical | any reference-kernel property |
| G1(chart) | 8 charts match ShaderToy table; trace contract; std430 layout | transport |
| G2 | Cascade layout, physical mapping, band reachability | directions' use in shading |
| G3 | Square-ring directions, weights, hemisphere sums | merge or feedback |
| G4 | Interval reach and blocker classification | anything temporal |
| G5 | Payload schema: distance alpha, sky sentinel, no boolean collapse | bounce correctness |
| G8 | Local material/sun transport per category | hierarchy |
| G6 | Weighted upper merge semantics on synthetic atlases | full hierarchy integration (Phase 6 scope) |

Until Phase 7 wires `ReferenceSurfaceC0` into the final renderer, **no visual
parity claims exist**; all evidence is algorithm-level.

---

## 5. Forbidden Shortcuts (re-stated from the plan)

- No calibration floors, proxy visibility, or direct-scale multipliers.
- No tuning constants to match a screenshot.
- No consolidating the two distinct PI literals.
- No replacing square-ring mapping with disk mappings.
- No generic image downsample/upsample as a merge substitute.
- No same-texel EMA as a feedback substitute (G7 explicitly tests this).

## 6. Current Status at Time of Writing

- Committed: Phase 0 `5535084`, Phase 1 `ea4e13d`, Phase 2 `154fd90`,
  Phase 3 `2be1a78`, Phase 4 `eb0bf72`.
- Phase 5 (G6) validated and pending commit alongside this document.
- Next: Phase 6 temporal hit-chart feedback (G7 + G10), which also completes
  the production C5→C0 hierarchy scheduling the Phase 5 oracle validated.
