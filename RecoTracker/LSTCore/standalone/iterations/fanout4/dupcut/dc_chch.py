#!/usr/bin/env python3
"""dc_chch.py -- anatomy of chain-vs-chain duplicates and of the collateral pool.

For every event of a hybrid chainproto output carrying tc_hitOT:
  * DUP side: chain pairs that match the same sim at > 0.75 (a chain-vs-chain
    duplicate). Histogram their shared-OT-hit count and whether the overlap
    contains a COMPLETE MiniDoublet (both hits of one MD, i.e. an even-indexed
    consecutive pair of the writer's [anchor, other] hit list).
  * COLLATERAL side: every OTHER accepted chain pair that shares >= 1 OT hit but
    does NOT share a sim. Those are the pairs a tightened claim would also kill.

The MD test only needs the hit list's [anchor,other] pairing, which k10AssembleChainTCs
guarantees, so no extra branch is required.

Usage: dc_chch.py <hybrid.root>
"""
import sys
from collections import Counter, defaultdict

import numpy as np
import uproot

PT_CUT = 0.9


def mdset(h):
    return set((min(h[i], h[i + 1]), max(h[i], h[i + 1])) for i in range(0, len(h) - 1, 2))


def band(eta):
    a = abs(eta)
    return "barrel" if a < 1.1 else ("trans" if a < 1.7 else "endcap")


def main():
    path = sys.argv[1]
    t = uproot.open(path)["tree"]
    a = t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll",
                  "tc_isChain", "tc_hitOT"], library="np")

    dup_sh = Counter()          # shared-hit count -> n dup pairs
    dup_md = Counter()          # (shared-hit count, shares complete MD) -> n
    dup_sh_band = defaultdict(Counter)
    col_sh = Counter()          # collateral (non-dup) accepted chain pairs
    col_md = Counter()
    n_ch = 0
    # per shared-hit count, the min nLayers of the pair (which one would die)
    dup_minlay = defaultdict(Counter)

    for i in range(len(a["tc_pt"])):
        ty = a["tc_type"][i]
        isch = a["tc_isChain"][i]
        pt = a["tc_pt"][i]
        eta = a["tc_eta"][i]
        nh = a["tc_nhitOT"][i]
        sia = a["tc_simIdxAll"][i]
        hits = a["tc_hitOT"][i]
        rows = [k for k in range(len(ty)) if isch[k] == 1]
        n_ch += sum(1 for k in rows if pt[k] > PT_CUT)

        hset = {k: set(hits[k]) for k in rows}
        mset = {k: mdset(hits[k]) for k in rows}
        sims = {k: set(int(s) for s in sia[k]) for k in rows}

        # hit -> chains, to enumerate only pairs that actually share a hit
        h2c = defaultdict(list)
        for k in rows:
            for h in hset[k]:
                h2c[h].append(k)
        cand = set()
        for h, ks in h2c.items():
            for x in range(len(ks)):
                for y in range(x + 1, len(ks)):
                    cand.add((ks[x], ks[y]))
        # dup pairs (share a sim) may share zero hits, so enumerate them via sims
        s2c = defaultdict(list)
        for k in rows:
            for s in sims[k]:
                s2c[s].append(k)
        duppairs = set()
        for s, ks in s2c.items():
            if len(ks) < 2:
                continue
            for x in range(len(ks)):
                for y in range(x + 1, len(ks)):
                    duppairs.add((min(ks[x], ks[y]), max(ks[x], ks[y])))

        for (x, y) in duppairs:
            if pt[x] <= PT_CUT or pt[y] <= PT_CUT:
                continue
            sh = len(hset[x] & hset[y])
            shmd = len(mset[x] & mset[y])
            key = sh if sh <= 6 else 7
            dup_sh[key] += 1
            dup_md[(key, shmd > 0)] += 1
            dup_sh_band[key][band(0.5 * (eta[x] + eta[y]))] += 1
            dup_minlay[key][min(nh[x], nh[y]) // 2] += 1

        for (x, y) in cand:
            p = (min(x, y), max(x, y))
            if p in duppairs:
                continue
            if pt[x] <= PT_CUT or pt[y] <= PT_CUT:
                continue
            sh = len(hset[x] & hset[y])
            shmd = len(mset[x] & mset[y])
            key = sh if sh <= 6 else 7
            col_sh[key] += 1
            col_md[(key, shmd > 0)] += 1

    print("chain TCs (pt>%.1f): %d" % (PT_CUT, n_ch))
    print()
    print("SHARED-OT-HIT SPECTRUM of accepted chain pairs (pt>%.1f both)" % PT_CUT)
    print("%-8s %10s %10s %10s %10s %8s   %s"
          % ("shared", "DUP pairs", "  of which", "COLLAT", "  of which", "dup/tot",
             "dup by band B/T/E"))
    print("%-8s %10s %10s %10s %10s %8s"
          % ("hits", "(same sim)", "share MD", "(diff sim)", "share MD", ""))
    for k in sorted(set(list(dup_sh) + list(col_sh))):
        lbl = str(k) if k <= 6 else ">=7"
        d, c = dup_sh[k], col_sh[k]
        bb = dup_sh_band[k]
        print("%-8s %10d %10d %10d %10d %8.3f   %d/%d/%d"
              % (lbl, d, dup_md[(k, True)], c, col_md[(k, True)],
                 d / max(d + c, 1), bb["barrel"], bb["trans"], bb["endcap"]))
    print("%-8s %10d %10d %10d %10d" % ("TOTAL", sum(dup_sh.values()),
                                        sum(v for (k, m), v in dup_md.items() if m),
                                        sum(col_sh.values()),
                                        sum(v for (k, m), v in col_md.items() if m)))
    print()
    print("min-nLayers of the dup pair, by shared-hit count (which chain a "
          "shorter-loses rule would drop)")
    for k in sorted(dup_minlay):
        c = dup_minlay[k]
        print("  shared=%s : %s" % (k, " ".join("%d:%d" % (n, c[n]) for n in sorted(c))))


if __name__ == "__main__":
    main()
