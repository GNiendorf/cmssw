#!/usr/bin/env python3
"""at_table.py -- scoreboard + attach confusion matrix per operating point.

Reads at_<tag>.json (compare_ab.py output) and at_<tag>.log ([ATTACHCM] block) for each
tag given on the command line and prints two markdown tables.
"""
import json
import re
import sys
import os

P = os.path.dirname(os.path.abspath(__file__))

# Reference rows the maintainer judges against.
LST = dict(eff=.8136, v01=.8473, v15=.7731, v510=.6530, v1030=.6291, d15=.4989,
           d510=.2281, fake=.0455, dup=.0513, nb=10.151, nt=10.010, ne=3.559)
FLOOR = dict(v15=.7832, v510=.7109, v1030=.6941, d15=.5398, d510=.2471)


def load(tag):
    j = json.load(open(f"{P}/at_{tag}.json"))["metrics"]
    g = lambda k: j[k]["proto"]
    r = dict(tag=tag,
             eff=g("eff_overall_incut"), v01=g("eff_vxy_0_1"), v15=g("eff_vxy_1_5"),
             v510=g("eff_vxy_5_10"), v1030=g("eff_vxy_10_30"),
             d15=g("eff_dxy_1_5"), d510=g("eff_dxy_5_10"),
             fake=g("fake_overall_incut"), dup=g("dup_overall_incut"),
             nb=g("mean_nhitOT_barrel"), nt=g("mean_nhitOT_transition"), ne=g("mean_nhitOT_endcap"),
             ntc=g("n_tc") / 300.0)
    cm = {}
    txt = open(f"{P}/at_{tag}.log", errors="ignore").read()
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
    # M16 row-retirement volume: the whole point of the contention rule.
    m = re.search(r"carried rows retired: pT5=(\d+) pT3=(\d+) pLS=(\d+)", txt)
    if m:
        cm.update(rt5=int(m.group(1)), rt3=int(m.group(2)), rpls=int(m.group(3)))
    m = re.search(r"chain-attached=(\d+) mean=([\d.]+)", txt)
    if m:
        cm["att_evt"] = float(m.group(2))
    r["cm"] = cm
    return r


def flag(r):
    bad = [k for k, v in FLOOR.items() if r[k] < v - 1e-9]
    return "PASS" if not bad else "FLOOR-FAIL:" + ",".join(bad)


rows = [load(t) for t in sys.argv[1:]]
print("### Scoreboard (pt>0.9, 300 evt PU200RelVal)\n")
hdr = ("| point | eff | vxy01 | vxy1-5 | vxy5-10 | vxy10-30 | dxy1-5 | dxy5-10 | fake | dup "
       "| nhitOT b/t/e | TC/evt | floors |")
print(hdr)
print("|" + "---|" * 13)
print(f"| **LST base300** | {LST['eff']:.4f} | {LST['v01']:.4f} | {LST['v15']:.4f} | {LST['v510']:.4f} "
      f"| {LST['v1030']:.4f} | {LST['d15']:.4f} | {LST['d510']:.4f} | {LST['fake']:.4f} | {LST['dup']:.4f} "
      f"| {LST['nb']:.2f}/{LST['nt']:.2f}/{LST['ne']:.2f} | 2035.3 | - |")
for r in rows:
    print(f"| {r['tag']} | {r['eff']:.4f} | {r['v01']:.4f} | {r['v15']:.4f} | {r['v510']:.4f} | {r['v1030']:.4f} "
          f"| {r['d15']:.4f} | {r['d510']:.4f} | {r['fake']:.4f} | {r['dup']:.4f} "
          f"| {r['nb']:.2f}/{r['nt']:.2f}/{r['ne']:.2f} | {r['ntc']:.0f} | {flag(r)} |")

print("\n### A. Attach decisions, classified by sim truth of BOTH sides\n")
print("| point | attaches/evt | same-sim | cross-sim | true-tgt + fake-pLS | fake-tgt + real-pLS | fake+fake "
      "| PRECISION |")
print("|" + "---|" * 8)
for r in rows:
    c = r["cm"]
    if not c or "n" not in c:
        continue
    print(f"| {r['tag']} | {c['nevt']:.1f} | {c['same']} ({c['same_f']:.4f}) | {c['cross']} ({c['cross_f']:.4f}) "
          f"| {c['tfp']} ({c['tfp_f']:.4f}) | {c['ftp']} ({c['ftp_f']:.4f}) | {c['ff']} ({c['ff_f']:.4f}) "
          f"| **{c['same_f']:.4f}** |")

print("\n### B. Target-level recall (denominator = targets that HAD a correct pLS in the prefilter)\n")
print("| point | targets w/ true pLS /evt | got the right pLS | got a WRONG pLS (head failure) "
      "| got NOTHING (recall failure) | targets w/o any true pLS /evt | ...attached anyway |")
print("|" + "---|" * 7)
for r in rows:
    c = r["cm"]
    if "had" not in c:
        continue
    print(f"| {r['tag']} | {c['had_evt']:.1f} | {c['right']} ({c['right_f']:.4f}) | {c['wrong']} ({c['wrong_f']:.4f}) "
          f"| {c['none']} ({c['none_f']:.4f}) | {c['notrue_evt']:.1f} | {c['notrue_att']} ({c['notrue_f']:.4f}) |")

print("\n### C. What the contention rule actually retired (the lever's real reach)\n")
print("| point | carried rows retired/evt: pT5 (wholesale -RT5) | pT3 (contention) | pLS type-8 (contention/-RPS) "
      "| type-8 retired real-pLS / fake-pLS | of the real ones, owner shares the sim | prefilter window recall |")
print("|" + "---|" * 7)
for r in rows:
    c = r["cm"]
    if "rt5" not in c:
        continue
    print(f"| {r['tag']} | {c['rt5']/300.0:.1f} | {c['rt3']/300.0:.1f} | {c['rpls']/300.0:.1f} "
          f"| {c.get('s_real',0)} / {c.get('s_fake',0)} | {c.get('s_same',0)} | {c.get('winrec',0):.4f} |")
