#!/usr/bin/env python3
"""RECON B part 2 -- per-sim OT-length comparison vs the LST baseline.

For every accepted sim track that BOTH LST and the prototype deliver (sim_tcIdx >= 0
in both), histogram (our matched TC's OT-hit count) - (LST matched TC's OT-hit count),
per eta region, and decompose which LOGICAL LAYERS are present in one and not the
other. Layers come from md_layer via the md anchor/other hit indices (covers every OT
hit any LST object uses).

usage: persim.py <proto_with_tc_hitOT.root> [label]
"""
import collections
import sys

import numpy as np
import uproot
import awkward as ak

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
BASE = S + "LSTNtuple_PU200RelVal_300evt.root"
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0)]


def region(eta):
    a = abs(eta)
    return "barrel" if a < 1.1 else ("transition" if a < 1.7 else "endcap")


BB = ["sim_pt", "sim_eta", "sim_tcIdx", "tc_nhitOT", "tc_type", "tc_eta",
      "tc_pt5Idx", "tc_pt3Idx", "tc_t5Idx", "tc_plsIdx",
      "pT5_t5Idx", "t5_hitIndices", "pT3_otHitIndices",
      "md_layer", "md_anchorHitIdx", "md_otherHitIdx", "md_isPLS", "run", "lumi", "evt"]
PB = ["sim_pt", "sim_eta", "sim_tcIdx", "tc_nhitOT", "tc_type", "tc_eta",
      "tc_hitOT", "tc_isChain", "tc_dbgNL", "run", "lumi", "evt"]


