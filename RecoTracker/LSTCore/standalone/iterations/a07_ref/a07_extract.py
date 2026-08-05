#!/usr/bin/env python3
"""Extract the per-TC / per-sim arrays needed by the A07 removal simulator into a pickle.

Usage: a07_extract.py <in.root> <out.pkl>
"""
import pickle
import sys
import ROOT


def main(inp, outp):
    f = ROOT.TFile.Open(inp)
    t = f.Get("tree")
    has = lambda n: t.GetBranch(n) is not None
    events = []
    for ev in t:
        n = len(ev.tc_type)
        e = {}
        e["pt"] = [float(x) for x in ev.tc_pt]
        e["eta"] = [float(x) for x in ev.tc_eta]
        e["type"] = [int(x) for x in ev.tc_type]
        e["fake"] = [int(x) for x in ev.tc_isFake]
        e["dup"] = [int(x) for x in ev.tc_isDuplicate]
        e["nhit"] = [int(x) for x in ev.tc_nhitOT]
        ch = [int(x) for x in ev.tc_isChain] if has("tc_isChain") else []
        e["deliv"] = ch if len(ch) == n else [0] * n
        for br, key in (("tc_dbgBr", "br"), ("tc_dbgNL", "nl"), ("tc_dbgNMD", "nmd"),
                        ("tc_dbgNB", "nb"), ("tc_dbgNPS", "nps"), ("tc_dbgNN", "nn"),
                        ("tc_dbgInLay", "inlay")):
            v = [int(x) for x in getattr(ev, br)] if has(br) else []
            e[key] = v if len(v) == n else [-1] * n
        for br, key in (("tc_dbgMP", "mp"), ("tc_dbgMD", "md"), ("tc_dbgDca", "dca")):
            v = [float(x) for x in getattr(ev, br)] if has(br) else []
            e[key] = v if len(v) == n else [0.0] * n
        e["sims"] = [[int(s) for s in v] for v in ev.tc_simIdxAll] if has("tc_simIdxAll") else [[]] * n
        e["spt"] = [float(x) for x in ev.sim_pt]
        e["seta"] = [float(x) for x in ev.sim_eta]
        e["sq"] = [int(x) for x in ev.sim_q]
        e["svx"] = [float(x) for x in ev.sim_vx]
        e["svy"] = [float(x) for x in ev.sim_vy]
        e["svz"] = [float(x) for x in ev.sim_vz]
        e["sdxy"] = [float(x) for x in ev.sim_pca_dxy] if has("sim_pca_dxy") else [0.0] * len(e["spt"])
        e["stc"] = [int(x) for x in ev.sim_tcIdx]
        events.append(e)
    f.Close()
    with open(outp, "wb") as fh:
        pickle.dump(events, fh, protocol=4)
    print("wrote %s : %d events" % (outp, len(events)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
