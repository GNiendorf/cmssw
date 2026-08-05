#!/usr/bin/env python3
"""m13_verify.py -- prove the m13 replica reproduces ab_m12_w7's chain TC slice.

Per event: chain-TC count, exact (pt,eta,phi) multiset agreement, and label(dump) vs
tc_isFake(harness) agreement. READ-ONLY.
"""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

R = np.load(f"{SCRATCH}/m13_chains.npy")
t = uproot.open(f"{SA}/prototype/ab_m12_w7.root")["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_isChain", "tc_type",
              "tc_nhitOT", "evt", "lumi"], library="np")

nev_ok = nev_cnt = 0
tot_ab = tot_rep = 0
lab_agree = lab_tot = 0
worst = 0.0
match_rows = []
for i in range(len(a["evt"])):
    evt = int(a["evt"][i])
    m = R["evt"] == evt
    if not m.any():
        continue
    r = R[m]
    ch = a["tc_isChain"][i] == 1
    pt, eta, phi = a["tc_pt"][i][ch], a["tc_eta"][i][ch], a["tc_phi"][i][ch]
    fk = a["tc_isFake"][i][ch]
    nh = a["tc_nhitOT"][i][ch]
    nev_cnt += 1
    tot_ab += len(pt)
    tot_rep += len(r)
    if len(pt) != len(r):
        print(f"  evt {evt}: COUNT MISMATCH ab={len(pt)} rep={len(r)}")
        continue
    # multiset compare on the float32 triples
    ka = np.sort(np.array([f"{x:.7g}|{y:.7g}|{z:.7g}" for x, y, z in zip(pt, eta, phi)]))
    kr = np.sort(np.array([f"{x:.7g}|{y:.7g}|{z:.7g}" for x, y, z in zip(r["pt"], r["eta"], r["phi"])]))
    if not np.array_equal(ka, kr):
        d = (ka != kr).sum()
        worst = max(worst, d / len(ka))
        print(f"  evt {evt}: {d}/{len(ka)} kinematic rows differ")
        continue
    nev_ok += 1
    # per-row join on the triple to compare label vs isFake and nhitOT
    dab = {}
    for j in range(len(pt)):
        dab.setdefault(f"{pt[j]:.7g}|{eta[j]:.7g}|{phi[j]:.7g}", []).append(j)
    for j in range(len(r)):
        k = f"{r['pt'][j]:.7g}|{r['eta'][j]:.7g}|{r['phi'][j]:.7g}"
        jj = dab[k].pop()
        lab_tot += 1
        lab_agree += int((r["label"][j] == 1) == (fk[jj] == 0))
        match_rows.append((evt, j, int(fk[jj]), int(nh[jj]), int(r["nhitOT"][j])))

mr = np.array(match_rows, dtype=[("evt", "i8"), ("j", "i4"), ("isFake", "i4"),
                                 ("nhitAB", "i4"), ("nhitRep", "i4")])
np.save(f"{SCRATCH}/m13_match.npy", mr)
print(f"events compared      : {nev_cnt}")
print(f"events EXACT         : {nev_ok}")
print(f"chain TCs ab / replica: {tot_ab} / {tot_rep}")
print(f"label==!isFake       : {lab_agree}/{lab_tot} = {lab_agree / max(lab_tot,1):.6f}")
print(f"nhitOT identical     : {(mr['nhitAB'] == mr['nhitRep']).mean():.6f}")
