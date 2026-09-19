#!/usr/bin/env python3
"""Lane MAP (rung 2): geometric module-map generator for displaced tracks.

A lower module A is connected to a lower module B when some helix of the family
    pT >= PTMIN,  |d0| <= D0,  produced at |z| <= ZV on a cylinder of radius rho in [0, min(VMAX, VFRAC * r_A)],  |cot(theta)| <= COTMAX
can pass through both.  Implementation: nine points on A (corners, edge mid-points, centre) x the corner values of
(curvature, d0) x the extreme cot(theta) are propagated to the surface of B (barrel: the cylinder through B's centre;
endcap: the plane through B's centre); B is connected if the (phi, z) or (phi, r) bounding box of the landing points
overlaps B's own bounding box (from its four corners) within the margins.
Layer steps generated: B_n -> B_n+1, B_n+2 ; B_n -> E1, E2 ; E_n -> E_n+1, E_n+2.   (STEPS below)
The result is the UNION with the stock map, stock connections first and in stock order.

usage: mapgen.py <sensor_corners.txt> <stock map .bin> <out .bin> [key=value ...]
WRITES only under ladder/maps/ or ladder/rungs/r2/map/.
"""
import os
import struct
import sys

import numpy as np

LAD = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/ladder"
sys.path.insert(0, os.path.join(LAD, "rungs/r0/ceiling/scripts"))
import lstgeom  # noqa: E402

PAR = dict(PTMIN=0.8, D0=16.0, ZV=30.0, VMAX=0.0, VFRAC=0.5, COTMAX=6.0, MPHI=0.005, MLONG=0.5, SKIP=1, ESKIP=1, BE2=1, NG=3, EDGE=10.0)
KB = 0.00299792458 * 3.8


def load_corners(path):
    rows = []
    for line in open(path):
        if line.startswith("#"):
            continue
        v = line.split()
        rows.append([float(x) for x in v])
    a = np.array(rows)
    det = a[:, 0].astype(np.int64)
    cor = a[:, 11:23].reshape(-1, 4, 3)       # (z, x, y) per corner
    return det, a, cor


def path_len(r, k, d0):
    """transverse path length from the point of closest approach to radius r (nan if not reached)"""
    with np.errstate(invalid="ignore", divide="ignore"):
        q = (r * r - d0 * d0) / (1.0 + k * d0)
        arg = 0.5 * np.abs(k) * np.sqrt(np.maximum(q, 0.0))
        s_curv = 2.0 / np.abs(k) * np.arcsin(arg)
        s = np.where(np.abs(k) < 1e-9, np.sqrt(np.maximum(q, 0.0)), s_curv)
        s = np.where((q < 0) | (arg > 1.0), np.nan, s)
    return s


def xy_at(s, k, d0):
    """position for a track leaving the PCA (0, d0) along +x with signed curvature k"""
    with np.errstate(invalid="ignore", divide="ignore"):
        ks = k * s
        x = np.where(np.abs(k) < 1e-9, s, np.sin(ks) / k)
        y = np.where(np.abs(k) < 1e-9, d0, d0 + (1.0 - np.cos(ks)) / k)
    return x, y


def wrap(a):
    return (a + np.pi) % (2 * np.pi) - np.pi


