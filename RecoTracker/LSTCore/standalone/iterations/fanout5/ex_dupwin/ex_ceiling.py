#!/usr/bin/env python3
"""ex_ceiling.py -- offline replica of the post-arbitration dedup, restricted to the
lever set the maintainer allows: hit overlap (-DD), a per-object |eta| BAND condition
(-DDE) and a pt-CONSISTENCY test (-DDT).  NO angular proximity (dR/dEta/dPhi between the
two members) is modelled anywhere -- that criterion is banned.

The replica core is the validated rd_ceiling.py walk (it reproduced the real -DD 2 -DDK 1
run to 4 decimals and predicted the -DDF no-ops).  Extra output vs rd_ceiling.py:
per-band dup in the maintainer's |eta| window, so the ceiling table is directly
comparable to the real scoreboards.
"""
import argparse
from collections import Counter, defaultdict

import numpy as np
import uproot

PT_CUT = 0.9
CHAINISH = (1, 2, 3)


def band(e):
    a = abs(e)
    if a < 1.1:
        return "b"
    if a < 1.7:
        return "t"
    return "e"


def run(A, minShared, etaMin, ptr, ddk=1):
    tot = dup = killed_tot = lost = risk = risk_d510 = risk_d15 = 0
    olnum, olden = defaultdict(float), defaultdict(int)
    wtot = wdup = 0          # maintainer window 1.5 <= |eta| < 3.0
    w25tot = w25dup = 0      # 1.5 <= |eta| < 2.5
    btot = bdup = 0          # |eta| < 1.5
    for i in range(len(A["tc_pt"])):
        pt, eta, nh = A["tc_pt"][i], A["tc_eta"][i], A["tc_nhitOT"][i]
        isch = A["tc_isChain"][i]
        hits, sia = A["tc_hitOT"][i], A["tc_simIdxAll"][i]
        n = len(pt)
        chain = [k for k in range(n) if int(isch[k]) in CHAINISH]
        hset = {k: set(hits[k]) for k in chain}
        kept = []
        order = sorted(chain, key=(lambda k: (-int(nh[k]), k)) if ddk == 1 else (lambda k: k))
        kidx = defaultdict(list)
        killed = set()
        for r in order:
            cnt = Counter()
            for h in hset[r]:
                for q in kidx[h]:
                    cnt[q] += 1
            drop = False
            for q, sh in cnt.items():
                if sh < minShared:
                    continue
                # per-object band condition on the CANDIDATE only (not a pair proximity)
                if etaMin > 0.0 and abs(eta[r]) < etaMin:
                    continue
                if ptr < 9.0:
                    pmin = min(pt[r], pt[q])
                    if pmin <= 0.0 or abs(pt[r] - pt[q]) / pmin >= ptr:
                        continue
                drop = True
                break
            if drop:
                killed.add(r)
            else:
                kept.append(r)
                for h in hset[r]:
                    kidx[h].append(r)
        killed_tot += len(killed)
        surv = [k for k in range(n) if k not in killed]
        s2 = defaultdict(list)
        for k in surv:
            for s in sia[k]:
                s2[int(s)].append(k)
        flagged = set()
        for s, rows in s2.items():
            if len(rows) > 1:
                flagged.update(rows)
        for k in surv:
            if pt[k] > PT_CUT:
                tot += 1
                sl = band(eta[k])
                olnum[sl] += nh[k]
                olden[sl] += 1
                isd = k in flagged
                if isd:
                    dup += 1
                ae = abs(eta[k])
                if 1.5 <= ae < 3.0:
                    wtot += 1
                    wdup += isd
                    if ae < 2.5:
                        w25tot += 1
                        w25dup += isd
                else:
                    if ae < 1.5:
                        btot += 1
                        bdup += isd
        allsims = set()
        for k in range(n):
            for s in sia[k]:
                allsims.add(int(s))
        lost += len(allsims - set(s2.keys()))
        stc, dxy = A["sim_tcIdx"][i], A["sim_pca_dxy"][i]
        for ai in range(len(stc)):
            t = int(stc[ai])
            if t >= 0 and t in killed:
                risk += 1
                if 5.0 <= abs(dxy[ai]) < 10.0:
                    risk_d510 += 1
                if 1.0 <= abs(dxy[ai]) < 5.0:
                    risk_d15 += 1
    return dict(tot=tot, rate=dup / max(tot, 1), killed=killed_tot, lost=lost,
                risk=risk, r510=risk_d510, r15=risk_d15,
                nb=olnum["b"] / max(olden["b"], 1), nt=olnum["t"] / max(olden["t"], 1),
                ne=olnum["e"] / max(olden["e"], 1),
                wrate=wdup / max(wtot, 1), w25=w25dup / max(w25tot, 1),
                brate=bdup / max(btot, 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    a = ap.parse_args()
    A = uproot.open(a.proto)["tree"].arrays(
        ["tc_pt", "tc_eta", "tc_nhitOT", "tc_simIdxAll", "tc_isChain", "tc_hitOT",
         "sim_pca_dxy", "sim_tcIdx"], library="np")
    cfgs = [
        ("flagship (no dedup)",          99, 0.0, 9.0),
        ("DD2 global raw",                2, 0.0, 9.0),
        ("DD2 + ptr<0.30",                2, 0.0, 0.30),
        ("DD2 + ptr<0.20",                2, 0.0, 0.20),
        ("DD2 + ptr<0.15",                2, 0.0, 0.15),
        ("DD2 + ptr<0.10",                2, 0.0, 0.10),
        ("DD2 + ptr<0.07",                2, 0.0, 0.07),
        ("DD2 + ptr<0.05",                2, 0.0, 0.05),
        ("DD2 + ptr<0.03",                2, 0.0, 0.03),
        ("DD2 eta>1.5 + ptr<0.20",        2, 1.5, 0.20),
        ("DD2 eta>1.5 + ptr<0.10",        2, 1.5, 0.10),
        ("DD2 eta>1.5 + ptr<0.05",        2, 1.5, 0.05),
        ("DD2 eta>1.1 + ptr<0.10",        2, 1.1, 0.10),
        ("DD2 eta>1.5 raw",               2, 1.5, 9.0),
        ("DD3 (no population expected)",  3, 0.0, 9.0),
    ]
    print("%-30s %8s %8s %8s %8s %8s %8s %7s %7s %7s %7s  %s"
          % ("config", "nTC", "dup", "d(dup)", "dup1.5-3", "dup1.5-2.5", "dup<1.5",
             "nhb", "nht", "nhe", "lostSim", "risk any/d15/d510"))
    base = None
    for lbl, ms, eg, pr in cfgs:
        R = run(A, ms, eg, pr)
        if base is None:
            base = R
        print("%-30s %8d %8.4f %+8.4f %8.4f %8.4f %8.4f %7.3f %7.3f %7.3f %7d  %d/%d/%d"
              % (lbl, R["tot"], R["rate"], R["rate"] - base["rate"], R["wrate"], R["w25"],
                 R["brate"], R["nb"], R["nt"], R["ne"], R["lost"],
                 R["risk"], R["r15"], R["r510"]))


if __name__ == "__main__":
    main()
