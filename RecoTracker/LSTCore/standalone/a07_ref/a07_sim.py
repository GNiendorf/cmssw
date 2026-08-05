#!/usr/bin/env python3
"""A07 REMOVAL SIMULATOR.

Given the FINBASE ntuple contents (pickled by a07_extract.py) it recomputes, EXACTLY as
the harness would, what happens to

    efficiency (TC_base incut) , duplicate rate , fake rate , per-region rates , nTC

if a set of already-delivered TCs is DELETED from the output. Nothing else changes -- so
it faithfully models any final-pass structural filter, and nothing else.

Harness conventions reproduced:
  fake / dup  : denominator = TCs with |eta| < 4.5 and pt > 0.9;
                numerator   = tc_isFake / recomputed tc_isDuplicate.
  duplicate   : a TC is duplicate if ANY sim it matches (>0.75) is matched by >= 2
                surviving TCs -- recomputed after the removal, both members flagged.
  efficiency  : accepted sims (index < len(sim_pt)) with pt > 0.9, |vz| < 30,
                sqrt(vx^2+vy^2) < 2.5, q != 0; numerator = covered by >= 1 surviving TC.
  regions     : barrel |eta|<1.1, transition 1.1-1.7, endcap >1.7 (TC eta for fake/dup,
                sim eta for efficiency).

Validation: the empty removal set must reproduce FINBASE
eff .80992 / dup .06230 / fake .05551 / nTC 618793 exactly.
"""
import pickle
import sys

PT_CUT = 0.9
ETA_CUT = 4.5
VZ = 30.0
VP = 2.5


def region(a):
    """Barrel/transition/endcap, or None outside the harness eta histogram (|eta|>=4.5,
    which lands in under/overflow and is excluded from every per-region band)."""
    a = abs(a)
    if a >= ETA_CUT:
        return None
    if a < 1.1:
        return 0
    if a < 1.7:
        return 1
    return 2


def load(path):
    with open(path, "rb") as fh:
        return pickle.load(fh)


def prep(events):
    """Precompute the per-event static parts (sim denominators, in-cut TC masks)."""
    for e in events:
        n = len(e["type"])
        e["incut"] = [(abs(e["eta"][i]) < ETA_CUT and e["pt"][i] > PT_CUT) for i in range(n)]
        e["treg"] = [region(e["eta"][i]) for i in range(n)]
        nsim = len(e["spt"])
        e["sden"] = [(e["spt"][s] > PT_CUT and abs(e["svz"][s]) < VZ and e["sq"][s] != 0 and
                      (e["svx"][s] ** 2 + e["svy"][s] ** 2) < VP * VP) for s in range(nsim)]
        e["sreg"] = [region(e["seta"][s]) for s in range(nsim)]
        # displacement-band denominator: the harness's ef_denom_vxy/dxy apply eta, pt and
        # vz cuts but NOT the vtx_perp cut (that is the whole point of the band plots).
        e["sbden"] = [(e["spt"][s] > PT_CUT and abs(e["svz"][s]) < VZ and e["sq"][s] != 0 and
                       abs(e["seta"][s]) < ETA_CUT) for s in range(nsim)]
        e["svxy"] = [(e["svx"][s] ** 2 + e["svy"][s] ** 2) ** 0.5 for s in range(nsim)]
        e["nsim"] = nsim
    return events


BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]


def band(v):
    for j, (lo, hi) in enumerate(BANDS):
        if lo <= v < hi:
            return j
    return None


def evaluate(events, kill=None, label=""):
    """kill(e, i) -> True to DELETE TC i of event e. None = baseline."""
    nTC = nF = 0
    regN = [0, 0, 0]
    regF = [0, 0, 0]
    regD = [0, 0, 0]
    regH = [0, 0, 0]
    nD = 0
    effN = effD = 0
    regEN = [0, 0, 0]
    regED = [0, 0, 0]
    nKill = nKillFake = nKillDup = 0
    vN, vD, dN, dD = [0] * 4, [0] * 4, [0] * 4, [0] * 4
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
                    nKillDup += e["dup"][i]
        # sim -> surviving TC count (full sim index space)
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
            nTC += 1
            regN[r] += 1
            regH[r] += e["nhit"][i]
            fk = 1 if not e["sims"][i] else 0
            if fk:
                nF += 1
                regF[r] += 1
            else:
                dup = 0
                for s in e["sims"][i]:
                    if simcnt.get(s, 0) > 1:
                        dup = 1
                        break
                if dup:
                    nD += 1
                    regD[r] += 1
        for s in range(e["nsim"]):
            cov = simcnt.get(s, 0) > 0
            if e["sbden"][s]:
                bv = band(e["svxy"][s])
                if bv is not None:
                    vD[bv] += 1
                    if cov:
                        vN[bv] += 1
                bd = band(abs(e["sdxy"][s]))
                if bd is not None:
                    dD[bd] += 1
                    if cov:
                        dN[bd] += 1
            if not e["sden"][s]:
                continue
            effD += 1
            if cov:
                effN += 1
            sr = e["sreg"][s]
            if sr is not None:
                regED[sr] += 1
                if cov:
                    regEN[sr] += 1
    out = dict(label=label, nTC=nTC, fake=nF / float(nTC), dup=nD / float(nTC),
               eff=effN / float(effD), nKill=nKill, nKillFake=nKillFake, nKillDup=nKillDup,
               effN=effN, effD=effD, nF=nF, nD=nD)
    for j, rn in enumerate(("B", "T", "E")):
        out["fak" + rn] = regF[j] / float(regN[j]) if regN[j] else 0.0
        out["dup" + rn] = regD[j] / float(regN[j]) if regN[j] else 0.0
        out["eff" + rn] = regEN[j] / float(regED[j]) if regED[j] else 0.0
        out["nh" + rn] = regH[j] / float(regN[j]) if regN[j] else 0.0
    for j, bn in enumerate(("01", "15", "510", "1030")):
        out["v" + bn] = vN[j] / float(vD[j]) if vD[j] else 0.0
        out["d" + bn] = dN[j] / float(dD[j]) if dD[j] else 0.0
    return out


HDR = ("%-34s %8s %8s %8s | %7s %7s %7s | %7s %7s %7s | %7s %7s %7s | "
       "%7s %7s %7s %7s | %8s %6s %6s" %
       ("label", "eff", "dup", "fake", "effB", "effT", "effE", "dupB", "dupT", "dupE",
        "fakB", "fakT", "fakE", "v15", "v510", "v1030", "d15", "nTCincut", "kill", "kFk%")
       + " | %6s %6s %6s" % ("nhB", "nhT", "nhE"))


def row(m):
    return ("%-34s %8.5f %8.5f %8.5f | %7.5f %7.5f %7.5f | %7.5f %7.5f %7.5f | "
            "%7.5f %7.5f %7.5f | %7.5f %7.5f %7.5f %7.5f | %8d %6d %6.1f"
            % (m["label"], m["eff"], m["dup"], m["fake"], m["effB"], m["effT"], m["effE"],
               m["dupB"], m["dupT"], m["dupE"], m["fakB"], m["fakT"], m["fakE"],
               m["v15"], m["v510"], m["v1030"], m["d15"], m["nTC"],
               m["nKill"], 100.0 * m["nKillFake"] / m["nKill"] if m["nKill"] else 0.0)
            + " | %6.3f %6.3f %6.3f" % (m["nhB"], m["nhT"], m["nhE"]))


if __name__ == "__main__":
    ev = prep(load(sys.argv[1]))
    print(HDR)
    print(row(evaluate(ev, None, "BASELINE (validation)")))
