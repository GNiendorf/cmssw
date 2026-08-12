#!/usr/bin/env python3
"""M3 (c): the P3 pre-K5 recall study ON JETS -- the case the round is actually paying for.

PU200 answers the losslessness question (see FINDINGS_MEM.md [M3 12:35]) but not the P3 question:
its mean per-MD degree is ~2, so a per-node top-C keeps 82% of edges at C=8 and saves nothing.
Jets have mean degree 62-272, so the same gate is where the 11.5x edge reduction lives.

One event at a time (jet event 0 alone is 47 M edges), aggregating counts, so nothing is held that
does not fit. Scores are trained ONLY on PU200 (the transfer case a shipped gate would face) and,
separately, on a disjoint set of jet events.

usage: recall_jet.py <dumpdir> <pu200.npz> <evt,evt,...> [--json out.json]
"""
import json
import sys

import numpy as np

M3 = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m3_ref"
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/s1_work")
sys.path.insert(0, M3)
from dumpio import iter_feat, iter_edges          # noqa: E402
from chainnodes import iter_chain_nodes, welded_pairs  # noqa: E402
from prep_recall import FEATCOLS, FEATNAMES      # noqa: E402

CS = [2, 3, 4, 6, 8, 16, 32, 64]
NAMES = FEATNAMES
CHEAP = ["dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
         "centerDist", "centerDistRel"]
SIGNFLIP = {"dKappa", "dTanLambda", "kinkPhi", "kinkTheta"}      # use -|x|
NEGATE = {"dKappaRel", "centerDist", "centerDistRel"}            # use -x


def per_node_rank(node, score):
    order = np.lexsort((-score, node))
    rank = np.empty(len(node), dtype=np.int32)
    ns = node[order]
    newrun = np.empty(len(ns), dtype=bool)
    newrun[0] = True
    newrun[1:] = ns[1:] != ns[:-1]
    idx = np.arange(len(ns), dtype=np.int64)
    runstart = np.maximum.accumulate(np.where(newrun, idx, 0))
    rank[order] = (idx - runstart).astype(np.int32)
    return rank


def load_event(d, i):
    ievt, nf, ein, eout, etyp, ef = next(iter_feat("%s/jet%d_edgefeat.bin" % (d, i)))
    _, nN, nE1, nE2, xin, xout, xtyp, lo = next(iter_edges("%s/jet%d_edges.bin" % (d, i)))
    _, cnN, cnE, runs, flags = next(iter_chain_nodes("%s/jet%d_chains.bin" % (d, i)))
    assert np.array_equal(ein, xin) and np.array_equal(eout, xout)
    wa, wb = welded_pairs(runs)
    key = ein.astype(np.int64) * np.int64(nN) + eout.astype(np.int64)
    wkey = np.unique(wa.astype(np.int64) * np.int64(nN) + wb.astype(np.int64))
    srt = np.argsort(key, kind="stable")
    pos = np.clip(np.searchsorted(key[srt], wkey), 0, len(key) - 1)
    ok = key[srt][pos] == wkey
    welded = np.zeros(len(key), dtype=np.uint8)
    welded[srt[pos[ok]]] = 1
    del key, srt, pos
    return dict(nN=nN, inner=ein, outer=eout, etype=etyp.astype(np.uint8),
                feat=ef[:, FEATCOLS].astype(np.float32), logOdds=lo.astype(np.float32),
                welded=welded, njoin=int(ok.sum()), nweldpairs=len(wkey), nchains=len(runs))


def signed(feat, nm):
    x = feat[:, NAMES.index(nm)]
    if nm in SIGNFLIP:
        return -np.abs(x)
    if nm in NEGATE:
        return -x
    return x


