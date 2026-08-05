#!/usr/bin/env python3
"""dc_layer.py -- LAYER-COMPLEMENTARITY as a chain-vs-chain duplicate discriminator.

Structural claim under test: two chains that are one trajectory split in two occupy
COMPLEMENTARY detector layers (A holds the inner layers, B the outer ones, overlapping
only at the seam), while two genuinely different tracks that happen to share a hit
(jet core, merged cluster) both span the SAME layers. Layer identity is detector
structure, not an angular distance, so the test carries no proximity information --
two real tracks a millirad apart still traverse the same layers and are protected.

Hit -> layer comes from the input ntuple's md_anchorHitIdx / md_otherHitIdx / md_layer
(1-6 barrel, 7-11 endcap), the same table the K9 claim would use in-kernel.

Usage: dc_layer.py <hybrid.root> [input_ntuple.root]
"""
import sys
from collections import Counter, defaultdict

import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"
PT_CUT = 0.9


def main():
    path = sys.argv[1]
    base = sys.argv[2] if len(sys.argv) > 2 else BASE
    t = uproot.open(path)["tree"]
    a = t.arrays(["tc_pt", "tc_simIdxAll", "tc_isChain", "tc_hitOT", "tc_nhitOT",
                  "lumi", "evt"], library="np")
    bt = uproot.open(base)["tree"]
    b = bt.arrays(["md_anchorHitIdx", "md_otherHitIdx", "md_layer", "lumi", "evt"],
                  library="np")
    bmap = {(int(b["lumi"][i]), int(b["evt"][i])): i for i in range(len(b["evt"]))}

    # (bucket) -> [dup, collat]
    tab = defaultdict(lambda: [0, 0])
    tab2 = defaultdict(lambda: [0, 0])
    for i in range(len(a["tc_pt"])):
        j = bmap[(int(a["lumi"][i]), int(a["evt"][i]))]
        h2l = {}
        ah, oh, ml = b["md_anchorHitIdx"][j], b["md_otherHitIdx"][j], b["md_layer"][j]
        for m in range(len(ml)):
            h2l[int(ah[m])] = int(ml[m])
            h2l[int(oh[m])] = int(ml[m])

        isch = a["tc_isChain"][i]
        pt = a["tc_pt"][i]
        sia = a["tc_simIdxAll"][i]
        hits = a["tc_hitOT"][i]
        rows = [k for k in range(len(isch)) if isch[k] == 1 and pt[k] > PT_CUT]
        hs = {k: set(int(x) for x in hits[k]) for k in rows}
        ls = {k: set(h2l.get(x, -1) for x in hs[k]) for k in rows}
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
            nhit = len(hs[x] & hs[y])
            nlsh = len(ls[x] & ls[y])
            nmin = min(len(ls[x]), len(ls[y]))
            same = bool(sims[x] & sims[y])
            tab[(min(nhit, 4), nlsh)][0 if same else 1] += 1
            frac = nlsh / max(nmin, 1)
            fb = "<=0.25" if frac <= 0.25 else ("<=0.5" if frac <= 0.5 else
                                                ("<=0.75" if frac <= 0.75 else ">0.75"))
            tab2[(min(nhit, 4), fb)][0 if same else 1] += 1

    print("chain pairs sharing >=1 OT hit: shared hits x shared LAYERS")
    print("%-6s %-8s %10s %10s %8s" % ("nhit", "nLaySh", "DUP", "COLLAT", "purity"))
    for k in sorted(tab):
        d, c = tab[k]
        if d + c < 10:
            continue
        print("%-6d %-8d %10d %10d %8.3f" % (k[0], k[1], d, c, d / (d + c)))
    print()
    print("chain pairs: shared hits x shared-layer FRACTION of the shorter chain")
    print("%-6s %-8s %10s %10s %8s" % ("nhit", "layFrac", "DUP", "COLLAT", "purity"))
    order = {"<=0.25": 0, "<=0.5": 1, "<=0.75": 2, ">0.75": 3}
    for k in sorted(tab2, key=lambda z: (z[0], order[z[1]])):
        d, c = tab2[k]
        print("%-6d %-8s %10d %10d %8.3f" % (k[0], k[1], d, c, d / max(d + c, 1)))
    print()
    tot = defaultdict(lambda: [0, 0])
    for k, v in tab2.items():
        tot[k[1]][0] += v[0]
        tot[k[1]][1] += v[1]
    print("%-10s %10s %10s %8s" % ("layFrac", "DUP", "COLLAT", "purity"))
    for k in sorted(tot, key=lambda z: order[z]):
        d, c = tot[k]
        print("%-10s %10d %10d %8.3f" % (k, d, c, d / max(d + c, 1)))


if __name__ == "__main__":
    main()
