# v2.0 — ShaderToy vs ours: visual comparison diagrams

**Date:** 2026-05-25.
**Companion to:** [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
(text-form deltas + theoretical fix).

Three side-by-side diagrams: (1) bake→consume workflow, (2) tech stack
implementation, (3) conceptual / mental model. All numbers and line refs
match the diff doc unless noted.

---

## 1. Workflow — bake & consume

How a frame's GI gets computed end-to-end. Where the cosine/area weighting
lives is the key visual difference.

```mermaid
flowchart TB
    subgraph ST["ShaderToy 3D RC"]
        direction TB
        ST_scene["Scene SDF + lights<br/>(Common.glsl)"]
        ST_probepix["Probe pixel knows<br/>gPos, gNor, gTan, gBit<br/>(SURFACE-ATTACHED)"]
        ST_trace["TraceRay over<br/>HEMISPHERE bins<br/>(probeTheta in [0,pi/2])"]
        ST_bake["Bake: store<br/>L x cos(theta) x dOmega<br/>pre-integrated"]
        ST_merge["Merge upper cascade<br/>via WeightedSample +<br/>smoothstep blend"]
        ST_cubemap["Cubemap atlas<br/>(per-probe-pixel)"]
        ST_consume["Consumer:<br/>4-tap cubemap fetch<br/>SUM hemisphere bins"]
        ST_out["Final irradiance<br/>= sum of bins<br/>(integral done at bake)"]

        ST_scene --> ST_probepix --> ST_trace --> ST_bake --> ST_merge --> ST_cubemap --> ST_consume --> ST_out
    end

    subgraph OURS["Our 3D RC (post-fix v2.0)"]
        direction TB
        OUR_scene["Scene SDF + lights<br/>(sdf_analytic.comp)"]
        OUR_probepos["Probe is a 3D grid cell<br/>(no normal at bake)<br/>(VOLUMETRIC)"]
        OUR_trace["TraceRay over<br/>FULL SPHERE bins<br/>(binToDir over [0,1]^2 -> S^2)"]
        OUR_bake["Bake: store<br/>raw L_in(omega)<br/>+ Phase2 binary alpha"]
        OUR_merge["Merge upper cascade<br/>via smoothstep:<br/>hit.rgb*l + upper*(1-l)"]
        OUR_atlas["Directional atlas 3D tex<br/>(D x D bins per cell)"]
        OUR_consume["Consumer reads pixel normal:<br/>SUM L x max(0, n.omega)<br/>x (4/D^2)  [POST-FIX]"]
        OUR_out["Final irradiance<br/>= Riemann sum<br/>(integral done at consume)"]

        OUR_scene --> OUR_probepos --> OUR_trace --> OUR_bake --> OUR_merge --> OUR_atlas --> OUR_consume --> OUR_out
    end

    ST_bake -.->|"cosine + area<br/>baked into stored L"| ST_consume
    OUR_consume -.->|"cosine + area applied<br/>per pixel normal here"| OUR_out

    classDef bakeStage fill:#dde7ff,stroke:#3a6,stroke-width:1px
    classDef consumeStage fill:#ffe7cc,stroke:#c63,stroke-width:1px
    classDef deltaNote fill:#fff4a3,stroke:#b80,stroke-width:1px

    class ST_bake,OUR_bake bakeStage
    class ST_consume,OUR_consume consumeStage
```

**Key visual takeaway:** ShaderToy folds the `cos x dOmega` weighting into
the BAKE (yellow→orange transition at `ST_bake`); ours leaves it for the
CONSUMER (yellow→orange at `OUR_consume`). Both produce correct Lambertian
irradiance; ours requires the consumer to know the surface normal of the
shaded pixel, which is fine because the consumer is the raymarch fragment
shader that has that normal anyway.

---

## 2. Implementation tech stack

What kind of GPU passes, languages, and data structures each implementation
uses. The architectural shape is set by the host platform first, by RC
correctness second.

```mermaid
flowchart LR
    subgraph STT["ShaderToy stack"]
        direction TB
        ST_lang["GLSL fragment shaders<br/>(Image, BufferA-D)"]
        ST_host["ShaderToy host:<br/>JS browser + WebGL2"]
        ST_storage["Storage: cubemap<br/>+ float framebuffer<br/>(BufferA-D = passes)"]
        ST_dispatch["Per-frame:<br/>1 cubemap pass (bake)<br/>+ 1 image pass (consume)"]
        ST_input["Input: TraceRay = manual<br/>SDF march in shader"]
        ST_output["Output: pixel colors via<br/>fragment write to backbuffer"]

        ST_lang --> ST_host --> ST_storage --> ST_dispatch --> ST_input --> ST_output
    end

    subgraph OURT["Our stack"]
        direction TB
        OUR_lang["GLSL 430/450:<br/>4 compute shaders<br/>+ 5 fragment shaders"]
        OUR_host["C++17 + CMake<br/>OpenGL 4.3 + GLFW"]
        OUR_storage["Storage: 3D textures<br/>(SDF, atlas, history) +<br/>SSBOs (params, debug)"]
        OUR_dispatch["Per-frame pipeline (6 passes):<br/>sdf_analytic - radiance_3d xN<br/>- reduction_3d - temporal_blend<br/>- raymarch.frag - gi_blur"]
        OUR_input["Input: SDF read from<br/>3D texture (pre-baked)"]
        OUR_output["Output: MRT raymarch<br/>(color + gbuffer)<br/>+ EXR screenshot path"]

        OUR_lang --> OUR_host --> OUR_storage --> OUR_dispatch --> OUR_input --> OUR_output
    end

    subgraph SCOPE["What this enables in ours but not ShaderToy"]
        direction TB
        S1["Temporal multi-bounce<br/>(C0 atlas feedback loop)"]
        S2["EMA history + AABB clamping<br/>(temporal_blend.comp)"]
        S3["Multiple cascade resolutions<br/>(non-colocated, D4/D8/D16/D16)"]
        S4["Hybrid PT correction layer<br/>(per-pixel accumulator)"]
        S5["RenderDoc capture + EXR<br/>(reference-grade debugging)"]
        S6["GI Quality Presets<br/>(MB / ST / WS toggles)"]
    end

    OUR_dispatch --> S1
    OUR_dispatch --> S2
    OUR_dispatch --> S3
    OUR_dispatch --> S4
    OUR_host --> S5
    OUR_host --> S6

    classDef stHost fill:#e2f0ff,stroke:#36c
    classDef ourHost fill:#fff2db,stroke:#a60
    classDef extra fill:#e8ffe8,stroke:#393

    class ST_lang,ST_host,ST_storage,ST_dispatch,ST_input,ST_output stHost
    class OUR_lang,OUR_host,OUR_storage,OUR_dispatch,OUR_input,OUR_output ourHost
    class S1,S2,S3,S4,S5,S6 extra
```

**Key visual takeaway:** ShaderToy is a 2-pass GLSL demo (one bake pass +
one consume pass). Ours is a 6-pass C++/OpenGL pipeline with first-class
temporal accumulation, multi-bounce feedback, multi-resolution cascades,
and a hybrid PT-correction overlay. The extra machinery is what lets us
measure GI quality against PT and ship a hybrid retirement path — none of
which is possible inside ShaderToy.

---

## 3. Conceptual / mental model

What each implementation thinks a "probe" is, and how that single mental
model decision cascades down into every primitive.

```mermaid
flowchart TB
    subgraph M_ST["ShaderToy mental model: <br/> SURFACE-ATTACHED PROBES"]
        direction TB
        MST_root["A probe lives ON a wall surface"]
        MST_normal["Probe has built-in normal (gNor):<br/>known at bake time"]
        MST_dome["Probe gathers HEMISPHERE only<br/>(everything above gNor)"]
        MST_cosbake["cos(theta) folded into BAKE<br/>(diffuse weighting per bin)"]
        MST_uv["Bounce light = local UV cubemap<br/>(4-tap average from wall UV)"]
        MST_consumer["Consumer: irrad = sum L_baked"]
        MST_consequence["Implication:<br/>cannot represent free-space<br/>radiance away from surfaces"]

        MST_root --> MST_normal --> MST_dome --> MST_cosbake --> MST_uv --> MST_consumer --> MST_consequence
    end

    subgraph M_OURS["Our mental model: <br/> VOLUMETRIC PROBES"]
        direction TB
        MOUR_root["A probe lives IN free space<br/>(a 3D grid cell, any geometry)"]
        MOUR_normal["No probe normal:<br/>only the shaded pixel has one<br/>(known at consume time)"]
        MOUR_sphere["Probe gathers FULL SPHERE<br/>(any direction may matter)"]
        MOUR_rawbake["Raw L_in stored per bin<br/>(no cosine, no area)"]
        MOUR_mc["Bounce light = MC single-bin<br/>cosine-sample + 8-probe trilinear"]
        MOUR_consumer["Consumer:<br/>irrad = (4/D^2) x sum L x max(0, n.omega)<br/>[post-fix v2.0]"]
        MOUR_consequence["Implication:<br/>same atlas serves any pixel normal,<br/>integral correctness lives at consume"]

        MOUR_root --> MOUR_normal --> MOUR_sphere --> MOUR_rawbake --> MOUR_mc --> MOUR_consumer --> MOUR_consequence
    end

    subgraph CONSEQ["Consequence: which ShaderToy primitives port"]
        direction TB
        C1["Delta #5 - bake-time cosine + dOmega<br/>NOT portable (needs gNor at bake)"]
        C2["Delta #4 - 4-tap UV cubemap MB<br/>NOT portable (needs surface UV)"]
        C3["Delta #1+#2 - hemispheric Riemann sum<br/>PORTABLE (consumer-side analogue)<br/>= today's v2.0 fix"]
        C4["Delta #6 - WeightedSample theta<br/>PARTIALLY portable<br/>(angle-adaptive variant deferred)"]
    end

    MST_cosbake -.->|"surface-anchored<br/>weighting"| C1
    MST_uv -.->|"UV-anchored<br/>4-tap"| C2
    MOUR_consumer ===|"VOLUMETRIC<br/>ANALOGUE"| C3
    MOUR_consumer -.-> C4

    classDef stModel fill:#e2f0ff,stroke:#36c,stroke-width:1px
    classDef ourModel fill:#fff2db,stroke:#a60,stroke-width:1px
    classDef notPortable fill:#ffe0e0,stroke:#c33
    classDef portable fill:#e0ffe0,stroke:#393

    class MST_root,MST_normal,MST_dome,MST_cosbake,MST_uv,MST_consumer,MST_consequence stModel
    class MOUR_root,MOUR_normal,MOUR_sphere,MOUR_rawbake,MOUR_mc,MOUR_consumer,MOUR_consequence ourModel
    class C1,C2 notPortable
    class C3,C4 portable
```

**Key visual takeaway:** Every algorithmic delta between the two impls is
downstream of ONE upstream choice: surface-attached vs volumetric probes.
ShaderToy folds direction-dependent weighting into the bake because it
knows the normal there. We can't, so the same weighting must live in the
consumer. The post-fix v2.0 (`irrad = (4/D^2) x Σ L cos⁺`) is the
volumetric analogue of ShaderToy's bake-time `L x cos(theta) x dOmega` —
same physics, different placement.

---

## 4. Where to point this diagram set

These diagrams complement (not replace) the prose diff. Use them to:

- **Brief a new contributor**: §3 in 60 seconds — what's a probe in each impl.
- **Justify the v2.0 fix**: §1 shows where the missing integral lived;
  §3 shows it was structurally forced by the volumetric mental model.
- **Pre-empt "why don't you just port ShaderToy?"**: §3 right-hand
  "not portable" boxes make the architectural constraint explicit.
- **Scope future work**: §2 right-hand "extra in ours" box is the set of
  features that don't even have a ShaderToy analog to crib from —
  multi-bounce equilibrium, hybrid retirement, GI presets all live here.

## Cross-reference

- Text-form deltas + theoretical fix: [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md)
- Post-fix CV1 result: [v20_postfix_cv1_impl.md](v20_postfix_cv1_impl.md)
- ShaderToy in-tree: [3d/shader_toy/](../../shader_toy/)
- Our cascade bake: [res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp)
- Our cascade consumer: [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag)
- GI Quality Presets: [doc/7/gi_presets.md](gi_presets.md)
