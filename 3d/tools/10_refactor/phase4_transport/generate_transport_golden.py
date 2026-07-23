#!/usr/bin/env python3
"""Generate independent golden fixtures for Phase 4 local single-cascade
transport (gates G5 payload contract, G8 material/direct-light transport).

Derivation source:
  shader_toy/Common.glsl:43-205  (scene, sky/sun, trace helpers, TraceRay)
  shader_toy/CubeA.glsl:150-192  (local transport, weights, payload semantics)

All values computed in IEEE-754 double precision. The C++ oracle and the GLSL
transport shader must agree with these fixtures within recorded tolerances.
Temporal feedback B(hit) is disabled in Phase 4 (zero), matching the plan's
Phase 4 scope; upper merge is not implemented.

Outputs:
  tools/10_refactor/phase4_transport/transport_golden_v1.json
  src/reference_transport_golden.inc
"""

import json
import math
import os

THETA_PI = 3.14192653
PI = 3.141592653
TEXEL = 1.0 / 256.0
REF_TIME = 0.0

# ---------------------------------------------------------------------------
# Common.glsl port (double precision)
# ---------------------------------------------------------------------------

def v_add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def v_sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def v_mul(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def v_dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def v_len(a): return math.sqrt(v_dot(a, a))
def v_norm(a):
    l = v_len(a)
    return (a[0]/l, a[1]/l, a[2]/l)

def get_sky_light(d):
    s = 1.0 - d[1]*0.5
    return (0.7*s, 0.8*s, 1.0*s)

def get_sun_light(t):
    return (2.5, 2.25, 1.625)

def get_sun_direction(t):
    nt = 1.0 + t*0.2
    return v_norm((-math.sin(nt*2.4), 1.0, -math.cos(nt*2.4)))

def rotate2(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0]*c - p[1]*s, p[0]*s + p[1]*c)

def repeat2(p, n):
    ang = 2.0*3.141592653/n
    sector = math.floor(math.atan2(p[0], p[1])/ang + 0.5)
    return rotate2(p, sector*ang)

def df_box2(px, py, bx, by):
    dx = abs(px - bx*0.5) - bx*0.5
    dy = abs(py - by*0.5) - by*0.5
    return min(max(dx, dy), 0.0) + math.hypot(max(dx, 0.0), max(dy, 0.0))

def interior_intersection(p):
    if math.hypot(p[0]-0.5, p[1]-0.0) < 0.25: return True
    if math.hypot(p[0]-0.87, p[1]-0.25) < 0.12: return True
    return False

def df_intersection(p, t):
    nt = 1.0 + t*0.2
    cx = 0.21 + (math.sin(nt)*0.5 + 0.5)*0.58
    cz = 0.21 + (math.cos(nt)*0.5 + 0.5)*0.58
    rp = v_sub(p, (cx, 0.5, cz))
    rep = repeat2((rp[0], rp[2]), 8.0)
    r = math.hypot(rp[0], rp[2])
    if (p[1] > 0.49 and abs(p[2]-0.5) > 0.04 and r < 0.2 and abs(r-0.1375) > 0.01
            and df_box2(rep[0]+0.01, rep[1]-0.015, 0.02, 0.3) > 0.0):
        return True
    return False

def a_quad(p, d, v_tan, v_bit, v_nor, p_size):
    nor_dot = v_dot(v_nor, d)
    p_dot = v_dot(v_nor, p)
    prod = nor_dot * p_dot
    sgn = -1.0 if prod < 0 else (1.0 if prod > 0 else 0.0)
    if sgn < -0.5:
        t = -p_dot / nor_dot
        hp = v_add(p, v_mul(d, t))
        hit2 = (v_dot(hp, v_tan), v_dot(hp, v_bit))
        if df_box2(hit2[0], hit2[1], p_size[0], p_size[1]) <= 0.0:
            return (hit2[0], hit2[1], t)
    return (-1.0, -1.0, -1.0)

def a_sphere(p, d, r):
    a = v_dot(p, p) - r*r
    b = 2.0*v_dot(p, d)
    re = b*b*0.25 - a
    if v_dot(p, d) < 0.0 and re > 0.0:
        return -b*0.5 - math.sqrt(re)
    return -1.0

