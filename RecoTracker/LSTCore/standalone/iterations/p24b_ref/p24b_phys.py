#!/usr/bin/env python3
"""P2.4b-1 physics table: the WOULD-BE pT3-class deliveries of the general attach vs LST's own
pT3 rows, on the same events, with the SAME matcher (bt3_simIdx and tc_simIdx are the identical
quantity computed by the identical code path -- see write_lst_ntuple.cc setBareT3AttachBranches).

MEASURES ONLY. Nothing here changes a decision.

  (a) reproduce : LST pT3-class TC rows whose matched sim we also deliver as a bare-T3 attach
  (b) gain/lose : sims LST delivers ONLY as a pT3 that we would keep or drop, and sims we would
                  deliver that no LST TC delivers at all
  (c) per-class efficiency contribution in vxy strata (the M16 judging protocol)
"""
import sys
import ROOT
from array import array

BANDS = [("vxy [0,1)", 0.0, 1.0), ("vxy [1,5)", 1.0, 5.0), ("vxy [5,10)", 5.0, 10.0),
         ("vxy [10,30)", 10.0, 30.0)]
PTMIN = 0.9
ETAMAX = 4.5


def main(path, thetaCut=None):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()

    # per-event accumulators
    tot = dict(nEvt=0, nBt3=0, nBt3Real=0, nPT3=0, nPT3Real=0,
               reproSim=0, reproSameT3=0, pt3WithSim=0,
               bt3New=0, bt3Dup=0)
    # per-band sim counters
    band = {b[0]: dict(denom=0, lstAny=0, lstPT3=0, lstPT3only=0, ours=0,
                       keepPT3only=0, gainNew=0, lstPT3andOurs=0) for b in BANDS}

    for i in range(n):
        t.GetEntry(i)
        tot["nEvt"] += 1

        bt3_sim = list(t.bt3_simIdx)
        bt3_log = list(t.bt3_logit)
        bt3_t3 = list(t.bt3_t3Idx)
        keep = [k for k in range(len(bt3_sim)) if (thetaCut is None or bt3_log[k] >= thetaCut)]
        tot["nBt3"] += len(keep)
        tot["nBt3Real"] += sum(1 for k in keep if bt3_sim[k] >= 0)

        tc_type = list(t.tc_type)
        tc_sim = list(t.tc_simIdx)
        pt3_t3 = list(t.pT3_t3Idx) if hasattr(t, "pT3_t3Idx") else []
        tc_pt3 = list(t.tc_pt3Idx) if hasattr(t, "tc_pt3Idx") else []

        # LST's pT3-class TC rows
        pt3rows = [r for r in range(len(tc_type)) if tc_type[r] == 5]
        tot["nPT3"] += len(pt3rows)
        tot["nPT3Real"] += sum(1 for r in pt3rows if tc_sim[r] >= 0)

        oursSims = set(bt3_sim[k] for k in keep if bt3_sim[k] >= 0)
        oursT3 = set(bt3_t3[k] for k in keep if bt3_t3[k] >= 0)

        # (a) reproduction, TC by TC
        for r in pt3rows:
            s = tc_sim[r]
            if s < 0:
                continue
            tot["pt3WithSim"] += 1
            if s in oursSims:
                tot["reproSim"] += 1
            if tc_pt3 and pt3_t3 and 0 <= tc_pt3[r] < len(pt3_t3):
                if pt3_t3[tc_pt3[r]] in oursT3:
                    tot["reproSameT3"] += 1

        # sims delivered by LST at all, and by LST as a pT3 row
        simAll = set(s for s in tc_sim if s >= 0)
        simPT3 = set(tc_sim[r] for r in pt3rows if tc_sim[r] >= 0)
        simNonPT3 = set(tc_sim[r] for r in range(len(tc_type)) if tc_type[r] != 5 and tc_sim[r] >= 0)
        simPT3only = simPT3 - simNonPT3

        tot["bt3New"] += len(oursSims - simAll)
        tot["bt3Dup"] += len(oursSims & simAll)

        sim_pt = list(t.sim_pt)
        sim_eta = list(t.sim_eta)
        sim_vxy = list(t.sim_vtxperp)
        nAcc = len(sim_pt)
        for s in range(nAcc):
            if sim_pt[s] < PTMIN or abs(sim_eta[s]) > ETAMAX:
                continue
            v = sim_vxy[s]
            for name, lo, hi in BANDS:
                if lo <= v < hi:
                    d = band[name]
                    d["denom"] += 1
                    inAll = s in simAll
                    inPT3 = s in simPT3
                    inOnly = s in simPT3only
                    inOurs = s in oursSims
                    d["lstAny"] += inAll
                    d["lstPT3"] += inPT3
                    d["lstPT3only"] += inOnly
                    d["ours"] += inOurs
                    d["lstPT3andOurs"] += (inPT3 and inOurs)
                    d["keepPT3only"] += (inOnly and inOurs)
                    d["gainNew"] += (inOurs and not inAll)
                    break

    ne = tot["nEvt"]
    tag = "ALL" if thetaCut is None else f"logit >= {thetaCut}"
    print(f"=== P2.4b-1 physics table ({ne} events, {tag}) : {path}")
    print()
    print(f"  would-be pT3-class deliveries : {tot['nBt3']/ne:8.1f} /evt   "
          f"real (sim-matched > 0.75) {tot['nBt3Real']/ne:7.1f} /evt   "
          f"purity {tot['nBt3Real']/max(1,tot['nBt3']):.4f}")
    print(f"  LST pT3-class TC rows         : {tot['nPT3']/ne:8.1f} /evt   "
          f"real {tot['nPT3Real']/ne:7.1f} /evt   "
          f"purity {tot['nPT3Real']/max(1,tot['nPT3']):.4f}")
    print()
    print("  (a) reproduction of LST's real pT3 rows")
    print(f"      LST pT3 rows with a matched sim   : {tot['pt3WithSim']/ne:8.1f} /evt")
    print(f"      ... same sim delivered by us      : {tot['reproSim']/ne:8.1f} /evt "
          f"({tot['reproSim']/max(1,tot['pt3WithSim']):.4f})")
    print(f"      ... and on the SAME T3            : {tot['reproSameT3']/ne:8.1f} /evt "
          f"({tot['reproSameT3']/max(1,tot['pt3WithSim']):.4f})")
    print()
    print(f"  (b) our sims already delivered by some LST TC : {tot['bt3Dup']/ne:8.2f} /evt")
    print(f"      our sims delivered by NO LST TC (pure gain): {tot['bt3New']/ne:8.2f} /evt")
    print()
    print("  (c) per-sim efficiency contribution by vxy stratum "
          f"(pt > {PTMIN}, |eta| < {ETAMAX})")
    hdr = (f"      {'band':12s} {'denom':>8s} {'LSTany':>8s} {'LSTpT3':>8s} {'pT3only':>8s} "
           f"{'ours':>8s} {'keepOnly':>9s} {'gainNew':>8s}")
    print(hdr)
    for name, _, _ in BANDS:
        d = band[name]
        de = max(1, d["denom"])
        print(f"      {name:12s} {d['denom']:8d} "
              f"{d['lstAny']/de:8.4f} {d['lstPT3']/de:8.4f} {d['lstPT3only']/de:8.4f} "
              f"{d['ours']/de:8.4f} {d['keepPT3only']:9d} {d['gainNew']:8d}")
    print()
    print("      denom      = sims in the band passing the pt/eta cuts")
    print("      LSTany     = fraction delivered by ANY LST TC (the baseline the flip must hold)")
    print("      LSTpT3     = fraction delivered by an LST pT3-class TC row")
    print("      pT3only    = fraction delivered ONLY as a pT3 row -- THE POPULATION AT RISK")
    print("      ours       = fraction our bare-T3 attach would deliver")
    print("      keepOnly   = COUNT of pT3-only sims our attach also delivers (the recovery)")
    print("      gainNew    = COUNT of sims we deliver that no LST TC delivers at all")


if __name__ == "__main__":
    th = float(sys.argv[2]) if len(sys.argv) > 2 else None
    main(sys.argv[1], th)
