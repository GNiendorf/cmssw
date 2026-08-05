#!/usr/bin/env python3
"""Evaluate the PRODUCTION chain-gate MLP v3 on the exact same frozen test-60 rows
and the exact same strata as gbdt_probe.py, so the GBDT-vs-MLP comparison covers
the branch-localized strata (nL>=5 / nL4 x displaced) that train_chain.py never
printed. Reads prototype/chain_mlp_v3.pt + chain_norm_v3.json (read-only)."""
import json
import sys

import numpy as np
import torch

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout/a9_gbdt")
from gbdt_probe import load_cache, report, MYDIR, PROTO  # noqa: E402

meta, X, names, tr, va, te = load_cache(f"{MYDIR}/chains_cache.npz")
norm = json.load(open(f"{PROTO}/chain_norm_v3.json"))
assert norm["feature_names"] == names
X = X.copy()
for c in norm["conditioning"]:
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
    elif c["op"] == "log10_1p":
        X[:, j] = np.log10(1.0 + X[:, j])
    else:
        raise ValueError(c["op"])
mu = np.array(norm["mean"], np.float32)
sd = np.array(norm["std"], np.float32)
Xs = (X - mu) / sd

ck = torch.load(f"{PROTO}/chain_mlp_v3.pt", map_location="cpu", weights_only=False)
m = torch.nn.Sequential(torch.nn.Linear(16, 24), torch.nn.ReLU(),
                        torch.nn.Linear(24, 24), torch.nn.ReLU(),
                        torch.nn.Linear(24, 1))
m.load_state_dict(ck["state_dict"])
m.eval()
Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
s = np.empty(len(Xte), np.float32)
with torch.no_grad():  # CMSSW torch build lacks numpy interop; tolist per block
    for i in range(0, len(Xte), 1 << 20):
        s[i:i + (1 << 20)] = m(Xte[i:i + (1 << 20)]).squeeze(1).tolist()
r = report("MLP_v3_chain_gate (frozen test-60)", s, meta, te)
json.dump({"mlp_v3": r}, open(f"{MYDIR}/mlp_v3_eval.json", "w"), indent=1)
print("\ncheck vs train_chain_v3.log: all should be 0.90973, disp>=1 0.82182, disp>=5 0.80329")
