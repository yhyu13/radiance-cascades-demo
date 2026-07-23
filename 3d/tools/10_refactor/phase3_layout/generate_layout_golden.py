#!/usr/bin/env python3
"""Generate independent golden fixtures for the Phase 3 parity layout kernel.

Derivation source: shader_toy/CubeA.glsl lines 62-192 (layout, probe coupling,
square-ring direction mapping, weights, intervals) and the locked chart table in
doc/10_refactor/3d_radiance_cascades_refactor_plan.md Section 4.0.

All values are computed in IEEE-754 double precision. The C++ oracle and the
GLSL decoder must agree with these fixtures within the recorded tolerances; a
CPU oracle agreeing with itself is not evidence.

Outputs:
  tools/10_refactor/phase3_layout/layout_golden_v1.json  (human record)
  src/reference_layout_golden.inc                        (C++ constexpr data)
"""

import json
import math
import os

THETA_PI = 3.14192653   # exact literal from CubeA.glsl line 135 (typo preserved)
PI = 3.141592653        # exact literal from CubeA.glsl lines 146, 190-191
TEXEL = 1.0 / 256.0
LOGICAL_W, LOGICAL_H = 1024, 3072
PHYS_W, PHYS_H = 1024, 512

CHARTS = {
    1: dict(name="floor",     gPos=(0.0, 0.0, 0.0),  gTan=(1,0,0), gBit=(0,0,1), gNor=(0,1,0),   gRes=(256,256), base=(0,0),     material=1),
    2: dict(name="ceiling",   gPos=(0.0, 0.5, 0.0),  gTan=(1,0,0), gBit=(0,0,1), gNor=(0,-1,0),  gRes=(256,256), base=(256,0),   material=1),
    3: dict(name="wall_x0",   gPos=(0.0, 0.0, 0.0),  gTan=(0,1,0), gBit=(0,0,1), gNor=(1,0,0),   gRes=(128,256), base=(512,0),   material=2),
    4: dict(name="wall_x1",   gPos=(1.0, 0.0, 0.0),  gTan=(0,1,0), gBit=(0,0,1), gNor=(-1,0,0),  gRes=(128,256), base=(640,0),   material=3),
    5: dict(name="wall_z0",   gPos=(0.0, 0.0, 0.0),  gTan=(0,1,0), gBit=(1,0,0), gNor=(0,0,1),   gRes=(128,256), base=(768,0),   material=1),
    6: dict(name="wall_z1",   gPos=(0.0, 0.0, 1.0),  gTan=(0,1,0), gBit=(1,0,0), gNor=(0,0,-1),  gRes=(128,256), base=(896,0),   material=1),
    7: dict(name="int_front", gPos=(0.0, 0.0, 0.47 - TEXEL), gTan=(0,1,0), gBit=(1,0,0), gNor=(0,0,-1), gRes=(128,256), base=(0,1536), material=4),
    8: dict(name="int_back",  gPos=(0.0, 0.0, 0.53 - TEXEL), gTan=(0,1,0), gBit=(1,0,0), gNor=(0,0,1),  gRes=(128,256), base=(128,1536), material=4),
}

def cascade_of(uvy):
    return int(math.floor((uvy % 1536.0) / 256.0))

def probe_size(cascade):
    return 2 ** (cascade + 1)

def cascade_reach(cascade):
    return 10000.0 if cascade > 4.5 else probe_size(cascade) / 32.0

def select_chart(x, y):
    """CubeA.glsl:68-121 hardcoded chart selection. Returns chart id or 0."""
    if x < 0.0 or x >= 1024.0 or y < 0.0 or y >= 3072.0:
        return 0
    if y < 1536.0:
        if x < 256.0: return 1
        if x < 512.0: return 2
        if x < 640.0: return 3
        if x < 768.0: return 4
        if x < 896.0: return 5
        return 6
    if x < 128.0: return 7
    if x < 256.0: return 8
    return 0

def fmod_glsl(a, b):
    return a - b * math.floor(a / b)

def piecewise_phi(rel_x, rel_y, thetai):
    """CubeA.glsl:137-145 square-perimeter azimuth, exact branch conditions."""
    if rel_x + 0.5 > thetai and rel_y - 0.5 > -thetai:
        return rel_x - rel_y
    if rel_y - 0.5 < -thetai and rel_x - 0.5 > -thetai:
        return thetai * 2.0 - rel_y - rel_x
    if rel_x - 0.5 < -thetai and rel_y + 0.5 < thetai:
        return thetai * 4.0 - rel_x + rel_y
    if rel_y + 0.5 > thetai and rel_x + 0.5 < thetai:
        return thetai * 8.0 - (rel_y - rel_x)
    return 0.0

