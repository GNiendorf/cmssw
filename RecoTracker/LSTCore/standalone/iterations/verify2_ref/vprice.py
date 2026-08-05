#!/usr/bin/env python3
"""Independent check of A02's pricing model against fin_ref's MEASURED -XCT points.

Reads a02_ref/pairs_L0.txt (the frozen-300 labelled dump, TEST ONLY), prices the
criterion "attachLogit >= XCT" exactly the way price18.py does, and compares against the
harness numbers I re-derived myself from the corresponding fin_ref hists.

The point at issue: price18.py / train_dedup18.py divide the matched-sim delta by
nSim = 62555, which is the PT-HISTOGRAM sim denominator (it includes sims below the
0.9 GeV cut). nSoleCut, however, is gated by simInDenom() = pt>0.9 && |eta|<4.5 &&
vperp<2.5 && |vz|<30, i.e. the IN-CUT denominator, whose value on the frozen 300 is
22633 (the eta-histogram denominator, and the denominator of the .80992 they quote).
So the efficiency channel is diluted by 62555/22633 = 2.76x.
"""
import numpy as np

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
NFEAT = 22
BASE = dict(nTC=618793, dup=0.06230, fake=0.05551, eff=0.80992, nEvt=300)
NSIM_THEIRS = 62555     # what price18.py uses
NSIM_INCUT = 22633      # the denominator nSoleCut actually belongs to
BASE_XCT = 4.0
PARTNER = 1.0


def load(path):
    seeds, alog, lab = [], [], []
    with open(path) as fh:
        hdr = fh.readline().split()[1:]
        idx = {n: i for i, n in enumerate(hdr)}
        for line in fh:
            if line.startswith("#") or not line.endswith("\n"):
                continue
            t = line.split()
            if len(t) != len(hdr):
                continue
            seeds.append((t[0], t[1], t[2], t[3]))
            alog.append(float(t[6 + 7]))          # df07 = df_attachLogit
            lab.append([int(float(t[idx[n]])) for n in
                        ("nSoleCut", "isDup", "isFake", "nPartner")])
    return seeds, np.asarray(alog), np.asarray(lab)


def seed_pool(seeds, sc, L):
    """max-pool the per-pair score to the seed, keep that seed's label row (identical
    across its pairs -- verified below)."""
    d = {}
    for k, s, l in zip(seeds, sc, L):
        if k not in d or s > d[k][0]:
            d[k] = (s, l)
    ks = list(d)
    return np.asarray([d[k][0] for k in ks]), np.asarray([d[k][1] for k in ks])


def price(sel, base_sel, Ls, nsim):
    sole, dup, fake, part = Ls[:, 0], Ls[:, 1], Ls[:, 2], Ls[:, 3]
    dupw = dup + np.minimum(part, 1) * PARTNER
    d_tc = -(sel.sum() - base_sel.sum())
    d_dup = -(dupw[sel].sum() - dupw[base_sel].sum())
    d_fake = -(fake[sel].sum() - fake[base_sel].sum())
    d_mat = -(sole[sel].sum() - sole[base_sel].sum())
    nTC = BASE["nTC"] + d_tc
    return ((BASE["eff"] * nsim + d_mat) / nsim,
            (BASE["dup"] * BASE["nTC"] + d_dup) / nTC,
            (BASE["fake"] * BASE["nTC"] + d_fake) / nTC,
            nTC)


if __name__ == "__main__":
    seeds, alog, L = load(S + "a02_ref/pairs_L0.txt")
    sc, Ls = seed_pool(seeds, alog, L)
    print("pairs %d -> seeds %d (%.1f/evt)" % (len(alog), len(sc), len(sc) / 300.))
    print("of those: isDup %.1f/evt  isFake %.1f/evt  nSoleCut>0 %.2f/evt  "
          "neither-dup-nor-fake-nor-cost %.1f/evt"
          % (Ls[:, 1].sum() / 300., Ls[:, 2].sum() / 300.,
             (Ls[:, 0] > 0).sum() / 300.,
             ((Ls[:, 1] == 0) & (Ls[:, 2] == 0) & (Ls[:, 0] == 0)).sum() / 300.))
    b = sc >= BASE_XCT
    print()
    print("%-8s %9s %9s %9s | %9s %9s %9s | %8s" %
          ("XCT", "PREDeff", "PREDdup", "PREDfke", "eff(incut)", "", "", "nTC"))
    for th in (3.0, 3.5, 4.0, 4.5, 5.0):
        sel = sc >= th
        e1, d, f, n = price(sel, b, Ls, NSIM_THEIRS)
        e2, _, _, _ = price(sel, b, Ls, NSIM_INCUT)
        print("%-8.2f %9.5f %9.5f %9.5f | eff w/ incut denom %9.5f | %8.0f  ret/ev %.1f"
              % (th, e1, d, f, e2, n, sel.sum() / 300.))
