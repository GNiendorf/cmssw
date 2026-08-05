#!/usr/bin/env python3
"""M13 mission-1, part 3: full harness-metric A/B for an EXTRA post-hoc gate kill at w7.

For a grid of thresholds T on a chain-level score, remove every accepted chain TC below T
and recompute the COMPLETE compare_ab.py metric set exactly (efficiency overall / vxy /
dxy / eta regions, fake rate overall + per region, dup rate, mean nhitOT) from the w7
output ntuple. Harness selections replicated verbatim from efficiency/src/performance.cc
(validated: eff denom 22784 / numer 18638 / 0.8180, FR 35200/573648 = 0.06136).

CAVEAT (stated in the findings): this is POST-HOC. Killing a chain in the real pipeline
happens BEFORE the K9 MD/hit claim, so the freed hits would be re-claimed by other chains
-- the real run recovers some efficiency AND admits some new TCs. These numbers are the
first-order estimate that ranks the levers, not a substitute for an A/B run.
"""
import pickle
import sys

import numpy as np
import uproot

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT, ETACUT, VZ, VPERP = 0.9, 4.5, 30.0, 2.5
DCASPLIT = 0.5

A = pickle.load(open(P + "m13_mf_accepted.pkl", "rb"))
o = uproot.open(P + "m13_mf_w7.root:tree")
od = o.arrays(["tc_pt", "tc_eta", "tc_nhitOT", "tc_isFake", "tc_isChain", "tc_simIdxAll",
               "sim_pt", "sim_eta", "sim_q", "sim_vx", "sim_vy", "sim_vz", "sim_pca_dxy"],
              library="np")
nev = len(od["tc_pt"])

nL = np.asarray(A["nL_d"])
dca = np.asarray(A["dca"])
br = np.where(nL <= 4, np.where(dca < DCASPLIT, 0, 1), np.where(dca < DCASPLIT, 2, 3))
mX, mP, mD = np.asarray(A["mX"]), np.asarray(A["mP"]), np.asarray(A["mD"])
SCORES = {
    "mX (global)": mX,
    "per-branch governing": np.where(br == 1, mD, np.where(br == 3, mX, mP)),
    "mX, exempt-5+ only": np.where(br == 3, mX, 1e9),
    "mX, 5+ only": np.where(nL >= 5, mX, 1e9),
    "mX, barrel chains only": np.where(np.abs(np.asarray(A["eta"])) < 1.1, mX, 1e9),
    "mX, barrel+transition": np.where(np.abs(np.asarray(A["eta"])) < 1.7, mX, 1e9),
    "mX, (nNodes=2,nL=5) cell": np.where((np.asarray(A["nNodes"]) == 2) & (nL == 5), mX, 1e9),
    "mX, IP branches only": np.where((br == 0) | (br == 2), mX, 1e9),
    "mX, exempt-T4 only": np.where(br == 1, mX, 1e9),
    # combo: IP branches always cut at mX<1.5; the scanned T applies to exempt-5+ only.
    "-MR sweep (the real knob)": mX,
    "combo IP@1.5 + exempt5+@T": np.where((br == 0) | (br == 2), mX + 1e-9, np.where(br == 3, mX, 1e9)),
}
if "combo" in (sys.argv[1] if len(sys.argv) > 1 else ""):
    _T0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0


# flatten per-event structure once
EV = []
ptr = 0
for i in range(nev):
    isch = np.asarray(od["tc_isChain"][i]) > 0
    nch = int(isch.sum())
    EV.append(dict(
        isch=isch, nch=nch, gs=slice(ptr, ptr + nch),
        pt=np.asarray(od["tc_pt"][i]), eta=np.asarray(od["tc_eta"][i]),
        nhit=np.asarray(od["tc_nhitOT"][i]), fake=np.asarray(od["tc_isFake"][i]) > 0,
        simall=od["tc_simIdxAll"][i],
        spt=np.asarray(od["sim_pt"][i]), seta=np.asarray(od["sim_eta"][i]),
        sq=np.asarray(od["sim_q"][i]), svx=np.asarray(od["sim_vx"][i]),
        svy=np.asarray(od["sim_vy"][i]), svz=np.asarray(od["sim_vz"][i]),
        sdxy=np.asarray(od["sim_pca_dxy"][i]),
        cntF=np.zeros(1 + max([max(l) for l in od["tc_simIdxAll"][i] if len(l)] or [0]),
                      dtype=np.int32)))
    ptr += nch
