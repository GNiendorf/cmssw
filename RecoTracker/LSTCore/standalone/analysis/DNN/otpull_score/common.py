"""Shared definitions of the otpull pair network tooling (train / export / WP / parity).

The network scores (OT T3, IT cell) pairs of LST's OT-seeded lookup (src/alpaka/OTPull.h, otpullScan with
otpullScoreMode 1). The input vector is built in otpullScan in exactly this order; the dump patch writes it verbatim
(before clipping), so the training rows ARE the deployed inputs.
"""
import glob
import os

import numpy as np

FEATURES = [
    "pullO",        # outer cell hit: radial residual to the T3 circle / sigma(pT bin)
    "pullZ",        # outer cell hit: z residual to the T3 r-z line / sigma
    "pullA",        # cell direction against the circle tangent / sigma
    "pullS",        # cell r-z slope minus the T3 slope / sigma
    "pullJ",        # rms of the joint circle fit of the 2 cell hits + 3 T3 anchors / sigma
    "log1pChi2",    # log10(1 + sum of the five squared pulls)
    "log10PtT3",    # log10 of the T3 circle pT (GeV)
    "firstLayer2",  # T3 starts on barrel OT layer 2 (else layer 1)
    "outerEndcap",  # the T3's outer module is in the endcap
    "outerLayer",   # layer number of the T3's outer module
    "absEtaT3",     # |asinh(T3 r-z slope)|
    "rCellIn",      # cell inner hit r (cm)
    "rCellOut",     # cell outer hit r (cm)
    "absZCellIn",   # cell inner hit |z| (cm)
    "absZCellOut",  # cell outer hit |z| (cm)
]
NIN = len(FEATURES)
# dump record of the dump patch (OTPull.h): id, cell, 15 raw inputs, logit, chi2, inGate1, pT bin, cost
REC_ID, REC_CELL, REC_X0 = 0, 1, 2
REC_LOGIT, REC_CHI2, REC_GATE1, REC_PB, REC_COST = 17, 18, 19, 20, 21
REC_N = 22
# working-point table (OTPull.h OTPullParams kNWpPt x kNWpEta; LSTProducer otpullScoreWP* defaults)
WP_PT_EDGES = [2.0, 5.0, 10.0, 25.0, 50.0]
WP_ETA_EDGES = [0.9, 1.5]


def to_t(a, dev, dtype=None):
    """numpy -> torch through DLPack (the CMSSW torch build has no NumPy bridge)."""
    import torch
    t = torch.from_dlpack(np.ascontiguousarray(a)).to(dev)
    return t if dtype is None else t.to(dtype)


def to_np(t):
    return np.from_dlpack(t.detach().cpu().contiguous()).copy()


def load_rows(pattern):
    """Concatenate make_rows.py outputs; returns dict of arrays + list of (file, provenance)."""
    files = sorted(glob.glob(pattern))
    assert files, f"no rows for {pattern}"
    keys = ["X", "y", "w", "ev", "tpvxy", "tppt", "tpsig", "chi2", "gate1", "logit", "t3", "cell", "cost"]
    out = {k: [] for k in keys}
    prov = []
    off = 0
    for f in files:
        z = np.load(f, allow_pickle=False)
        for k in keys:
            v = z[k]
            out[k].append(v + off if k == "ev" else v)
        off += int(z["nev"]) + 1
        prov.append((os.path.basename(f), str(z["prov"])))
    return {k: np.concatenate(v) for k, v in out.items()}, prov


def split_events(ev, seed):
    """60/20/20 split by event (per sample, deterministic): part 0 train, 1 val, 2 test"""
    u = np.unique(ev)
    perm = np.random.default_rng(seed).permutation(u)
    ntr, nva = int(0.6 * len(u)), int(0.2 * len(u))
    part = np.zeros(len(ev), np.int8)
    part[np.isin(ev, perm[ntr:ntr + nva])] = 1
    part[np.isin(ev, perm[ntr + nva:])] = 2
    return part, (u, ntr, nva)


def cell_index(X):
    """working-point cell (pT row, |eta| column) of each pair, from its T3 inputs"""
    pt = 10.0 ** X[:, FEATURES.index("log10PtT3")]
    eta = X[:, FEATURES.index("absEtaT3")]
    ip = np.searchsorted(np.array(WP_PT_EDGES), pt, side="right")
    ie = np.searchsorted(np.array(WP_ETA_EDGES), eta, side="right")
    return ip, ie


def mlp_numpy(X, layers, clip_lo=None, clip_hi=None):
    """float32 numpy evaluation of the exported network (clip, then 2 ReLU layers, logit)."""
    w1, b1, w2, b2, w3, b3 = [np.asarray(a, np.float32) for a in layers]
    x = np.asarray(X, np.float32)
    if clip_lo is not None:
        x = np.minimum(np.maximum(x, np.asarray(clip_lo, np.float32)), np.asarray(clip_hi, np.float32))
    h = np.maximum(x @ w1 + b1, 0)
    h = np.maximum(h @ w2 + b2, 0)
    return (h @ w3 + b3)[:, 0]


def read_header(path):
    """(clipLo, clipHi, [w1, b1, w2, b2, w3, b3]) of OTPullNeuralNetworkWeights.h (wgtT_* are [in][out])."""
    import re
    txt = open(path).read()

    def arr(name):
        m = re.search(name + r"\[(\d+)\](?:\[(\d+)\])?\s*=\s*\{(.*?)\};", txt, re.S)
        v = np.array([float(x) for x in re.findall(r"-?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?", m.group(3))], np.float32)
        return v.reshape(int(m.group(1)), int(m.group(2))) if m.group(2) else v
    layers = [arr(n) for n in ("wgtT_layer1", "bias_layer1", "wgtT_layer2", "bias_layer2", "wgtT_output_layer",
                               "bias_output_layer")]
    return arr("kClipLo"), arr("kClipHi"), layers
