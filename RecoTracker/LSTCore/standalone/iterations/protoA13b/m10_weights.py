#!/usr/bin/env python3
"""m10_weights.py - parse the generated C++ MLP weight headers into numpy.

READ-ONLY forensics helper (mission B). Replicates EdgeInference.cc /
ChainInference.cc exactly (preprocess: log10(1+x) -> clip -> standardize; then
Linear/ReLU/Linear/ReLU/Linear, no sigmoid).
"""
import re
import numpy as np


def _floats(text):
    return [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", text)]


def _block(src, name):
    i = src.index(name)
    j = src.index("{", i)
    k = src.index("};", j)
    return src[j + 1:k]


def parse(path, ns):
    src = open(path).read()
    # strip comments
    src = re.sub(r"//[^\n]*", "", src)
    kin = int(re.search(r"kInput\s*=\s*(\d+)", src).group(1))
    khid = int(re.search(r"kHidden\s*=\s*(\d+)", src).group(1))
    out = {}
    out["mean"] = np.array(_floats(_block(src, "kFeatMean")), dtype=np.float64)
    out["std"] = np.array(_floats(_block(src, "kFeatStd")), dtype=np.float64)
    out["clip_lo"] = np.array(_floats(_block(src, "kClipLo")), dtype=np.float64)
    out["clip_hi"] = np.array(_floats(_block(src, "kClipHi")), dtype=np.float64)
    log_txt = _block(src, "kLog10p1")
    out["log10p1"] = np.array([t.strip() == "true" for t in log_txt.split(",")], dtype=bool)
    out["w1"] = np.array(_floats(_block(src, "wgt_l1")), dtype=np.float64).reshape(kin, khid)
    out["b1"] = np.array(_floats(_block(src, "bias_l1")), dtype=np.float64)
    out["w2"] = np.array(_floats(_block(src, "wgt_l2")), dtype=np.float64).reshape(khid, khid)
    out["b2"] = np.array(_floats(_block(src, "bias_l2")), dtype=np.float64)
    out["wo"] = np.array(_floats(_block(src, "wgt_out")), dtype=np.float64)
    out["bo"] = float(re.search(r"bias_out\s*=\s*([-+]?[\d.eE+-]+)f", src).group(1))
    assert out["mean"].size == kin and out["w1"].shape == (kin, khid)
    out["kInput"], out["kHidden"] = kin, khid
    return out


def logit(X, W):
    """X: (N, kInput) float array in RAW feature order."""
    x = X.astype(np.float64, copy=True)
    lg = W["log10p1"]
    if lg.any():
        x[:, lg] = np.log10(1.0 + x[:, lg])
    x = np.clip(x, W["clip_lo"], W["clip_hi"])
    x = (x - W["mean"]) / W["std"]
    h = np.maximum(x @ W["w1"] + W["b1"], 0.0)
    h = np.maximum(h @ W["w2"] + W["b2"], 0.0)
    return h @ W["wo"] + W["bo"]


if __name__ == "__main__":
    W = parse("edge_mlp_weights.h", "edgemlp")
    print("edge weights parsed:", W["kInput"], W["kHidden"], "bias_out", W["bo"])
    C = parse("chain_mlp_weights.h", "chainmlp")
    print("chain weights parsed:", C["kInput"], C["kHidden"], "bias_out", C["bo"])