assert ptr == len(mX)

VXY_B = [(0, 1), (1, 5), (5, 10), (10, 30)]
# compare_ab.py sums the eta histograms by bin CENTRE over [-4.5, 4.5] and drops
# under/overflow, so the "endcap" band is 1.7 <= |eta| < 4.5, not unbounded.
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)]


def metrics(killmask):
    """killmask: bool over the 170918 chain TCs (True = removed). Returns metric dict."""
    ef_n = ef_d = 0
    vxy_n = np.zeros(4); vxy_d = np.zeros(4)
    dxy_n = np.zeros(4); dxy_d = np.zeros(4)
    efr_n = np.zeros(3); efr_d = np.zeros(3)
    fr_n = fr_d = 0
    frr_n = np.zeros(3); frr_d = np.zeros(3)
    dr_n = dr_d = 0
    ol_n = ol_d = 0
    olr_n = np.zeros(3); olr_d = np.zeros(3)
    for i in range(nev):
        e = EV[i]
        keep = np.ones(len(e["pt"]), dtype=bool)
        ci = np.nonzero(e["isch"])[0]
        keep[ci[killmask[e["gs"]]]] = False
        nS = len(e["spt"])
        # Duplicates are counted over the FULL sim list (pileup included), exactly as
        # OutputWriter/the production writer do; efficiency uses the accepted prefix only.
        cnt = np.zeros(nS, dtype=np.int32)
        cntF = e["cntF"]
        cntF.fill(0)
        matched_of = [None] * len(e["pt"])
        for t in np.nonzero(keep)[0]:
            lst = list(e["simall"][t])
            matched_of[t] = lst
            for s in lst:
                cntF[s] += 1
                if 0 <= s < nS:
                    cnt[s] += 1
        # --- efficiency ---
        base = (e["sq"] != 0) & (e["spt"] > PTCUT)
        sel_eta = base & (np.abs(e["svz"]) < VZ) & (np.hypot(e["svx"], e["svy"]) < VPERP)
        pas = cnt > 0
        ef_d += int(sel_eta.sum()); ef_n += int((sel_eta & pas).sum())
        ae = np.abs(e["seta"])
        for k, (nm, lo, hi) in enumerate(REG):
            s = sel_eta & (ae >= lo) & (ae < hi)
            efr_d[k] += int(s.sum()); efr_n[k] += int((s & pas).sum())
        sel_v = base & (np.abs(e["seta"]) < ETACUT) & (np.abs(e["svz"]) < VZ)
        vp = np.hypot(e["svx"], e["svy"])
        adxy = np.abs(e["sdxy"])
        for k, (lo, hi) in enumerate(VXY_B):
            s = sel_v & (vp >= lo) & (vp < hi)
            vxy_d[k] += int(s.sum()); vxy_n[k] += int((s & pas).sum())
            s = sel_v & (adxy >= lo) & (adxy < hi)
            dxy_d[k] += int(s.sum()); dxy_n[k] += int((s & pas).sum())
        # --- fake / dup / length over kept TCs ---
        kk = np.nonzero(keep)[0]
        inc = kk[e["pt"][kk] > PTCUT]
        fr_d += len(inc); fr_n += int(e["fake"][inc].sum())
        aet = np.abs(e["eta"])
        for k, (nm, lo, hi) in enumerate(REG):
            s = inc[(aet[inc] >= lo) & (aet[inc] < hi)]
            frr_d[k] += len(s); frr_n[k] += int(e["fake"][s].sum())
            olr_d[k] += len(s); olr_n[k] += int(e["nhit"][s].sum())
        ol_d += len(inc); ol_n += int(e["nhit"][inc].sum())
        isdup = np.zeros(len(e["pt"]), dtype=bool)
        for t in inc:
            for s in (matched_of[t] or []):
                if cntF[s] > 1:
                    isdup[t] = True
                    break
        dr_d += len(inc); dr_n += int(isdup[inc].sum())
    m = {"eff": ef_n / ef_d, "fake": fr_n / fr_d, "dup": dr_n / dr_d, "len": ol_n / ol_d,
         "nTC": fr_d, "nfake": fr_n}
    for k, (lo, hi) in enumerate(VXY_B):
        m["vxy%d_%d" % (lo, hi)] = vxy_n[k] / vxy_d[k]
        m["dxy%d_%d" % (lo, hi)] = dxy_n[k] / dxy_d[k]
    for k, (nm, lo, hi) in enumerate(REG):
        m["eff_" + nm] = efr_n[k] / efr_d[k]
        m["fr_" + nm] = frr_n[k] / frr_d[k]
        m["len_" + nm] = olr_n[k] / olr_d[k]
    return m


