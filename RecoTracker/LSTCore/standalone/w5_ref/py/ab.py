#!/usr/bin/env python3
"""W5: OFFLINE A/B of weld argmax keys, on the validated replay.

The weld is a pure function of the edge rows, and wr.py proves PER EVENT that the replay
reproduces the kernel's own welded set exactly (0 mismatches, 7.1M PU200 + 347M jets edge rows).
So a candidate KEY can be priced at the weld stage without a build.  The stage observable is
JPR3's R8 = REACH: a sim is REACHED when at least one edge joining two of its own >75% T3s is
welded.  R8 is what the re-key moves (+.0378 on jets, N3's deployed instrument) and what the
displaced bands lose; nothing downstream of the weld is modelled here.

Offline is a licence to build, never a result.

usage: ab.py [nev_pu] [nev_jet]
"""
import glob
import os
import sys

import numpy as np

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]


def ordf(x):
    b = np.ascontiguousarray(np.asarray(x, dtype=np.float32)).view(np.uint32)
    return np.where(b >> 31, b ^ np.uint32(0xFFFFFFFF), b ^ np.uint32(0x80000000)).astype(np.uint64)


class Ctx:
    """One event, with the two node-groupings precomputed so a key scan is O(n) per sweep."""

    def __init__(self, z):
        self.z = z
        self.ei = z["ei"].astype(np.int64)
        self.eo = z["eo"].astype(np.int64)
        self.nN = int(z["nT3"])
        self.elig = z["elig"]
        self.oi = np.argsort(self.ei, kind="stable")
        self.oo = np.argsort(self.eo, kind="stable")
        self.si, self.hi = self._seg(self.ei[self.oi])
        self.so, self.ho = self._seg(self.eo[self.oo])

    @staticmethod
    def _seg(v):
        if len(v) == 0:
            return np.zeros(0, np.int64), np.zeros(0, np.int64)
        st = np.flatnonzero(np.concatenate(([True], v[1:] != v[:-1])))
        return st, v[st]

    def gmax(self, val, order, starts, heads):
        out = np.zeros(self.nN, dtype=np.uint64)
        if len(starts):
            out[heads] = np.maximum.reduceat(val[order], starts)
        return out

    def replay(self, key, sweeps=2):
        outW = np.zeros(self.nN, dtype=bool)
        inW = np.zeros(self.nN, dtype=bool)
        done = np.zeros(len(self.ei), dtype=bool)
        for _ in range(sweeps):
            live = self.elig & ~outW[self.ei] & ~inW[self.eo]
            if not live.any():
                break
            kk = np.where(live, key, np.uint64(0))
            bo = self.gmax(kk, self.oi, self.si, self.hi)
            bi = self.gmax(kk, self.oo, self.so, self.ho)
            m = self.elig & (bo[self.ei] == key) & (bi[self.eo] == key) & (key != np.uint64(0))
            if not m.any():
                break
            done |= m
            outW[self.ei[m]] = True
            inW[self.eo[m]] = True
        return done


