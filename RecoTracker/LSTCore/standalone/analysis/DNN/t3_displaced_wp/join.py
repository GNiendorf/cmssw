#!/usr/bin/env python3
"""Lane T3-DESIGN (rung 3): join the T3 probe sidecar to the ceiling truth.

A probe record is a pair of EXISTING segments sharing their middle MD, all six hits flagged.  It is a
TRUE consecutive segment pair of track T when each of its three MD hit pairs is a (lower x upper)
combination of one opportunity node of T (three distinct nodes).  Same unit as the lossmap's 'trips'.
Local pT per node: hypot(simhit_px, simhit_py) of the track's own sim hit on the node's primary lower
hit, earliest tof (function entry_pt of rungs/r1/verify/scripts/localpt.py, imported, not copied).

usage: join.py <probe dir> <ceiling pkl> <input ntuple> <out.npz>     writes only <out.npz> (lane dir)
"""
import os
import pickle
import sys

import numpy as np

LAD = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/ladder"
sys.path.insert(0, LAD + "/scripts")
sys.path.insert(0, LAD + "/rungs/r1/verify/scripts")
OKDIR = "/displaced_ref/ladder/rungs/r3/design/"

T3P = [("dirMaxPull2", "<f4"), ("dirSumChi2", "<f4"), ("dirNTested", "<i4"), ("rzRegion", "<i4"), ("rzFallback", "<i4"),
       ("rzLinResid", "<f4"), ("rzChi2", "<f4"), ("rzValue", "<f4"), ("rzCut", "<f4"), ("sinBetaInSqOverR2", "<f4"),
       ("betaInCut", "<f4"), ("cosPositive", "<i4")]
T3_REC = [("hits", "<i4", (6,)), ("md", "<i4", (3,)), ("seg", "<i4", (2,)), ("module", "<i4", (3,)),
          ("lstLayer", "<i2", (3,)), ("subdet", "<i2", (3,)), ("moduleType", "<i2", (3,)), ("mdLoose", "<i2", (3,)),
          ("pointing", "<i4"), ("dirFail", "<i4"), ("rzPass", "<i4"), ("dnnPass", "<i4"), ("verdictAuth", "<i4"),
          ("t3Idx", "<i4"), ("t3Flags", "<i4"), ("connectedMax", "<i4"), ("radius", "<f4"), ("betaIn", "<f4"),
          ("eta1", "<f4"), ("score", "<f4", (3,))] + T3P
T3_DTYPE = np.dtype(T3_REC)


