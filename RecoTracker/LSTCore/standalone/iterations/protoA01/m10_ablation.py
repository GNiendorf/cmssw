#!/usr/bin/env python3
"""m10_ablation.py - exact single-axis ablations of the m8_h4b anchor, run with the
production binary; per-sim recovery by vxy band on the harness denominator. READ-ONLY."""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SC = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

RUNS = [("anchor h4b", f"{SA}/prototype/ab_m8_h4b.root"),
        ("claim off (-F 1.0)", f"{SC}/m10_claimoff.root"),
        ("F 0.3->0.5", f"{SC}/m10_f05.root"),
        ("pixdrop off (-P)", f"{SC}/m10_pixoff.root"),
        ("claim+pixdrop off", f"{SC}/m10_claimpix.root"),
        ("T4 gate off", f"{SC}/m10_t4off.root"),
        ("thetaEdge -1", f"{SC}/m10_e1.root"),
        ("thetaEdge -2", f"{SC}/m10_e2.root")]
ST = [("prompt<1", 0, 1), ("vxy[1,5)", 1, 5), ("vxy[5,10)", 5, 10), ("vxy[10,30)", 10, 30)]


def load(path):
    t = uproot.open(path)["tree"]
    a = t.arrays(["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_tcIdx",
                  "sim_pca_dxy", "tc_isFake", "tc_isDuplicate", "tc_isChain", "tc_type",
                  "tc_simIdxAll", "evt", "lumi"], library="np")
    g = lambda k: np.concatenate(a[k])
    vxy = np.hypot(g("sim_vx"), g("sim_vy"))
    den = (g("sim_pt") > 0.9) & (np.abs(g("sim_eta")) < 4.5) & (np.abs(g("sim_vz")) < 30) & (g("sim_q") != 0)
    tc = g("sim_tcIdx")
    # chain coverage per sim
    chain = []
    for i in range(len(a["evt"])):
        n = len(a["sim_pt"][i])
        c = np.zeros(n, dtype=bool)
        isc = a["tc_isChain"][i]
        for k, sl in enumerate(a["tc_simIdxAll"][i]):
            if isc[k]:
                for s in sl:
                    if 0 <= s < n:
                        c[s] = True
        chain.append(c)
    chain = np.concatenate(chain)
    isF = np.concatenate(a["tc_isFake"]); isD = np.concatenate(a["tc_isDuplicate"])
    isc = np.concatenate(a["tc_isChain"])
    nTC = len(isF)
    return dict(den=den, vxy=vxy, dxy=np.abs(g("sim_pca_dxy")), tc=tc, chain=chain,
                fr=isF.mean(), dr=isD.mean(), nTC=nTC / 300.0,
                frChain=isF[isc == 1].mean() if (isc == 1).any() else float("nan"),
                nChainTC=(isc == 1).sum() / 300.0)


ref = None
print(f"{'run':22s}" + "".join(f"{n:>13s}" for n, _, _ in ST) +
      f"{'fake':>8s}{'dup':>7s}{'TC/evt':>9s}{'chFR':>7s}")
for name, path in RUNS:
    d = load(path)
    cells = []
    for nm, lo, hi in ST:
        m = d["den"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
        eff = (d["tc"][m] >= 0).mean()
        if ref is None:
            cells.append(f"{eff:.4f}")
        else:
            cells.append(f"{eff:.4f}({eff-ref[nm]:+.3f})")
    if ref is None:
        ref = {}
        for nm, lo, hi in ST:
            m = d["den"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
            ref[nm] = (d["tc"][m] >= 0).mean()
    print(f"{name:22s}" + "".join(f"{c:>13s}" for c in cells) +
          f"{d['fr']:8.4f}{d['dr']:7.4f}{d['nTC']:9.0f}{d['frChain']:7.3f}")

print("\nchain-TC coverage (fraction of denominator sims covered by a chain TC):")
ref2 = None
print(f"{'run':22s}" + "".join(f"{n:>13s}" for n, _, _ in ST))
for name, path in RUNS:
    d = load(path)
    cells = []
    for nm, lo, hi in ST:
        m = d["den"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
        v = d["chain"][m].mean()
        cells.append(f"{v:.4f}" if ref2 is None else f"{v:.4f}({v-ref2[nm]:+.3f})")
    if ref2 is None:
        ref2 = {}
        for nm, lo, hi in ST:
            m = d["den"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
            ref2[nm] = d["chain"][m].mean()
    print(f"{name:22s}" + "".join(f"{c:>13s}" for c in cells))