def a_cyl_z(p, d, r):
    dxy2 = d[0]*d[0] + d[1]*d[1]
    if dxy2 == 0.0:
        return -1.0  # NaN path in GLSL compares false; equivalent here
    a = ((p[0]*p[0] + p[1]*p[1]) - r*r)/dxy2
    b = 2.0*(p[0]*d[0] + p[1]*d[1])/dxy2
    re = b*b*0.25 - a
    if re > 0.0:
        return -b*0.5 + math.sqrt(re)  # reference keeps the +sqrt root
    return -1.0

def a_box_normal(origin, idir, bmin, bmax):
    t_min = tuple((bmin[i]-origin[i])*idir[i] for i in range(3))
    t_max = tuple((bmax[i]-origin[i])*idir[i] for i in range(3))
    t1 = tuple(min(t_min[i], t_max[i]) for i in range(3))
    t2 = tuple(max(t_min[i], t_max[i]) for i in range(3))
    sign_dir = tuple(-(max(0.0, 1.0 if idir[i] > 0 else (-1.0 if idir[i] < 0 else 0.0))*2.0 - 1.0) for i in range(3))
    if t1[0] > max(t1[1], t1[2]):
        n = (sign_dir[0], 0.0, 0.0)
    elif t1[1] > t1[2]:
        n = (0.0, sign_dir[1], 0.0)
    else:
        n = (0.0, 0.0, sign_dir[2])
    return (max(t1[0], max(t1[1], t1[2])), min(t2[0], min(t2[1], t2[2])), n)

Z_MIN = 0.47 - TEXEL
Z_MAX = 0.53 - TEXEL

MAT_DIFFUSE = 0
MAT_BLACK = 1
MAT_REFLECTIVE = 2
MAT_EMISSIVE = 3
MAT_SKY = 4

