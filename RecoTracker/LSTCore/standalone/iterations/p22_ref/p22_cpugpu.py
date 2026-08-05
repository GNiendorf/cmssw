#!/usr/bin/env python3
"""P2.2 CPU vs GPU consistency summary.

Exact per-chain CPU/GPU parity is NOT expected at this phase: a pre-existing (not chain-related)
CPU/GPU difference in the T3 multiplicity means the two backends do not even build the same node
set, which is P2.5 scope. What this checks is that the chain stage behaves the same way on both:
chain yield per node, nLayers shape, kill fraction, branch mix and trim mix.
"""
import sys
from collections import Counter
from p22_compare import read

cpu = read(sys.argv[1])
gpu = read(sys.argv[2])
n = min(len(cpu), len(gpu))
print(f"{'evt':>3} {'nT3 cpu/gpu':>17} {'edges cpu/gpu':>19} {'chains cpu/gpu':>17} "
      f"{'killed cpu/gpu':>17} {'trimmed cpu/gpu':>16}")
tot = [Counter(), Counter()]
totb = [Counter(), Counter()]
for i in range(n):
    c, g = cpu[i], gpu[i]
    kc = sum(1 for x in c["chains"] if x["flags"] & 1)
    kg = sum(1 for x in g["chains"] if x["flags"] & 1)
    tc = sum(1 for x in c["chains"] if x["trim"])
    tg = sum(1 for x in g["chains"] if x["trim"])
    print(f"{i:>3} {c['nT3']:>8}/{g['nT3']:<8} {c['nEdge']:>9}/{g['nEdge']:<9} "
          f"{len(c['chains']):>8}/{len(g['chains']):<8} {kc:>8}/{kg:<8} {tc:>7}/{tg:<8}")
    for j, ev in enumerate((c, g)):
        for x in ev["chains"]:
            tot[j][x["nLay"]] += 1
            totb[j][x["branch"]] += 1
print()
print("nLayers distribution   cpu:", dict(sorted(tot[0].items())))
print("                       gpu:", dict(sorted(tot[1].items())))
print("branch code (0=T4 IP, 1=T4 exempt, 2=5+ IP, 3=5+ exempt)")
print("                       cpu:", dict(sorted(totb[0].items())))
print("                       gpu:", dict(sorted(totb[1].items())))
nc = sum(len(e["chains"]) for e in cpu[:n])
ng = sum(len(e["chains"]) for e in gpu[:n])
kc = sum(1 for e in cpu[:n] for x in e["chains"] if x["flags"] & 1)
kg = sum(1 for e in gpu[:n] for x in e["chains"] if x["flags"] & 1)
print(f"\ntotal chains cpu/gpu {nc}/{ng}  ({100.0*(ng-nc)/nc:+.3f}%)")
print(f"kill fraction cpu/gpu {kc/nc:.5f} / {kg/ng:.5f}")
# per-event chain-identity overlap on the pre-trim node tuple
inter = 0
for i in range(n):
    a = set(x["nodes"] for x in cpu[i]["chains"])
    b = set(x["nodes"] for x in gpu[i]["chains"])
    inter += len(a & b)
print(f"chains with an identical member-node tuple on both backends: {inter} "
      f"({100.0*inter/nc:.2f}% of CPU) -- bounded above by the pre-existing T3 index difference")
