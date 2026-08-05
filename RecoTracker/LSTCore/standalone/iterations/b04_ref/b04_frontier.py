#!/usr/bin/env python3
"""B04 -- OWNER-CREDIBILITY frontier for the bare-chain crossclean arm.

Input: the B4P rows written by protoB04 -B4D 2.  One row per
(seed that this arm could still take, delivered seedless chain TC inside the -XCW2
window):

    B4P <evt> <pls> <band> <cov> <fate> <logit> <br> <mX> <nL> <chainScore>

band 0=barrel 1=transition 2=endcap
cov  0 = the seed's sim ALREADY has a delivered SEEDLESS CHAIN TC   (a duplicate; free)
     1 = its sim has some other delivered TC (seeded chain / pT3-class)
     2 = truth-matched, no such cover                                (retiring costs)
     3 = no truth match                                              (retiring is a fake
                                                                      removal, free)
fate 2 = already retired by the crossclean, 3 = survives as a bare type-8 row
br   the target chain's -G 6 admission branch: 0 T4 IP, 1 T4 exempt, 2 5+ IP, 3 5+ exempt
mX   the target's 3-class true-vs-fake margin

The mechanism only ADDS retirements, so a policy is scored on the fate==3 seeds it would
newly take.  Prices are the B00 calibration measured with the A07 removal simulator on the
same CHAINFINAL 977:
   dupB  -0.003409 per newly retired barrel cov==0 seed per event   (.02502 / 7.34)
   dupT  -0.005402 per newly retired trans  cov==0 seed per event   (.01750 / 3.24)
   eff   -0.00047  per newly retired barrel "class B" seed per event (B00 M4), where
         class B = cov 2 + cov 3 lumped; this script also reports the cov-2-only count so
         the price can be sharpened.
"""
import sys
from collections import defaultdict

LOG = sys.argv[1] if len(sys.argv) > 1 else \
    "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/synth_ref/r_B4G0.log"

DUP_PER_SEED = {0: 0.003409, 1: 0.005402, 2: 0.0}
EFF_PER_B = {0: -0.00047, 1: -0.00047, 2: -0.00047}


def load(path):
    seeds = {}
    nev = set()
    with open(path, errors="replace") as f:
        for ln in f:
            if not ln.startswith("B4P "):
                continue
            t = ln.split()
            ev, p = int(t[1]), int(t[2])
            nev.add(ev)
            key = (ev, p)
            s = seeds.get(key)
            if s is None:
                s = seeds[key] = {"band": int(t[3]), "cov": int(t[4]),
                                  "fate": int(t[5]), "tg": []}
            s["tg"].append((float(t[6]), int(t[7]), float(t[8]), int(t[9]), float(t[10])))
    return seeds, len(nev)


def evaluate(seeds, nev, band, accept):
    """accept(target) -> bool.  Returns per-event newly retired counts by cov."""
    out = defaultdict(float)
    for s in seeds.values():
        if s["band"] != band or s["fate"] != 3:
            continue
        for tg in s["tg"]:
            if accept(tg):
                out[s["cov"]] += 1.0
                break
    return {c: out[c] / nev for c in range(4)}


def line(tag, r, band, base_dup):
    dup = base_dup - DUP_PER_SEED[band] * r[0]
    deff = EFF_PER_B[band] * (r[2] + r[3])
    deff2 = EFF_PER_B[band] * r[2] * ((r[2] + r[3]) / r[2] if r[2] > 0 else 0)
    return ("  %-34s  A0 %6.2f  A1 %5.2f  B2 %6.2f  C3 %6.2f | dup %.5f  d_eff(lump) %+.5f"
            % (tag, r[0], r[1], r[2], r[3], dup, deff))


def main():
    seeds, nev = load(LOG)
    print("events %d  seeds-with-window-pairs %d" % (nev, len(seeds)))
    for band, nm, base_dup, base_thr in ((0, "BARREL", 0.03056, 4.0),
                                         (1, "TRANS", 0.02793, 4.0),
                                         (2, "ENDCAP", 0.07184, 4.0)):
        pop = defaultdict(float)
        for s in seeds.values():
            if s["band"] == band and s["fate"] == 3:
                pop[s["cov"]] += 1.0
        print("\n=== %s ===  reachable survivors/evt: A0 %.2f  A1 %.2f  B2 %.2f  C3 %.2f"
              % (nm, pop[0] / nev, pop[1] / nev, pop[2] / nev, pop[3] / nev))
        # branch census of the targets of the reachable A0 vs B2 survivors
        cen = defaultdict(lambda: defaultdict(float))
        for s in seeds.values():
            if s["band"] != band or s["fate"] != 3:
                continue
            best = {}
            for lg, br, mx, nl, sc in s["tg"]:
                if br not in best or lg > best[br]:
                    best[br] = lg
            for br in best:
                cen[s["cov"]][br] += 1.0
        print("  targets by branch (seeds having >=1 target of that branch, /evt):")
        for c in range(4):
            if not cen[c]:
                continue
            print("    cov%d  " % c + "  ".join("br%d %6.2f" % (b, cen[c][b] / nev)
                                                for b in sorted(cen[c])))

        print("  FLAT threshold on the pair logit (= the -XCT band knob):")
        for t in (4.0, 3.5, 3.0, 2.5, 2.0, 1.5, 1.0, 0.5, 0.0, -1.0, -2.0):
            r = evaluate(seeds, nev, band, lambda tg, t=t: tg[0] >= t)
            print(line("XCT %.1f" % t, r, band, base_dup))

        print("  OWNER-CREDIBILITY: bar t only when the owner is branch 2 (5+ IP),"
              " otherwise stays at 4.0:")
        for t in (3.5, 3.0, 2.5, 2.0, 1.5, 1.0, 0.5, 0.0, -1.0, -2.0, -4.0):
            r = evaluate(seeds, nev, band,
                         lambda tg, t=t: tg[0] >= (t if tg[1] == 2 else 4.0))
            print(line("XCQ br2 %.1f" % t, r, band, base_dup))

        print("  OWNER-CREDIBILITY: branch 2 or 3 (any 5+ layer owner):")
        for t in (3.0, 2.0, 1.0, 0.0, -2.0):
            r = evaluate(seeds, nev, band,
                         lambda tg, t=t: tg[0] >= (t if tg[3] >= 5 else 4.0))
            print(line("XCQ nL>=5 %.1f" % t, r, band, base_dup))

        print("  OWNER-CREDIBILITY: branch 2 AND owner margin mX >= m, bar 0.0:")
        for m in (0.0, 2.0, 4.0, 6.0, 8.0):
            r = evaluate(seeds, nev, band,
                         lambda tg, m=m: tg[0] >= (0.0 if (tg[1] == 2 and tg[2] >= m) else 4.0))
            print(line("XCQ br2 mX>=%.1f bar0" % m, r, band, base_dup))

        print("  OWNER-CREDIBILITY: branch 2, bar t, PLUS flat floor 3.0 elsewhere:")
        for t in (2.0, 1.0, 0.0, -2.0):
            r = evaluate(seeds, nev, band,
                         lambda tg, t=t: tg[0] >= (t if tg[1] == 2 else 3.0))
            print(line("XCQ br2 %.1f + XCT 3.0" % t, r, band, base_dup))


if __name__ == "__main__":
    main()
