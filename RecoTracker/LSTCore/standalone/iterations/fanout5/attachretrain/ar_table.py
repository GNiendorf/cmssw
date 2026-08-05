#!/usr/bin/env python3
"""ar_table.py -- M19 scoreboard + attach confusion matrix + per-eta-band dup, per point.

Reads ar_<tag>.json (compare_ab.py) and ar_<tag>.log ([ATTACHCM] block) for every tag on
the command line. Descends from at_table.py; the differences are the M19 floor set, the
d510 TRACK COUNT (the margin is 0.58 tracks, so the rate alone is unreadable) and the
per-eta-band dup columns that are the primary target of this task.
"""
import json
import os
import re
import sys

P = os.path.dirname(os.path.abspath(__file__))

LST = dict(eff=.8136, v01=.8473, v15=.7731, v510=.6530, v1030=.6291, d15=.4989,
           d510=.2281, fake=.0455, dup=.0513, nb=10.151, nt=10.010, ne=3.559,
           dupb=.0097, dupt=.0130, dupe=.0846)
# M19 FLAGSHIP baseline (300 evt) -- the row every candidate is a delta against.
FL = dict(eff=.8132, v01=.8461, v15=.8042, v510=.7287, v1030=.7190, d15=.5633,
          d510=.2491, fake=.0474, dup=.0571, nb=9.794, nt=9.698, ne=3.417,
          dupb=.0140, dupt=.0230, dupe=.0902)
# M19 HARD FLOORS (maintainer). d510 is handled separately: it must HOLD 71/285.
FLOOR_MIN = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613)
FAKE_MAX = .0480
D510_NUM = 71
D510_DEN = 285


def load(tag):
    j = json.load(open(f"{P}/ar_{tag}.json"))["metrics"]
    g = lambda k: j[k]["proto"]
    r = dict(tag=tag,
             eff=g("eff_overall_incut"), v01=g("eff_vxy_0_1"), v15=g("eff_vxy_1_5"),
             v510=g("eff_vxy_5_10"), v1030=g("eff_vxy_10_30"),
             d15=g("eff_dxy_1_5"), d510=g("eff_dxy_5_10"),
             fake=g("fake_overall_incut"), dup=g("dup_overall_incut"),
             dupb=g("dup_barrel"), dupt=g("dup_transition"), dupe=g("dup_endcap"),
             nb=g("mean_nhitOT_barrel"), nt=g("mean_nhitOT_transition"),
             ne=g("mean_nhitOT_endcap"), ntc=g("n_tc") / 300.0)
    r["d510n"] = round(r["d510"] * D510_DEN)
    cm = {}
    try:
        txt = open(f"{P}/ar_{tag}.log", errors="ignore").read()
    except OSError:
        txt = ""
    m = re.search(r"WALL ([\d.]+) s", txt)
    if m:
        r["wall"] = float(m.group(1)) / 300.0 * 1000.0  # ms/evt
    m = re.search(r"DECISIONS n=(\d+) \(([\d.]+)/evt\) \| same_sim=(\d+) \(([\d.]+)\)"
                  r" cross_sim=(\d+) \(([\d.]+)\) true_tgt_fake_pls=(\d+) \(([\d.]+)\)"
                  r" fake_tgt_real_pls=(\d+) \(([\d.]+)\) fake_both=(\d+) \(([\d.]+)\)", txt)
    if m:
        cm.update(n=int(m.group(1)), nevt=float(m.group(2)),
                  same=int(m.group(3)), same_f=float(m.group(4)),
                  cross=int(m.group(5)), cross_f=float(m.group(6)),
                  tfp=int(m.group(7)), tfp_f=float(m.group(8)),
                  ftp=int(m.group(9)), ftp_f=float(m.group(10)),
                  ff=int(m.group(11)), ff_f=float(m.group(12)))
    m = re.search(r"RECALL true pairs in prefilter=(\d+) \(([\d.]+)/evt\) \| attached=(\d+) \(([\d.]+)\)"
                  r" missed_below_theta=(\d+) \(([\d.]+)\) missed_contention=(\d+) \(([\d.]+)\)"
                  r" missed_other=(\d+) \(([\d.]+)\)", txt)
    if m:
        cm.update(tp=int(m.group(1)), att_r=float(m.group(4)),
                  mbt=int(m.group(5)), mbt_f=float(m.group(6)),
                  mc=int(m.group(7)), mc_f=float(m.group(8)),
                  mo=int(m.group(9)), mo_f=float(m.group(10)))
    m = re.search(r"PREFILTER true pairs possible=(\d+) \(([\d.]+)/evt\) kept=(\d+) -> window recall=([\d.]+)", txt)
    if m:
        cm["winrec"] = float(m.group(4))
    m = re.search(r"SUPPRESSED type-8 rows: real_pLS=(\d+) \(of which owner shares the sim=(\d+)\) fake_pLS=(\d+)", txt)
    if m:
        cm.update(s_real=int(m.group(1)), s_same=int(m.group(2)), s_fake=int(m.group(3)))
    m = re.search(r"TARGETS with a true pLS available=(\d+) \(([\d.]+)/evt\) \| got_right=(\d+) \(([\d.]+)\)"
                  r" got_wrong=(\d+) \(([\d.]+)\) got_none=(\d+) \(([\d.]+)\)", txt)
    if m:
        cm.update(had=int(m.group(1)), had_evt=float(m.group(2)),
                  right=int(m.group(3)), right_f=float(m.group(4)),
                  wrong=int(m.group(5)), wrong_f=float(m.group(6)),
                  none=int(m.group(7)), none_f=float(m.group(8)))
    m = re.search(r"TARGETS with NO true pLS available=(\d+) \(([\d.]+)/evt\) \| attached anyway=(\d+) \(([\d.]+)\)", txt)
    if m:
        cm.update(notrue=int(m.group(1)), notrue_evt=float(m.group(2)),
                  notrue_att=int(m.group(3)), notrue_f=float(m.group(4)))
    m = re.search(r"carried rows retired: pT5=(\d+) pT3=(\d+) pLS=(\d+)", txt)
    if m:
        cm.update(rt5=int(m.group(1)), rt3=int(m.group(2)), rpls=int(m.group(3)))
    r["cm"] = cm
    return r


