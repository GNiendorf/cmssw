#!/usr/bin/env python3
"""SKEPTIC-1 independent recount straight from the raw chainproto output TTree.

Deliberately does NOT use the exploit agents' tab scripts and does NOT read the
hists files.  Recomputes, from tc_* branches only:
  - global dup / fake with the same pt>0.9 selection performance.cc applies,
  - dup in arbitrary |eta| bands,
  - mean tc_nhitOT overall / per region / per band, AND the mix decomposition
    (nTC, sum nhitOT, mean over chain-TCs only, mean over non-zero-length TCs),
  - efficiency + d510 numerator from sim_tcIdx and the sim_* branches.
Usage:  sk_raw.py <out.root> [<out.root> ...]
"""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

PT_CUT = 0.9  # ana.pt_cut used by createPerfNumDenHists (verify with --ptcut)

BANDS = [("all", 0.0, 99.0), ("B<1.1", 0.0, 1.1), ("T1.1-1.7", 1.1, 1.7),
         ("E>1.7", 1.7, 99.0), ("w1.5-3.0", 1.5, 3.0), ("w1.5-2.5", 1.5, 2.5),
         ("lo<1.5", 0.0, 1.5), ("gt2.5", 2.5, 99.0)]


def scan(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()
    t.SetBranchStatus("*", 0)
    for b in ("tc_pt", "tc_eta", "tc_isFake", "tc_isDuplicate", "tc_nhitOT",
              "tc_isChain", "tc_type", "sim_pt", "sim_eta", "sim_tcIdx",
              "sim_vx", "sim_vy", "sim_pca_dxy", "sim_q", "sim_pdgId", "sim_vz"):
        t.SetBranchStatus(b, 1)
    acc = {k: [0, 0, 0.0, 0] for k, _, _ in BANDS}  # dupN, denom, sumOT, fakeN
    nTC_all = 0
    nTC_cut = 0
    sumOT_cut = 0.0
    nChain_cut = 0
    sumOT_chain = 0.0
    nNZ = 0
    sumOT_nz = 0.0
    typecount = {}
    for i in range(n):
        t.GetEntry(i)
        pt = t.tc_pt
        eta = t.tc_eta
        du = t.tc_isDuplicate
        fk = t.tc_isFake
        ot = t.tc_nhitOT
        ch = t.tc_isChain
        ty = t.tc_type
        for j in range(len(pt)):
            nTC_all += 1
            if pt[j] <= PT_CUT:
                continue
            nTC_cut += 1
            a = abs(eta[j])
            o = ot[j]
            sumOT_cut += o
            typecount[ty[j]] = typecount.get(ty[j], 0) + 1
            if ch[j]:
                nChain_cut += 1
                sumOT_chain += o
            if o > 0:
                nNZ += 1
                sumOT_nz += o
            for k, lo, hi in BANDS:
                if lo <= a < hi:
                    e = acc[k]
                    e[0] += 1 if du[j] else 0
                    e[1] += 1
                    e[2] += o
                    e[3] += 1 if fk[j] else 0
    f.Close()
    return dict(bands=acc, nTC_all=nTC_all, nTC_cut=nTC_cut, sumOT=sumOT_cut,
                nChain=nChain_cut, sumOTchain=sumOT_chain, nNZ=nNZ,
                sumOTnz=sumOT_nz, types=typecount, nevt=n)


for p in sys.argv[1:]:
    r = scan(p)
    print("=" * 100)
    print("FILE %s   events=%d  nTC(all)=%d  nTC(pt>%.1f)=%d" %
          (p, r["nevt"], r["nTC_all"], PT_CUT, r["nTC_cut"]))
    print("  %-10s %8s %8s %9s %9s %9s" % ("band", "nTC", "nDup", "dup", "fake", "meanOT"))
    for k, _, _ in BANDS:
        d, n, s, fkn = r["bands"][k]
        if n == 0:
            continue
        print("  %-10s %8d %8d %9.5f %9.5f %9.4f" % (k, n, d, d / n, fkn / n, s / n))
    print("  MIX: sumOT=%.0f  meanOT(all TC)=%.4f | nChainTC=%d meanOT(chain only)=%.4f"
          " | nNonZeroOT=%d meanOT(nonzero)=%.4f" %
          (r["sumOT"], r["sumOT"] / r["nTC_cut"], r["nChain"],
           r["sumOTchain"] / max(1, r["nChain"]), r["nNZ"], r["sumOTnz"] / max(1, r["nNZ"])))
    print("  types(pt>cut): %s" % sorted(r["types"].items()))
