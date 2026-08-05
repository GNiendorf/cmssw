#!/usr/bin/env python3
"""M16b dup decomposition BY DELIVERY CODE.

diag_dups.py answers chain-vs-pixel; the M16 defect was attach-vs-attach, which
needs the finer tc_isChain (= OutTC::deliv) axis:
  0 carried baseline pixel row   1 bare chain TC   2 attach pT5-class   3 attach pT3-class
A duplicate PAIR is two TCs in the same event sharing a matched sim.
Usage: m16b_duppairs.py file.root [file.root ...]
"""
import sys
from collections import Counter

import awkward as ak
import uproot

NAME = {0: "carried", 1: "chain", 2: "attach", 3: "attT3", -1: "?"}

for path in sys.argv[1:]:
    t = uproot.open(path)["tree"]
    a = t.arrays(["tc_simIdxAll", "tc_isChain", "tc_type", "tc_isDuplicate"])
    pc = Counter()
    npair = 0
    ndup = Counter()
    for ev in range(len(a)):
        sims = a["tc_simIdxAll"][ev]
        dl = ak.to_list(a["tc_isChain"][ev])
        dup = ak.to_list(a["tc_isDuplicate"][ev])
        for i, d in enumerate(dl):
            if dup[i]:
                ndup[d] += 1
        s2t = {}
        for i, ss in enumerate(ak.to_list(sims)):
            for s in ss:
                s2t.setdefault(int(s), []).append(i)
        for s, tcs in s2t.items():
            if len(tcs) < 2:
                continue
            for i in range(len(tcs)):
                for j in range(i + 1, len(tcs)):
                    k = tuple(sorted((NAME.get(dl[tcs[i]], "?"), NAME.get(dl[tcs[j]], "?"))))
                    pc[k] += 1
                    npair += 1
    print("== %s" % path.split("/")[-1])
    print("   dup TCs by deliv: " + "  ".join("%s=%d" % (NAME[k], v) for k, v in sorted(ndup.items())))
    print("   dup PAIRS total=%d" % npair)
    for k, v in sorted(pc.items(), key=lambda x: -x[1]):
        print("     %-20s %7d  (%5.1f%%)" % ("-".join(k), v, 100.0 * v / max(npair, 1)))
