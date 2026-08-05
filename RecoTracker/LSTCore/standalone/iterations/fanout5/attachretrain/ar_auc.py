#!/usr/bin/env python3
"""ar_auc.py -- head-quality table over the M19 attach variants (frozen TEST-60).

Reads attach_<v>_testauc.json for every variant named on the command line, plus the
RESIDENT head's report (fanout4/compose_attach/attach_g1_testauc.json) as row zero, and
prints the AUC block plus the CHAIN-universe precision/recall frontier that picks -a.
"""
import json
import os
import sys

P = os.path.dirname(os.path.abspath(__file__))
G1 = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
      "fanout4/compose_attach/attach_g1_testauc.json")


def get(d, k):
    v = d.get(k)
    return v["auc"] if isinstance(v, dict) else None


def fmt(v, n=5):
    return "n/a" if v is None else f"{v:.{n}f}"


rows = []
try:
    g = json.load(open(G1))
    rows.append(("g1 (resident)", g["test"], g.get("objective"), g.get("best_epoch")))
except OSError:
    pass
for v in sys.argv[1:]:
    j = json.load(open(f"{P}/attach_{v}_testauc.json"))
    rows.append((v, j["test"], j.get("objective"), j.get("best_epoch")))

print("### Head quality on the FROZEN TEST-60 (wgt-population-weighted AUC)\n")
print("| head | objective | best ep | ALL | CHAIN tt0 | BARE-T3 tt1 | tt0 prompt vxy<1 "
      "| tt0 displ vxy>=1 | tt0 pileup-sim | tt0 nLay=5 | tt0 nLay>=6 |")
print("|" + "---|" * 11)
for name, t, obj, ep in rows:
    if obj:
        o = (f"g-={obj['gamma_neg']:g} disp={obj['disp_mid']:g}/{obj['disp_hi']:g} "
             f"chw={obj['chain_weight']:g} val={obj['val_metric']}")
    else:
        o = "plain BCE (M16 g1 recipe)"
    print(f"| {name} | {o} | {ep} | {fmt(get(t,'ALL (both target types)'))} "
          f"| **{fmt(get(t,'CHAIN targets (ttype 0)'))}** | {fmt(get(t,'BARE-T3 targets (ttype 1)'))} "
          f"| {fmt(get(t,'  tt0 prompt vxy<1'))} | {fmt(get(t,'  tt0 displaced vxy >=1 (all)'))} "
          f"| {fmt(get(t,'  tt0 pileup-sim true'))} | {fmt(get(t,'chain nLayers=5'))} "
          f"| {fmt(get(t,'chain nLayers>=6'))} |")

print("\n### True-pair counts behind the displaced columns (why they are noisy)\n")
print("| head | tt0 true (all) | tt0 prompt vxy<1 | tt0 displ vxy[1,5) | tt0 displ vxy>=5 "
      "| tt0 pileup-sim | tt0 fakes |")
print("|" + "---|" * 7)
for name, t, obj, ep in rows:
    c = t.get("CHAIN targets (ttype 0)", {})
    p = t.get("  tt0 prompt vxy<1", {})
    d15 = t.get("  tt0 displaced vxy [1,5)", {})
    d5 = t.get("  tt0 displaced vxy [5,10)", {})
    d10 = t.get("  tt0 displaced vxy >=10", {})
    pu = t.get("  tt0 pileup-sim true", {})
    n5 = d5.get("n_true", 0) + d10.get("n_true", 0)
    print(f"| {name} | {c.get('n_true','-')} | {p.get('n_true','-')} | {d15.get('n_true','-')} "
          f"| {n5} | {pu.get('n_true','-')} | {c.get('n_fake','-')} |")

print("\n### CHAIN-universe frontier (unweighted; this universe has wgt==1 everywhere)\n")
print("The head's logit scale is NOT comparable across variants -- the same numeric -a")
print("means different things. Read the frontier, not the cut value.\n")
for name, t, obj, ep in rows:
    fr = t.get("chain_frontier")
    if not fr:
        print(f"_{name}: no frontier recorded (resident head predates the instrument)_\n")
        continue
    dr = t.get("chain_displaced_recall") or {}
    nd = t.get("chain_displaced_true_n", 0)
    print(f"**{name}** (displaced true pairs vxy>=1 in TEST-60: {nd})\n")
    print(f"| cut | tp | fp | precision | recall | displ recall (n={nd}) |")
    print("|" + "---|" * 6)
    for k, r in fr.items():
        p = "n/a" if r["precision"] is None else f"{r['precision']:.4f}"
        rc = "n/a" if r["recall"] is None else f"{r['recall']:.4f}"
        d = dr.get(k)
        ds = "n/a" if d is None else f"{d:.4f}"
        print(f"| {r['cut']:g} | {r['tp']} | {r['fp']} | {p} | {rc} | {ds} |")
    print()

print("\n### BARE-T3 universe frontier (the -AT3 side; FLAGSHIP holds -AT3 at 6.0)\n")
print("| head | cut 4 prec | cut 5 prec | cut 6 prec | cut 7 prec | cut 8 prec | recall@6 |")
print("|" + "---|" * 7)
for name, t, obj, ep in rows:
    fr = t.get("t3_frontier")
    if not fr:
        print(f"| {name} | - | - | - | - | - | - |")
        continue
    g = lambda c, f: (f"{fr[c][f]:.4f}" if fr.get(c) and fr[c][f] is not None else "n/a")
    print(f"| {name} | {g('4','precision')} | {g('5','precision')} | {g('6','precision')} "
          f"| {g('7','precision')} | {g('8','precision')} | {g('6','recall')} |")