def decode(uv):
    """Full layout decode. Returns dict mirroring the C++/GLSL record."""
    x, y = uv
    chart_id = select_chart(x, y)
    if chart_id == 0:
        return dict(active=0, chartId=0, materialId=0, cascade=0, probeSize=0,
                    probePos=(0,0,0), probeDir=(0,0,0), thetaIndex=0.0, theta=0.0, phi=0.0,
                    solidAngleWeight=0.0, lambertWeight=0.0, reach=0.0,
                    physical=(-1,-1), binCount=0, binIndex=-1)
    ch = CHARTS[chart_id]
    cascade = cascade_of(y)
    psize = probe_size(cascade)
    g_res = ch["gRes"]
    mod_uv = (fmod_glsl(x, g_res[0]), fmod_glsl(y, g_res[1]))
    probe_positions = (g_res[0] / psize, g_res[1] / psize)
    px = g_res[0] / 2.0  # unused; kept for clarity
    ppos = [ch["gPos"][i]
            + fmod_glsl(mod_uv[0], probe_positions[0]) * psize / 256.0 * ch["gTan"][i]
            + fmod_glsl(mod_uv[1], probe_positions[1]) * psize / 256.0 * ch["gBit"][i]
            for i in range(3)]
    probe_uv = (math.floor(mod_uv[0] / probe_positions[0]) + 0.5,
                math.floor(mod_uv[1] / probe_positions[1]) + 0.5)
    rel = (probe_uv[0] - psize * 0.5, probe_uv[1] - psize * 0.5)
    thetai = max(abs(rel[0]), abs(rel[1]))
    theta = thetai / psize * THETA_PI
    phi_u = piecewise_phi(rel[0], rel[1], thetai)
    bin_count = 4 + 8.0 * math.floor(thetai)
    phi = phi_u * PI * 2.0 / bin_count
    d_local = (math.sin(phi) * math.sin(theta), math.cos(phi) * math.sin(theta), math.cos(theta))
    pdir = tuple(d_local[0] * ch["gTan"][i] + d_local[1] * ch["gBit"][i] + d_local[2] * ch["gNor"][i]
                 for i in range(3))
    saw = ((math.cos(theta - PI / psize) - math.cos(theta + PI / psize)) / bin_count)
    lw = math.cos(theta)
    reach = cascade_reach(cascade)
    if y < 1536.0:
        physical = (x, y - 256.0 * cascade)
    else:
        physical = (x, 256.0 + y - (1536.0 + 256.0 * cascade))
    bin_index = int(round(phi_u)) % int(bin_count)
    return dict(active=1, chartId=chart_id, materialId=ch["material"], cascade=cascade,
                probeSize=psize, probePos=tuple(ppos), probeDir=pdir,
                thetaIndex=thetai, theta=theta, phi=phi,
                solidAngleWeight=saw, lambertWeight=lw, reach=reach,
                physical=physical, binCount=int(bin_count), binIndex=bin_index)

# ---------------------------------------------------------------------------
# Layout fixtures: boundaries, band transitions, chart edges, inactive regions
# ---------------------------------------------------------------------------

layout_uvs = []
for chart_id, ch in CHARTS.items():
    for cascade in range(6):
        band_y = 256.0 * cascade + (1536.0 if chart_id >= 7 else 0.0)
        bx, by = ch["base"]
        y0 = band_y
        w = ch["gRes"][0]
        for dx, dy, tag in ((0.5, 0.5, "first"), (w - 0.5, 255.5, "last"),
                            (w / 2.0 - 0.5, 127.5, "mid")):
            layout_uvs.append((bx + dx, y0 + dy, f"{ch['name']}_c{cascade}_{tag}"))
# band transitions (primary page, floor chart)
for y, tag in ((255.5, "band_c0_top"), (256.5, "band_c1_bottom"),
               (1535.5, "primary_page_top"), (1536.5, "interior_page_bottom"),
               (3071.5, "domain_top")):
    layout_uvs.append((0.5, y, f"transition_{tag}"))
