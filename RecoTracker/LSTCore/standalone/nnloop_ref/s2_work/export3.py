#!/usr/bin/env python3
"""S2: export a retrained 3-class gate head to the DEPLOYED src/alpaka/Chain3NetworkWeights.h
form (HOST_DEVICE_CONSTANT arrays in ALPAKA_ACCELERATOR_NAMESPACE::lst::dnn::chain3mlp), with
the affine margin pin BAKED INTO THE OUTPUT LAYER.

THE PIN IS NOT A NEW CONSTANT.  m -> a*m + b for all three margins is obtained by scaling
every output row by `a` and shifting the PROMPT and DISPLACED biases by `b` relative to the
FAKE bias:
    wgt_out' = a * wgt_out ;  bias_out' = a * bias_out ;  bias_out'[1] += b ;  bias_out'[2] += b
then  zF' = a zF,  zP' = a zP + b,  zD' = a zD + b, hence
      mP' = a mP + b,  mD' = a mD + b,  mX' = a mX + b.
So the head stays a 25->32->32->3 MLP, no code change, no extra file, and NO ORDERING of any
chain by any margin changes -- which is what protects K9's order key (ChainArbitrate.h:141).

SELF-TEST (`--selftest`): regenerate the SHIPPED deployed header from
prototype/chain3_mlp_m12.pt + chain3_norm_m12.json and compare EVERY numeric literal against
the current src/alpaka/Chain3NetworkWeights.h as float32.  Comparing float32 VALUES and not
decimal text matters -- a shortest-round-trip literal may print fewer digits (S1 [21:40]).
"""
import argparse
import json
import os
import re
import sys

import numpy as np

UNCLIPPED = 1e30
ARRAYS = ["kFeatMean", "kFeatStd", "kClipLo", "kClipHi", "kLog10p1", "kSrcCol",
          "wgt_l1", "bias_l1", "wgt_l2", "bias_l2", "wgt_out", "bias_out"]
CF = ["nNodes", "nLayers", "sumEdgeLogit", "minEdgeLogit", "meanEdgeLogit",
      "fullFitChi2PerHit", "rzLineChi2PerHit", "fitKappa", "dKappaFitVsMedianT3", "ptEst",
      "innermostLayer", "layerSpan", "nPS", "nBarrel", "maxJunctionDegProduct",
      "chargeConsistency", "maxXyResid", "maxRzResid", "stdEdgeLogit", "maxBridgeChi2",
      "minT3FakeScore", "maxT3FakeScore", "meanT3PromptScore", "minT3DisplacedScore",
      "meanT3DisplacedScore"]


def f32(v):
    s = "%.9g" % float(np.float32(v))
    if "." not in s and "e" not in s and "n" not in s and "i" not in s:
        s += ".0"
    return s + "f"


def arr1(vals, ind=" " * 6):
    return "{\n" + wrap_tokens([f32(v) for v in vals], ind) + "}"


def wrap_tokens(toks, ind, lim=118):
    """Greedy line filling on token boundaries; commas end lines, as clang-format does."""
    lines, cur = [], ""
    for k, t in enumerate(toks):
        piece = t + ("," if k + 1 < len(toks) else "")
        if cur and len(ind) + len(cur) + 1 + len(piece) > lim:
            lines.append(ind + cur)
            cur = piece
        else:
            cur = (cur + " " + piece) if cur else piece
    if cur:
        lines.append(ind + cur)
    return "\n".join(lines)


def arr2(mat, ind=" " * 6):
    """Matrix initialiser wrapped so NO line exceeds 120 columns (CMSSW's limit).

    A 32-wide row is ~450 characters, so one row per line put 57 lines over the limit -- the
    same nit S1 recorded at [S1 21:40] for the edge header. Wrapping is whitespace-only and the
    generator's own re-parse check proves every float32 literal survives it.
    """
    toks = []
    for row in mat:
        vals = [f32(v) for v in row]
        toks.append("{" + vals[0])
        toks.extend(vals[1:-1])
        toks.append(vals[-1] + "}")
    return "{\n" + wrap_tokens(toks, ind) + "}"


def parse_header(path):
    """Every declared array -> np.float64 flat array (bools/ints coerced)."""
    txt = open(path).read()
    out = {}
    for nm in ARRAYS:
        m = re.search(r"\b%s\s*\[[^=]*=\s*(\{.*?\});" % re.escape(nm), txt, re.S)
        if m is None:
            raise KeyError("array %s not found in %s" % (nm, path))
        body = m.group(1)
        body = body.replace("true", "1").replace("false", "0")
        toks = re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f?", body)
        out[nm] = np.array([float(t.rstrip("f")) for t in toks], dtype=np.float64)
    return out


