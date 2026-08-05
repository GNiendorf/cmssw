#!/usr/bin/env python3
"""m16r_verdict.py -- classification of the ctl_noatt LOST sims by MEASURED ablation
(READ-ONLY; consumes m16r_sims_*.npy written by m16r_run.sh).

Ablations (all on top of the exact ctl_noatt command line):
  base      = ctl_noatt reproduction (bit-identical counts to ab_ctl_noatt)
  thetaoff  = -T4/-T5/-T6/-U4/-U5/-U6 -> -1e9        (legacy per-length score cut off)
  gateoff   = -M4/-M4D/-M5/-M6/-MD/-MR/-MRI/-C25(D) -> -1e9   (3-class gate kills off)
  claimoff  = -F 2 -W 0 -FC -1 -PU 0                 (K9 hit-claim arbitration off)
  alloff    = all of the above + -P                  (every welded chain delivered)

Class assignment for a sim LOST by base, in priority order:
  (a) FORMATION : still lost in alloff  -> no welded chain 75%-matches it at all
  (b) GATE      : recovered by gateoff or thetaoff (acceptance-cut kill)
  (c) CLAIM     : recovered by claimoff only
  (b/c) either  : recovered by alloff but by neither single ablation
"""
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
TAGS = ["base", "thetaoff", "gateoff", "claimoff", "alloff"]


def key(a):
    return a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)


def main():
    A = {t: np.load(f"{SCRATCH}/m16r_sims_{t}.npy") for t in TAGS}
    b = A["base"]
    k0 = key(b)
    match = {}
    for t in TAGS:
        a = A[t]
        assert np.array_equal(key(a), k0), t
        match[t] = a["anyTC"].copy()

    band = (np.abs(b["vz"]) < 30) & (b["q"] != 0)
    over = band & (b["vxy"] < 2.5)
    D = np.load(f"{SCRATCH}/m10_deep.npy")
    assert np.array_equal(key(D), k0)

    for dname, den in [("PROMPT/OVERALL (vxy<2.5)", over), ("ALL-VXY (band denom)", band)]:
        lost = den & b["baseTC"] & ~match["base"]
        N = lost.sum()
        print(f"\n################ {dname}: LOST = {N} ################")
        rec = {t: (lost & match[t]) for t in TAGS[1:]}
        for t in TAGS[1:]:
            print(f"  recovered by {t:9s}: {rec[t].sum():4d}")
        cls = np.full(len(b), "", dtype=object)
        a_form = lost & ~match["alloff"]
        b_gate = lost & ~a_form & (match["gateoff"] | match["thetaoff"])
        c_claim = lost & ~a_form & ~b_gate & match["claimoff"]
        other = lost & ~a_form & ~b_gate & ~c_claim
        print(f"  (a) FORMATION  (lost even with every chain delivered): {a_form.sum():4d}"
              f"  ({100.0*a_form.sum()/N:.1f}%)")
        print(f"  (b) GATE       (theta / 3-class acceptance kill)     : {b_gate.sum():4d}"
              f"  ({100.0*b_gate.sum()/N:.1f}%)")
        print(f"       ...of which recovered by thetaoff only : "
              f"{(b_gate & match['thetaoff'] & ~match['gateoff']).sum()}")
        print(f"       ...of which recovered by gateoff  only : "
              f"{(b_gate & match['gateoff'] & ~match['thetaoff']).sum()}")
        print(f"       ...both                                 : "
              f"{(b_gate & match['gateoff'] & match['thetaoff']).sum()}")
        print(f"  (c) CLAIM      (K9 arbitration / ordering only)      : {c_claim.sum():4d}"
              f"  ({100.0*c_claim.sum()/N:.1f}%)")
        print(f"  (e) COMBINATION (needs >1 stage relaxed)             : {other.sum():4d}"
              f"  ({100.0*other.sum()/N:.1f}%)")

        # ---- profiles per class ----
        print("\n  profile  [pt 0.9-1.5 / 1.5-3 / 3-10 / >10 | barrel / trans / endcap |"
              " vxy<1 / 1-5 / 5-10 / >=10 | base pT5 / pT3 / pLS / OT]")
        for lab, m in [("(a)form", a_form), ("(b)gate", b_gate), ("(c)claim", c_claim),
                       ("(e)comb", other), ("ALL", lost)]:
            L = b[m]
            pt = L["pt"]
            ae = np.abs(L["eta"])
            vx = L["vxy"]
            print(f"   {lab:8s} N={m.sum():4d} | "
                  f"{(pt<1.5).sum():3d} {((pt>=1.5)&(pt<3)).sum():3d} {((pt>=3)&(pt<10)).sum():3d} {(pt>=10).sum():3d} | "
                  f"{(ae<1.1).sum():3d} {((ae>=1.1)&(ae<1.7)).sum():3d} {(ae>=1.7).sum():3d} | "
                  f"{(vx<1).sum():3d} {((vx>=1)&(vx<5)).sum():3d} {((vx>=5)&(vx<10)).sum():3d} {(vx>=10).sum():3d} | "
                  f"{L['base7'].sum():3d} {L['base5'].sum():3d} {L['base8'].sum():3d} {L['baseOT'].sum():3d}")

        # ---- (d) REMATCH potential on the formation residual ----
        # m10_deep: bestPur = best MD-purity of ANY welded chain for this sim,
        #           bestNMD/bestNMDm = that chain's MD count / matched MDs.
        # Harness fraction is over unique HITS; a chain TC carries 2 hits per MD, a pLS
        # adds p pixel hits.  Rescue iff (2*m + p)/(2*n + p) > 0.75.
        print("\n  (d) ATTACH-WITH-REMATCH potential (formation residual + everything lost):")
        for lab, m in [("(a)form", a_form), ("ALL lost", lost)]:
            d = D[m]
            n, mm = d["bestNMD"].astype(float), d["bestNMDm"].astype(float)
            pur = np.where(n > 0, mm / np.maximum(n, 1), -1.0)
            print(f"   {lab:9s} N={m.sum():4d}  no covering chain at all (bestNMD==0): "
                  f"{(d['bestNMD']==0).sum():4d}")
            hist = [("<=0.4", pur <= 0.4), ("(0.4,0.6]", (pur > 0.4) & (pur <= 0.6)),
                    ("(0.6,0.7]", (pur > 0.6) & (pur <= 0.7)),
                    ("(0.7,0.75)", (pur > 0.7) & (pur < 0.75)),
                    ("==0.75", np.abs(pur - 0.75) < 1e-6), (">0.75", pur > 0.75 + 1e-6)]
            print("     bestPur: " + "  ".join(f"{h}={s.sum()}" for h, s in hist))
            for p in (3, 4):
                ok = (n > 0) & ((2 * mm + p) / (2 * n + p) > 0.75)
                print(f"     rescued by prepending {p} correct pixel hits: {ok.sum():4d}"
                      f"  ({100.0*ok.sum()/max(m.sum(),1):.1f}% of the class)")
        # nLayers of the formation residual's best chain
        d = D[a_form]
        print(f"   formation-residual best-chain nLayers: "
              f"{np.bincount(np.clip(d['bestNL'], 0, 9), minlength=10)[:10].tolist()}")


if __name__ == "__main__":
    main()
