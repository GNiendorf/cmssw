#!/usr/bin/env python3
"""Join the T4-band target ledger with the PROTO delivery outcome from a hybrid run.

For each traced sim: was it delivered at all in the proto output, by a chain TC or by a
carried pixel TC, and of what type. Event alignment via (lumi, evt) like m16r_sims.py.
"""
import json
import sys

import numpy as np
import uproot

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
P = S + "/fanout4/t4trace"

ab_path = sys.argv[1] if len(sys.argv) > 1 else P + "/m16r_base.root"
led = json.load(open(P + "/t4_targets.json"))

ab = uproot.open(ab_path)["tree"]
a = ab.arrays(["sim_tcIdx", "tc_type", "tc_isChain", "tc_simIdxAll", "tc_isFake",
               "run", "lumi", "evt"], library="np")
bt = uproot.open(S + "/LSTNtuple_PU200RelVal_300evt.root")["tree"]
b = bt.arrays(["lumi", "evt"], library="np")
# base entry index i -> proto output row
pmap = {(int(a["lumi"][k]), int(a["evt"][k])): k for k in range(len(a["evt"]))}
base_key = {i: (int(b["lumi"][i]), int(b["evt"][i])) for i in range(len(b["evt"]))}

out = {}
for r in led:
    i = r["iev"]
    k = pmap[base_key[i]]
    s = r["sim"]
    delivered = int(a["sim_tcIdx"][k][s] >= 0)
    chain_hit, pix_hit, ctypes = 0, 0, []
    for j, sl in enumerate(a["tc_simIdxAll"][k]):
        if s in sl:
            if a["tc_isChain"][k][j]:
                chain_hit = 1
                ctypes.append(int(a["tc_type"][k][j]))
            else:
                pix_hit = 1
                ctypes.append(-int(a["tc_type"][k][j]))
    out[(i, s)] = dict(delivered=delivered, chain=chain_hit, pix=pix_hit, types=sorted(set(ctypes)))

nT4 = [r for r in led if r["type"] == 9]
nT5 = [r for r in led if r["type"] == 4]
for name, grp in (("LST-T4-delivered", nT4), ("LST-T5-delivered (control)", nT5)):
    d = sum(out[(r["iev"], r["sim"])]["delivered"] for r in grp)
    c = sum(out[(r["iev"], r["sim"])]["chain"] for r in grp)
    p = sum(out[(r["iev"], r["sim"])]["pix"] for r in grp)
    print("%-28s n=%2d  proto delivered=%2d  (via chain TC=%2d, via carried pixel TC=%2d)"
          % (name, len(grp), d, c, p))

with open(P + "/t4_outcome.json", "w") as fh:
    json.dump({"%d_%d" % k: v for k, v in out.items()}, fh)
print("wrote", P + "/t4_outcome.json")