def build(model, norm, affine=(1.0, 0.0), src_note=""):
    import torch
    blob = torch.load(model, map_location="cpu", weights_only=False)
    sd = blob["state_dict"]
    arch = blob["arch"]
    n_in, n_hid, n_out = arch[0], arch[1], arch[3]
    assert n_out == 3 and arch[2] == n_hid
    nj = json.load(open(norm))
    names, mean, std = nj["feature_names"], nj["mean"], nj["std"]
    assert len(names) == n_in == len(mean) == len(std)

    src_col = []
    for nm in names:
        assert nm.startswith("cf_")
        base = nm[3:]
        src_col.append(-1 if base == "dcaXY" else CF.index(base))
    assert src_col.count(-1) == 1

    clip_lo = [-UNCLIPPED] * n_in
    clip_hi = [UNCLIPPED] * n_in
    log10p1 = [False] * n_in
    for c in nj.get("conditioning") or []:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            clip_lo[j], clip_hi[j] = c["lo"], c["hi"]
        else:
            log10p1[j] = True

    t2l = lambda k: sd[k].to(torch.float32).tolist()
    w1, b1 = t2l("0.weight"), t2l("0.bias")
    w2, b2 = t2l("2.weight"), t2l("2.bias")
    wo, bo = t2l("4.weight"), t2l("4.bias")
    a, b = affine
    wo = [[a * v for v in row] for row in wo]
    bo = [a * v for v in bo]
    bo[1] += b
    bo[2] += b

    w1t = [[w1[o][i] for o in range(n_hid)] for i in range(n_in)]
    w2t = [[w2[o][i] for o in range(n_hid)] for i in range(n_hid)]
    wot = [[wo[o][i] for o in range(n_out)] for i in range(n_hid)]

    feat = "\n".join("//  [%2d] %s  <- cf column %d" % (j, nm, src_col[j])
                     for j, nm in enumerate(names))
    hdr = """// GENERATED by nnloop_ref/s2_work/export3.py -- DO NOT EDIT BY HAND.
// Sources:
//   model: %s
//   norm:  %s
// Model meta: best_epoch=%s best_sel=%s
//             best_val_meta=%.60s seed=%s arch=%s
//%s
// AFFINE MARGIN PIN BAKED INTO THE OUTPUT LAYER: a=%.9g b=%.9g.
// wgt_out and bias_out are scaled by a and the prompt/displaced biases shifted by b relative
// to fake, so mP/mD/mX all map as m -> a*m + b. This changes NO ordering by any margin (the
// map is monotone and identical for all three), which is what protects K9's best-first ORDER
// KEY, orderKey = score - orderAlpha*max(0, orderHinge - marginX) (ChainArbitrate.h:141) --
// the one consumer of a gate margin that is a RANKING and not a bar.
//
// ANGLE-1 3-CLASS chain gate. Outputs (softmax logits, NO softmax applied in C++):
//   [0] fake, [1] prompt-true (simVxy < 1 cm), [2] displaced-true (simVxy >= 1 cm)
// Downstream decisions use MARGINS (softmax is monotone in them):
//   mP = out[1] - out[0], mD = out[2] - out[0], mX = max(out[1],out[2]) - out[0].
//
// Convention (mirrors src/alpaka/NeuralNetwork.h): weights stored TRANSPOSED,
// wgt[in][out], inner loop output[o] += input[i] * wgt[i][o].
//
// Per-input preprocessing, applied in this order:
//   1. if (kLog10p1[i]) x = log10(1 + x)
//   2. x = min(max(x, kClipLo[i]), kClipHi[i])   (+-1e30 = unclipped)
//   3. x = (x - kFeatMean[i]) / kFeatStd[i]
//
// Input feature order; kSrcCol[i] is the ChainFeatures column feeding input i
// (-1 = the per-chain transverse DCA from k8ChainDcaXY):
%s
#ifndef RecoTracker_LSTCore_src_alpaka_Chain3NetworkWeights_h
#define RecoTracker_LSTCore_src_alpaka_Chain3NetworkWeights_h

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/HostDeviceConstant.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst::dnn::chain3mlp {

  constexpr int kInput = %d;
  constexpr int kHidden = %d;
  constexpr int kOutput = %d;

  HOST_DEVICE_CONSTANT float kFeatMean[kInput] = %s;

  HOST_DEVICE_CONSTANT float kFeatStd[kInput] = %s;

  HOST_DEVICE_CONSTANT float kClipLo[kInput] = %s;

  HOST_DEVICE_CONSTANT float kClipHi[kInput] = %s;

  HOST_DEVICE_CONSTANT bool kLog10p1[kInput] = {\n%s};

  // ChainFeatures column feeding each input; -1 = the per-chain dcaXY argument.
  HOST_DEVICE_CONSTANT int kSrcCol[kInput] = {\n%s};

  HOST_DEVICE_CONSTANT float wgt_l1[kInput][kHidden] = %s;

  HOST_DEVICE_CONSTANT float bias_l1[kHidden] = %s;

  HOST_DEVICE_CONSTANT float wgt_l2[kHidden][kHidden] = %s;

  HOST_DEVICE_CONSTANT float bias_l2[kHidden] = %s;

  HOST_DEVICE_CONSTANT float wgt_out[kHidden][kOutput] = %s;

  HOST_DEVICE_CONSTANT float bias_out[kOutput] = %s;

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst::dnn::chain3mlp

#endif
""" % (model, norm, blob.get("best_epoch"), blob.get("best_sel"), blob.get("best_val_meta"),
       blob.get("seed"), arch, src_note, a, b, feat, n_in, n_hid, n_out,
       arr1(mean), arr1(std), arr1(clip_lo), arr1(clip_hi),
       wrap_tokens(["true" if v else "false" for v in log10p1], " " * 6),
       wrap_tokens([str(v) for v in src_col], " " * 6),
       arr2(w1t), arr1(b1), arr2(w2t), arr1(b2), arr2(wot), arr1(bo))
    ref = {"kFeatMean": mean, "kFeatStd": std, "kClipLo": clip_lo, "kClipHi": clip_hi,
           "kLog10p1": [1.0 if v else 0.0 for v in log10p1],
           "kSrcCol": [float(v) for v in src_col],
           "wgt_l1": np.array(w1t).ravel(), "bias_l1": b1,
           "wgt_l2": np.array(w2t).ravel(), "bias_l2": b2,
           "wgt_out": np.array(wot).ravel(), "bias_out": bo}
    return hdr, ref


