#!/usr/bin/env python3
"""a15_cmp.py -- like-for-like duplicate-composition diff, ours vs LST.

Maps our delivery classes onto the LST class they replace:
  cPLS -> PLS   chT5 -> T5   chT4 -> T4   aPT5 -> PT5   aPT3 -> PT3
so every cell (class-pair x region) can be compared per event. Prints the cells
sorted by |delta per event|, which is the list of things this round has to explain.
"""
import argparse
import json
from collections import Counter

MAP = {"cPLS": "PLS", "zPLS": "PLS", "chT5": "T5", "chT4": "T4", "aPT5": "PT5",
       "aPT3": "PT3", "cPT5": "PT5", "cPT3": "PT3"}


def canon_pair(k):
    a, b, r = k.split("|")
    a, b = MAP.get(a, a), MAP.get(b, b)
    if a > b:
        a, b = b, a
    return "%s+%s|%s" % (a, b, r)


def load(p):
    with open(p) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--lst", required=True)
    args = ap.parse_args()
    P, L = load(args.proto), load(args.lst)
    nP, nL = P["nev"], L["nev"]

    print("=" * 92)
    print("LIKE-FOR-LIKE DUP COMPOSITION DIFF   proto=%s (%d evt)  vs  LST (%d evt)"
          % (P["label"], nP, nL))
    print("=" * 92)
    print("headline dup   proto %.5f (%d/%d)   LST %.5f (%d/%d)   delta %+.5f"
          % (P["dup"], P["nDupi"], P["nTCi"], L["dup"], L["nDupi"], L["nTCi"],
             P["dup"] - L["dup"]))
    print("per event      proto %.1f TC / %.1f dup      LST %.1f TC / %.1f dup"
          % (P["nTCi"] / nP, P["nDupi"] / nP, L["nTCi"] / nL, L["nDupi"] / nL))

    print("\nDUP TCs PER EVENT BY CLASS (ours mapped onto the LST class it replaces)")
    pc, lc = Counter(), Counter()
    for k, v in P["dup_cls"].items():
        pc[MAP.get(k, k)] += v / nP
    for k, v in L["dup_cls"].items():
        lc[MAP.get(k, k)] += v / nL
    ptc, ltc = Counter(), Counter()
    for k, v in P["tc_cls"].items():
        ptc[MAP.get(k, k)] += v / nP
    for k, v in L["tc_cls"].items():
        ltc[MAP.get(k, k)] += v / nL
    print("  %-6s %9s %9s %9s | %9s %9s %9s" % ("class", "dup/evt P", "dup/evt L", "delta",
                                                "TC/evt P", "TC/evt L", "dTC"))
    for c in sorted(set(pc) | set(lc), key=lambda x: -(abs(pc[x] - lc[x]))):
        print("  %-6s %9.2f %9.2f %+9.2f | %9.1f %9.1f %+9.1f"
              % (c, pc[c], lc[c], pc[c] - lc[c], ptc[c], ltc[c], ptc[c] - ltc[c]))

    print("\nDUP PAIRS PER EVENT BY CELL (class-pair x region), sorted by |delta|")
    pp, lp = Counter(), Counter()
    for k, v in P["pair_cell"].items():
        pp[canon_pair(k)] += v / nP
    for k, v in L["pair_cell"].items():
        lp[canon_pair(k)] += v / nL
    print("  %-22s %10s %10s %10s" % ("cell", "proto/evt", "LST/evt", "delta"))
    tot = 0.0
    for c in sorted(set(pp) | set(lp), key=lambda x: -abs(pp[x] - lp[x])):
        d = pp[c] - lp[c]
        if abs(d) < 0.02 and abs(pp[c]) < 0.05:
            continue
        tot += d
        print("  %-22s %10.2f %10.2f %+10.2f" % (c, pp[c], lp[c], d))
    print("  %-22s %10.2f %10.2f %+10.2f"
          % ("TOTAL(all cells)", sum(pp.values()), sum(lp.values()),
             sum(pp.values()) - sum(lp.values())))

    print("\nSHARED-STRUCTURE MIX PER EVENT")
    ps, ls = Counter(), Counter()
    for k, v in P["pair_struct"].items():
        ps[k.split("|")[2]] += v / nP
    for k, v in L["pair_struct"].items():
        ls[k.split("|")[2]] += v / nL
    print("  %-12s %10s %10s %10s" % ("structure", "proto/evt", "LST/evt", "delta"))
    for s in sorted(set(ps) | set(ls), key=lambda x: -abs(ps[x] - ls[x])):
        print("  %-12s %10.2f %10.2f %+10.2f" % (s, ps[s], ls[s], ps[s] - ls[s]))


if __name__ == "__main__":
    main()
