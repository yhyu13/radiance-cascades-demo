#!/usr/bin/env python3
"""
Temporal blend bug diagnostic — run via renderdoccmd.exe python debug_temporal.py <capture.rdc>
Looks for:
  1. Whether temporal_blend.comp was dispatched at all
  2. What probeAtlasHistory / probeGridHistory contain after the dispatch
  3. Uniform values (uAlpha, uSize) passed to the blend shader
  4. Whether probeAtlasTexture / probeGridTexture (the sources) are non-zero
"""
import sys, pathlib, base64, anthropic
import renderdoc as rd

CAP_PATH = sys.argv[1] if len(sys.argv) > 1 else "3d_temporal_fail.rdc"

def tex_to_b64_png(controller, rid, slice_=0, mip=0):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.mip = mip
    ts.slice.sliceIndex = slice_
    ts.alpha = rd.AlphaMapping.Discard
    ts.destType = rd.FileType.PNG
    buf = rd.BufferFileOutput()
    controller.SaveTexture(ts, buf)
    return base64.standard_b64encode(bytes(buf)).decode("utf-8")

def ask_claude(b64, prompt):
    client = anthropic.Anthropic()
    msg = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=512,
        messages=[{"role": "user", "content": [
            {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": b64}},
            {"type": "text", "text": prompt},
        ]}],
    )
    return msg.content[0].text

def main():
    cap = rd.OpenCaptureFile()
    cap.OpenFile(CAP_PATH, "", None)
    controller = cap.OpenCapture(rd.ReplayOptions(), None)

    actions = controller.GetRootActions()
    resources = controller.GetResources()
    res_by_name = {r.name: r for r in resources}
    res_by_id   = {r.resourceId: r for r in resources}

    report = [f"# Temporal Blend Debug: {CAP_PATH}\n"]

    # ── 1. Find temporal_blend.comp dispatches ──────────────────────────────────
    def walk(acts, depth=0):
        found = []
        for a in acts:
            name = a.customName or ""
            if "temporal" in name.lower() or "Dispatch" in str(a.flags):
                found.append((depth, a))
            found += walk(a.children, depth+1)
        return found

    dispatches = []
    def walk_dispatches(acts):
        for a in acts:
            if rd.ActionFlags.Dispatch in a.flags:
                dispatches.append(a)
            walk_dispatches(a.children)
    walk_dispatches(actions)

    report.append(f"## Total compute dispatches in capture: {len(dispatches)}\n")
    for d in dispatches:
        report.append(f"  EID={d.eventId}  name={d.customName or '(unnamed)'}\n")

    # ── 2. Texture inventory — find history and source textures ─────────────────
    patterns = {
        "probeAtlasHistory":  "probeAtlasHistory",
        "probeGridHistory":   "probeGridHistory",
        "probeAtlasTexture":  "probeAtlasTexture",
        "probeGridTexture":   "probeGridTexture",
    }
    found_tex = {}
    for label, pat in patterns.items():
        matches = [r for r in resources if pat.lower() in r.name.lower()]
        if matches:
            found_tex[label] = matches[0]
            report.append(f"Found {label}: id={matches[0].resourceId} name='{matches[0].name}'\n")
        else:
            report.append(f"NOT FOUND: {label}\n")

    # ── 3. If dispatches exist, step to last dispatch and inspect state ─────────
    if dispatches:
        last_eid = dispatches[-1].eventId
        controller.SetFrameEvent(last_eid, True)
        report.append(f"\n## Stepped to last dispatch EID={last_eid}\n")

        pipe = controller.GetPipelineState()

        # Compute shader name
        cs = pipe.GetShader(rd.ShaderStage.Compute)
        cs_name = res_by_id[cs].name if cs in res_by_id else str(cs)
        report.append(f"Active compute shader: {cs_name}\n")

        # Uniforms / push constants — try to read bound uniforms via reflection
        refl = controller.GetShader(rd.ShaderStage.Compute, rd.ShaderEntryPoint("main", rd.ShaderStage.Compute), rd.ShaderCompileFlags())

    # ── 4. Snapshot textures at end of frame ────────────────────────────────────
    # Set to last event
    all_eids = [d.eventId for d in dispatches]
    if all_eids:
        controller.SetFrameEvent(max(all_eids), True)

    client = anthropic.Anthropic()
    for label, res in found_tex.items():
        try:
            b64 = tex_to_b64_png(controller, res.resourceId, slice_=0)
            analysis = ask_claude(b64,
                f"This is a 3D texture slice (Z=0) named '{label}' from a radiance cascade GI system.\n"
                f"Is this texture fully black/zero? If not, describe what colors/values are present.\n"
                f"One or two sentences only."
            )
            report.append(f"\n### {label} (slice 0)\n{analysis}\n")
        except Exception as e:
            report.append(f"\n### {label}: ERROR reading texture: {e}\n")

    controller.Shutdown()
    cap.Shutdown()

    out = pathlib.Path("doc/cluade_plan/AI/analysis/temporal_debug.md")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(report))
    print(f"[debug] Report: {out}")
    print("".join(report))

if __name__ == "__main__":
    main()
