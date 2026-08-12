#!/usr/bin/env python3
"""M3 (c): JR's P3 recall study -- would a cheap geometry-only pre-K5 gate keep the welded edges?

The question the round needs answered before anyone implements P3: K5 (the 40->32->32->3 MLP) is
70% of the CPU chain block at 119 ns/edge and 23% of the GPU one, and 99.98% of jet edges are
scored and thrown away. If a cheap pre-score can pick the survivors BEFORE K5, the claim on the
table is 5.4x CPU / 6.2x GPU. The risk is pure RECALL: the pre-score must not drop an edge the
weld would have consumed.

Label = the actual welded edge set (chains.bin pre-trim node runs; 100.0000% joined to the edge
dump, so it is exact, not a proxy).

Two gate families are measured:
  per-node top-C   keep edge if it is in the top C of its inner node's OUT list or the top C of
                   its outer node's IN list, ranked by the score under test. This is JR's family
                   B; the memory bound is then 2*C*nNodes rows, exact and sample-independent.
  global cut       keep the top q-quantile of the score over the whole event.

Scores under test:
  logOdds     the MLP itself -- the ORACLE / upper bound. Its top-C recall is the ceiling any
              pre-score can reach and is also M2's losslessness check.
  single features (signed so that bigger = more weldable)
  lin8 / lin12  logistic regression on the cheap features, trained on a disjoint event half
  mlp8x8      an 8->8->1 net on the same features (72 MACs vs the real head's ~2400)

usage: recall.py <npz> [--tag NAME] [--json out.json]
"""
import json
import sys

import numpy as np

CS = [2, 3, 4, 6, 8, 16, 32]
QS = [0.5, 0.25, 0.125, 0.0625, 0.03, 0.015]

# feature index -> name (as packed by prep_recall.py)
NAMES = ["dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
         "centerDist", "centerDistRel", "sharedLayer", "sharedIsPS", "degIn", "degOut"]
# the subset a pre-K5 kernel can build from the two node rows + the two triplet centres only
CHEAP = ["dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
         "centerDist", "centerDistRel"]
# the subset needing NOTHING but the two 13-float node rows (no sqrt, no triplet centre, no
# module/MD lookup) -- the cheapest thing that could possibly be a gate
MINIMAL = ["dKappaRel", "dTanLambda", "kinkPhi", "kinkTheta"]


def per_node_rank(node, score, nnodes):
    """rank of each edge within its node's list, 0 = best (largest score). O(E log E)."""
    order = np.lexsort((-score, node))
    rank = np.empty(len(node), dtype=np.int32)
    ns = node[order]
    # position within each run of equal node id
    newrun = np.empty(len(ns), dtype=bool)
    newrun[0] = True
    newrun[1:] = ns[1:] != ns[:-1]
    idx = np.arange(len(ns))
    runstart = np.maximum.accumulate(np.where(newrun, idx, 0))
    rank[order] = idx - runstart
    return rank


def topc_keep(inner, outer, score, nnodes, C):
    ro = per_node_rank(inner, score, nnodes)   # rank in the inner node's OUT list
    ri = per_node_rank(outer, score, nnodes)   # rank in the outer node's IN list
    return (ro < C) | (ri < C)


def eval_score(name, score, ev, res):
    inner, outer, welded, nn = ev["inner"], ev["outer"], ev["welded"], ev["nn"]
    nw = int(welded.sum())
    ne = len(welded)
    for C in CS:
        keep = topc_keep(inner, outer, score, nn, C)
        res.setdefault(name, {})["topC%d" % C] = dict(
            recall=float(welded[keep].sum()) / nw, keep=float(keep.sum()) / ne)
    thr = np.quantile(score, 1.0 - np.array(QS))
    for q, t in zip(QS, thr):
        keep = score >= t
        res.setdefault(name, {})["glob%.4g" % q] = dict(
            recall=float(welded[keep].sum()) / nw, keep=float(keep.sum()) / ne)
    return res


def fit_logreg(X, y, Xt):
    from sklearn.linear_model import LogisticRegression
    from sklearn.preprocessing import StandardScaler
    sc = StandardScaler().fit(X)
    m = LogisticRegression(max_iter=300, C=1.0).fit(sc.transform(X), y)
    return m.decision_function(sc.transform(Xt)), (sc, m)


def fit_mlp(X, y, Xt, hidden=8):
    from sklearn.neural_network import MLPClassifier
    from sklearn.preprocessing import StandardScaler
    sc = StandardScaler().fit(X)
    m = MLPClassifier(hidden_layer_sizes=(hidden,), max_iter=60, random_state=0,
                      learning_rate_init=3e-3, batch_size=4096).fit(sc.transform(X), y)
    return m.predict_proba(sc.transform(Xt))[:, 1], (sc, m)


