#!/usr/bin/env python3
"""dc_plscell.py -- is the chain-vs-carried-pLS duplicate cell INHERITED from LST or NEW?

Per sim track (full sim list, > 0.75 matching, same events):
  LST  side: how many TCs match it, and of which classes.
  PROTO side: same, with classes CH / cpT3 / cpLS.

Partition the proto CH+cpLS duplicate groups into
  INHERITED  -- the same sim already had an OT-object + pLS duplicate group in LST
                (pT5/T5/T4/pT3 together with a pLS), i.e. the cell simply changed name;
  NEW        -- the sim had NO OT object in LST (its only TC was the pLS, or pLSs), and
                the chain is a brand new second delivery.
For the NEW half also report whether LST already reconstructed that sim at all
(sim was matched by >= 1 TC), which decides whether the chain buys any efficiency.

Usage: dc_plscell.py <hybrid.root> [input_ntuple.root]
"""
import sys
from collections import Counter, defaultdict

import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"
PT_CUT = 0.9
FRAC = 0.75
OT = {7: "pT5", 5: "pT3", 4: "T5", 9: "T4"}


def main():
    path = sys.argv[1]
    base = sys.argv[2] if len(sys.argv) > 2 else BASE
    p = uproot.open(path)["tree"].arrays(
        ["tc_pt", "tc_eta", "tc_type", "tc_isChain", "tc_simIdxAll", "lumi", "evt"], library="np")
    b = uproot.open(base)["tree"].arrays(
        ["tc_pt", "tc_type", "tc_simIdxAll", "tc_simIdxAllFrac", "lumi", "evt"], library="np")
    bmap = {(int(b["lumi"][i]), int(b["evt"][i])): i for i in range(len(b["evt"]))}

    cnt = Counter()
    band = defaultdict(Counter)
    for i in range(len(p["evt"])):
        j = bmap[(int(p["lumi"][i]), int(p["evt"][i]))]
        # LST side: sim -> set of classes (pt>cut members only)
        lst_cls = defaultdict(set)
        lst_any = set()
        bty, bsia, bfr, bpt = b["tc_type"][j], b["tc_simIdxAll"][j], b["tc_simIdxAllFrac"][j], b["tc_pt"][j]
        for k in range(len(bty)):
            for s, f in zip(bsia[k], bfr[k]):
                if f <= FRAC:
                    continue
                lst_any.add(int(s))
                if bpt[k] > PT_CUT:
                    lst_cls[int(s)].add("pLS" if int(bty[k]) == 8 else "OT")
        # proto side
        pty, pisch, psia, ppt, peta = (p["tc_type"][i], p["tc_isChain"][i],
                                       p["tc_simIdxAll"][i], p["tc_pt"][i], p["tc_eta"][i])
        sim2 = defaultdict(list)
        for k in range(len(pty)):
            if ppt[k] <= PT_CUT:
                continue
            c = "CH" if pisch[k] == 1 else ("cpLS" if int(pty[k]) == 8 else "cpT3")
            for s in psia[k]:
                sim2[int(s)].append((c, k))
        for s, mem in sim2.items():
            cls = set(m[0] for m in mem)
            if len(mem) < 2 or "CH" not in cls or "cpLS" not in cls:
                continue
            eta = abs(peta[[m[1] for m in mem if m[0] == "CH"][0]])
            bnd = "barrel" if eta < 1.1 else ("trans" if eta < 1.7 else "endcap")
            lc = lst_cls.get(s, set())
            if "OT" in lc and "pLS" in lc:
                key = "INHERITED (LST had OT+pLS dup too)"
            elif "OT" in lc:
                key = "LST had OT but no in-cut pLS"
            elif "pLS" in lc:
                key = "NEW: LST had pLS only -> chain is a new 2nd TC"
            elif s in lst_any:
                key = "NEW: LST matched only below the pt cut"
            else:
                key = "NEW: LST did not reconstruct this sim at all"
            cnt[key] += 1
            band[key][bnd] += 1
    tot = sum(cnt.values())
    print("proto CH+cpLS duplicate GROUPS (pt>%.1f members): %d" % (PT_CUT, tot))
    print("%-48s %8s %7s   %s" % ("origin", "groups", "frac", "B/T/E"))
    for k, v in sorted(cnt.items(), key=lambda x: -x[1]):
        bb = band[k]
        print("%-48s %8d %7.3f   %d/%d/%d" % (k, v, v / tot, bb["barrel"], bb["trans"], bb["endcap"]))


if __name__ == "__main__":
    main()
