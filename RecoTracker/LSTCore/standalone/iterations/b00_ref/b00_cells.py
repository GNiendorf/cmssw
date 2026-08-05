#!/usr/bin/env python3
"""B00 -- price the addressable CELLS on CHAINFINAL (977) with the A07 removal simulator.

Every cell is an ORACLE deletion: it uses truth to select exactly the rows the cell is
about, so the number it returns is the CEILING of that cell (what a perfect knob would
buy).  The achievable fraction is set by the logit frontier (-XCD 4), reported separately.
"""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, evaluate, HDR, row  # noqa: E402

PKL = sys.argv[1] if len(sys.argv) > 1 else \
    "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"

CHAIN = (4, 9)      # seedless chain TCs
SEEDED = 7
PT3 = 5
PLS = 8


def annotate(events):
    """Per event, per TC: does this row share a sim with a row of another class?"""
    for e in events:
        n = len(e["type"])
        sim2 = {}
        for i in range(n):
            for s in e["sims"][i]:
                sim2.setdefault(s, []).append(i)
        e["sim2"] = sim2
        partners = []
        for i in range(n):
            ps = set()
            for s in e["sims"][i]:
                for j in sim2[s]:
                    if j != i:
                        ps.add(e["type"][j])
            partners.append(ps)
        e["ptn"] = partners
    return events


def band(e, i, lo, hi):
    a = abs(e["eta"][i])
    return lo <= a < hi


def main():
    ev = annotate(prep(load(PKL)))
    print(HDR)
    base = evaluate(ev, None, "CHAINFINAL 977 (validation)")
    print(row(base))

    B, T, E, ALL = (0.0, 1.1), (1.1, 1.7), (1.7, 99.0), (0.0, 99.0)
    CASES = []

    def add(lbl, fn):
        CASES.append((lbl, fn))

    # ---- CELL 1: the bare seed of a (seedless chain + bare pLS) pair ------------------
    for nm, (lo, hi) in (("B", B), ("T", T), ("E", E), ("BT", (0.0, 1.7)), ("ALL", ALL)):
        add("C1 %-3s drop barePLS w/ chain partner" % nm,
            lambda e, i, lo=lo, hi=hi: (e["type"][i] == PLS and band(e, i, lo, hi) and
                                        bool(e["ptn"][i] & {4, 9})))
    # ---- CELL 1b: the CHAIN of that pair instead (the other side of the same pair) ----
    for nm, (lo, hi) in (("B", B), ("T", T)):
        add("C1b %-2s drop chainTC w/ barePLS ptn" % nm,
            lambda e, i, lo=lo, hi=hi: (e["type"][i] in CHAIN and band(e, i, lo, hi) and
                                        PLS in e["ptn"][i]))
    # ---- CELL 2: seedless chain that shares its sim with a SEEDED (type 7) chain ------
    for nm, (lo, hi) in (("B", B), ("T", T), ("E", E), ("BT", (0.0, 1.7))):
        add("C2 %-3s drop seedless chain w/ t7 ptn" % nm,
            lambda e, i, lo=lo, hi=hi: (e["type"][i] in CHAIN and band(e, i, lo, hi) and
                                        SEEDED in e["ptn"][i]))
    # ---- CELL 3: seedless chain that shares its sim with a pT3-class row --------------
    for nm, (lo, hi) in (("BT", (0.0, 1.7)),):
        add("C3 %-3s drop seedless chain w/ pT3 ptn" % nm,
            lambda e, i, lo=lo, hi=hi: (e["type"][i] in CHAIN and band(e, i, lo, hi) and
                                        PT3 in e["ptn"][i]))
    # ---- CELL 4: FAKE seedless chains (the barrel/transition fake driver) -------------
    for nm, (lo, hi) in (("B", B), ("T", T), ("BT", (0.0, 1.7)), ("E", E)):
        add("C4 %-3s drop FAKE seedless chain" % nm,
            lambda e, i, lo=lo, hi=hi: (e["type"][i] in CHAIN and band(e, i, lo, hi) and
                                        e["fake"][i] == 1))
    # by admission branch (tc_dbgBr): 2 = T5 IP branch, 3 = T5 displaced-exempt branch,
    # 0 = T4 IP, 1 = T4 exempt
    for br in (0, 1, 2, 3):
        add("C4br%d BT drop FAKE seedless br=%d" % (br, br),
            lambda e, i, br=br: (e["type"][i] in CHAIN and band(e, i, 0.0, 1.7) and
                                 e["fake"][i] == 1 and e["br"][i] == br))
    # ---- CELL 5: FAKE bare pLS / FAKE pT3-class / FAKE seeded ------------------------
    add("C5 BT  drop FAKE barePLS", lambda e, i: e["type"][i] == PLS and band(e, i, 0.0, 1.7) and e["fake"][i] == 1)
    add("C6 BT  drop FAKE pT3-class", lambda e, i: e["type"][i] == PT3 and band(e, i, 0.0, 1.7) and e["fake"][i] == 1)
    add("C7 BT  drop FAKE seeded(t7)", lambda e, i: e["type"][i] == SEEDED and band(e, i, 0.0, 1.7) and e["fake"][i] == 1)
    add("C8 BT  drop FAKE T4chain", lambda e, i: e["type"][i] == 9 and band(e, i, 0.0, 1.7) and e["fake"][i] == 1)
    # ---- CELL 9: combined primary target ---------------------------------------------
    add("C9 BT  C1 + C2 (both dup cells)",
        lambda e, i: (band(e, i, 0.0, 1.7) and
                      ((e["type"][i] == PLS and bool(e["ptn"][i] & {4, 9})) or
                       (e["type"][i] in CHAIN and SEEDED in e["ptn"][i]))))
    add("C10 BT C9 + C4 (dup + fake ceiling)",
        lambda e, i: (band(e, i, 0.0, 1.7) and
                      ((e["type"][i] == PLS and bool(e["ptn"][i] & {4, 9})) or
                       (e["type"][i] in CHAIN and SEEDED in e["ptn"][i]) or
                       (e["type"][i] in CHAIN and e["fake"][i] == 1))))

    res = []
    for lbl, fn in CASES:
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))

    print("\nDELTAS vs CHAINFINAL")
    print("%-38s %8s %8s %8s | %8s %8s %8s | %8s %8s %8s | %7s %7s %7s %7s | %7s" %
          ("label", "d_eff", "d_dup", "d_fake", "d_dupB", "d_dupT", "d_dupE",
           "d_fakB", "d_fakT", "d_fakE", "d_v15", "d_v510", "d_v1030", "d_d15", "d_nhB"))
    for m in res:
        print("%-38s %+8.5f %+8.5f %+8.5f | %+8.5f %+8.5f %+8.5f | %+8.5f %+8.5f %+8.5f | "
              "%+7.4f %+7.4f %+7.4f %+7.4f | %+7.3f" %
              (m["label"], m["eff"] - base["eff"], m["dup"] - base["dup"], m["fake"] - base["fake"],
               m["dupB"] - base["dupB"], m["dupT"] - base["dupT"], m["dupE"] - base["dupE"],
               m["fakB"] - base["fakB"], m["fakT"] - base["fakT"], m["fakE"] - base["fakE"],
               m["v15"] - base["v15"], m["v510"] - base["v510"], m["v1030"] - base["v1030"],
               m["d15"] - base["d15"], m["nhB"] - base["nhB"]))


if __name__ == "__main__":
    main()