def trace_ray(p, d, maxt, time):
    """Direct Common.glsl:140-205 port. Returns hit record dict."""
    h = dict(t=maxt, hit=False, chart=0, uv=(-1.0, -1.0), n=(-20.0,)*3,
             kind=MAT_SKY, response=(0.0,)*3, emission=(0.0,)*3)

    def take(t, uv, chart, n, kind, response, emission=(0.0,)*3):
        if t > -0.5 and t < h["t"]:
            h.update(t=t, hit=True, chart=chart, uv=uv, n=n, kind=kind,
                     response=response, emission=emission)

    # Floor / ceiling (animated exclusion applies)
    uvt = a_quad(p, d, (1,0,0), (0,0,1), (0,1,0), (1.0, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 1, (0,1,0), MAT_DIFFUSE, (0.9,)*3)
    uvt = a_quad(v_sub(p, (0,0.5,0)), d, (1,0,0), (0,0,1), (0,1,0), (1.0, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 2, (0,-1,0), MAT_DIFFUSE, (0.9,)*3)
    # X walls
    uvt = a_quad(p, d, (0,1,0), (0,0,1), (1,0,0), (0.5, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 3, (1,0,0), MAT_DIFFUSE, (0.9, 0.1, 0.1))
    uvt = a_quad(v_sub(p, (1,0,0)), d, (0,1,0), (0,0,1), (-1,0,0), (0.5, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 4, (-1,0,0), MAT_DIFFUSE, (0.05, 0.95, 0.1))
    # Z walls
    uvt = a_quad(p, d, (0,1,0), (1,0,0), (0,0,-1), (0.5, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 5, (0,0,1), MAT_DIFFUSE, (0.9,)*3)
    uvt = a_quad(v_sub(p, (0,0,1)), d, (0,1,0), (1,0,0), (0,0,-1), (0.5, 1.0))
    if uvt[2] > -0.5 and not df_intersection(v_add(p, v_mul(d, uvt[2])), time):
        take(uvt[2], uvt[:2], 6, (0,0,-1), MAT_DIFFUSE, (0.9,)*3)
    # Interior wall (openings exclude)
    uvt = a_quad(v_sub(p, (0,0,Z_MIN)), d, (0,1,0), (1,0,0), (0,0,-1), (0.5, 1.0))
    if uvt[2] > -0.5 and not interior_intersection(v_add(p, v_mul(d, uvt[2]))):
        take(uvt[2], uvt[:2], 7, (0,0,-1), MAT_DIFFUSE, (0.99,)*3)
    uvt = a_quad(v_sub(p, (0,0,Z_MAX)), d, (0,1,0), (1,0,0), (0,0,-1), (0.5, 1.0))
    if uvt[2] > -0.5 and not interior_intersection(v_add(p, v_mul(d, uvt[2]))):
        take(uvt[2], uvt[:2], 8, (0,0,1), MAT_DIFFUSE, (0.99,)*3)
    # Black uncharted cylinders
    for cx, cy, r in ((0.5, 0.0, 0.25), (0.87, 0.25, 0.12)):
        sp = v_sub(p, (cx, cy, 0.0))
        st = a_cyl_z(sp, d, r)
        z = sp[2] + d[2]*st
        if st > 0.0 and st < h["t"] and Z_MIN <= z <= Z_MAX:
            rn = v_norm((sp[0]+d[0]*st, sp[1]+d[1]*st, 0.0))
            take(st, (1.0, 1.0), 0, (-rn[0], -rn[1], 0.0), MAT_BLACK, (0.0,)*3)
    # Mirror sphere
    sp = v_sub(p, (0.15, 0.1005, 0.3))
    st = a_sphere(sp, d, 0.1)
    if st > -0.5:
        take(st, (1.0, 1.0), 0, v_norm(v_add(sp, v_mul(d, st))), MAT_REFLECTIVE, (0.0,)*3)
    # Mirror box
    sp = v_sub(p, (0.86, 0.14, 0.86))
    sd = v_norm(d)
    idir = tuple(1.0/sd[i] if sd[i] != 0.0 else float("inf") for i in range(3))
    bb = a_box_normal(sp, idir, (-0.08,)*3, (0.08,)*3)
    if bb[0] > 0.0 and bb[1] > bb[0]:
        take(bb[0], (1.0, 1.0), 0, v_norm(bb[2]), MAT_REFLECTIVE, (0.0,)*3)
    return h

# ---------------------------------------------------------------------------
# CubeA.glsl local transport (B=0 in Phase 4)
# ---------------------------------------------------------------------------

def weight(theta_index, probe_size):
    theta = theta_index/probe_size*THETA_PI
    bin_count = 4.0 + 8.0*math.floor(theta_index)
    saw = ((math.cos(theta - PI/probe_size) - math.cos(theta + PI/probe_size))
           / bin_count)
    return saw*math.cos(theta), theta, saw, math.cos(theta)

def shade_local(origin, direction, maxt, theta_index, probe_size, time=REF_TIME):
    d = v_norm(direction)
    h = trace_ray(origin, d, maxt, time)
    W, theta, saw, lw = weight(theta_index, probe_size)
    if not h["hit"]:
        return dict(rgb=v_mul(get_sky_light(d), W), alpha=-1.0, category="sky",
                    trace=h, W=W)
    pos = v_add(origin, v_mul(d, h["t"]))
    if h["kind"] == MAT_REFLECTIVE:
        rgb = (0.0, 0.0, 0.0)
        category = "reflective"
    elif h["kind"] == MAT_EMISSIVE:
        rgb = h["emission"]
        category = "emissive"
    elif h["kind"] == MAT_BLACK:
        rgb = (0.0, 0.0, 0.0)
        category = "black_uncharted"
    else:
        category = "diffuse_backface"
        rgb = (0.0, 0.0, 0.0)
        if v_dot(h["n"], d) < 0.0:
            category = "diffuse_frontface"
            direct = (0.0, 0.0, 0.0)
            sun_dir = get_sun_direction(time)
            ndl = v_dot(h["n"], sun_dir)
            if ndl > 0.0:
                s_pos = v_add(pos, v_mul(h["n"], 0.001))
                shadow = trace_ray(s_pos, sun_dir, 10000.0, time)
                if not shadow["hit"]:
                    direct = v_mul(get_sun_light(time), ndl)
            rgb = (direct[0]*h["response"][0], direct[1]*h["response"][1],
                   direct[2]*h["response"][2])
    return dict(rgb=v_mul(rgb, W), alpha=h["t"], category=category, trace=h,
                W=W, position=pos)

# ---------------------------------------------------------------------------
# Fixture set: every locked material/hit category + sun visibility + weights
# ---------------------------------------------------------------------------

fixtures = []

def add_ray(name, origin, direction, maxt, theta_index, probe_size, expect_category):
    s = shade_local(origin, direction, maxt, theta_index, probe_size)
    fixtures.append(dict(
        name=name, type="trace",
        origin=origin, direction=v_norm(direction), maxT=maxt,
        thetaIndex=theta_index, probeSize=probe_size,
        expect=dict(category=expect_category, rgb=s["rgb"], alpha=s["alpha"],
                    W=s["W"], chart=s["trace"]["chart"] if s["trace"]["hit"] else 0,
                    sunOccluded=(expect_category == "diffuse_frontface_occluded"))))
    assert s["category"].startswith(expect_category.split("_occluded")[0]) or \
        expect_category == "diffuse_frontface_occluded", (name, s["category"])

SUN = get_sun_direction(REF_TIME)

# The reference room is fully enclosed; direct sun reaches a surface only
# when the shadow ray escapes through the animated ceiling exclusion blades.
# Deterministically scan for genuinely lit points (never assume them).
def scan_lit(surface, nx, nz):
    """surface: 'floor'|'x1'|'z0'|'x0'. Returns (origin, direction) or None."""
    for iy in range(nz):
        for ix in range(nx):
            u = (ix + 0.5) / nx
            v = (iy + 0.5) / nz
            if surface == "floor":
                origin, direction = (u, 0.4, v), (0, -1, 0)
            elif surface == "x1":
                origin, direction = (0.6, v, u), (1, 0, 0)
            elif surface == "z0":
                origin, direction = (u, v, 0.4), (0, 0, -1)
            else:
                origin, direction = (0.4, v, u), (-1, 0, 0)
            s = shade_local(origin, direction, 1.0, 0.5, 2)
            if s["category"] == "diffuse_frontface" and max(s["rgb"]) > 0.0:
                return origin, direction, s
    return None

# Sky miss (payload: negative alpha, environment radiance)
add_ray("sky_miss_short_interval", (0.25, 0.25, 0.25), (0, -1, 0), 0.1, 0.5, 2, "sky")

# Diffuse frontface lit (floor, shadow ray escapes through exclusion blades)
lit_floor = scan_lit("floor", 20, 20)
assert lit_floor is not None, "no sunlit floor point found"
o, d, f = lit_floor
fixtures.append(dict(name="floor_frontface_lit", type="trace",
    origin=o, direction=d, maxT=1.0, thetaIndex=0.5, probeSize=2,
    expect=dict(category="diffuse_frontface", rgb=f["rgb"], alpha=f["alpha"],
                W=f["W"], chart=1, sunOccluded=False)))

# Diffuse frontface occluded (floor point whose shadow ray hits the ceiling)
occ_floor = None
for ix in range(20):
    for iz in range(20):
        o = ((ix + 0.5)/20, 0.4, (iz + 0.5)/20)
        s = shade_local(o, (0, -1, 0), 1.0, 0.5, 2)
        if s["category"] == "diffuse_frontface" and max(s["rgb"]) == 0.0:
            occ_floor = (o, (0, -1, 0), s)
            break
    if occ_floor: break
assert occ_floor is not None
o, d, f = occ_floor
fixtures.append(dict(name="floor_frontface_occluded", type="trace",
    origin=o, direction=d, maxT=1.0, thetaIndex=0.5, probeSize=2,
    expect=dict(category="diffuse_frontface", rgb=f["rgb"], alpha=f["alpha"],
                W=f["W"], chart=1, sunOccluded=True)))

# Green wall identity: sun-facing normal (-X); requires a lit point.
lit_x1 = scan_lit("x1", 20, 20)
if lit_x1 is not None:
    o, d, f = lit_x1
    assert f["rgb"][1] > f["rgb"][0] * 5.0
    fixtures.append(dict(name="wall_x1_green_identity_lit", type="trace",
        origin=o, direction=d, maxT=1.0, thetaIndex=0.5, probeSize=2,
        expect=dict(category="diffuse_frontface", rgb=f["rgb"], alpha=f["alpha"],
                    W=f["W"], chart=4, sunOccluded=False)))
green_lit = lit_x1 is not None

# Red wall frontface, sun-facing away (dot(+X, sunDir) < 0 -> zero)
f = shade_local((0.25, 0.25, 0.25), (-1, 0, 0), 1.0, 0.5, 2)
assert f["category"] == "diffuse_frontface" and max(f["rgb"]) == 0.0
fixtures.append(dict(name="wall_x0_red_unlit", type="trace",
    origin=(0.25, 0.25, 0.25), direction=(-1, 0, 0), maxT=1.0,
    thetaIndex=0.5, probeSize=2,
    expect=dict(category="diffuse_frontface", rgb=f["rgb"], alpha=f["alpha"],
                W=f["W"], chart=3, sunOccluded=False)))
# Diffuse backface (floor seen from below)
add_ray("floor_backface", (0.25, -0.25, 0.25), (0, 1, 0), 1.0, 0.5, 2, "diffuse_backface")
# Interior backface (front wall viewed from behind)
add_ray("interior_front_backface", (0.25, 0.25, Z_MIN + 0.03), (0, 0, -1), 1.0, 0.5, 2, "diffuse_backface")
# Black uncharted cylinder (far-side root quirk preserved)
f = shade_local((0.3, 0.1, 0.496), (1, 0, 0), 1.0, 0.5, 2)
assert f["category"] == "black_uncharted" and f["alpha"] > 0.4
fixtures.append(dict(name="black_cylinder_hits", type="trace",
    origin=(0.3, 0.1, 0.496), direction=(1, 0, 0), maxT=1.0,
    thetaIndex=0.5, probeSize=2,
    expect=dict(category="black_uncharted", rgb=f["rgb"], alpha=f["alpha"],
                W=f["W"], chart=0, sunOccluded=False)))
# Reflective sphere / box (zero contribution, distance alpha)
add_ray("reflective_sphere", (0.15, 0.1005, 0.0), (0, 0, 1), 1.0, 0.5, 2, "reflective")
add_ray("reflective_box", (0.86, 0.14, 0.5), (0, 0, 1), 1.0, 0.5, 2, "reflective")
# Weight coupling: identical lit ray at two probe resolutions/thetas
lit_o, lit_d, _ = lit_floor
add_ray("floor_lit_w_ps4_t15", lit_o, lit_d, 1.0, 1.5, 4, "diffuse_frontface")
add_ray("floor_lit_w_ps8_t35", lit_o, lit_d, 1.0, 3.5, 8, "diffuse_frontface")

print(f"green wall lit point found: {green_lit}")

# Emissive (synthetic category: Common.glsl defines no emissive geometry)
emission = (2.0, 1.5, 1.0)
W, theta, saw, lw = weight(0.5, 2)
fixtures.append(dict(name="emissive_synthetic", type="synthetic",
    hit=dict(distance=0.3, normal=(0, 1, 0), materialKind=MAT_EMISSIVE,
             response=emission, chart=0),
    direction=(0, -1, 0), thetaIndex=0.5, probeSize=2,
    expect=dict(category="emissive", rgb=v_mul(emission, W), alpha=0.3, W=W,
                chart=0, sunOccluded=False)))

document = {
    "schema_version": "reference-transport-golden-v1",
    "payload_schema": "ReferenceSurfaceTexelV1",
    "derivation": {
        "common": "shader_toy/Common.glsl:43-205",
        "cube": "shader_toy/CubeA.glsl:150-192",
        "phase_scope": "local transport only: B(hit)=0, no upper merge"
    },
    "constants": {
        "reference_time": REF_TIME,
        "sun_direction": SUN,
        "sun_radiance": get_sun_light(REF_TIME),
        "sky_base": (0.7, 0.8, 1.0),
        "sky_y_scale": 0.5,
        "theta_pi_literal": THETA_PI,
        "pi_literal": PI,
        "shadow_bias": 0.001,
        "shadow_max_distance": 10000.0
    },
    "tolerances": {
        "ids": "exact",
        "alpha_distance": 0.0001,
        "rgb_absolute": 0.0005
    },
    "fixtures": fixtures
}

here = os.path.dirname(os.path.abspath(__file__))
json_path = os.path.join(here, "transport_golden_v1.json")
with open(json_path, "w", encoding="utf-8", newline="\n") as f:
    json.dump(document, f, indent=1)

# C++ constexpr fixture data
def f(x): return repr(float(x))

inc = []
inc.append("// GENERATED by tools/10_refactor/phase4_transport/generate_transport_golden.py")
inc.append("// Independent double-precision fixtures from Common.glsl + CubeA.glsl local transport.")
inc.append("struct GoldenTransportFixture {")
inc.append("  const char* name;")
inc.append("  int type;              // 0 = trace ray, 1 = synthetic hit")
inc.append("  double ox, oy, oz;     // trace origin / unused for synthetic")
inc.append("  double dx, dy, dz;     // ray/probe direction")
inc.append("  double maxT;")
inc.append("  double thetaIndex, probeSize;")
inc.append("  int materialKind;      // synthetic: material kind; trace: 9 = derive")
inc.append("  double hx, hy, hz;     // synthetic hit normal")
inc.append("  double ex, ey, ez;     // synthetic emission/response")
inc.append("  double hdist;          // synthetic hit distance")
inc.append("  int expectedCategory;  // 0 diffuse,1 black,2 reflective,3 emissive,4 sky,5 diffuse_backface")
inc.append("  int expectedChart;")
inc.append("  int sunOccluded;")
inc.append("  double er, eg, eb;     // expected rgb")
inc.append("  double ealpha;         // expected alpha")
inc.append("  double eW;             // expected weight")
inc.append("};")

CAT = {"diffuse_frontface": 0, "black_uncharted": 1, "reflective": 2,
       "emissive": 3, "sky": 4, "diffuse_backface": 5}

inc.append(f"static const GoldenTransportFixture kGoldenTransport[{len(fixtures)}] = {{")
for fx in fixtures:
    e = fx["expect"]
    if fx["type"] == "trace":
        inc.append("  {" + ", ".join([
            f'"{fx["name"]}"', "0",
            f(fx["origin"][0]), f(fx["origin"][1]), f(fx["origin"][2]),
            f(fx["direction"][0]), f(fx["direction"][1]), f(fx["direction"][2]),
            f(fx["maxT"]), f(fx["thetaIndex"]), f(fx["probeSize"]),
            "9", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0",
            str(CAT[e["category"]]), str(e["chart"]), "1" if e["sunOccluded"] else "0",
            f(e["rgb"][0]), f(e["rgb"][1]), f(e["rgb"][2]),
            f(e["alpha"]), f(e["W"])]) + "},")
    else:
        h = fx["hit"]
        inc.append("  {" + ", ".join([
            f'"{fx["name"]}"', "1",
            "0.0", "0.0", "0.0",
            f(fx["direction"][0]), f(fx["direction"][1]), f(fx["direction"][2]),
            "10000.0", f(fx["thetaIndex"]), f(fx["probeSize"]),
            str(h["materialKind"]),
            f(h["normal"][0]), f(h["normal"][1]), f(h["normal"][2]),
            f(h["response"][0]), f(h["response"][1]), f(h["response"][2]),
            f(h["distance"]),
            str(CAT[e["category"]]), str(e["chart"]), "0",
            f(e["rgb"][0]), f(e["rgb"][1]), f(e["rgb"][2]),
            f(e["alpha"]), f(e["W"])]) + "},")
inc.append("};")

src_inc = os.path.join(here, "..", "..", "..", "src", "reference_transport_golden.inc")
with open(os.path.abspath(src_inc), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(inc) + "\n")

print(f"transport fixtures: {len(fixtures)}")
print(f"wrote {json_path}")
print(f"wrote {os.path.abspath(src_inc)}")
