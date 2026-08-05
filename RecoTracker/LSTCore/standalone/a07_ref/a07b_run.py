#!/usr/bin/env python3
"""A07 (resumed session): independent re-verification of the fake-angle headline claims
with a freshly written simulator (a07_ref/sim_cut.py), on the FULL 977."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_cut import load, score, header, line

path = sys.argv[1]
evs = load(path)
print("loaded", len(evs), "events from", path)

CASES = [
    ("BASELINE",        None),
    # pT3-class cell (tc_isChain == 3 is the attach-pT3 delivery class)
    ("PERFECT_pT3",     lambda e,j: e.ch[j]==3 and len(e.sims[j])==0),
    ("DROP_pT3_ALL",    lambda e,j: e.ch[j]==3),
    # 4-layer chain (T4-class) rows
    ("DROP_T4_BARREL",  lambda e,j: e.ty[j]==9 and abs(e.et[j])<1.1),
    ("DROP_T4_ALL",     lambda e,j: e.ty[j]==9),
    ("DROP_T4_EXEMPT",  lambda e,j: e.ty[j]==9 and e.br[j]==1),
    ("PERFECT_T4",      lambda e,j: e.ty[j]==9 and len(e.sims[j])==0),
    # bare 5+ chain rows
    ("PERFECT_BARE5",   lambda e,j: e.ty[j]==4 and len(e.sims[j])==0),
    # the -ZP8 bare-seed cell (delivery class 4) -- duplicate angle, quoted for handover
    ("PERFECT_ZP8_DUP", None),  # handled specially below
]

rows = []
for tag, fn in CASES:
    if tag == "PERFECT_ZP8_DUP":
        continue
    rows.append((tag, score(evs, fn)))

# -ZP8 duplicate rows: needs the baseline duplicate flag, which is stored per row.
zp8dup = lambda e,j: e.ch[j]==4 and e.isd[j]==1
rows.append(("PERFECT_ZP8_DUP", score(evs, zp8dup)))

print(header())
for tag, m in rows:
    print(line(tag, m))
print()
print("%-18s %8s %8s %8s %8s %8s %8s %8s" % ("tag","effB","effT","effE","fakB","fakT","fakE","nh_all"))
for tag, m in rows:
    print("%-18s %8.5f %8.5f %8.5f %8.5f %8.5f %8.5f %8.4f" %
          (tag, m['eff_barrel'], m['eff_transition'], m['eff_endcap'],
           m['fake_barrel'], m['fake_transition'], m['fake_endcap'], m['nh']))
print()
b = rows[0][1]
print("%-18s %9s %9s %9s %9s %9s %9s %9s" % ("delta vs BASE","d_eff","d_dup","d_fake","d_v15","d_v510","d_v1030","d_d15"))
for tag, m in rows[1:]:
    print("%-18s %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f" %
          (tag, m['eff']-b['eff'], m['dup']-b['dup'], m['fake']-b['fake'],
           m['vxy_1_5']-b['vxy_1_5'], m['vxy_5_10']-b['vxy_5_10'],
           m['vxy_10_30']-b['vxy_10_30'], m['dxy_1_5']-b['dxy_1_5']))