# inactive regions
for x, y, tag in ((255.5, 1536.5, "interior_page_last_active"),
                  (256.5, 1536.5, "interior_page_inactive_x256"),
                  (700.5, 2000.5, "interior_page_inactive_mid"),
                  (1024.5, 100.5, "outside_domain_x"),
                  (100.5, 3072.5, "outside_domain_y"),
                  (-0.5, 100.5, "outside_domain_negx"),
                  (100.5, -0.5, "outside_domain_negy")):
    layout_uvs.append((x, y, f"inactive_{tag}"))

layout_fixtures = []
for x, y, name in layout_uvs:
    rec = decode((x, y))
    rec["name"] = name
    rec["uv"] = (x, y)
    layout_fixtures.append(rec)

# ---------------------------------------------------------------------------
# Direction fixtures: full cells for small probe sizes, outer rings for large
#
# Reference coupling: angular resolution probeSize^2 x spatial density
# (gRes/probeSize)^2. Direction index (dx, dy) lives at atlas texel
# (dx*probePositions + 0.5, dy*probePositions + 0.5) within the chart/band.
# ---------------------------------------------------------------------------

def cell_positions(psize):
    return [(i + 0.5 - psize * 0.5, j + 0.5 - psize * 0.5)
            for j in range(psize) for i in range(psize)]

def direction_uv(chart_id, cascade, dx, dy):
    ch = CHARTS[chart_id]
    psize = probe_size(cascade)
    probe_positions = (ch["gRes"][0] / psize, ch["gRes"][1] / psize)
    band_y = 256.0 * cascade + (1536.0 if chart_id >= 7 else 0.0)
    return (ch["base"][0] + dx * probe_positions[0] + 0.5,
            band_y + dy * probe_positions[1] + 0.5)

direction_fixtures = []
for psize in (2, 4):
    cascade = int(math.log2(psize)) - 1
    for rx, ry in cell_positions(psize):
        dx = int(rx + psize * 0.5)
        dy = int(ry + psize * 0.5)
        uv = direction_uv(1, cascade, dx, dy)
        rec = decode(uv)
        assert rec["thetaIndex"] == max(abs(rx), abs(ry)), (uv, rec, (rx, ry))
        rec["name"] = f"cell_ps{psize}_rel({rx:+.1f},{ry:+.1f})"
        rec["uv"] = uv
        rec["cellRel"] = (rx, ry)
        direction_fixtures.append(rec)

for psize in (8, 16, 32, 64):
    cascade = int(math.log2(psize)) - 1
    thetai_ring = psize * 0.5 - 0.5
    for rx, ry in cell_positions(psize):
        if max(abs(rx), abs(ry)) != thetai_ring:
            continue
        dx = int(rx + psize * 0.5)
        dy = int(ry + psize * 0.5)
        uv = direction_uv(1, cascade, dx, dy)
        rec = decode(uv)
        assert rec["thetaIndex"] == thetai_ring, (uv, rec, (rx, ry))
        rec["name"] = f"ring_ps{psize}_rel({rx:+.1f},{ry:+.1f})"
        rec["uv"] = uv
        rec["cellRel"] = (rx, ry)
        direction_fixtures.append(rec)

# Interior-page chart coupling proof: direction cell on the interior back
# chart (gRes 128x256) for C1, exercising anisotropic probe positions.
for rx, ry in cell_positions(4):
    dx = int(rx + 2.0)
    dy = int(ry + 2.0)
    uv = direction_uv(8, 1, dx, dy)
    rec = decode(uv)
    assert rec["thetaIndex"] == max(abs(rx), abs(ry)), (uv, rec, (rx, ry))
    rec["name"] = f"cell_iback_ps4_rel({rx:+.1f},{ry:+.1f})"
    rec["uv"] = uv
    rec["cellRel"] = (rx, ry)
    direction_fixtures.append(rec)

# ring bin coverage proof
ring_coverage = []
for psize in (2, 4, 8, 16, 32, 64):
    seen = {}
    for rx, ry in cell_positions(psize):
        thetai = max(abs(rx), abs(ry))
        bin_count = int(4 + 8 * math.floor(thetai))
        phi_u = piecewise_phi(rx, ry, thetai)
        idx = int(round(phi_u)) % bin_count
        key = (thetai, idx)
        if key in seen:
            raise SystemExit(f"duplicate bin {key} in probeSize {psize}")
        seen[key] = (rx, ry)
    rings = {}
    for (thetai, idx) in seen:
        rings.setdefault(thetai, set()).add(idx)
    ring_coverage.append(dict(
        probeSize=psize,
        rings=[dict(thetaIndex=t, binCount=int(4 + 8 * math.floor(t)),
                    covered=len(sorted(idxs)))
               for t, idxs in sorted(rings.items())],
        complete=all(len(idxs) == int(4 + 8 * math.floor(t))
                     for t, idxs in rings.items())))

