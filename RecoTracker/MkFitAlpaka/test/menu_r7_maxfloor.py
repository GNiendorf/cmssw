#!/usr/bin/env python3
"""maxfloor.py <port log> <port label> <floor label> <floor file>... : D7-a check = the port vs the MAX floor over the legitimate
stock builds (v2-Ofast, v3-noOfast, v3-B6 vs the v3-Ofast reference + NaN guard). The rule is monotone in the floor rate, so a
metric passes iff it passes against at least one variant's floor (replay_floor_check.py per variant, combined here). Prints, per
metric: port n/N, the best variant's allowed count, ratio port/allowed, verdict; then the overall verdict."""
import subprocess, sys, re, os
log, plab, flab, floors = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
C = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_floor_check.py"
best = {}; order = []
for f in floors:
    out = subprocess.run(["python3", C, f, log, "--label", flab, "--port-label", plab, "--no-paired"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r"(\S.*?)\s+(\d+)/(\d+)\s+\(\S+\)\s+(\d+)/(\d+)\s+\(\S+\)\s+([0-9.]+)\s+(ok|WORSE|MISSING)", line)
        if not m: continue
        k = m[1].strip(); port, N, allowed, v = int(m[4]), int(m[5]), float(m[6]), m[7]
        if k not in best: order.append(k)
        if k not in best or allowed > best[k][2]: best[k] = (port, N, allowed, os.path.basename(f))
fails = 0
print(f"metric                       port n/N              max-floor allowed  (variant)                       port/allowed  verdict")
for k in order:
    port, N, allowed, var = best[k]; ok = port <= allowed; fails += not ok
    print(f"{k:28s} {port:7d}/{N:<8d} {allowed:10.1f}  ({var[:34]:34s}) {port/allowed if allowed else 0:6.2f}  {'ok' if ok else 'WORSE'}")
print(f"D7-a max-floor verdict ({plab} vs {flab}): {'PASS' if not fails else f'FAIL ({fails} metrics)'}")
