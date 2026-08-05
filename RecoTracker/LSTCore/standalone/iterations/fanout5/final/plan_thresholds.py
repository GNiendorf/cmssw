#!/usr/bin/env python3
"""ANGLE-1 threshold planner: per -G 6 branch, the pass fraction of fake /
prompt-true / displaced-true chains at a grid of margin thresholds. Used to pick the
A/B grid AND to cross-check the C++ funnel (if the C++ margins were mis-exported the
observed accept fractions would not match these)."""
import os
import sys

import numpy as np
import torch
import uproot

D = os.path.dirname(os.path.abspath(__file__))
blob = torch.load(f"{D}/chain3_mlp_v1.pt", map_location="cpu", weights_only=False)
import json
norm = json.load(open(f"{D}/chain3_norm_v1.json"))
names = norm["feature_names"]
mu = np.array(norm["mean"], dtype=np.float32)
sd = np.array(norm["std"], dtype=np.float32)

f = uproot.open(f"{D}/chains17_300evt.root")
tree = f["chains"]
br = ["evt", "label", "simVxy", "nLayers", "dca"] + [f"cf_{i:02d}" for i in range(16)]
a = tree.arrays(br, library="np")
n = len(a["label"])
X = np.empty((n, 17), dtype=np.float32)
for i in range(16):
    X[:, i] = a[f"cf_{i:02d}"]
X[:, 16] = a["dca"]

# frozen test-60 split of the PRIMARY file (same rng recipe as train_chain3.py)
rng = np.random.default_rng(42)
key = a["evt"].astype(np.uint64)
uniq = np.unique(key)
rng.shuffle(uniq)
nu = len(uniq)
te_keys = uniq[int(round(0.6 * nu)) + int(round(0.2 * nu)):]
te = np.isin(key, te_keys)

for c in norm["conditioning"]:
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
    else:
        X[:, j] = np.log10(1.0 + X[:, j])
Xs = (X - mu) / sd

model = torch.nn.Sequential(torch.nn.Linear(17, 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 3))
model.load_state_dict(blob["state_dict"])
model.eval()
with torch.no_grad():
    z = np.asarray(model(torch.tensor(Xs[te])).tolist(), dtype=np.float32)
mP = z[:, 1] - z[:, 0]
mD = z[:, 2] - z[:, 0]
mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]

lab = a["label"][te] == 1
vxy = a["simVxy"][te]
nl = a["nLayers"][te]
dca = a["dca"][te]
cls = np.where(~lab, 0, np.where(vxy < 1, 1, 2))

branches = {
    "T4-class (nL<=4)": (nl <= 4, mX, "mX"),
    "IP 5+ (dca<0.5)": ((nl >= 5) & (dca < 0.5), mP, "mP"),
    "exempt 5+ (dca>=0.5) [mD]": ((nl >= 5) & (dca >= 0.5), mD, "mD"),
    "exempt 5+ (dca>=0.5) [mX]": ((nl >= 5) & (dca >= 0.5), mX, "mX"),
}
grid = [-3, -2.5, -2, -1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0]
for tag, (m, s, sname) in branches.items():
    nf = int((m & (cls == 0)).sum())
    npr = int((m & (cls == 1)).sum())
    nd = int((m & (cls == 2)).sum())
    print(f"\n=== {tag}  score={sname}  n: fake={nf} prompt={npr} disp={nd} ===")
    print(f"{'thr':>6} {'keepFake':>9} {'keepPrompt':>11} {'keepDisp':>9}")
    for t in grid:
        kf = float(((s >= t) & m & (cls == 0)).sum()) / max(nf, 1)
        kp = float(((s >= t) & m & (cls == 1)).sum()) / max(npr, 1)
        kd = float(((s >= t) & m & (cls == 2)).sum()) / max(nd, 1)
        print(f"{t:>6.1f} {kf:>9.3f} {kp:>11.3f} {kd:>9.3f}")
print("\nglobal fake population split: "
      f"T4={int(((nl<=4)&(cls==0)).sum())} IP5+={int(((nl>=5)&(dca<0.5)&(cls==0)).sum())} "
      f"exempt5+={int(((nl>=5)&(dca>=0.5)&(cls==0)).sum())}")
