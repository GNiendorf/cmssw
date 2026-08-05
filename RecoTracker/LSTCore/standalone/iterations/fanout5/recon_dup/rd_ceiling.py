#!/usr/bin/env python3
"""rd_ceiling.py -- validated offline replica of the post-arbitration dedup with the
kinematic gates rd_gate.py motivated, so each candidate lever gets a ceiling in dup
points / track length / displaced risk before anyone writes code for it.

The replica reproduced the REAL -DD 2 -DDK 1 run to 4 decimals (dup .0501, nhitOT
9.795/9.688/3.368) and correctly predicted -DDF 0.30 to be a no-op, so its deltas are
trustworthy for gates that are not yet expressible as flags.
"""
import argparse
from collections import Counter, defaultdict

import numpy as np
import uproot

PT_CUT = 0.9
CHAINISH = (1, 2, 3)


def dphi(a, b):
    d = a - b
    while d > np.pi:
        d -= 2 * np.pi
    while d < -np.pi:
        d += 2 * np.pi
    return d


def run(A, minShared, etaGate, ptr, drmax, ddp=0, ddk=1):
    tot = dup = killed_tot = lost = risk = risk_d510 = risk_d15 = 0
    olnum, olden = defaultdict(float), defaultdict(int)
    for i in range(len(A["tc_pt"])):
        pt, eta, phi, nh = A["tc_pt"][i], A["tc_eta"][i], A["tc_phi"][i], A["tc_nhitOT"][i]
        ty, isch = A["tc_type"][i], A["tc_isChain"][i]
        hits, sia = A["tc_hitOT"][i], A["tc_simIdxAll"][i]
        n = len(pt)
        chain = [k for k in range(n) if int(isch[k]) in CHAINISH]
        hset = {k: set(hits[k]) for k in chain}
        kept = []
        if ddp:
            for k in range(n):
                if int(isch[k]) == 0 and int(ty[k]) in (5, 7) and len(hits[k]) > 0:
                    kept.append(k)
                    hset[k] = set(hits[k])
        order = sorted(chain, key=(lambda k: (-int(nh[k]), k)) if ddk == 1 else (lambda k: k))
        kidx = defaultdict(list)
        for q in kept:
            for h in hset[q]:
                kidx[h].append(q)
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
                if etaGate > 0.0 and 0.5 * (abs(eta[r]) + abs(eta[q])) < etaGate:
                    continue
                if ptr < 9.0 and abs(pt[r] - pt[q]) / max(min(pt[r], pt[q]), 1e-6) >= ptr:
                    continue
                if drmax < 9.0 and np.hypot(eta[r] - eta[q], dphi(phi[r], phi[q])) >= drmax:
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
                sl = "b" if abs(eta[k]) < 1.1 else ("t" if abs(eta[k]) < 1.7 else "e")
                olnum[sl] += nh[k]
                olden[sl] += 1
                if k in flagged:
                    dup += 1
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
    return dict(tot=tot, dup=dup, rate=dup / max(tot, 1), killed=killed_tot, lost=lost,
                risk=risk, r510=risk_d510, r15=risk_d15,
                nb=olnum["b"] / max(olden["b"], 1), nt=olnum["t"] / max(olden["t"], 1),
                ne=olnum["e"] / max(olden["e"], 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    a = ap.parse_args()
    A = uproot.open(a.proto)["tree"].arrays(
        ["tc_pt", "tc_eta", "tc_phi", "tc_type", "tc_nhitOT", "tc_simIdxAll",
         "tc_isChain", "tc_hitOT", "sim_pca_dxy", "sim_tcIdx"], library="np")
    cfgs = [
        ("flagship (no dedup)",           99, 0.0, 9.0, 9.0),
        ("DD2 global (REAL-VALIDATED)",    2, 0.0, 9.0, 9.0),
        ("DD2 eta>1.5",                    2, 1.5, 9.0, 9.0),
        ("DD2 eta>1.5 + ptr<0.50",         2, 1.5, 0.50, 9.0),
        ("DD2 eta>1.5 + ptr<0.30",         2, 1.5, 0.30, 9.0),
        ("DD2 eta>1.5 + ptr<0.20",         2, 1.5, 0.20, 9.0),
        ("DD2 eta>1.5 + ptr<0.10",         2, 1.5, 0.10, 9.0),
        ("DD2 eta>1.5 + ptr<.20 + dR<.02", 2, 1.5, 0.20, 0.02),
        ("DD2 global + ptr<0.20",          2, 0.0, 0.20, 9.0),
        ("DD2 global + ptr<0.10",          2, 0.0, 0.10, 9.0),
        ("DD2 global + ptr<.20 + dR<.02",  2, 0.0, 0.20, 0.02),
        ("DD2 global + ptr<.10 + dR<.02",  2, 0.0, 0.10, 0.02),
        ("DD2 eta>2.0 + ptr<0.20",         2, 2.0, 0.20, 9.0),
    ]
    print("%-34s %8s %8s %8s %8s %7s %7s %7s %7s  %s"
          % ("config", "nTC", "rate", "d(dup)", "killed", "nhb", "nht", "nhe", "lostSim",
             "risk any/d15/d510"))
    base = None
    for lbl, ms, eg, pr, dr in cfgs:
        R = run(A, ms, eg, pr, dr)
        if base is None:
            base = R
        print("%-34s %8d %8.4f %+8.4f %8d %7.3f %7.3f %7.3f %7d  %d/%d/%d"
              % (lbl, R["tot"], R["rate"], R["rate"] - base["rate"], R["killed"],
                 R["nb"], R["nt"], R["ne"], R["lost"], R["risk"], R["r15"], R["r510"]))


if __name__ == "__main__":
    main()
