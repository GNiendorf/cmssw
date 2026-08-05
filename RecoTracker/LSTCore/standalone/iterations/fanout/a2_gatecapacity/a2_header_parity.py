#!/usr/bin/env python3
"""a2 parity: the GENERATED chain_mlp_weights.h must reproduce the torch model.

Parses the constexpr arrays out of the header, re-implements ChainInference.cc's
arithmetic exactly (log10_1p -> clip -> standardize -> 2x[linear+relu] -> linear),
and compares against torch on real chain-dump rows.

ChainInference.cc itself is unchanged by a2 (it is generic in kInput/kHidden), and
the dump writes cf_[i] = ChainFeatures f[i] with no reordering, so header-vs-torch
agreement is the whole exposure of the export step.
"""
import re
import sys

import numpy as np

HDR = sys.argv[1] if len(sys.argv) > 1 else "chain_mlp_weights.h"
MODEL = sys.argv[2] if len(sys.argv) > 2 else "chain_mlp_a2.pt"
DUMP = sys.argv[3] if len(sys.argv) > 3 else "chains_a2_300evt.root"
N = 20000

src = open(HDR).read()


def scalar(name):
    m = re.search(rf"constexpr\s+\w+\s+{name}\s*=\s*([^;]+);", src)
    return float(m.group(1).rstrip("f"))


def arr(name):
    m = re.search(rf"constexpr\s+(?:float|bool)\s+{name}\s*\[[^=]*=\s*(\{{.*?\}});", src, re.S)
    body = m.group(1)
    depth0 = body[1:-1]
    if "{" in depth0:  # 2D
        rows = re.findall(r"\{([^{}]*)\}", depth0)
        return np.array([[float(v.strip().rstrip("f")) for v in r.split(",") if v.strip()]
                         for r in rows], dtype=np.float32)
    toks = [t.strip() for t in depth0.split(",") if t.strip()]
    if toks[0] in ("true", "false"):
        return np.array([t == "true" for t in toks])
    return np.array([float(t.rstrip("f")) for t in toks], dtype=np.float32)


kin = int(re.search(r"constexpr int kInput = (\d+);", src).group(1))
khid = int(re.search(r"constexpr int kHidden = (\d+);", src).group(1))
mean, std = arr("kFeatMean"), arr("kFeatStd")
clo, chi, lg = arr("kClipLo"), arr("kClipHi"), arr("kLog10p1")
w1, b1 = arr("wgt_l1"), arr("bias_l1")
w2, b2 = arr("wgt_l2"), arr("bias_l2")
wo, bo = arr("wgt_out"), scalar("bias_out")
print(f"header: kInput={kin} kHidden={khid} w1{w1.shape} w2{w2.shape} wo{wo.shape}")

import uproot
t = uproot.open(DUMP)["chains"]
n = min(N, t.num_entries)
a = t.arrays([f"cf_{i:02d}" for i in range(kin)], entry_stop=n, library="np")
X = np.stack([a[f"cf_{i:02d}"] for i in range(kin)], axis=1).astype(np.float32)

# --- C++ path (ChainInference.cc preprocess + unrolled linear/relu) ---
Xc = X.copy()
for i in range(kin):
    if lg[i]:
        Xc[:, i] = np.log10(1.0 + Xc[:, i])
Xc = np.clip(Xc, clo, chi)
Xc = (Xc - mean) / std
h1 = np.maximum(Xc @ w1 + b1, 0)
h2 = np.maximum(h1 @ w2 + b2, 0)
cpp = h2 @ wo + bo

# --- torch reference ---
import json
import torch
blob = torch.load(MODEL, map_location="cpu", weights_only=False)
norm = json.load(open(sys.argv[4] if len(sys.argv) > 4 else "chain_norm_a2.json"))
names = norm["feature_names"]
Xt = X.copy()
for c in norm.get("conditioning") or []:
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(Xt[:, j], c["lo"], c["hi"], out=Xt[:, j])
    else:
        Xt[:, j] = np.log10(1.0 + Xt[:, j])
Xt = (Xt - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
m = torch.nn.Sequential(torch.nn.Linear(kin, khid), torch.nn.ReLU(),
                        torch.nn.Linear(khid, khid), torch.nn.ReLU(),
                        torch.nn.Linear(khid, 1))
m.load_state_dict(blob["state_dict"])
m.eval()
with torch.no_grad():
    ref = np.asarray(m(torch.tensor(np.ascontiguousarray(Xt))).squeeze(1).tolist())

d = np.abs(cpp - ref)
print(f"rows={n}  max|dLogit|={d.max():.3e}  mean|dLogit|={d.mean():.3e}  "
      f"sign agreement={float(((cpp > 0) == (ref > 0)).mean()):.6f}")
print("PARITY OK" if d.max() < 1e-4 else "PARITY FAIL")
