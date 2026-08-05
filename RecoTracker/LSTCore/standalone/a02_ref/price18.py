#!/usr/bin/env python3
"""A02 M18 -- EXACT first-order scoreboard pricing from a sharp-labelled -XCP dump.

Every retirement of a bare seed row moves the scoreboard by a known amount, because the
harness's own definitions are per row:

    nTC      -= 1                       (the row disappears)
    nFake    -= isFake                  (it was a fake row)
    nDup     -= isDup [+ nPartner adj]  (it was a duplicate row; and a partner left with
                                         exactly one other row stops being a duplicate)
    nMatched -= nSoleCut                (accepted in-denominator sims that lose their
                                         only >75% match)

so a criterion's (eff, dup, fake) can be predicted from the dump alone once the absolute
baseline counts are pinned. Predictions are labelled PRED everywhere; only harness runs
are reported as measured.

Usage:
    price18.py --pairs pairs_L0.txt [--score-col df07] [--thresholds 4.5,4,3.5,3]
               [--scores extra_scores.txt]
"""

import argparse
import sys

import numpy as np

# FINBASE, the assembled baseline, frozen 300 events (fin_ref scoreboard).
BASE = dict(nTC=618793, dup=0.06230, fake=0.05551, eff=0.80992, nSim=62555, nEvt=300)
BASE["nDup"] = BASE["dup"] * BASE["nTC"]
BASE["nFake"] = BASE["fake"] * BASE["nTC"]
BASE["nMatched"] = BASE["eff"] * BASE["nSim"]
BASE_XCT = 4.0  # the criterion those absolute numbers were measured at


def load(path):
    with open(path) as fh:
        hdr = fh.readline().split()[1:]
    idx = {n: i for i, n in enumerate(hdr)}
    need = ["nSoleCut", "isDup", "isFake", "nPartner"]
    for n in need:
        if n not in idx:
            sys.exit("dump %s has no column %s -- rerun with -XCL 1" % (path, n))
    ncol = len(hdr)
    keys, seeds, rows = [], [], []
    with open(path) as fh:
        for line in fh:
            if line.startswith("#") or not line.endswith("\n"):
                continue
            t = line.split()
            if len(t) != ncol:
                continue
            rows.append(t)
    return hdr, idx, rows


def seed_table(hdr, idx, rows, score_col):
    """Collapse pairs to seeds: per-seed max score and the (identical) label columns."""
    best, lab = {}, {}
    for t in rows:
        k = (t[0], t[1], t[2], t[3])
        s = float(t[idx[score_col]])
        if k not in best or s > best[k]:
            best[k] = s
        if k not in lab:
            lab[k] = (int(float(t[idx["nSoleCut"]])), int(float(t[idx["isDup"]])),
                      int(float(t[idx["isFake"]])), int(float(t[idx["nPartner"]])))
    ks = list(best.keys())
    ev = sorted(set((k[0], k[1], k[2]) for k in ks))
    sc = np.array([best[k] for k in ks])
    L = np.array([lab[k] for k in ks], dtype=np.int32)
    return ks, ev, sc, L


