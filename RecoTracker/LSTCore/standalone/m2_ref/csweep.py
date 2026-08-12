#!/usr/bin/env python3
"""Table the per-node top-C sweep out of a directory of per-event logs.

usage: csweep.py <dir> [Clist] [evtlist]
   e.g. csweep.py m2_ref/j10 0,4,8,16,32,64 0,1,2,3,4,5,6,7,8,9

Reads <dir>/C<C>_evt<i>.log and prints, per C: the pooled enumerated / eligible / kept edge
counts, the kept fraction, the mean and max Graph ms, the max [MEM] Total, and the chain and
member-node counts per event (which is the internal quantity a cap can move even when the
ntuple does not).
"""
import os
import re
import sys
import statistics as st

d = sys.argv[1]
Cs = [int(x) for x in (sys.argv[2] if len(sys.argv) > 2 else "0,4,8,16,32,64").split(",")]
evts = [int(x) for x in (sys.argv[3] if len(sys.argv) > 3 else ",".join(str(i) for i in range(10))).split(",")]

row = re.compile(r"^\s+\d+\s+" + r"\s+".join([r"([-\d.]+)"] * 11) + r"\s*$")
chain = re.compile(r"\[CHAIN\] nodes\(nT3\)=(\d+) E1=(\d+) E2=(\d+) E=(\d+)")
topc = re.compile(r"\[CHAIN TOPC\] C=(\d+) enumerated=(\d+) eligible=(\d+) kept=(\d+)")
chains = re.compile(r"\[MEM\] Chains: (\d+) chains / (\d+) member nodes")
memtot = re.compile(r"\[MEM\] Total: ([\d.]+) MB")
memedge = re.compile(r"\[MEM\] ChainEdges: (\d+) allocated \(([\d.]+) MB\)")
memslot = re.compile(r"\[MEM\] ChainTopC: tile (\d+) rows \+ (\d+) slots \(C=(\d+)\) allocated \(([\d.]+) MB\)")

print("%-4s %-4s %14s %14s %12s %8s %10s %9s %9s %8s %8s"
      % ("C", "evt", "enumerated", "eligible", "kept", "kept%", "Graph ms", "chains", "members", "MEMtot", "edgeMB"))
per_c = {}
for C in Cs:
    tot = dict(en=0, el=0, kept=0, g=[], mem=[], ch=[], mem_edge=[], missing=[])
    for i in evts:
        p = os.path.join(d, "C%d_evt%d.log" % (C, i))
        if not os.path.exists(p):
            tot["missing"].append(i)
            continue
        txt = open(p, errors="ignore").read()
        g = None
        for line in txt.splitlines():
            m = row.match(line)
            if m:
                g = [float(x) for x in m.groups()]
        mc, mt, mch, mm, ms = (chain.search(txt), topc.search(txt), chains.search(txt),
                               memtot.search(txt), memslot.search(txt))
        me = memedge.search(txt)
        if g is None:
            tot["missing"].append(i)
            print("%-4s %-4s  NO TIMING ROW (crash?)" % (C, i))
            continue
        en = int(mt.group(2)) if mt else (int(mc.group(4)) if mc else 0)
        el = int(mt.group(3)) if mt else 0
        kept = int(mt.group(4)) if mt else en
        tot["en"] += en
        tot["el"] += el
        tot["kept"] += kept
        tot["g"].append(g[4])
        if mm:
            tot["mem"].append(float(mm.group(1)))
        if me:
            tot["mem_edge"].append(float(me.group(2)))
        if mch:
            tot["ch"].append((int(mch.group(1)), int(mch.group(2))))
        print("%-4s %-4s %14d %14d %12d %7.3f%% %10.1f %9s %9s %8.1f %8.1f"
              % (C, i, en, el, kept, 100.0 * kept / max(1, en), g[4],
                 mch.group(1) if mch else "-", mch.group(2) if mch else "-",
                 float(mm.group(1)) if mm else -1, float(me.group(2)) if me else -1))
    per_c[C] = tot

print()
print("%-4s %16s %16s %8s %10s %10s %10s %10s"
      % ("C", "pooled enum", "pooled kept", "kept%", "Graph mean", "Graph max", "MEM max", "edgeMB max"))
for C in Cs:
    t = per_c[C]
    if not t["g"]:
        continue
    print("%-4s %16d %16d %7.3f%% %10.1f %10.1f %10.1f %10.1f%s"
          % (C, t["en"], t["kept"], 100.0 * t["kept"] / max(1, t["en"]),
             sum(t["g"]) / len(t["g"]), max(t["g"]),
             max(t["mem"]) if t["mem"] else -1, max(t["mem_edge"]) if t["mem_edge"] else -1,
             ("  missing/crashed: %s" % t["missing"]) if t["missing"] else ""))
