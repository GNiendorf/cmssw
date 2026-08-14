#!/usr/bin/env python3
"""W5: the CHAIN-COMPOSITION observable, which is where the PU200 displaced cost actually lives.

R8 (does a same-sim edge weld at all) is saturated on PU200 and moves by ~nothing there, so it
cannot be the referee for the displaced bands.  N3's mechanism sentence says why: an E1 step spans
TWO MDs and an E2 step spans ONE, so demoting E1 shortens the chain a thin-supply track can build
and it stops reaching `kChainTCMinLayers`.  This measures exactly that:

  for each sim, take its own TRUE welded edges, walk the maximal runs they form, and record the
  largest number of DISTINCT MDs any one run covers ("mdSpan").

mdSpan is the pre-gate skeleton of the best chain the sim can get.  A key that keeps R8 but cuts
mdSpan is buying reach with track length, which is the trade that killed the global re-key.

usage: comp.py [nev_pu] [nev_jet]
"""
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ab  # noqa: E402

SA = ab.SA
THRESH = [3, 4, 5, 6, 7, 8]


def md_span(z, welded):
    """-> dict sim -> largest distinct-MD count over the maximal runs of its own welded edges."""
    tr = z["trueEdge"] & welded
    if not tr.any():
        return {}
    ei = z["ei"].astype(np.int64)[tr]
    eo = z["eo"].astype(np.int64)[tr]
    sm = z["esim"].astype(np.int64)[tr]
    md = np.stack([z["t3md0"], z["t3md1"], z["t3md2"]], 1).astype(np.int64)
    # the welded graph is a set of disjoint simple paths, so union-find over the true welded edges
    # recovers each sim's runs; parent is keyed by node index.
    parent = {}

    def find(x):
        r = x
        while parent.get(r, r) != r:
            r = parent[r]
        while parent.get(x, x) != x:
            parent[x], x = r, parent[x]
        return r

    for a, b in zip(ei.tolist(), eo.tolist()):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    runs = {}
    for a, b, s in zip(ei.tolist(), eo.tolist(), sm.tolist()):
        r = find(a)
        d = runs.setdefault((r, s), set())
        d.update(md[a].tolist())
        d.update(md[b].tolist())
    best = {}
    for (r, s), mset in runs.items():
        if len(mset) > best.get(s, 0):
            best[s] = len(mset)
    return best


def run(tag, d, specs, nev=None):
    fl = sorted(glob.glob(os.path.join(d, "e*.npz")),
                key=lambda s: int(os.path.basename(s)[1:-4]))
    if nev:
        fl = fl[:nev]
    acc = {}
    n_ev = 0
    for p in fl:
        z = np.load(p, allow_pickle=True)
        if int(z["replay_mismatch"]) != 0:
            continue
        n_ev += 1
        c = ab.Ctx(z)
        band, core = z["sim_band"], z["sim_core"]
        vxy, dxy = z["sim_vxy"], z["sim_dxy"]
        disp = band & ((vxy >= 1.0) | (dxy >= 1.0))
        cells = [("all", band), ("prompt", band & ~disp), ("displaced", disp)]
        if core.any():
            cells.append(("jetcore", core))
        for name, fn, kw in specs:
            best = md_span(z, c.replay(fn(c, **kw)))
            a = acc.setdefault(name, {})
            if not best:
                continue
            sims = np.array(list(best.keys()), dtype=np.int64)
            spans = np.array(list(best.values()), dtype=np.int64)
            for nm, sel in cells:
                m = sel[sims]
                a[nm + "_n"] = a.get(nm + "_n", 0) + int(m.sum())
                a[nm + "_s"] = a.get(nm + "_s", 0) + int(spans[m].sum())
                for t in THRESH:
                    k = "%s_ge%d" % (nm, t)
                    a[k] = a.get(k, 0) + int((spans[m] >= t).sum())
        del z, c
    print("\n=== %s : %d events === mdSpan = distinct MDs on the sim's best welded run" % (tag, n_ev))
    names = [s[0] for s in specs]
    base = acc[names[0]]
    for nm in ("all", "prompt", "displaced", "jetcore"):
        if not base.get(nm + "_n"):
            continue
        print(" %s (sims/evt with any welded true edge, BASE = %.1f)"
              % (nm, base[nm + "_n"] / n_ev))
        print("   %-12s %10s %10s " % ("key", "sims/evt", "meanSpan")
              + " ".join("%10s" % ("frac>=%d" % t) for t in THRESH))
        for name in names:
            a = acc[name]
            n = a.get(nm + "_n", 0)
            if not n:
                continue
            row = "   %-12s %10.1f %10.3f " % (name, n / n_ev, a[nm + "_s"] / n)
            for t in THRESH:
                f = a["%s_ge%d" % (nm, t)] / n
                if name == names[0]:
                    row += "%10.4f" % f
                else:
                    fb = base["%s_ge%d" % (nm, t)] / base[nm + "_n"]
                    row += "%10s" % ("%+.4f" % (f - fb))
            print(row)


if __name__ == "__main__":
    specs = [("BASE", ab.k_base, {}), ("E2FIRST", ab.k_e2first, {}),
             ("D8", ab.k_e2deg, {"knee": 8.0}), ("D32", ab.k_e2deg, {"knee": 32.0}),
             ("D128", ab.k_e2deg, {"knee": 128.0}),
             ("SPEC0.5", ab.k_spec, {"beta": 0.5}), ("SPEC2.0", ab.k_spec, {"beta": 2.0})]
    run("PU200 event_1000", SA + "/w5_ref/dp", specs,
        int(sys.argv[1]) if len(sys.argv) > 1 else 24)
    run("JETS tune", SA + "/w5_ref/dj", specs,
        int(sys.argv[2]) if len(sys.argv) > 2 else 8)