BASE = {"eff": 0.8135533707865169, "fake": 0.04549548412077136, "dup": 0.05130131393988367,
        "len": 6.513753861434475, "vxy0_1": 0.8473293277191655, "vxy1_5": 0.7731448763250883,
        "vxy5_10": 0.6529968454258676, "vxy10_30": 0.6290849673202614,
        "dxy0_1": 0.8342135282436943, "dxy1_5": 0.4989270386266094,
        "dxy5_10": 0.22807017543859648, "dxy10_30": 0.04700854700854701,
        "eff_barrel": 0.9241759478006525, "eff_transition": 0.8826032540675844,
        "eff_endcap": 0.755587679306127, "fr_barrel": 0.043718245800889476,
        "fr_transition": 0.04586185932172028, "fr_endcap": 0.0463368023577086,
        "len_barrel": 10.150563479612545, "len_transition": 10.009780338875526,
        "len_endcap": 3.5594032050101307}
KEYS = ["eff", "vxy0_1", "vxy1_5", "vxy5_10", "vxy10_30", "dxy0_1", "dxy1_5", "dxy5_10",
        "dxy10_30", "eff_barrel", "eff_transition", "eff_endcap", "fake", "fr_barrel",
        "fr_transition", "fr_endcap", "dup", "len", "len_barrel", "len_transition", "len_endcap"]

which = sys.argv[1] if len(sys.argv) > 1 else "mX (global)"
grid = [float(x) for x in sys.argv[2:]] or [-1e9, -0.4, -0.2, 0.0, 0.18, 0.35, 0.6]
sc = SCORES[which]
print("axis: %s   grid: %s" % (which, grid))
rows = []
for T in grid:
    if which.startswith("-MR"):
        thr5 = np.where(nL >= 6, -0.6053881223017352, 0.8974147108711509)
        km = (((br == 2) & (mP < thr5)) | (br == 3)) & (mX < T)
    elif which.startswith("combo"):
        km = (((br == 0) | (br == 2)) & (mX < 1.5)) | ((br == 3) & (mX < T))
    else:
        km = sc < T
    m = metrics(km)
    m["_T"] = T
    m["_killed"] = int(km.sum())
    rows.append(m)
    print("  T=%-8s killed %6d chain TCs" % (T, km.sum()))

print("\n%-16s %9s" % ("metric", "baseline") + "".join("%11s" % ("T=%g" % r["_T"]) for r in rows))
for k in KEYS:
    line = "%-16s %9.4f" % (k, BASE[k])
    for r in rows:
        d = r[k] - BASE[k]
        line += "%11s" % ("%.4f%+.4f" % (r[k], d) if False else "%.4f" % r[k])
    print(line)
print("\n%-16s %9s" % ("delta vs base", "") + "".join("%11s" % ("T=%g" % r["_T"]) for r in rows))
for k in KEYS:
    line = "%-16s %9s" % (k, "")
    for r in rows:
        line += "%+11.4f" % (r[k] - BASE[k])
    print(line)
print("\nPASS/FAIL vs the winner rule (every eff band and length >= baseline - 0.005):")
for r in rows:
    bad = [k for k in KEYS if k.startswith(("eff", "vxy", "dxy")) and r[k] < BASE[k] - 0.005]
    badl = [k for k in KEYS if k.startswith("len") and r[k] < BASE[k] - 0.005]
    print("  T=%-8g fake %.4f dup %.4f nTC %d | eff fails: %s | length fails: %s"
          % (r["_T"], r["fake"], r["dup"], r["nTC"], bad or "NONE", badl or "NONE"))
