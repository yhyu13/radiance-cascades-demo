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

## Reading order

1. Read `phase6a_screenshot_ai_plan.md` — simpler, implement first
2. Read `phase6b_renderdoc_ai_plan.md` — builds on 6a patterns

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