def main():
    import probe_read as pr
    import uproot
    from localpt import entry_pt, BR
    pdir, ceilp, inp, out = sys.argv[1], sys.argv[2], sys.argv[3], os.path.abspath(sys.argv[4])
    assert OKDIR in out and out.endswith(".npz"), "REFUSING output path " + out
    rec, lost = pr.read(pdir + "/t3_probe.bin", T3_DTYPE)
    print("probe records", len(rec), "lost to the buffer cap", lost, flush=True)
    C = pickle.load(open(ceilp, "rb"))
    by_entry = {e["entry"]: (fp, e) for fp, e in C.items()}
    n = len(rec)
    tk = np.full(n, -1, np.int64)          # global track id (event order x track position)
    nodes = np.full((n, 3), -1, np.int64)  # node index within the track
    amb = np.zeros(n, bool)
    prim = np.zeros((n, 3), bool)          # the MD is the node's PRIMARY hit pair (earliest sim hit on each sensor)
    trk_tab = []                           # per matched track: dict
    trk_id = {}
    order = np.argsort(rec["entry"], kind="stable")
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
            ks = [(min(h[0], h[1]), max(h[0], h[1])), (min(h[2], h[3]), max(h[2], h[3])), (min(h[4], h[5]), max(h[4], h[5]))]
            c = [keymap.get(k, ()) for k in ks]
            if not (c[0] and c[1] and c[2]):
                continue
            common = set(t for t, _ in c[0]) & set(t for t, _ in c[1]) & set(t for t, _ in c[2])
            if not common:
                continue
            if len(common) > 1:
                amb[i] = True
            ti = min(common)
            nd = [[k for t, k in cc if t == ti][0] for cc in c]
            if len(set(nd)) < 3:
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
            prim[i] = [ks[c_] == (min(trn[nd[c_]][2], trn[nd[c_]][3]), max(trn[nd[c_]][2], trn[nd[c_]][3])) for c_ in range(3)]
    true = tk >= 0
    print("true consecutive segment pairs", int(true.sum()), "ambiguous", int(amb.sum()), "tracks", len(trk_tab), flush=True)

    # local pT for the nodes that appear
    q = {}
    for i in np.nonzero(true)[0]:
        t = trk_tab[tk[i]]
        e = C[t["fp"]]["tracks"][t["pos"]]
        for k in nodes[i]:
            q[(t["entry"], tk[i], int(k))] = (e["md"][k][2], e["md"][k][3], t["trkNtupIdx"], e["md"][k][0])
    keys = sorted(q)     # python list of a local table, not an LST kernel
    qe = np.array([k[0] for k in keys]); hl = np.array([q[k][0] for k in keys]); hu = np.array([q[k][1] for k in keys])
    qt = np.array([q[k][2] for k in keys])
    lp = np.full(len(keys), np.nan)
    tree = uproot.open(inp)["trackingNtuple/tree"]
    step = 20
    xyz = np.full((n, 6, 3), np.nan, np.float32)   # positions of the six hits (input ph2_x/y/z)
    import awkward as ak
    for start in range(0, tree.num_entries, step):
        sel = np.nonzero((qe >= start) & (qe < start + step))[0]
        rsel = np.nonzero((rec["entry"] >= start) & (rec["entry"] < start + step))[0]
        if len(sel) == 0 and len(rsel) == 0:
            continue
        arr = tree.arrays(BR + ["ph2_x", "ph2_y", "ph2_z"], entry_start=start, entry_stop=min(start + step, tree.num_entries), library="ak")
        for en in np.unique(rec["entry"][rsel]):
            ii = rsel[rec["entry"][rsel] == en]
            ev = arr[int(en) - start]
            for c, b in enumerate(("ph2_x", "ph2_y", "ph2_z")):
                xyz[ii, :, c] = ak.to_numpy(ev[b])[rec["hits"][ii]]
        for en in np.unique(qe[sel]):
            idx = sel[qe[sel] == en]
            ev = arr[int(en) - start]
            l1, _ = entry_pt(ev, hl[idx], qt[idx])
            l2, _ = entry_pt(ev, hu[idx], qt[idx])
            lp[idx] = np.where(np.isnan(l1), l2, l1)
        if start % 200 == 0:
            print("local pT entries", start, flush=True)
    lpof = {k: lp[j] for j, k in enumerate(keys)}
    lp3 = np.full((n, 3), np.nan)
    lay3 = np.full((n, 3), -1, np.int64)
    for i in np.nonzero(true)[0]:
        t = trk_tab[tk[i]]
        for c, k in enumerate(nodes[i]):
            lp3[i, c] = lpof[(t["entry"], tk[i], int(k))]
            lay3[i, c] = q[(t["entry"], tk[i], int(k))][3]
    tt = {k: np.array([t[k] for t in trk_tab]) for k in ("pt", "eta", "vxy", "vz", "pdg", "C4", "nlay", "vx", "vy", "phi", "q", "entry", "simidx")}
    np.savez(out, rec=rec, xyz=xyz, prim=prim, tk=tk, nodes=nodes, lp3=lp3, lay3=lay3, amb=amb, **{"trk_" + k: v for k, v in tt.items()},
             trk_fp=np.array([t["fp"] for t in trk_tab]))
    print("wrote", out)


if __name__ == "__main__":
    main()
