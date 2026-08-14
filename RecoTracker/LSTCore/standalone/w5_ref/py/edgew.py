#!/usr/bin/env python3
"""JPR: parse src/alpaka/EdgeNetworkWeights.h into numpy, and replay the K5 edge head offline.

Why: the shipped eligibility rule is
    cell = (etype-1)*kWpBins + wpBin[inner]
    elig = (zP - zF) >= kWpPrompt[cell]  OR  (zD - zF) >= kWpDisp[cell]
(src/alpaka/ChainEdges.h:513-532).  Only the SCALAR logOdds = max(mP, mD) is written to the
'P21E' dump, and weldBar (the materialized eligibility) is NOT dumped at all.  So the only way to
recover eligibility offline, without touching the tree, is to replay the head on the 'P21F'
feature dump (13 inner node feats + 13 outer node feats + 14 edge feats = 40 = kInput) and apply
the same tables.  Round-trip against the dumped logOdds is the proof that the replay is exact.
"""
import os
import re

import numpy as np

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "../../../src/alpaka/EdgeNetworkWeights.h")


def _floats(txt):
    return np.array([float(x) for x in re.findall(r"(-?[0-9.]+(?:[eE][-+]?[0-9]+)?)f", txt)],
                    dtype=np.float64)


def load(path=SRC):
    txt = open(path).read()
    out = {}

    def grab(name):
        # match a real DECLARATION "<type> name[...] = { ... };" -- the bare name also occurs in
        # the comments (e.g. ">= kWpDisp[cell]"), which a looser regex silently mis-grabs.
        m = re.search(r"(?:float|bool)\s+" + re.escape(name) + r"\s*\[[^;{]*?=\s*\{(.*?)\};", txt, re.S)
        assert m, "missing " + name
        return m.group(1)

    for k in ("kFeatMean", "kFeatStd", "kClipLo", "kClipHi", "bias_l1", "bias_l2",
              "bias_out", "kWpPrompt", "kWpDisp"):
        out[k] = _floats(grab(k))
    lg = grab("kLog10p1")
    out["kLog10p1"] = np.array([w.strip() == "true" for w in lg.split(",") if w.strip()], dtype=bool)
    for k, shp in (("wgt_l1", (40, 32)), ("wgt_l2", (32, 32)), ("wgt_out", (3, 32))):
        v = _floats(grab(k))
        assert v.size == shp[0] * shp[1], (k, v.size)
        out[k] = v.reshape(shp)
    assert out["kFeatMean"].size == 40 and out["kWpPrompt"].size == 40 and out["kWpDisp"].size == 40
    assert out["kLog10p1"].size == 40
    return out


def stdz(x, W):
    """x: (n,40) raw features in head-input order.  Returns the standardized block."""
    v = x.astype(np.float64, copy=True)
    lg = W["kLog10p1"]
    if lg.any():
        v[:, lg] = np.log10(1.0 + v[:, lg])
    v = np.clip(v, W["kClipLo"], W["kClipHi"])
    return (v - W["kFeatMean"]) / W["kFeatStd"]


def logits(x, W):
    """3 class logits (n,3) for raw features x (n,40)."""
    v = stdz(x, W)
    h = np.maximum(v @ W["wgt_l1"] + W["bias_l1"], 0.0)
    h = np.maximum(h @ W["wgt_l2"] + W["bias_l2"], 0.0)
    return h @ W["wgt_out"].T + W["bias_out"]


def margins(x, W):
    z = logits(x, W)
    mP = z[:, 1] - z[:, 0]
    mD = z[:, 2] - z[:, 0]
    return mP, mD, np.maximum(mP, mD)


def eligible(mP, mD, etype, wpbin, W):
    """The shipped OR-rule.  etype in {1,2}; wpbin = nodes.wpBin()[inner] in [0,20)."""
    cell = (etype.astype(np.int64) - 1) * 20 + wpbin.astype(np.int64)
    return (mP >= W["kWpPrompt"][cell]) | (mD >= W["kWpDisp"][cell])


def wp_bin(t3_pt, md0_abs_eta):
    """ChainEdges.h:414-419.  wpPt = radius*k2Rinv1GeVf*2 (== t3_pt), wpEta = |md0 anchor eta|."""
    ptbin = (np.asarray(t3_pt) > 5.0).astype(np.int64)
    ae = np.abs(np.asarray(md0_abs_eta))
    etabin = np.where(ae > 2.5, 9, np.minimum((ae / 0.25).astype(np.int64), 9))
    return ptbin * 10 + etabin


if __name__ == "__main__":
    W = load()
    print("loaded EdgeNetworkWeights.h")
    for k in sorted(W):
        v = W[k]
        print("  %-12s %-12s" % (k, getattr(v, "shape", None)), v.ravel()[:4])
    print("kWpPrompt E1:", np.round(W["kWpPrompt"][:20], 3))
    print("kWpPrompt E2:", np.round(W["kWpPrompt"][20:], 3))
    print("kWpDisp   E1:", np.round(W["kWpDisp"][:20], 3))
    print("kWpDisp   E2:", np.round(W["kWpDisp"][20:], 3))