def verify(path, ref):
    got = parse_header(path)
    nlit = 0
    for k, v in ref.items():
        a = np.asarray(got[k], dtype=np.float32)
        b = np.asarray(v, dtype=np.float32)
        assert a.shape == b.shape, "%s size %d vs %d" % (k, a.size, b.size)
        assert np.array_equal(a, b), "%s MISMATCH (max |d| %g)" % (k, np.abs(a - b).max())
        nlit += a.size
    return nlit


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model")
    p.add_argument("--norm")
    p.add_argument("--out")
    p.add_argument("--a", type=float, default=1.0)
    p.add_argument("--b", type=float, default=0.0)
    p.add_argument("--barfit", help="read the affine from a barfit.py report")
    p.add_argument("--selftest", action="store_true")
    a = p.parse_args()

    if a.selftest:
        O = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore"
        hdr, ref = build(O + "/standalone/prototype/chain3_mlp_m12.pt",
                         O + "/standalone/prototype/chain3_norm_m12.json")
        n = verify(O + "/src/alpaka/Chain3NetworkWeights.h", ref)
        print("SELFTEST PASS: the SHIPPED deployed header reproduces "
              "prototype/chain3_mlp_m12.pt exactly -- %d literals, float32-equal" % n)
        tmp = "/tmp/claude-31734/_s2_self.h"
        open(tmp, "w").write(hdr)
        n2 = verify(tmp, ref)
        print("SELFTEST PASS: my generator's own output re-parses to the same %d literals" % n2)
        return 0

    aa, bb = a.a, a.b
    if a.barfit:
        r = json.load(open(a.barfit))
        aa, bb = r["affine"]["a"], r["affine"]["b"]
    hdr, ref = build(a.model, a.norm, (aa, bb))
    open(a.out, "w").write(hdr)
    n = verify(a.out, ref)
    over = [l for l in hdr.split("\n") if len(l) > 120]
    print("wrote %s: %d literals verified float32-equal to the .pt (+ affine a=%.9g b=%.9g); "
          "%d lines over 120 cols" % (a.out, n, aa, bb, len(over)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