def main(proto, label):
    b = uproot.open(BASE)["tree"].arrays(BB, library="ak")
    p = uproot.open(proto)["tree"].arrays(PB, library="ak")
    n = min(len(b), len(p))
    assert np.all(ak.to_numpy(b["evt"][:n]) == ak.to_numpy(p["evt"][:n])), "event mismatch"

    dh = collections.defaultdict(collections.Counter)      # region -> delta hist
    misslay = collections.defaultdict(collections.Counter)  # region -> layer LST has, we lack
    gainlay = collections.defaultdict(collections.Counter)  # region -> layer we have, LST lacks
    nmiss = collections.defaultdict(collections.Counter)    # region -> #missing-layers hist
    tot = collections.Counter()
    pos = collections.Counter()  # innermost/outermost/middle classification of missing layer
    ourlen = collections.Counter()
    lstlen = collections.Counter()

    for e in range(n):
        # hit -> layer map
        ml = ak.to_numpy(b["md_layer"][e])
        ispls = ak.to_numpy(b["md_isPLS"][e]).astype(bool)
        ot = ~ispls          # pixel MDs index the pix hit space, OT MDs the ph2 space
        mlo = ml[ot]
        h2l = {}
        for arr in (ak.to_numpy(b["md_anchorHitIdx"][e])[ot], ak.to_numpy(b["md_otherHitIdx"][e])[ot]):
            for h, L in zip(arr.tolist(), mlo.tolist()):
                h2l[h] = L

        bt_type = ak.to_numpy(b["tc_type"][e])
        bt_n = ak.to_numpy(b["tc_nhitOT"][e])
        b_p5 = ak.to_numpy(b["tc_pt5Idx"][e])
        b_p3 = ak.to_numpy(b["tc_pt3Idx"][e])
        b_t5 = ak.to_numpy(b["tc_t5Idx"][e])
        p5t5 = ak.to_numpy(b["pT5_t5Idx"][e])
        t5h = b["t5_hitIndices"][e]
        p3h = b["pT3_otHitIndices"][e]

        def lst_hits(i):
            t = bt_type[i]
            if t == 7:
                k = b_p5[i]
                if 0 <= k < len(p5t5):
                    j = p5t5[k]
                    if 0 <= j < len(t5h):
                        return ak.to_numpy(t5h[j]).tolist()
            elif t == 4:
                j = b_t5[i]
                if 0 <= j < len(t5h):
                    return ak.to_numpy(t5h[j]).tolist()
            elif t == 5:
                k = b_p3[i]
                if 0 <= k < len(p3h):
                    return ak.to_numpy(p3h[k]).tolist()
            return None

        bsim = ak.to_numpy(b["sim_tcIdx"][e])
        psim = ak.to_numpy(p["sim_tcIdx"][e])
        spt = ak.to_numpy(b["sim_pt"][e])
        set_ = ak.to_numpy(b["sim_eta"][e])
        pt_n = ak.to_numpy(p["tc_nhitOT"][e])
        ph = p["tc_hitOT"][e]

        for s in range(len(bsim)):
            if spt[s] <= 0.9:
                continue
            bi, pi = bsim[s], psim[s]
            if bi < 0 or pi < 0:
                continue
            r = region(set_[s])
            tot[r] += 1
            d = int(pt_n[pi]) - int(bt_n[bi])
            dh[r][d] += 1
            ourlen[(r, int(pt_n[pi]))] += 1
            lstlen[(r, int(bt_n[bi]))] += 1
            bh = lst_hits(bi)
            if bh is None or pi >= len(ph):
                continue
            oh = ak.to_numpy(ph[pi]).tolist()
            Lb = set(h2l[h] for h in bh if h in h2l)
            Lo = set(h2l[h] for h in oh if h in h2l)
            miss = Lb - Lo
            gain = Lo - Lb
            nmiss[r][len(miss)] += 1
            for L in miss:
                misslay[r][L] += 1
                if not Lo:
                    pos[(r, "n/a")] += 1
                elif L < min(Lo):
                    pos[(r, "inner")] += 1
                elif L > max(Lo):
                    pos[(r, "outer")] += 1
                else:
                    pos[(r, "middle")] += 1
            for L in gain:
                gainlay[r][L] += 1

    print("=" * 96)
    print("PER-SIM OT-LENGTH DELTA  (ours - LST), sims delivered by BOTH, pt>0.9   [%s]" % label)
    for r, _, _ in REG:
        T = tot[r]
        if not T:
            continue
        h = dh[r]
        mean = sum(k * v for k, v in h.items()) / T
        print("\n-- %s: %d common sims, mean delta = %+0.3f OT hits" % (r, T, mean))
        keys = sorted(h)
        print("   delta : " + " ".join("%+d:%5.1f%%" % (k, 100.0 * h[k] / T) for k in keys if h[k] / T > 0.004))
        print("   shorter %.1f%% | equal %.1f%% | longer %.1f%%"
              % (100.0 * sum(v for k, v in h.items() if k < 0) / T,
                 100.0 * h[0] / T,
                 100.0 * sum(v for k, v in h.items() if k > 0) / T))
        print("   our nhitOT : " + " ".join("%d:%.1f%%" % (L, 100.0 * c / T)
                                            for (rr, L), c in sorted(ourlen.items()) if rr == r and c / T > 0.004))
        print("   LST nhitOT : " + " ".join("%d:%.1f%%" % (L, 100.0 * c / T)
                                            for (rr, L), c in sorted(lstlen.items()) if rr == r and c / T > 0.004))
        nm = nmiss[r]
        NT = sum(nm.values()) or 1
        print("   #LST layers we LACK: " + " ".join("%d:%.1f%%" % (k, 100.0 * nm[k] / NT) for k in sorted(nm)))
        print("   layers we LACK (count): " + " ".join("L%d:%d" % (k, v) for k, v in sorted(misslay[r].items())))
        print("   layers we GAIN (count): " + " ".join("L%d:%d" % (k, v) for k, v in sorted(gainlay[r].items())))
        print("   missing-layer position: " + " ".join("%s=%d" % (k[1], v) for k, v in sorted(pos.items()) if k[0] == r))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].split("/")[-1])