def fit_models(npz):
    from sklearn.linear_model import LogisticRegression
    from sklearn.neural_network import MLPClassifier
    from sklearn.preprocessing import StandardScaler
    d = np.load(npz, allow_pickle=True)
    feat, welded = d["feat"], d["welded"]
    nev = len(d["nnodes"])
    isTrain = np.repeat(np.arange(nev) < nev // 2, np.diff(d["evoff"]))
    out = {}
    for nm, cols in (("lin_cheap8", CHEAP), ("lin_all12", NAMES)):
        J = [NAMES.index(c) for c in cols]
        sc = StandardScaler().fit(feat[isTrain][:, J])
        m = LogisticRegression(max_iter=300).fit(sc.transform(feat[isTrain][:, J]), welded[isTrain])
        out[nm] = (J, sc, m, "dec")
    J = [NAMES.index(c) for c in CHEAP]
    sc = StandardScaler().fit(feat[isTrain][:, J])
    m = MLPClassifier(hidden_layer_sizes=(8,), max_iter=60, random_state=0,
                      learning_rate_init=3e-3, batch_size=4096).fit(
        sc.transform(feat[isTrain][:, J]), welded[isTrain])
    out["mlp8x8_cheap8"] = (J, sc, m, "prob")
    return out


def apply_model(mdl, feat):
    J, sc, m, kind = mdl
    X = sc.transform(feat[:, J])
    return (m.decision_function(X) if kind == "dec" else m.predict_proba(X)[:, 1]).astype(np.float32)


def main():
    d = sys.argv[1]
    npz = sys.argv[2]
    evts = [int(x) for x in sys.argv[3].split(",")]
    print("fitting pre-scores on PU200 (%s) ..." % npz, flush=True)
    models = fit_models(npz)
    print("fitted:", list(models), flush=True)

    scores = ["logOdds(oracle)"] + ["f:" + n for n in ("dKappaRel", "kinkPhi", "kinkTheta",
                                                       "centerDistRel", "degIn")] + list(models)
    agg = {s: {C: [0, 0] for C in CS} for s in scores}     # [welded_kept, edges_kept]
    tot_w = tot_e = 0
    per_evt = {}
    for i in evts:
        ev = load_event(d, i)
        nw, ne = int(ev["welded"].sum()), len(ev["welded"])
        print("\njet evt %d: nodes %d edges %d welded %d/%d joined chains %d  yield %.5f%%"
              % (i, ev["nN"], ne, ev["njoin"], ev["nweldpairs"], ev["nchains"],
                 100.0 * nw / ne), flush=True)
        assert ev["njoin"] == ev["nweldpairs"], "welded pair missing from the edge dump"
        tot_w += nw
        tot_e += ne
        per_evt[i] = dict(nodes=int(ev["nN"]), edges=ne, welded=nw, chains=ev["nchains"], s={})
        for s in scores:
            if s == "logOdds(oracle)":
                sc = ev["logOdds"]
            elif s.startswith("f:"):
                sc = signed(ev["feat"], s[2:])
            else:
                sc = apply_model(models[s], ev["feat"])
            ro = per_node_rank(ev["inner"], sc)
            ri = per_node_rank(ev["outer"], sc)
            best = np.minimum(ro, ri)
            del ro, ri, sc
            per_evt[i]["s"][s] = {}
            for C in CS:
                keep = best < C
                wk = int(ev["welded"][keep].sum())
                ek = int(keep.sum())
                agg[s][C][0] += wk
                agg[s][C][1] += ek
                per_evt[i]["s"][s]["C%d" % C] = dict(recall=wk / nw, keep=ek / ne)
            print("   %-18s recall " % s + " ".join(
                "C%d %.5f" % (C, per_evt[i]["s"][s]["C%d" % C]["recall"]) for C in CS), flush=True)
        del ev

    print("\n=== POOLED over %d jet events: %d edges, %d welded (%.5f%% yield) ==="
          % (len(evts), tot_e, tot_w, 100.0 * tot_w / tot_e))
    hdr = "%-20s" % "score" + "".join("%18s" % ("C=%d" % C) for C in CS)
    print(hdr)
    print("-" * len(hdr))
    for s in scores:
        row = "%-20s" % s
        for C in CS:
            wk, ek = agg[s][C]
            row += "%10.5f/%.4f" % (wk / tot_w, ek / tot_e)
        print(row)
    print("(cells are  welded-recall / fraction-of-edges-kept)")

    if "--json" in sys.argv:
        with open(sys.argv[sys.argv.index("--json") + 1], "w") as f:
            json.dump(dict(pooled={s: {str(C): agg[s][C] for C in CS} for s in scores},
                           tot_welded=tot_w, tot_edges=tot_e, per_evt=per_evt), f, indent=1)


if __name__ == "__main__":
    main()