# ------------------------------------------------------------------ keys
def k_base(c, **kw):
    z = c.z
    return (ordf(z["lo"]) << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_e2first(c, **kw):
    z = c.z
    w = (ordf(z["lo"]) >> np.uint64(1)) | np.where(z["et"] == 2, np.uint64(0x80000000),
                                                  np.uint64(0))
    return (w << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_e2deg(c, knee=32.0, **kw):
    """E2-first CONDITIONED on local occupancy: the family bit is set only at junctions whose
    incidence degree product is at or above the knee.  Every row goes through the same monotone
    transform, so the comparison stays one total order."""
    z = c.z
    dpj = z["degIn"].astype(np.int64) * z["degOut"].astype(np.int64)
    hi = (z["et"] == 2) & (dpj >= knee)
    w = (ordf(z["lo"]) >> np.uint64(1)) | np.where(hi, np.uint64(0x80000000), np.uint64(0))
    return (w << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_spec(c, beta=1.0, **kw):
    """The SPECIFICITY key. The head never sees how many competitors an edge's junction has, so
    logOdds is uncalibrated with respect to junction occupancy -- and the same-sim rate of an
    eligible edge falls from .84 at degProd 1 to .0001 at degProd >= 16k, in BOTH samples and in
    BOTH families. This subtracts that: s = lo - beta * log2(degProd)."""
    z = c.z
    dpj = np.maximum(z["degIn"].astype(np.float64) * z["degOut"].astype(np.float64), 1.0)
    s = (z["lo"].astype(np.float64) - beta * np.log2(dpj)).astype(np.float32)
    return (ordf(s) << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_famoff(c, off=0.5, **kw):
    z = c.z
    lo = z["lo"].astype(np.float32) + np.where(z["et"] == 2, np.float32(off), np.float32(0))
    return (ordf(lo) << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_cal(c, A=None, B=None, **kw):
    z = c.z
    cell = (z["et"].astype(np.int64) - 1) * 20 + z["wpb"].astype(np.int64)
    s = (A[cell] * z["lo"].astype(np.float64) + B[cell]).astype(np.float32)
    return (ordf(s) << np.uint64(32)) | z["tiew"].astype(np.uint64)


def k_caldeg(c, A=None, B=None, knee=32.0, **kw):
    """the calibration above the knee; at or below it BOTH families are scored on the E1 row, so
    the scale stays coherent and the family term is identically zero."""
    z = c.z
    dpj = z["degIn"].astype(np.int64) * z["degOut"].astype(np.int64)
    fam = np.where(dpj >= knee, z["et"].astype(np.int64) - 1, 0)
    cell = fam * 20 + z["wpb"].astype(np.int64)
    s = (A[cell] * z["lo"].astype(np.float64) + B[cell]).astype(np.float32)
    return (ordf(s) << np.uint64(32)) | z["tiew"].astype(np.uint64)


def reach(z, welded):
    esim = z["esim"].astype(np.int64)
    tr = z["trueEdge"]
    ns = len(z["sim_pt"])
    has = np.zeros(ns, dtype=bool)
    got = np.zeros(ns, dtype=bool)
    has[esim[tr]] = True
    got[esim[tr & welded]] = True
    return has, got


def run(tag, d, specs, nev=None):
    fl = sorted(glob.glob(os.path.join(d, "e*.npz")),
                key=lambda s: int(os.path.basename(s)[1:-4]))
    if nev:
        fl = fl[:nev]
    acc, n_ev = {}, 0
    for p in fl:
        z = np.load(p, allow_pickle=True)
        if int(z["replay_mismatch"]) != 0:
            continue
        n_ev += 1
        c = Ctx(z)
        band, core = z["sim_band"], z["sim_core"]
        vxy, dxy = z["sim_vxy"], z["sim_dxy"]
        tr = z["trueEdge"]
        for name, fn, kw in specs:
            w = c.replay(fn(c, **kw))
            has, got = reach(z, w)
            a = acc.setdefault(name, {})
            a["nweld"] = a.get("nweld", 0) + int(w.sum())
            a["ntrue"] = a.get("ntrue", 0) + int((w & tr).sum())
            cells = [("all", band & has)]
            if core.any():
                cells.append(("core", core & has))
            for i, (lo, hi) in enumerate(BANDS):
                cells.append(("vxy%d" % i, band & has & (vxy >= lo) & (vxy < hi)))
                cells.append(("dxy%d" % i, band & has & (dxy >= lo) & (dxy < hi)))
            for nm, s in cells:
                a[nm + "_d"] = a.get(nm + "_d", 0) + int(s.sum())
                a[nm + "_n"] = a.get(nm + "_n", 0) + int((s & got).sum())
        del z, c
    base = acc[specs[0][0]]
    cols = ["all"] + (["core"] if base.get("core_d") else []) + \
        [p + str(i) for p in ("vxy", "dxy") for i in range(4)]
    print("\n=== %s : %d events === (R8 reach = a same-sim edge is welded)" % (tag, n_ev))
    print("%-18s %9s %9s " % ("key", "welds/evt", "true/evt")
          + " ".join("%15s" % c for c in cols))
    for name, _, _ in specs:
        a = acc[name]
        row = "%-18s %9.1f %9.2f " % (name, a["nweld"] / n_ev, a["ntrue"] / n_ev)
        for c in cols:
            d_, n_ = a.get(c + "_d", 0), a.get(c + "_n", 0)
            r = n_ / d_ if d_ else float("nan")
            if name == specs[0][0]:
                row += "%15s" % ("%.4f(%d)" % (r, d_))
            else:
                rb = base[c + "_n"] / base[c + "_d"] if base.get(c + "_d") else float("nan")
                row += "%15s" % ("%.4f%+.4f" % (r, r - rb))
        print(row)
    return acc


if __name__ == "__main__":
    npu = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    nje = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    specs = [("BASE", k_base, {}), ("E2FIRST", k_e2first, {}),
             ("FAMOFF0.5", k_famoff, {"off": 0.5})]
    for k in (4, 8, 16, 32, 64, 128, 256):
        specs.append(("E2DEG%d" % k, k_e2deg, {"knee": float(k)}))
    run("PU200 event_1000", SA + "/w5_ref/dp", specs, npu)
    run("JETS tune", SA + "/w5_ref/dj", specs, nje)