# hemisphere weight sums per probeSize (double precision)
hemisphere_sums = []
for psize in (2, 4, 8, 16, 32, 64):
    sum_saw = 0.0
    sum_w = 0.0
    for rx, ry in cell_positions(psize):
        thetai = max(abs(rx), abs(ry))
        theta = thetai / psize * THETA_PI
        bin_count = 4 + 8.0 * math.floor(thetai)
        saw = (math.cos(theta - PI / psize) - math.cos(theta + PI / psize)) / bin_count
        lw = math.cos(theta)
        sum_saw += saw
        sum_w += saw * lw
    hemisphere_sums.append(dict(probeSize=psize, solidAngleSum=sum_saw,
                                cosineWeightedSum=sum_w))

# interval fixtures + merge transition table
interval_fixtures = [dict(cascade=c, probeSize=probe_size(c), reach=cascade_reach(c),
                          unbounded=1 if c == 5 else 0) for c in range(6)]

transition_fixtures = []
for c in range(6):
    psize = probe_size(c)
    base = TEXEL * psize * 1.5
    if c == 0:
        min_dist, interval = 0.0, 2.0 * base
    else:
        min_dist, interval = base, base
    samples = []
    for d in (0.0, base * 0.5, base, base * 1.5, base * 2.0, base * 3.0):
        l = 1.0 - min(max((d - min_dist) / interval, 0.0), 1.0)
        samples.append(dict(distance=d, lerp=l))
    transition_fixtures.append(dict(cascade=c, probeSize=psize, base=base,
                                    interpMinDist=min_dist, interpMaxInterval=interval,
                                    samples=samples))

# synthetic blocker distances (ray cast from (0.25, h, 0.25) toward -Y)
blocker_fixtures = []
for c in range(5):
    reach = cascade_reach(c)
    blocker_fixtures.append(dict(cascade=c, reach=reach,
                                 insideDistance=reach * 0.99,
                                 outsideDistance=reach * 1.01))
blocker_fixtures.append(dict(cascade=5, reach=10000.0, insideDistance=0.25,
                             outsideDistance=-1.0,
                             note="C5 reaches the entire parity scene; scene diagonal bound=1.224744871391589"))

# band statistics (expected physical-texture readback per cascade)
band_stats = []
for c in range(6):
    band_stats.append(dict(
        cascade=c, probeSize=probe_size(c), reach=cascade_reach(c),
        activeTexels=(1024 * 256) + (256 * 256),
        inactiveTexels=(1024 - 256) * 256,
        activePrimaryTexels=1024 * 256,
        activeInteriorTexels=256 * 256))

document = {
    "schema_version": "reference-layout-golden-v1",
    "derivation": {
        "cube": "shader_toy/CubeA.glsl:62-192",
        "common": "shader_toy/Common.glsl",
        "plan_chart_table": "doc/10_refactor/3d_radiance_cascades_refactor_plan.md:191-204"
    },
    "constants": {
        "theta_pi_literal": THETA_PI,
        "pi_literal": PI,
        "texel_scale": TEXEL,
        "logical_domain": [LOGICAL_W, LOGICAL_H],
        "physical_per_cascade": [PHYS_W, PHYS_H],
        "cascade_count": 6,
        "c5_reach": 10000.0
    },
    "tolerances": {
        "ids_and_indices": "exact",
        "positions_directions_weights": 2e-5,
        "hemisphere_sum_relative": 1e-3
    },
    "layout_fixtures": layout_fixtures,
    "direction_fixtures": direction_fixtures,
    "ring_coverage": ring_coverage,
    "hemisphere_sums": hemisphere_sums,
    "interval_fixtures": interval_fixtures,
    "transition_fixtures": transition_fixtures,
    "blocker_fixtures": blocker_fixtures,
    "band_stats": band_stats
}

here = os.path.dirname(os.path.abspath(__file__))
json_path = os.path.join(here, "layout_golden_v1.json")
with open(json_path, "w", encoding="utf-8", newline="\n") as f:
    json.dump(document, f, indent=1)

