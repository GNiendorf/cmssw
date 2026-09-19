#!/usr/bin/env python3
"""Lane BUILD-DESIGN (rung 4): join the T4 / T5 probe sidecars to the ceiling truth.

A probe record is a pair of EXISTING triplets the builder would pair (T4: shared segment, 4 MDs; T5: shared MD, 5 MDs),
every hit flagged.  It is a TRUE pair of track T when each of its MD hit pairs is a (lower x upper) combination of one
opportunity node of T, all nodes distinct.  Local pT per node: entry_pt of rungs/r1/verify/scripts/localpt.py (imported).

usage: join45.py <t4|t5> <probe dir> <ceiling pkl> <input ntuple> <out.npz>     writes only <out.npz> (lane dir)
"""
import os
import pickle
import sys

import numpy as np

LAD = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/ladder"
sys.path.insert(0, LAD + "/scripts")
sys.path.insert(0, LAD + "/rungs/r1/verify/scripts")
OKDIR = "/displaced_ref/ladder/rungs/r4/design/"

T5_REC = [("hits", "<i4", (10,)), ("mds", "<i4", (5,)), ("t3", "<i4", (2,)), ("modules", "<i4", (5,)),
          ("lstLayers", "<i2", (5,)), ("moduleTypes", "<i2", (5,)), ("fail", "<u4"), ("verdictProbe", "<i4"),
          ("verdictAuth", "<i4"), ("objIdx", "<i4"), ("t3Flags", "<i4", (2,)), ("startValid", "<i4"), ("dnnScore", "<f4"),
          ("wp98", "<f4"), ("dBeta", "<f4", (2,)), ("dBetaCut2", "<f4", (2,)), ("rzChi2", "<f4"), ("rzLinear", "<i4"),
          ("tight", "<i4"), ("radii", "<f4", (3,)), ("innerCenter", "<f4", (2,)), ("t3Scores", "<f4", (6,)),
          ("xyz", "<f4", (15,)), ("connectedMax", "<i4"), ("modTot", "<i4"), ("modN", "<i4"), ("modCap", "<i4")]
T4_REC = [("hits", "<i4", (8,)), ("mds", "<i4", (4,)), ("t3", "<i4", (2,)), ("modules", "<i4", (4,)),
          ("lstLayers", "<i2", (4,)), ("moduleTypes", "<i2", (4,)), ("fail", "<u4"), ("verdictProbe", "<i4"),
          ("verdictAuth", "<i4"), ("objIdx", "<i4"), ("t4IsDup", "<i4"), ("t3Flags", "<i4", (2,)), ("t3PartOf", "<i4", (2,)),
          ("charge", "<i4", (2,)), ("scores", "<f4", (3,)), ("wpDisp", "<f4"), ("wpFake", "<f4"), ("dBeta", "<f4"),
          ("dBetaCut2", "<f4"), ("rzChi2", "<f4"), ("rzLinear", "<i4"), ("radii", "<f4", (4,)), ("innerCenter", "<f4", (2,)),
          ("t3Scores", "<f4", (6,)), ("xyz", "<f4", (12,)), ("connectedLSMax", "<i4"), ("modTot", "<i4"), ("modN", "<i4"),
          ("modCap", "<i4")]
DT = {"t4": np.dtype(T4_REC), "t5": np.dtype(T5_REC)}
assert DT["t4"].itemsize == 280 and DT["t5"].itemsize == 292