def flag(r):
    bad = [k for k, v in FLOOR_MIN.items() if r[k] < v - 1e-9]
    if r["d510n"] < D510_NUM:
        bad.append(f"d510({r['d510n']}/{D510_DEN})")
    if r["fake"] > FAKE_MAX + 1e-9:
        bad.append("fake")
    return "PASS" if not bad else "FAIL:" + ",".join(bad)


rows = [load(t) for t in sys.argv[1:]]

print("### Scoreboard (pt>0.9, 300 evt PU200RelVal). d510 shown as rate (count/285).\n")
print("| point | eff | vxy01 | vxy1-5 | vxy5-10 | vxy10-30 | dxy1-5 | dxy5-10 | fake | dup "
      "| nhitOT b/t/e | TC/evt | ms/evt | floors |")
print("|" + "---|" * 14)
print(f"| _LST target_ | {LST['eff']:.4f} | {LST['v01']:.4f} | {LST['v15']:.4f} | {LST['v510']:.4f} "
      f"| {LST['v1030']:.4f} | {LST['d15']:.4f} | {LST['d510']:.4f} (65) | {LST['fake']:.4f} | {LST['dup']:.4f} "
      f"| {LST['nb']:.2f}/{LST['nt']:.2f}/{LST['ne']:.2f} | 2035 | - | - |")
print(f"| **FLAGSHIP base** | {FL['eff']:.4f} | {FL['v01']:.4f} | {FL['v15']:.4f} | {FL['v510']:.4f} "
      f"| {FL['v1030']:.4f} | {FL['d15']:.4f} | {FL['d510']:.4f} (71) | {FL['fake']:.4f} | {FL['dup']:.4f} "
      f"| {FL['nb']:.2f}/{FL['nt']:.2f}/{FL['ne']:.2f} | 2056 | - | ref |")
