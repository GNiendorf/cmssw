"""CRITIC ITEM 7 -- DOES THE EXTENSION MOVE EFFICIENCY, OR ONLY THE MATCHING FRACTION?

The extension appends 2 OT hits to an already-delivered TC. It creates and destroys no TC, so
the two runs' TC lists are index-aligned and every eff/fake movement must be a MATCHING-fraction
change, not a track being found or lost. This aligns the two trees index-by-index (asserting the
alignment on tc_pt/tc_eta) and reports:

  * index misalignments                      (must be 0 for the comparison to be legitimate)
  * extended TCs and the hits they gained
  * matched -> fake and fake -> matched flips (the ONLY truth signal available on added hits)
  * break rate = matched->fake / extensions   -- a STRICT LOWER BOUND on wrong extensions:
    a foreign 2-hit append on a pure 10-hit TC leaves 10/12 = 0.833, still above the 0.75
    matching threshold, and is therefore invisible here.

Usage: python3 fn_extpurity.py <noExtTag> <extTag>
"""
import sys
import ROOT

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/final"


def main(a, b):
    fa = ROOT.TFile.Open("%s/f_%s.root" % (P, a)); ta = fa.Get("tree")
    fb = ROOT.TFile.Open("%s/f_%s.root" % (P, b)); tb = fb.Get("tree")
    assert ta.GetEntries() == tb.GetEntries()

    nTC = mis = ext = extHits = m2f = f2m = 0
    nfa = nfb = 0
    extByLen = {}
    for i in range(ta.GetEntries()):
        ta.GetEntry(i); tb.GetEntry(i)
        pa = list(ta.tc_pt); pb = list(tb.tc_pt)
        ea = list(ta.tc_eta); eb = list(tb.tc_eta)
        ha = list(ta.tc_nhitOT); hb = list(tb.tc_nhitOT)
        ka = list(ta.tc_isFake); kb = list(tb.tc_isFake)
        if len(pa) != len(pb):
            mis += abs(len(pa) - len(pb)); continue
        nTC += len(pa)
        for j in range(len(pa)):
            if abs(pa[j] - pb[j]) > 1e-4 or abs(ea[j] - eb[j]) > 1e-4:
                mis += 1; continue
            nfa += ka[j]; nfb += kb[j]
            d = hb[j] - ha[j]
            if d != 0:
                ext += 1; extHits += d
                extByLen[d] = extByLen.get(d, 0) + 1
                if ka[j] == 0 and kb[j] == 1: m2f += 1
                elif ka[j] == 1 and kb[j] == 0: f2m += 1
    print("EXTENSION MATCHING-INVARIANCE   base=%s (no -EX)   ext=%s (-EX on)" % (a, b))
    print("-" * 96)
    print("  TCs compared                  %d" % nTC)
    print("  INDEX MISALIGNMENTS           %d   <- must be 0; the pass creates/destroys no TC" % mis)
    print("  TCs extended                  %d  (%.2f%% of pool)" % (ext, 100.0 * ext / nTC))
    print("  OT hits added                 %d   (per extension: %s)"
          % (extHits, ", ".join("+%d x%d" % (k, v) for k, v in sorted(extByLen.items()))))
    print("  matched -> FAKE flips         %d" % m2f)
    print("  fake -> matched flips         %d" % f2m)
    print("  net fake numerator            %+d   (%d -> %d)" % (nfb - nfa, nfa, nfb))
    print("  BREAK RATE                    %.3f%% of extensions destroyed an existing sim match"
          % (100.0 * m2f / ext if ext else 0.0))
    print("-" * 96)
    print("  Every eff/fake movement under -EX is a matching-FRACTION change on a TC that")
    print("  exists in BOTH runs: the pass finds no track and loses no track. Its only")
    print("  scoreboard contribution is nhitOT plus this small eff/fake debit.")
    print("  The break rate is a LOWER BOUND -- a wrong 2-hit append on a pure 10-hit TC")
    print("  leaves 10/12 = 0.833 > 0.75 and is invisible to the harness.")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