def main():
    import probe_read as pr
    import uproot
    from localpt import entry_pt, BR
    kind, pdir, ceilp, inp, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], os.path.abspath(sys.argv[5])
    assert OKDIR in out and out.endswith(".npz"), "REFUSING output path " + out
    N = 4 if kind == "t4" else 5
    rec, lost = pr.read(pdir + "/%s_probe.bin" % kind, DT[kind])
    print("probe records", len(rec), "lost to the buffer cap", lost, flush=True)
    C = pickle.load(open(ceilp, "rb"))
    by_entry = {e["entry"]: (fp, e) for fp, e in C.items()}
    n = len(rec)
    tk = np.full(n, -1, np.int64)
    nodes = np.full((n, N), -1, np.int64)
    amb = np.zeros(n, bool)
    prim = np.zeros((n, N), bool)
    trk_tab, trk_id = [], {}
    order = np.argsort(rec["entry"], kind="stable")   # numpy on a local table, not an LST kernel
    ents, starts = np.unique(rec["entry"][order], return_index=True)
    ends = list(starts[1:]) + [n]
    for ent, a, b in zip(ents, starts, ends):
        if int(ent) not in by_entry:
            continue
        fp, e = by_entry[int(ent)]
        keymap = {}
        for ti, tr in enumerate(e["tracks"]):
            for k in range(len(tr["md"])):
                for x in tr["md_hl_all"][k]:
                    for y in tr["md_hu_all"][k]:
                        keymap.setdefault((min(x, y), max(x, y)), []).append((ti, k))
        for i in order[a:b]:
            h = rec["hits"][i]
            ks = [(min(h[2 * j], h[2 * j + 1]), max(h[2 * j], h[2 * j + 1])) for j in range(N)]
            c = [keymap.get(k, ()) for k in ks]
            if not all(c):
                continue
            common = set(t for t, _ in c[0])
            for cc in c[1:]:
                common &= set(t for t, _ in cc)
            if not common:
                continue
            if len(common) > 1:
                amb[i] = True
            ti = min(common)
            nd = [[k for t, k in cc if t == ti][0] for cc in c]
            if len(set(nd)) < N:
                continue
            g = trk_id.setdefault((fp, ti), len(trk_tab))
            if g == len(trk_tab):
                tr = e["tracks"][ti]
                trk_tab.append(dict(fp=fp, entry=int(ent), pos=ti, simidx=tr["simidx"], trkNtupIdx=tr["trkNtupIdx"], pt=tr["pt"],
                                    eta=tr["eta"], vxy=tr["vxy"], vz=tr["vz"], pdg=tr["pdg"], C4=bool(tr["C4"]), nlay=tr["nlay"],
                                    vx=tr["vx"], vy=tr["vy"], phi=tr["phi"], q=tr["q"]))
            tk[i] = g
            nodes[i] = nd
            trn = e["tracks"][ti]["md"]
            prim[i] = [ks[c_] == (min(trn[nd[c_]][2], trn[nd[c_]][3]), max(trn[nd[c_]][2], trn[nd[c_]][3])) for c_ in range(N)]
    true = tk >= 0
    print("true pairs", int(true.sum()), "ambiguous", int(amb.sum()), "tracks", len(trk_tab), flush=True)

    q = {}
    for i in np.nonzero(true)[0]:
        t = trk_tab[tk[i]]
        e = C[t["fp"]]["tracks"][t["pos"]]
        for k in nodes[i]:
            q[(t["entry"], tk[i], int(k))] = (e["md"][k][2], e["md"][k][3], t["trkNtupIdx"], e["md"][k][0])
    keys = list(q)
    qe = np.array([k[0] for k in keys]); hl = np.array([q[k][0] for k in keys]); hu = np.array([q[k][1] for k in keys])
    qt = np.array([q[k][2] for k in keys])
    lp = np.full(len(keys), np.nan)
    tree = uproot.open(inp)["trackingNtuple/tree"]
    step = 20
    for start in range(0, tree.num_entries, step):
        sel = np.nonzero((qe >= start) & (qe < start + step))[0]
        if len(sel) == 0:
            continue
        arr = tree.arrays(BR, entry_start=start, entry_stop=min(start + step, tree.num_entries), library="ak")
        for en in np.unique(qe[sel]):
            idx = sel[qe[sel] == en]
            ev = arr[int(en) - start]
            l1, _ = entry_pt(ev, hl[idx], qt[idx])
            l2, _ = entry_pt(ev, hu[idx], qt[idx])
            lp[idx] = np.where(np.isnan(l1), l2, l1)
        if start % 200 == 0:
            print("local pT entries", start, flush=True)
    lpof = {k: lp[j] for j, k in enumerate(keys)}
    lpN = np.full((n, N), np.nan)
    layN = np.full((n, N), -1, np.int64)
    for i in np.nonzero(true)[0]:
        t = trk_tab[tk[i]]
        for c, k in enumerate(nodes[i]):
            lpN[i, c] = lpof[(t["entry"], tk[i], int(k))]
            layN[i, c] = q[(t["entry"], tk[i], int(k))][3]
    tt = {k: np.array([t[k] for t in trk_tab]) for k in ("pt", "eta", "vxy", "vz", "pdg", "C4", "nlay", "vx", "vy", "phi", "q", "entry", "simidx")}
    np.savez(out, rec=rec, prim=prim, tk=tk, nodes=nodes, lp=lpN, lay=layN, amb=amb, **{"trk_" + k: v for k, v in tt.items()},
             trk_fp=np.array([t["fp"] for t in trk_tab]))
    print("wrote", out)


if __name__ == "__main__":
    main()
