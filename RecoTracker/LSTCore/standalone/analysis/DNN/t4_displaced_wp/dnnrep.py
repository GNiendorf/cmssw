#!/usr/bin/env python3
"""Offline replicas of the T4 and T5 DNNs (weights parsed from the head's headers), fed from probe records.
Import only; self-test: dnnrep.py <t4.npz> <t5.npz> prints the agreement with the scores LST recorded."""
import re
import sys

import numpy as np

WT = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/ladder/wt/l2/RecoTracker/LSTCore/src/alpaka/"


def parse(path):
    txt = open(path).read()
    out = {}
    for m in re.finditer(r"(?:const|HOST_DEVICE_CONSTANT) float (\w+)((?:\[\d+\])+)\s*=\s*\{(.*?)\};", txt, re.S):
        name, dims, body = m.group(1), [int(x) for x in re.findall(r"\[(\d+)\]", m.group(2))], m.group(3)
        vals = np.array([float(x.rstrip("f")) for x in re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?f?", body) if x not in ("f",)], np.float32)
        out[name] = vals.reshape(dims)
    return out


def mlp(x, W, names):
    h = x.astype(np.float32)
    for k, (w, b) in enumerate(names):
        h = h @ W[w] + W[b]
        if k < len(names) - 1:
            h = np.maximum(h, 0)
    return h


def dphi(a, b):
    d = a - b
    return (d + np.pi) % (2 * np.pi) - np.pi


def t4_features(xyz, radii, t3s):
    """xyz (n,4,3) anchors [cm]; radii (n,4): inner, outer, regression, non-anchor regression; t3s (n,6)."""
    x, y, z = xyz[:, :, 0], xyz[:, :, 1], xyz[:, :, 2]
    r = np.hypot(x, y)
    eta = np.abs(np.arcsinh(z / r))
    phi = np.arctan2(y, x)
    az = np.abs(z)
    ZM, RM, EN = 267.2349854, 110.1099396, 2.5
    f = [eta[:, 0] / EN, np.abs(phi[:, 0]) / np.pi, az[:, 0] / ZM, r[:, 0] / RM]
    for k in (1, 2, 3):
        f += [eta[:, k] - eta[:, k - 1], dphi(phi[:, k], phi[:, k - 1]) / np.pi, (az[:, k] - az[:, k - 1]) / ZM, (r[:, k] - r[:, k - 1]) / RM]
    f += [1 / radii[:, 0], 1 / radii[:, 1], radii[:, 0] / radii[:, 1], 1 / radii[:, 2], 1 / radii[:, 3]]
    f += [t3s[:, 0], t3s[:, 1], t3s[:, 2], t3s[:, 3], t3s[:, 4], t3s[:, 5], t3s[:, 3] - t3s[:, 0], t3s[:, 4] - t3s[:, 1], t3s[:, 5] - t3s[:, 2]]
    return np.stack(f, 1).astype(np.float32)


T4_NAMES = ["eta1", "phi1", "z1", "r1", "deta21", "dphi21", "dz21", "dr21", "deta32", "dphi32", "dz32", "dr32", "deta43", "dphi43", "dz43", "dr43",
            "1/Rin", "1/Rout", "Rin/Rout", "1/Rreg", "1/RregNA", "fake1", "prompt1", "disp1", "fake2", "prompt2", "disp2", "dfake", "dprompt", "ddisp"]


def t4_scores(F):
    W = parse(WT + "T4NeuralNetworkWeights.h")
    o = mlp(F, W, [("wgtT_layer1", "bias_layer1"), ("wgtT_layer2", "bias_layer2"), ("wgtT_output_layer", "bias_output_layer")])
    e = np.exp(o - o.max(1, keepdims=True))
    return e / e.sum(1, keepdims=True)      # fake, prompt, displaced


def t5_features(xyz, radii):
    """xyz (n,5,3); radii (n,3): inner, bridge, outer."""
    x, y, z = xyz[:, :, 0], xyz[:, :, 1], xyz[:, :, 2]
    r = np.hypot(x, y)
    eta = np.abs(np.arcsinh(z / r))
    phi = np.arctan2(y, x)
    az = np.abs(z)
    ZM, RM, EN = 267.2349854, 110.1099396, 2.5
    f = [eta[:, 0] / EN, np.abs(phi[:, 0]) / np.pi, az[:, 0] / ZM, r[:, 0] / RM]
    for k in (1, 2, 3, 4):
        f += [eta[:, k] - eta[:, k - 1], dphi(phi[:, k], phi[:, k - 1]) / np.pi, (az[:, k] - az[:, k - 1]) / ZM, (r[:, k] - r[:, k - 1]) / RM]
    f += [np.log10(radii[:, 0]), np.log10(radii[:, 1]), np.log10(radii[:, 2])]
    return np.stack(f, 1).astype(np.float32)


def t5_score(F):
    W = parse(WT + "T5NeuralNetworkWeights.h")
    o = mlp(F, W, [("wgtT_layer1", "bias_layer1"), ("wgtT_layer2", "bias_layer2"), ("wgtT_output_layer", "bias_output_layer")])[:, 0]
    return 1 / (1 + np.exp(-o))


def main():
    d4 = np.load(sys.argv[1], allow_pickle=True)["rec"]
    s = t4_scores(t4_features(d4["xyz"].reshape(-1, 4, 3), d4["radii"], d4["t3Scores"]))
    ok = np.isfinite(d4["scores"]).all(1)
    print("T4 DNN replica: max |delta| %.2e, within 1e-4: %.5f of %d" % (np.nanmax(np.abs(s[ok] - d4["scores"][ok])), np.mean(np.abs(s[ok] - d4["scores"][ok]).max(1) < 1e-4), ok.sum()))
    d5 = np.load(sys.argv[2], allow_pickle=True)["rec"]
    s5 = t5_score(t5_features(d5["xyz"].reshape(-1, 5, 3), d5["radii"]))
    ok = np.isfinite(d5["dnnScore"])
    print("T5 DNN replica: max |delta| %.2e, within 1e-4: %.5f of %d" % (np.nanmax(np.abs(s5[ok] - d5["dnnScore"][ok])), np.mean(np.abs(s5[ok] - d5["dnnScore"][ok]) < 1e-4), ok.sum()))


if __name__ == "__main__":
    main()