def price(sel, base_sel, L, scale, partner=1.0):
    """sel/base_sel: boolean seed masks. Returns predicted (eff, dup, fake, nTC)."""
    sole, dup, fake, part = L[:, 0], L[:, 1], L[:, 2], L[:, 3]
    padj = np.minimum(part, 1) * partner
    d_tc = -(sel.sum() - base_sel.sum()) * scale
    d_dup = -((dup + padj)[sel].sum() - (dup + padj)[base_sel].sum()) * scale
    d_fake = -(fake[sel].sum() - fake[base_sel].sum()) * scale
    d_mat = -(sole[sel].sum() - sole[base_sel].sum()) * scale
    nTC = BASE["nTC"] + d_tc
    return ((BASE["nMatched"] + d_mat) / BASE["nSim"],
            (BASE["nDup"] + d_dup) / nTC,
            (BASE["nFake"] + d_fake) / nTC,
            nTC)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--score-col", default="df07")
    ap.add_argument("--thresholds", default="5,4.5,4,3.5,3,2")
    ap.add_argument("--partner", type=float, default=1.0)
    ap.add_argument("--scores", default=None,
                    help="optional file 'run lumi evt seed score' to price instead")
    ap.add_argument("--score-thresholds", default=None)
    args = ap.parse_args()

    hdr, idx, rows = load(args.pairs)
    ks, ev, sc, L = seed_table(hdr, idx, rows, args.score_col)
    scale = BASE["nEvt"] / float(len(ev))
    print("dump %s: %d pairs, %d seeds, %d events (scale x%.4f to 300 evts)"
          % (args.pairs, len(rows), len(ks), len(ev), scale))
    print("pool per event: %.1f seeds | isDup %.1f  isFake %.1f  soleCut %.2f  neither %.1f"
          % (len(ks) / len(ev), L[:, 1].sum() / len(ev), L[:, 2].sum() / len(ev),
             (L[:, 0] > 0).sum() / len(ev),
             ((L[:, 1] == 0) & (L[:, 2] == 0) & (L[:, 0] == 0)).sum() / len(ev)))

    base_sel = sc >= BASE_XCT
    print("\nATTACH LOGIT (-XCT), the baseline's own bracket -- PRED")
    print("%-10s %8s %8s %8s %9s  %7s %7s %7s" %
          ("thr", "eff", "dup", "fake", "nTC", "ret/ev", "dup/ev", "sole/ev"))
    for th in [float(x) for x in args.thresholds.split(",")]:
        sel = sc >= th
        e, d, f, n = price(sel, base_sel, L, scale, args.partner)
        print("%-10.2f %8.5f %8.5f %8.5f %9.0f  %7.1f %7.1f %7.2f" %
              (th, e, d, f, n, sel.sum() / len(ev), L[sel, 1].sum() / len(ev),
               L[sel, 0].sum() / len(ev)))

    # ORACLE: retire everything that is free AND beneficial.
    oracle = ((L[:, 1] > 0) | (L[:, 2] > 0)) & (L[:, 0] == 0)
    e, d, f, n = price(oracle, base_sel, L, scale, args.partner)
    print("\nORACLE  retire iff (isDup or isFake) and nSoleCut==0 -- PRED")
    print("  eff %.5f  dup %.5f  fake %.5f  nTC %.0f   (%.1f seeds/evt)"
          % (e, d, f, n, oracle.sum() / len(ev)))
    free = L[:, 0] == 0
    e, d, f, n = price(free, base_sel, L, scale, args.partner)
    print("ORACLE-B retire iff nSoleCut==0 (ignore the neutral rows) -- PRED")
    print("  eff %.5f  dup %.5f  fake %.5f  nTC %.0f   (%.1f seeds/evt)"
          % (e, d, f, n, free.sum() / len(ev)))

    # CALIBRATION of the one free coefficient (the second-order partner credit) against
    # the four -XCT points the Baseline agent measured on these same 300 events.
    MEAS = {4.5: (0.81037, 0.06675, 0.05539), 4.0: (0.80992, 0.06230, 0.05551),
            3.5: (0.80904, 0.05925, 0.05558), 3.0: (0.80776, 0.05689, 0.05562)}
    print("\nCALIBRATION vs the measured -XCT bracket (fin_ref)")
    print("%-9s %9s %9s %9s %9s" % ("partner", "rms dup", "rms eff", "rms fake", "maxdev dup"))
    for pw in [0.0, 0.25, 0.5, 0.75, 1.0]:
        de, dd, df = [], [], []
        for th, (me, md, mf) in MEAS.items():
            e, d, f, _ = price(sc >= th, base_sel, L, scale, pw)
            de.append(e - me)
            dd.append(d - md)
            df.append(f - mf)
        print("%-9.2f %9.5f %9.5f %9.5f %9.5f"
              % (pw, np.sqrt(np.mean(np.square(dd))), np.sqrt(np.mean(np.square(de))),
                 np.sqrt(np.mean(np.square(df))), max(abs(x) for x in dd)))
    print("per-point residual (PRED - measured) at the chosen --partner %.2f:" % args.partner)
    for th, (me, md, mf) in sorted(MEAS.items(), reverse=True):
        e, d, f, _ = price(sc >= th, base_sel, L, scale, args.partner)
        print("  -XCT %-5.2f eff %+.5f  dup %+.5f  fake %+.5f" % (th, e - me, d - md, f - mf))

    if args.scores:
        sm = {}
        with open(args.scores) as fh:
            for line in fh:
                t = line.split()
                if len(t) < 5:
                    continue
                sm[(t[0], t[1], t[2], t[3])] = float(t[4])
        v = np.array([sm.get(k, -1e9) for k in ks])
        miss = int((v <= -1e8).sum())
        print("\nEXTERNAL SCORE (%s): %d/%d seeds scored" % (args.scores, len(ks) - miss, len(ks)))
        ths = args.score_thresholds or "3,2,1.5,1,0.5,0,-1"
        print("%-10s %8s %8s %8s %9s  %7s %7s %7s" %
              ("thr", "eff", "dup", "fake", "nTC", "ret/ev", "dup/ev", "sole/ev"))
        for th in [float(x) for x in ths.split(",")]:
            sel = v >= th
            e, d, f, n = price(sel, base_sel, L, scale, args.partner)
            print("%-10.2f %8.5f %8.5f %8.5f %9.0f  %7.1f %7.1f %7.2f" %
                  (th, e, d, f, n, sel.sum() / len(ev), L[sel, 1].sum() / len(ev),
                   L[sel, 0].sum() / len(ev)))


if __name__ == "__main__":
    main()
