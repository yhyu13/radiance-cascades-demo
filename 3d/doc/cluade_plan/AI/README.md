# Phase 6 — AI-Automated Debug Pipeline

**Goal:** Close the loop between visual artifacts and diagnosis by using Claude's vision
capability to analyze rendered frames and pipeline textures automatically.

---

## Two components

| Component | Trigger | What it captures | Output |
|---|---|---|---|
| **6a — Screenshot + Vision** | Press `P` in app | Final rendered frame (no ImGui) | `analysis/<ts>.md` — artifact names, locations, causes |
| **6b — RenderDoc + Pipeline** | Press `G` in app | Full GPU frame (.rdc) | `analysis/<stem>_pipeline.md` — per-stage texture analysis |

---

## Directory layout

```
doc/cluade_plan/AI/
  README.md                          ← this file
  phase6a_screenshot_ai_plan.md      ← Phase 6a implementation plan
  phase6b_renderdoc_ai_plan.md       ← Phase 6b implementation plan
  screenshots/                       ← PNG frame dumps (Phase 6a output)
  captures/                          ← RenderDoc .rdc captures (Phase 6b output)
  analysis/                          ← AI analysis .md files (both phases)
```

---

## Phase index

| Phase | Doc | What it adds |
|---|---|---|
| 6a | `phase6a_screenshot_ai_plan.md` | P-key screenshot + single-image Claude triage |
| 6b | `phase6b_renderdoc_ai_plan.md` | G-key RenderDoc capture + pipeline analysis |
| 12a | `phase12a_autocapture_impl.md` | Auto-capture delay + probe stats JSON |
| 12b | `phase12b_burst_capture_impl.md` | Burst state machine: modes 0/3/6 over 3 frames |
| 13 | `phase13_quality_lift_plan.md` | Jitter retuning + bilateral luminance edge-stop |
| 14a | `phase14a_multiframe_sequence_capture_impl.md` | N-frame sequence capture for temporal stability analysis |
| 14b | `phase14b_c0_coverage_analysis.md` | C0 surfPct root cause + `uC0MinRange` fix design (NOT YET IMPLEMENTED) |

## Reading order (new)

1. `phase6a_screenshot_ai_plan.md` — simpler, implement first
2. `phase6b_renderdoc_ai_plan.md` — builds on 6a patterns
3. `phase12b_burst_capture_impl.md` — multi-mode burst
4. `phase14a_multiframe_sequence_capture_impl.md` — temporal sequence

---

## Quick start (after implementation)

**Phase 6a:**
```bash
pip install anthropic
export ANTHROPIC_API_KEY=sk-ant-...
# Run the app, press P
```

**Phase 6b:**
```bash
# RenderDoc must be installed at C:\Program Files\RenderDoc\
# Run the app, press G
# Analysis runs automatically via renderdoccmd.exe python ...
```

---

## Why this matters

The radiance cascades pipeline has ~6 passes (voxelize, SDF bake, 4× cascade bake,
reduction, raymarching). A bug in any pass produces a wrong final image but the
symptom in the output is often ambiguous. Phase 6 makes the diagnostic loop explicit:

```
observe artifact → P or G → AI names the artifact and its likely pass → fix
```

This replaces "look at it and guess" with a structured, logged, reproducible analysis.