def main():
    npz = sys.argv[1]
    tag = sys.argv[sys.argv.index("--tag") + 1] if "--tag" in sys.argv else npz
    d = np.load(npz, allow_pickle=True)
    inner, outer, feat, lo, welded = d["inner"], d["outer"], d["feat"], d["logOdds"], d["welded"]
    evoff, nnodes = d["evoff"], d["nnodes"]
    nev = len(nnodes)
    print("%s: %d events, %d edges, %d welded (%.4f%% yield)"
          % (tag, nev, len(inner), int(welded.sum()), 100.0 * welded.sum() / len(inner)))

    # GLOBAL node ids so the whole sample can be ranked in one pass (node ids are per-event local)
    base = np.zeros(nev + 1, dtype=np.int64)
    base[1:] = np.cumsum(nnodes)
    rep = np.repeat(base[:nev], np.diff(evoff))
    gin = inner.astype(np.int64) + rep
    gout = outer.astype(np.int64) + rep
    ev = dict(inner=gin, outer=gout, welded=welded, nn=int(base[-1]))

    # train / test split by EVENT half
    half = nev // 2
    isTest = np.repeat(np.arange(nev) >= half, np.diff(evoff))
    res = {}
    meta = dict(tag=tag, nev=int(nev), nedges=int(len(inner)), nwelded=int(welded.sum()),
                yield_pct=float(100.0 * welded.sum() / len(inner)))

    # -- oracle: the MLP scalar itself
    eval_score("logOdds(oracle)", lo, ev, res)

    # -- single cheap features, signed so bigger = better
    for nm in NAMES:
        j = NAMES.index(nm)
        x = feat[:, j]
        s = -np.abs(x) if nm in ("dKappa", "dTanLambda", "kinkPhi", "kinkTheta") else \
            (-x if nm in ("dKappaRel", "centerDist", "centerDistRel") else x)
        eval_score("f:" + nm, s.astype(np.float32), ev, res)

    # -- fitted scores (train on the first event half, evaluated on ALL edges but reported on the
    #    held-out half separately so a memorization artefact cannot hide)
    for nm, cols in (("lin_min4", MINIMAL), ("lin_cheap8", CHEAP), ("lin_all12", NAMES)):
        J = [NAMES.index(c) for c in cols]
        X = feat[:, J]
        s, _ = fit_logreg(X[~isTest], welded[~isTest], X)
        eval_score(nm, s.astype(np.float32), ev, res)
        evt = dict(inner=gin[isTest], outer=gout[isTest], welded=welded[isTest], nn=ev["nn"])
        eval_score(nm + "@heldout", s[isTest].astype(np.float32), evt, res)

    J = [NAMES.index(c) for c in CHEAP]
    X = feat[:, J]
    s, _ = fit_mlp(X[~isTest], welded[~isTest], X)
    eval_score("mlp8x8_cheap8", s.astype(np.float32), ev, res)
    evt = dict(inner=gin[isTest], outer=gout[isTest], welded=welded[isTest], nn=ev["nn"])
    eval_score("mlp8x8_cheap8@heldout", s[isTest].astype(np.float32), evt, res)

    # -- how deep in a node's own logOdds ordering does the weld actually reach?
    ro = per_node_rank(gin, lo, ev["nn"])
    ri = per_node_rank(gout, lo, ev["nn"])
    wr = np.minimum(ro[welded > 0], ri[welded > 0])
    meta["welded_minrank_pctl"] = {str(p): float(np.percentile(wr, p))
                                   for p in (50, 90, 99, 99.9, 100)}
    meta["welded_minrank_max"] = int(wr.max())
    for C in CS:
        meta["welded_within_top%d" % C] = float((wr < C).mean())

    hdr = "%-24s" % "score" + "".join("%8s" % ("C=%d" % c) for c in CS) + \
          "".join("%10s" % ("q=%.3g" % q) for q in QS)
    print(hdr)
    print("-" * len(hdr))
    for k, v in res.items():
        row = "%-24s" % k
        for C in CS:
            row += "%8.4f" % v["topC%d" % C]["recall"]
        for q in QS:
            row += "%10.4f" % v["glob%.4g" % q]["recall"]
        print(row)
    print()
    print("kept fraction of edges (same columns):")
    for k, v in res.items():
        row = "%-24s" % k
        for C in CS:
            row += "%8.4f" % v["topC%d" % C]["keep"]
        for q in QS:
            row += "%10.4f" % v["glob%.4g" % q]["keep"]
        print(row)
    print()
    print("weld depth in a node's own logOdds order:", json.dumps(meta["welded_minrank_pctl"]),
          "max", meta["welded_minrank_max"])
    for C in CS:
        print("  welded edges within top-%-2d of at least one endpoint: %.6f"
              % (C, meta["welded_within_top%d" % C]))

    if "--json" in sys.argv:
        with open(sys.argv[sys.argv.index("--json") + 1], "w") as f:
            json.dump(dict(meta=meta, res=res), f, indent=1)


if __name__ == "__main__":
    main()
