#!/usr/bin/env python3
"""B03 -- A07 simulator extended with ALL FOUR displaced bands in both families and with
absolute SIM COUNTS (the maintainer's protection is stated in sims, not rates)."""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, band, region  # noqa: E402

CHAIN = (4, 9)


def evaluate2(events, kill=None, label=""):
    nTC = nF = nD = 0
    regN = [0, 0, 0]; regF = [0, 0, 0]; regD = [0, 0, 0]; regH = [0, 0, 0]
    effN = effD = 0
    regEN = [0, 0, 0]; regED = [0, 0, 0]
    nKill = nKillFake = 0
    vN = [0] * 4; vD = [0] * 4; dN = [0] * 4; dD = [0] * 4
    for e in events:
        n = len(e["type"])
        if kill is None:
            keep = [True] * n
        else:
            keep = [not kill(e, i) for i in range(n)]
            for i in range(n):
                if not keep[i]:
                    nKill += 1
                    nKillFake += e["fake"][i]
        simcnt = {}
        for i in range(n):
            if not keep[i]:
                continue
            for s in e["sims"][i]:
                simcnt[s] = simcnt.get(s, 0) + 1
        for i in range(n):
            if not keep[i] or not e["incut"][i]:
                continue
            r = e["treg"][i]
            nTC += 1; regN[r] += 1; regH[r] += e["nhit"][i]
            if not e["sims"][i]:
                nF += 1; regF[r] += 1
            else:
                if any(simcnt.get(s, 0) > 1 for s in e["sims"][i]):
                    nD += 1; regD[r] += 1
        for s in range(e["nsim"]):
            cov = simcnt.get(s, 0) > 0
            if e["sbden"][s]:
                b = band(e["svxy"][s])
                if b is not None:
                    vD[b] += 1
                    if cov: vN[b] += 1
                b = band(abs(e["sdxy"][s]))
                if b is not None:
                    dD[b] += 1
                    if cov: dN[b] += 1
            if not e["sden"][s]:
                continue
            effD += 1
            if cov: effN += 1
            sr = e["sreg"][s]
            if sr is not None:
                regED[sr] += 1
                if cov: regEN[sr] += 1
    out = dict(label=label, nTC=nTC, fake=nF / float(nTC), dup=nD / float(nTC),
               eff=effN / float(effD), nKill=nKill, nKillFake=nKillFake,
               effN=effN, effD=effD)
    for j, rn in enumerate(("B", "T", "E")):
        out["fak" + rn] = regF[j] / float(regN[j]); out["dup" + rn] = regD[j] / float(regN[j])
        out["eff" + rn] = regEN[j] / float(regED[j]); out["nh" + rn] = regH[j] / float(regN[j])
    for j, bn in enumerate(("01", "15", "510", "1030")):
        out["v" + bn] = vN[j] / float(vD[j]); out["d" + bn] = dN[j] / float(dD[j])
        out["Nv" + bn] = vN[j]; out["Nd" + bn] = dN[j]
    return out


HDR2 = ("%-30s %8s %8s %8s | %7s %7s %7s | %7s %7s %7s | %7s %7s %7s | %6s %6s %6s %6s %6s %6s" %
        ("label", "eff", "dup", "fake", "effB", "effT", "effE", "dupB", "dupT", "dupE",
         "fakB", "fakT", "fakE", "v15", "v510", "v103", "d15", "d510", "d103"))


def row2(m):
    return ("%-30s %8.5f %8.5f %8.5f | %7.5f %7.5f %7.5f | %7.5f %7.5f %7.5f | "
            "%7.5f %7.5f %7.5f | %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f"
            % (m["label"], m["eff"], m["dup"], m["fake"], m["effB"], m["effT"], m["effE"],
               m["dupB"], m["dupT"], m["dupE"], m["fakB"], m["fakT"], m["fakE"],
               m["v15"], m["v510"], m["v1030"], m["d15"], m["d510"], m["d1030"]))


DHDR = ("%-30s %8s %7s | %8s %8s %8s | %8s %8s | %6s %6s %6s %6s %6s %6s | %7s %5s" %
        ("label", "d_eff", "dSims", "d_fakB", "d_fakT", "d_fakE", "d_dupB", "d_dupT",
         "v15", "v510", "v103", "d15", "d510", "d103", "kill", "kFk%"))


def drow(m, b):
    return ("%-30s %+8.5f %+7d | %+8.5f %+8.5f %+8.5f | %+8.5f %+8.5f | "
            "%+6d %+6d %+6d %+6d %+6d %+6d | %7d %5.1f"
            % (m["label"], m["eff"] - b["eff"], m["effN"] - b["effN"],
               m["fakB"] - b["fakB"], m["fakT"] - b["fakT"], m["fakE"] - b["fakE"],
               m["dupB"] - b["dupB"], m["dupT"] - b["dupT"],
               m["Nv15"] - b["Nv15"], m["Nv510"] - b["Nv510"], m["Nv1030"] - b["Nv1030"],
               m["Nd15"] - b["Nd15"], m["Nd510"] - b["Nd510"], m["Nd1030"] - b["Nd1030"],
               m["nKill"], 100.0 * m["nKillFake"] / max(m["nKill"], 1)))