# ---------------------------------------------------------------------------
# C++ constexpr fixture data (consumed by the validator; double precision)
# ---------------------------------------------------------------------------

def f(x):
    s = repr(float(x))
    return s

inc = []
inc.append("// GENERATED by tools/10_refactor/phase3_layout/generate_layout_golden.py")
inc.append("// Independent double-precision fixtures from CubeA.glsl layout semantics.")
inc.append("// Do not edit by hand; regenerate instead.")
inc.append("struct GoldenLayoutFixture { double uvx, uvy; int active, chartId, materialId, cascade, probeSize; double ppx, ppy, ppz, reach, physx, physy; };")
inc.append("struct GoldenDirectionFixture { double uvx, uvy; double ppx, ppy, ppz; double dx, dy, dz; double thetai, theta, phi, saw, lw; int binCount, binIndex; };")
inc.append("struct GoldenIntervalFixture { int cascade, probeSize; double reach; int unbounded; };")
inc.append("struct GoldenTransitionFixture { int cascade; double base, minDist, interval; };")
inc.append("struct GoldenHemisphereSum { int probeSize; double solidAngleSum, cosineWeightedSum; };")
inc.append("struct GoldenBandStat { int cascade, probeSize; double reach; int activeTexels, inactiveTexels; };")

inc.append(f"static const GoldenLayoutFixture kGoldenLayout[{len(layout_fixtures)}] = {{")
for rec in layout_fixtures:
    pp = rec["probePos"]; ph = rec["physical"]
    inc.append("  {" + ", ".join([
        f(rec["uv"][0]), f(rec["uv"][1]), str(rec["active"]), str(rec["chartId"]),
        str(rec["materialId"]), str(rec["cascade"]), str(rec["probeSize"]),
        f(pp[0]), f(pp[1]), f(pp[2]), f(rec["reach"]), f(ph[0]), f(ph[1])]) + "},")
inc.append("};")

inc.append(f"static const GoldenDirectionFixture kGoldenDirection[{len(direction_fixtures)}] = {{")
for rec in direction_fixtures:
    pp = rec["probePos"]; d = rec["probeDir"]
    inc.append("  {" + ", ".join([
        f(rec["uv"][0]), f(rec["uv"][1]), f(pp[0]), f(pp[1]), f(pp[2]),
        f(d[0]), f(d[1]), f(d[2]), f(rec["thetaIndex"]), f(rec["theta"]),
        f(rec["phi"]), f(rec["solidAngleWeight"]), f(rec["lambertWeight"]),
        str(rec["binCount"]), str(rec["binIndex"])]) + "},")
inc.append("};")

inc.append(f"static const GoldenIntervalFixture kGoldenIntervals[{len(interval_fixtures)}] = {{")
for rec in interval_fixtures:
    inc.append("  {%d, %d, %s, %d}," % (rec["cascade"], rec["probeSize"],
                                        f(rec["reach"]), rec["unbounded"]))
inc.append("};")

inc.append(f"static const GoldenTransitionFixture kGoldenTransitions[{len(transition_fixtures)}] = {{")
for rec in transition_fixtures:
    inc.append("  {%d, %s, %s, %s}," % (rec["cascade"], f(rec["base"]),
                                        f(rec["interpMinDist"]), f(rec["interpMaxInterval"])))
inc.append("};")

inc.append(f"static const GoldenHemisphereSum kGoldenHemisphere[{len(hemisphere_sums)}] = {{")
for rec in hemisphere_sums:
    inc.append("  {%d, %s, %s}," % (rec["probeSize"], f(rec["solidAngleSum"]),
                                    f(rec["cosineWeightedSum"])))
inc.append("};")

inc.append(f"static const GoldenBandStat kGoldenBandStats[{len(band_stats)}] = {{")
for rec in band_stats:
    inc.append("  {%d, %d, %s, %d, %d}," % (rec["cascade"], rec["probeSize"],
                                            f(rec["reach"]), rec["activeTexels"],
                                            rec["inactiveTexels"]))
inc.append("};")

src_inc = os.path.join(here, "..", "..", "..", "src", "reference_layout_golden.inc")
with open(os.path.abspath(src_inc), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(inc) + "\n")

print(f"layout fixtures:    {len(layout_fixtures)}")
print(f"direction fixtures: {len(direction_fixtures)}")
print(f"ring coverage complete: {all(r['complete'] for r in ring_coverage)}")
print(f"wrote {json_path}")
print(f"wrote {os.path.abspath(src_inc)}")
