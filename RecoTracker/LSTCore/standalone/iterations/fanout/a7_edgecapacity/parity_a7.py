#!/usr/bin/env python3
"""ANGLE 7 parity reference: dumps RAW 40-feature rows + python logits for the
capacity/e2head models so parity_a7.cc (which includes the generated header and
replicates EdgeInference.cc's forward pass, head selection included) can be diffed
against them. Same assembly order as train_edge.py / EdgeInference.cc.
"""
import argparse
import json
import os

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
NN, NE = 13, 14

p = argparse.ArgumentParser()
p.add_argument("--model", required=True)
p.add_argument("--variant", choices=["wide", "e2head"], required=True)
p.add_argument("--norm", required=True)
p.add_argument("--input", default=f"{D}/edges_smoke.root")
p.add_argument("--n-edges", type=int, default=3000)
a = p.parse_args()

import torch
import torch.nn as nn
import uproot

f = uproot.open(a.input)
br = [f"ni_{i:02d}" for i in range(NN)] + [f"no_{i:02d}" for i in range(NN)] \
     + [f"ef_{i:02d}" for i in range(NE)]
arr = f["edges"].arrays(br, entry_stop=200000, library="np")
X = np.stack([arr[b] for b in br], axis=1).astype(np.float32)
# stratify so BOTH heads are exercised (the dump is ordered E1-block then E2-block)
_e2 = X[:, 2 * NN] > 1.5
_i1 = np.flatnonzero(~_e2)[: a.n_edges // 2]
_i2 = np.flatnonzero(_e2)[: a.n_edges // 2]
X = X[np.concatenate([_i1, _i2])]
np.savetxt(f"{D}/parity_a7_feats.txt", X, fmt="%.9g")

norm = json.load(open(a.norm))
names = norm["feature_names"]
Xc = X.copy()
for c in norm.get("conditioning") or []:
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
    else:
        Xc[:, j] = np.log10(1.0 + Xc[:, j])
Xs = (Xc - np.array(norm["mean"], np.float32)) / np.array(norm["std"], np.float32)

blob = torch.load(a.model, map_location="cpu", weights_only=False)
sd = blob["state_dict"]
H = sd["0.bias"].numel() if a.variant == "wide" else sd["trunk.0.bias"].numel()
if a.variant == "wide":
    m = nn.Sequential(nn.Linear(40, H), nn.ReLU(), nn.Linear(H, H), nn.ReLU(),
                      nn.Linear(H, 1))
    m.load_state_dict(sd)
    with torch.no_grad():
        lg = np.array(m(torch.tensor(Xs)).squeeze(1).tolist(), dtype=np.float64)
else:
    trunk = nn.Sequential(nn.Linear(40, H), nn.ReLU())
    h1 = nn.Sequential(nn.Linear(H, H), nn.ReLU(), nn.Linear(H, 1))
    h2 = nn.Sequential(nn.Linear(H, H), nn.ReLU(), nn.Linear(H, 1))
    trunk.load_state_dict({k[len("trunk."):]: v for k, v in sd.items()
                           if k.startswith("trunk.")})
    h1.load_state_dict({k[len("head1."):]: v for k, v in sd.items()
                        if k.startswith("head1.")})
    h2.load_state_dict({k[len("head2."):]: v for k, v in sd.items()
                        if k.startswith("head2.")})
    with torch.no_grad():
        t = trunk(torch.tensor(Xs))
        o1 = np.array(h1(t).squeeze(1).tolist(), dtype=np.float64)
        o2 = np.array(h2(t).squeeze(1).tolist(), dtype=np.float64)
    isE2 = X[:, 2 * NN] > 1.5
    lg = np.where(isE2, o2, o1)
    print(f"rows: {int(isE2.sum())} E2 / {int((~isE2).sum())} E1")
np.savetxt(f"{D}/parity_a7_py.txt", lg, fmt="%.9g")
print(f"wrote {len(lg)} rows; logit mean {lg.mean():.4f} min {lg.min():.4f} "
      f"max {lg.max():.4f}")