for r in rows:
    w = f"{r['wall']:.0f}" if "wall" in r else "-"
    print(f"| {r['tag']} | {r['eff']:.4f} | {r['v01']:.4f} | {r['v15']:.4f} | {r['v510']:.4f} | {r['v1030']:.4f} "
          f"| {r['d15']:.4f} | {r['d510']:.4f} ({r['d510n']}) | {r['fake']:.4f} | {r['dup']:.4f} "
          f"| {r['nb']:.2f}/{r['nt']:.2f}/{r['ne']:.2f} | {r['ntc']:.0f} | {w} | {flag(r)} |")

print("\n### DUP BY ETA BAND (primary target: the maintainer-identified |eta| 1.5-3 window)\n")
print("| point | dup overall | d(dup) | dup barrel | d | dup transition 1.1-1.7 | d "
      "| dup endcap >1.7 | d |")
print("|" + "---|" * 9)
print(f"| _LST target_ | {LST['dup']:.4f} | - | {LST['dupb']:.4f} | - | {LST['dupt']:.4f} | - "
      f"| {LST['dupe']:.4f} | - |")
print(f"| **FLAGSHIP base** | {FL['dup']:.4f} | ref | {FL['dupb']:.4f} | ref | {FL['dupt']:.4f} | ref "
      f"| {FL['dupe']:.4f} | ref |")
for r in rows:
    print(f"| {r['tag']} | {r['dup']:.4f} | {r['dup']-FL['dup']:+.4f} | {r['dupb']:.4f} | {r['dupb']-FL['dupb']:+.4f} "
          f"| {r['dupt']:.4f} | {r['dupt']-FL['dupt']:+.4f} | {r['dupe']:.4f} | {r['dupe']-FL['dupe']:+.4f} |")

print("\n### A. Attach decisions classified by sim truth of BOTH sides\n")
print("| point | attaches/evt | same-sim | cross-sim | true-tgt + fake-pLS | fake-tgt + real-pLS "
      "| fake+fake | PRECISION |")
print("|" + "---|" * 8)
for r in rows:
    c = r["cm"]
    if "n" not in c:
        continue
    print(f"| {r['tag']} | {c['nevt']:.1f} | {c['same']} ({c['same_f']:.4f}) | {c['cross']} ({c['cross_f']:.4f}) "
          f"| {c['tfp']} ({c['tfp_f']:.4f}) | {c['ftp']} ({c['ftp_f']:.4f}) | {c['ff']} ({c['ff_f']:.4f}) "
          f"| **{c['same_f']:.4f}** |")

print("\n### B. Target-level recall (denominator = targets that HAD a correct pLS in the prefilter)\n")
print("| point | tgts w/ true pLS /evt | got the RIGHT pLS | got a WRONG pLS (head failure) "
      "| got NOTHING (recall failure) | tgts w/o any true pLS /evt | ...attached anyway |")
print("|" + "---|" * 7)
for r in rows:
    c = r["cm"]
    if "had" not in c:
        continue
    print(f"| {r['tag']} | {c['had_evt']:.1f} | {c['right']} ({c['right_f']:.4f}) | {c['wrong']} ({c['wrong_f']:.4f}) "
          f"| {c['none']} ({c['none_f']:.4f}) | {c['notrue_evt']:.1f} | {c['notrue_att']} ({c['notrue_f']:.4f}) |")

print("\n### C. Pair-level recall + what the contention rule retired\n")
print("| point | true pairs in prefilter/evt | attached | missed below theta | missed contention "
      "| missed other | type-8 retired real/fake pLS | of real, owner shares sim | window recall |")
print("|" + "---|" * 9)
for r in rows:
    c = r["cm"]
    if "tp" not in c:
        continue
    print(f"| {r['tag']} | {c['tp']/300.0:.1f} | {c['att_r']:.4f} | {c['mbt']} ({c['mbt_f']:.4f}) "
          f"| {c['mc']} ({c['mc_f']:.4f}) | {c['mo']} ({c['mo_f']:.4f}) "
          f"| {c.get('s_real',0)} / {c.get('s_fake',0)} | {c.get('s_same',0)} | {c.get('winrec',0):.4f} |")
