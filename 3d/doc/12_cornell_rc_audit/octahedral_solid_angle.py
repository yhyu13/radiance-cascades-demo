#!/usr/bin/env python3
"""A2 gate: octahedral per-bin solid-angle verification.

Reproduces the shader's dirToOct/octToDir mapping (radiance_3d.comp:175-199,
raymarch.frag:358-368) and numerically integrates the per-bin solid angle to
verify the physical invariant:

    sum_b DeltaOmega_b ~= 4*pi   (full sphere)
    sum_b DeltaOmega_b ~= 2*pi   (front hemisphere, n.z >= 0)

The shader's UV lives in [0,1]^2 (mapping to the [-1,1]^2 octahedral square),
so per-bin planar area is 1/D^2 in shader-UV, and the octahedral projection is
NOT area-preserving: per-bin solid angle varies ~1x .. ~5.2x.  This script
produces the per-bin DeltaOmega weight table the consumer fix needs.
"""
import math


def oct_to_dir(uv):
    """Mirror of radiance_3d.comp:184-189 (uv in [0,1]^2)."""
    u = uv[0] * 2.0 - 1.0
    v = uv[1] * 2.0 - 1.0
    d = [u, v, 1.0 - abs(u) - abs(v)]
    if d[2] < 0.0:
        ox, oy = d[0], d[1]
        d[0] = (1.0 - abs(oy)) * (1.0 if ox >= 0.0 else -1.0)
        d[1] = (1.0 - abs(ox)) * (1.0 if oy >= 0.0 else -1.0)
    ln = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return (d[0] / ln, d[1] / ln, d[2] / ln)


def jacobian(uv, h=1e-5):
    """|dn/du x dn/dv| in shader-UV space (sphere area element)."""
    def tangent(axis):
        p = [uv[0], uv[1]]
        m = [uv[0], uv[1]]
        p[axis] += h
        m[axis] -= h
        np_ = oct_to_dir(p)
        nm = oct_to_dir(m)
        return ((np_[0] - nm[0]) / (2.0 * h),
                (np_[1] - nm[1]) / (2.0 * h),
                (np_[2] - nm[2]) / (2.0 * h))

    du = tangent(0)
    dv = tangent(1)
    cr = (du[1] * dv[2] - du[2] * dv[1],
          du[2] * dv[0] - du[0] * dv[2],
          du[0] * dv[1] - du[1] * dv[0])
    return math.sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2])


def bin_solid_angle(dx, dy, D, S=24):
    """Integrate J over the bin; return (total, front-hemisphere) solid angle."""
    acc = 0.0
    acc_hemi = 0.0
    for i in range(S):
        for j in range(S):
            uv = ((dx + (i + 0.5) / S) / D, (dy + (j + 0.5) / S) / D)
            n = oct_to_dir(uv)
            jac = jacobian(uv)
            acc += jac
            if n[2] >= 0.0:
                acc_hemi += jac
    area = 1.0 / (S * S * D * D)
    return acc * area, acc_hemi * area


def main():
    print("Octahedral per-bin solid angle (shader dirToOct/octToDir)")
    print("=" * 68)
    for D in (4, 8, 16):
        total = 0.0
        hemi = 0.0
        lo = 1e30
        hi = -1e30
        for dx in range(D):
            for dy in range(D):
                da, dhemi = bin_solid_angle(dx, dy, D)
                total += da
                hemi += dhemi
                if da < lo:
                    lo = da
                if da > hi:
                    hi = da
        print("D=%-2d  total=%.6f (4pi=%.6f, err=%.2e)  hemi=%.6f (2pi=%.6f, err=%.2e)"
              % (D, total, 4 * math.pi, abs(total - 4 * math.pi),
                 hemi, 2 * math.pi, abs(hemi - 2 * math.pi)))
        print("        per-bin min=%.6f max=%.6f ratio=%.3f  (uniform 1/D^2*4pi=%.6f)"
              % (lo, hi, hi / lo, 4 * math.pi / (D * D)))
        # constant-radiance Lambert check: integral of cos^+ over hemisphere = pi
        lam = 0.0
        for dx in range(D):
            for dy in range(D):
                for i in range(24):
                    for j in range(24):
                        uv = ((dx + (i + 0.5) / 24) / D, (dy + (j + 0.5) / 24) / D)
                        n = oct_to_dir(uv)
                        if n[2] >= 0.0:
                            lam += jacobian(uv) * n[2]
        lam *= 1.0 / (24 * 24 * D * D)
        print("        int cos^+ dOmega (hemi, expect pi=%.6f) = %.6f (err=%.2e)"
              % (math.pi, lam, abs(lam - math.pi)))
    print("=" * 68)
    print("Invariant check: total ~= 4pi, hemi ~= 2pi, int cos^+ ~= pi.")


if __name__ == "__main__":
    main()