def main():
    corners_path, stock_path, out = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
    assert "/displaced_ref/ladder/maps/" in out or "/displaced_ref/ladder/rungs/r2/map/" in out, "REFUSING " + out
    par = dict(PAR)
    for kv in sys.argv[4:]:
        k, v = kv.split("=")
        par[k] = type(PAR[k])(float(v))
    print("parameters:", par)
    det, a, cor = load_corners(corners_path)
    p = lstgeom.parse(det)
    low = p["isLower"].astype(bool)
    det, cor, lay = det[low], cor[low], p["loglayer"][low]
    print("lower modules:", len(det), " CMSSW lower flag agrees:", (a[low, 6] == 1).mean())
    nm = len(det)
    cz, cx, cy = cor[:, :, 0], cor[:, :, 1], cor[:, :, 2]
    cen = cor.mean(axis=1)
    rc = np.hypot(cen[:, 1], cen[:, 2]); zc = cen[:, 0]; phic = np.arctan2(cen[:, 2], cen[:, 1])
    crn_r = np.hypot(cx, cy)
    # sample points on every module: bilinear NG x NG grid (corner order from CMSSW is cyclic)
    ng = int(par["NG"])
    u = np.linspace(0, 1, ng)
    pts = []
    for uu in u:
        for vv in u:
            w = np.array([(1 - uu) * (1 - vv), uu * (1 - vv), uu * vv, (1 - uu) * vv])
            pts.append(np.einsum("c,mcx->mx", w, cor))
    pts = np.stack(pts, axis=1)               # [nm, npt, 3] (z, x, y)
    pr = np.hypot(pts[:, :, 1], pts[:, :, 2]); pz = pts[:, :, 0]
    pphi = wrap(np.arctan2(pts[:, :, 2], pts[:, :, 1]) - phic[:, None])
    # helix corner values
    kmax = KB / par["PTMIN"]
    hk, hd = np.meshgrid(np.array([-kmax, 0.0, kmax]), np.array([-par["D0"], 0.0, par["D0"]]))
    hk, hd = hk.ravel()[None, None, :], hd.ravel()[None, None, :]
    R1 = pr[:, :, None]; Z1 = pz[:, :, None]
    s1 = path_len(R1, hk, hd)
    x1, y1 = xy_at(s1, hk, hd); psi1 = np.arctan2(y1, x1)
    # cot(theta) range of the family at every (point, helix)
    rho = np.minimum(par["VMAX"], par["VFRAC"] * R1)
    sv = np.where(rho > np.abs(hd), path_len(np.maximum(rho, np.abs(hd) + 1e-6), hk, hd), 0.0)
    sv = np.nan_to_num(sv, nan=0.0)
    with np.errstate(invalid="ignore", divide="ignore"):
        c = np.stack([(Z1 - par["ZV"]) / s1, (Z1 + par["ZV"]) / s1, (Z1 - par["ZV"]) / (s1 - sv), (Z1 + par["ZV"]) / (s1 - sv)])
    cot_lo = np.clip(np.nanmin(c, axis=0), -par["COTMAX"], par["COTMAX"])
    cot_hi = np.clip(np.nanmax(c, axis=0), -par["COTMAX"], par["COTMAX"])

    # allowed layer steps
    steps = {}
    for l in range(1, 7):
        t = []
        if l + 1 <= 6: t.append(l + 1)
        if par["SKIP"] and l + 2 <= 6: t.append(l + 2)
        t.append(7)
        if par["BE2"]: t.append(8)
        steps[l] = t
    for l in range(7, 12):
        t = []
        if l + 1 <= 11: t.append(l + 1)
        if par["ESKIP"] and l + 2 <= 11: t.append(l + 2)
        steps[l] = t

    # target bounding boxes relative quantities
    b_zlo, b_zhi = cz.min(axis=1), cz.max(axis=1)
    b_rlo, b_rhi = crn_r.min(axis=1), crn_r.max(axis=1)
    cphi = np.arctan2(cy, cx)
    conn = [[] for _ in range(nm)]
    rbar = {l: float(np.mean(rc[lay == l])) for l in range(1, 7)}
    zedge = {l: float(np.abs(cz[lay == l]).max()) for l in range(1, 7)}
    print("barrel layer mean r:", {k: round(v, 1) for k, v in rbar.items()}, " |z| edge:", {k: round(v, 1) for k, v in zedge.items()})
    for la in sorted(steps):
        ia = np.where(lay == la)[0]
        for lb in steps[la]:
            ib_all = np.where(lay == lb)[0]
            key = np.round(b_rlo[ib_all] * 2) * 1000 + np.round(b_rhi[ib_all] * 2)
            if la <= 6 and lb >= 7:
                key = key * np.sign(zc[ib_all])
                # barrel -> endcap: only helices that leave the barrel in z before the layer a skip could not excuse
                lblock = la + 2 if par["SKIP"] else la + 1
                if lblock <= 6:
                    ds_edge = path_len(np.full_like(R1[ia], rbar[lblock]), hk, hd) - s1[ia]
                    with np.errstate(invalid="ignore", divide="ignore"):
                        creq = {+1: (zedge[lblock] - par["EDGE"] - Z1[ia]) / ds_edge, -1: (-zedge[lblock] + par["EDGE"] - Z1[ia]) / ds_edge}
                else:
                    creq = None
            nadd = 0
            for sig in np.unique(key):
                ib = ib_all[key == sig]
                lo_l = np.full(len(ia), np.inf); hi_l = np.full(len(ia), -np.inf)
                lo_p = np.full(len(ia), np.inf); hi_p = np.full(len(ia), -np.inf)
                # the family is propagated to the smallest and the largest radius of the target group
                for rt in (b_rlo[ib].min(), b_rhi[ib].max()):
                    s2 = path_len(np.full_like(R1[ia], rt), hk, hd)
                    ok = np.isfinite(s2) & (s2 > s1[ia])
                    x2, y2 = xy_at(s2, hk, hd)
                    ph = pphi[ia][:, :, None] + wrap(np.arctan2(y2, x2) - psi1[ia])
                    ds = s2 - s1[ia]
                    clo, chi = cot_lo[ia], cot_hi[ia]
                    if la <= 6 and lb >= 7 and creq is not None:
                        if sig > 0:
                            clo = np.maximum(clo, np.nan_to_num(creq[+1], nan=1e9))
                        else:
                            chi = np.minimum(chi, np.nan_to_num(creq[-1], nan=-1e9))
                        ok = ok & (clo <= chi)
                    za = Z1[ia] + clo * ds; zb = Z1[ia] + chi * ds
                    lo_l = np.minimum(lo_l, np.where(ok, np.minimum(za, zb), np.inf).min(axis=(1, 2)))
                    hi_l = np.maximum(hi_l, np.where(ok, np.maximum(za, zb), -np.inf).max(axis=(1, 2)))
                    lo_p = np.minimum(lo_p, np.where(ok, ph, np.inf).min(axis=(1, 2)))
                    hi_p = np.maximum(hi_p, np.where(ok, ph, -np.inf).max(axis=(1, 2)))
                t_lo, t_hi = b_zlo[ib], b_zhi[ib]
                # target phi boxes relative to each A centre
                rel = wrap(cphi[ib][None, :, :] - phic[ia][:, None, None])      # [na, nb, 4]
                near = np.abs(wrap(phic[ib][None, :] - phic[ia][:, None])) < 1.6
                p_lo, p_hi = rel.min(axis=2), rel.max(axis=2)
                hit = (near & (p_lo <= hi_p[:, None] + par["MPHI"]) & (p_hi >= lo_p[:, None] - par["MPHI"]) &
                       (t_lo[None, :] <= hi_l[:, None] + par["MLONG"]) & (t_hi[None, :] >= lo_l[:, None] - par["MLONG"]))
                aa, bb = np.nonzero(hit)
                for i, j in zip(aa, bb):
                    conn[ia[i]].append(int(det[ib[j]]))
                nadd += len(aa)
            print("step %2d -> %2d : %7d connections from %5d modules (%.1f per module)" % (la, lb, nadd, len(ia), nadd / max(len(ia), 1)))

    stock = lstgeom.load_map(stock_path)
    idx = {int(d): i for i, d in enumerate(det)}
    outmap = {}
    n_stock_missing = 0
    for i in range(nm):
        d = int(det[i])
        new = set(conn[i])
        st = stock.get(d, [])
        n_stock_missing += sum(1 for x in st if x not in new)
        lst_ = list(st) + sorted(new - set(st))
        if lst_:
            outmap[d] = lst_
    for d, st in stock.items():
        if d not in idx:
            outmap[d] = list(st)
    n = np.array([len(v) for v in outmap.values()])
    ns = sum(len(v) for v in stock.values())
    print("stock connections %d, of which NOT regenerated by the family: %d (%.4f) -- kept by the union" % (ns, n_stock_missing, n_stock_missing / ns))
    print("new map: lists %d connections %d  fan-out mean %.1f pct 50/90/99/100: %s" % (len(n), n.sum(), n.mean(), np.percentile(n, [50, 90, 99, 100])))
    with open(out + ".tmp", "wb") as f:
        for d in sorted(outmap):
            v = outmap[d]
            f.write(struct.pack("<II", d, len(v)))
            f.write(struct.pack("<%dI" % len(v), *v))
    os.replace(out + ".tmp", out)
    print("wrote", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
