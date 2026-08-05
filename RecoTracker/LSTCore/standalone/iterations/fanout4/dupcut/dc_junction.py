#!/usr/bin/env python3
"""dc_junction.py -- is the shared MD of a chain pair a TERMINAL (junction) MD?

Hypothesis under test: a chain-vs-chain DUPLICATE is one trajectory split into two
chains that K6 failed to weld, so the overlap sits at the SEAM -- the last MD of the
inner chain is the first MD of the outer one. A genuine crossing/shared-hit pair
instead overlaps at an INTERIOR MD of at least one of the two chains.

Both facts are pure shared-hit structure (the writer's hit list is
[anchor,other] per MD, innermost-first, per k10AssembleChainTCs), so a rule built
on them carries no proximity information.

Usage: dc_junction.py <hybrid.root>
"""
import sys
from collections import Counter, defaultdict

import uproot

PT_CUT = 0.9


def mdlist(h):
    return [(min(h[i], h[i + 1]), max(h[i], h[i + 1])) for i in range(0, len(h) - 1, 2)]


def classify(ma, mb):
    """Overlap geometry of two ordered MD lists. Returns (nSharedMD, kind)."""
    sa, sb = set(ma), set(mb)
    sh = sa & sb
    if not sh:
        return 0, "none"
    ia = sorted(ma.index(m) for m in sh)
    ib = sorted(mb.index(m) for m in sh)
    na, nb = len(ma), len(mb)
    # terminal = the shared block touches an end of the chain
    ta = (ia[0] == 0) or (ia[-1] == na - 1)
    tb = (ib[0] == 0) or (ib[-1] == nb - 1)
    if ta and tb:
        kind = "seam"        # touches an end of BOTH -> end-to-end continuation
    elif ta or tb:
        kind = "one-end"
    else:
        kind = "interior"
    return len(sh), kind


def main():
    path = sys.argv[1]
    t = uproot.open(path)["tree"]
    a = t.arrays(["tc_pt", "tc_type", "tc_nhitOT", "tc_simIdxAll", "tc_isChain",
                  "tc_hitOT"], library="np")

    dup = Counter()   # (nSharedMD, kind) -> pairs matching the same sim
    col = Counter()   # (nSharedMD, kind) -> pairs NOT matching the same sim
    dup_hit = Counter()   # extra loose hits beyond complete MDs
    for i in range(len(a["tc_pt"])):
        isch = a["tc_isChain"][i]
        pt = a["tc_pt"][i]
        sia = a["tc_simIdxAll"][i]
        hits = a["tc_hitOT"][i]
        rows = [k for k in range(len(isch)) if isch[k] == 1 and pt[k] > PT_CUT]
        ml = {k: mdlist(hits[k]) for k in rows}
        hs = {k: set(hits[k]) for k in rows}
        sims = {k: set(int(s) for s in sia[k]) for k in rows}

        h2c = defaultdict(list)
        for k in rows:
            for h in hs[k]:
                h2c[h].append(k)
        cand = set()
        for h, ks in h2c.items():
            for x in range(len(ks)):
                for y in range(x + 1, len(ks)):
                    cand.add((min(ks[x], ks[y]), max(ks[x], ks[y])))
        for (x, y) in cand:
            nsh, kind = classify(ml[x], ml[y])
            nhit = len(hs[x] & hs[y])
            same = bool(sims[x] & sims[y])
            key = (nsh, kind, nhit if nhit <= 4 else 5)
            (dup if same else col)[key] += 1
    keys = sorted(set(list(dup) + list(col)))
    print("accepted-chain pairs that share >= 1 OT hit (pt>%.1f both)" % PT_CUT)
    print("%-6s %-9s %-6s %10s %10s %8s" % ("nMDsh", "geometry", "nhit", "DUP", "COLLAT", "purity"))
    for k in keys:
        d, c = dup[k], col[k]
        if d + c < 5:
            continue
        print("%-6d %-9s %-6s %10d %10d %8.3f"
              % (k[0], k[1], str(k[2]) if k[2] <= 4 else ">=5", d, c, d / (d + c)))
    print()
    # aggregate by geometry only
    agg_d, agg_c = Counter(), Counter()
    for k in keys:
        agg_d[(k[0] > 0, k[1])] += dup[k]
        agg_c[(k[0] > 0, k[1])] += col[k]
    print("%-24s %10s %10s %8s" % ("geometry", "DUP", "COLLAT", "purity"))
    for k in sorted(set(list(agg_d) + list(agg_c))):
        d, c = agg_d[k], agg_c[k]
        print("%-24s %10d %10d %8.3f" % (str(k), d, c, d / max(d + c, 1)))


if __name__ == "__main__":
    main()
