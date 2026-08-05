"""CRITIC ITEM 6 -- MIX-FREE LENGTH. compare_ab's mean_nhitOT is sum(tc_nhitOT)/nTC over the
WHOLE delivered pool, which contains bare-pLS rows with tc_nhitOT == 0. Deleting TCs therefore
raises the mean with no track getting longer. This tool re-expresses length with denominators
that cannot be moved by a dedup lever:

  (1) REAL/MIX decomposition of the pooled mean vs a reference run:
        mean_new - mean_ref = REAL (dSum / N_ref) + MIX (S_new * (1/N_new - 1/N_ref))
  (2) mean OT hits over OT-BEARING TCs only (nhitOT > 0)  -- pLS rows removed
  (3) mean OT hits over SIM-MATCHED TCs only              -- fixed physics denominator
  (4) per-class means with counts (chain-T5 / chain-T4 / pT5 / pT3 / bare pLS)
  (5) TOTAL delivered OT hits per region -- a pure sum, no denominator at all

Usage: python3 fn_len.py <ref_tag> <tag> [<tag> ...]
"""
import sys, os
import ROOT

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/final"
# performance.cc fills the ol_ set with pt > 0.9 and these |eta| regions.
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 1e9)]
LST = dict(barrel=10.151, transition=10.010, endcap=3.559)
# LSTObjType: T5=4, pT3=5, pT5=7, pLS=8, T4=9
TYPENAME = {4: "chainT5", 9: "chainT4", 7: "pT5", 5: "pT3", 8: "pLS"}


# The LST baseline delivered in the SAME tree format (mode=identity pass-through over the
# same 300 events). Lets the LST length TARGET be recomputed with the identical mix-free
# denominators instead of being quoted from the pooled metric.
LSTFILE = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
           "standalone/prototype/base300_identity.root")


def scan(tag):
    fn = LSTFILE if tag == "LST" else "%s/f_%s.root" % (P, tag)
    if not os.path.exists(fn):
        return None
    f = ROOT.TFile.Open(fn)
    t = f.Get("tree")
    if not t:
        for k in f.GetListOfKeys():
            o = k.ReadObj()
            if isinstance(o, ROOT.TTree):
                t = o; break
    br = set(b.GetName() for b in t.GetListOfBranches())
    # "matched" = the harness's own verdict: a TC is fake iff tc_isFake, so !isFake is the
    # sim-matched set that the efficiency/fake numerators are built from.
    matchbr = "tc_isFake" if "tc_isFake" in br else None
    out = {}
    for r, _, _ in REG:
        out[r] = dict(n=0, s=0.0, nOT=0, sOT=0.0, nM=0, sM=0.0, cls={})
    n = t.GetEntries()
    for i in range(n):
        t.GetEntry(i)
        pt = list(t.tc_pt); eta = list(t.tc_eta); nh = list(t.tc_nhitOT)
        ty = list(t.tc_type) if "tc_type" in br else [0] * len(pt)
        mt = list(getattr(t, matchbr)) if matchbr else None
        for j in range(len(pt)):
            if pt[j] <= 0.9:
                continue
            a = abs(eta[j])
            for r, lo, hi in REG:
                if lo <= a < hi:
                    d = out[r]; break
            else:
                continue
            v = nh[j]
            d["n"] += 1; d["s"] += v
            if v > 0:
                d["nOT"] += 1; d["sOT"] += v
            if mt is not None and mt[j] == 0:  # tc_isFake == 0 -> sim-matched
                d["nM"] += 1; d["sM"] += v
            c = TYPENAME.get(ty[j], "t%d" % ty[j])
            e = d["cls"].setdefault(c, [0, 0.0])
            e[0] += 1; e[1] += v
    f.Close()
    out["_matched"] = matchbr is not None
    return out


def main(tags):
    ref = tags[0]
    R = scan(ref)
    if R is None:
        print("missing raw root for ref %s" % ref); return
    data = {ref: R}
    for t in tags[1:]:
        d = scan(t)
        if d is None:
            print("missing raw root for %s" % t)
        else:
            data[t] = d

    print("=" * 132)
    print("(1) POOLED MEAN nhitOT = sum/nTC  (compare_ab's metric) with REAL/MIX decomposition vs ref '%s'" % ref)
    print("=" * 132)
    print("%-16s %-11s %9s %9s %9s %9s %9s %8s" %
          ("tag", "region", "mean", "d(mean)", "REAL", "MIX", "d(sumOT)", "d(nTC)"))
    for t in tags:
        if t not in data: continue
        for r, _, _ in REG:
            a = data[ref][r]; b = data[t][r]
            mr = a["s"] / a["n"]; mn = b["s"] / b["n"]
            real = (b["s"] - a["s"]) / a["n"]
            mix = b["s"] * (1.0 / b["n"] - 1.0 / a["n"])
            print("%-16s %-11s %9.4f %+9.4f %+9.4f %+9.4f %+9.0f %+8d" %
                  (t if r == "barrel" else "", r, mn, mn - mr, real, mix,
                   b["s"] - a["s"], b["n"] - a["n"]))
    print()
    print("=" * 132)
    print("(2)(3) MIX-FREE MEANS -- denominators a dedup lever cannot move")
    print("=" * 132)
    print("%-16s %-11s %10s %9s %10s %9s %10s %9s" %
          ("tag", "region", "mean/OT-TC", "d", "mean/match", "d", "nOT-TC", "nMatch"))
    for t in tags:
        if t not in data: continue
        for r, _, _ in REG:
            a = data[ref][r]; b = data[t][r]
            mo = b["sOT"] / b["nOT"] if b["nOT"] else 0.0
            ao = a["sOT"] / a["nOT"] if a["nOT"] else 0.0
            mm = b["sM"] / b["nM"] if b["nM"] else float("nan")
            am = a["sM"] / a["nM"] if a["nM"] else float("nan")
            print("%-16s %-11s %10.4f %+9.4f %10.4f %+9.4f %10d %9d" %
                  (t if r == "barrel" else "", r, mo, mo - ao, mm, mm - am, b["nOT"], b["nM"]))
    print()
    print("=" * 132)
    print("(5) TOTAL DELIVERED OT HITS per region -- pure sum, no denominator")
    print("=" * 132)
    print("%-16s %12s %10s %12s %10s %12s %10s" %
          ("tag", "barrel", "d", "transition", "d", "endcap", "d"))
    for t in tags:
        if t not in data: continue
        row = [t]
        for r, _, _ in REG:
            row += [data[t][r]["s"], data[t][r]["s"] - data[ref][r]["s"]]
        print("%-16s %12.0f %+10.0f %12.0f %+10.0f %12.0f %+10.0f" % tuple(row))
    print()
    print("=" * 132)
    print("(4) PER-CLASS mean nhitOT (count) -- shows exactly where the pool mix moved")
    print("=" * 132)
    for t in tags:
        if t not in data: continue
        for r, _, _ in REG:
            cl = data[t][r]["cls"]
            s = "  ".join("%s %.3f(%d)" % (k, v[1] / v[0], v[0])
                          for k, v in sorted(cl.items()) if v[0])
            print("%-16s %-11s %s" % (t if r == "barrel" else "", r, s))


if __name__ == "__main__":
    main(sys.argv[1:])
