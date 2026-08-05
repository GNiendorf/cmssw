#!/usr/bin/env python3
"""Export the ANGLE-1 3-CLASS chain gate (torch state_dict + norm json) to a constexpr
C++ header, following export_chain_weights.py / src/alpaka/NeuralNetwork.h convention:
weights emitted TRANSPOSED (torch stores [out,in]; header stores wgt[in][out]) so the
C++ inner loop is output[o] += input[i] * wgt[i][o].

Differences vs export_chain_weights.py:
  - kInput is whatever the trained model used; the header additionally emits
    kSrcCol[kInput], the ChainFeatures COLUMN each input reads (-1 = the per-chain
    dcaXY). This lets a model drop ChainFeatures columns (M12 drops maxBridgeChi2)
    without any C++ edit -- the inference gathers f[kSrcCol[i]],
  - kOutput = 3 (fake / prompt-true / displaced-true softmax logits), so the output
    layer is a matrix wgt_out[kHidden][kOutput] + bias_out[kOutput],
  - namespace chain3bmlp (coexists with the 2-class chainmlp header; -G 0..5 keep
    using the old one byte-identically).

Usage:
  python3 export_chain3b_weights.py --model chain3_mlp_m17b.pt --norm chain3_norm_m17b.json
"""

import argparse
import json
import os
import sys

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))
UNCLIPPED = 1e30


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "chain3_mlp_m17b.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "chain3_norm_m17b.json"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "chain3b_mlp_weights.h"))
    return p.parse_args()


def f32(v):
    import numpy as np
    s = f"{float(np.float32(v)):.9g}"
    if "." not in s and "e" not in s and "n" not in s and "i" not in s:
        s += ".0"
    return s + "f"


def fmt_array_1d(vals, per_line=6, indent="    "):
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append(indent + ", ".join(f32(v) for v in vals[i:i + per_line]) + ",")
    body = "\n".join(lines)[:-1]
    return "{\n" + body + "\n}"


def fmt_array_2d(mat, indent="    "):
    rows = [indent + fmt_array_1d(row, per_line=6, indent=indent + "    ") for row in mat]
    return "{\n" + ",\n".join(rows) + "\n}"


def main():
    args = parse_args()
    import torch

    try:
        blob = torch.load(args.model, map_location="cpu")
    except Exception:
        blob = torch.load(args.model, map_location="cpu", weights_only=False)
    sd = blob["state_dict"]
    arch = blob.get("arch")
    assert len(arch) == 4 and arch[3] == 3, f"expected [in,hid,hid,3], got {arch}"
    n_in, n_hid, n_out = arch[0], arch[1], arch[3]
    assert arch[2] == n_hid, "hidden sizes must match"

    with open(args.norm) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    mean = norm["mean"]
    std = norm["std"]
    assert len(names) == n_in == len(mean) == len(std), "norm json size mismatch"
    model_names = blob.get("feature_names")
    if model_names is not None:
        assert model_names == names, "model/norm feature_names disagree"

    # Source-column map: every input name must be either "cf_<ChainFeatures name>" or
    # the special "cf_dcaXY" (-1). kChainFeatNames is the single source of truth, read
    # straight out of ChainFeatures.cc so a contract change can never drift silently.
    import re
    cf_cc = open(os.path.join(PROTO_DIR, "ChainFeatures.cc")).read()
    blk = cf_cc[cf_cc.index("kChainFeatNames"):]
    blk = blk[blk.index("{") + 1:blk.index("};")]
    cf_names = re.findall(r'"([^"]+)"', blk)
    src_col = []
    for nm in names:
        assert nm.startswith("cf_"), f"input name '{nm}' does not start with cf_"
        base = nm[3:]
        if base == "dcaXY":
            src_col.append(-1)
        else:
            assert base in cf_names, f"'{base}' is not a ChainFeatures column"
            src_col.append(cf_names.index(base))
    assert src_col.count(-1) == 1, "expected exactly one dcaXY input"
    print(f"source columns: {src_col} (over {len(cf_names)} ChainFeatures columns)")

    clip_lo = [-UNCLIPPED] * n_in
    clip_hi = [UNCLIPPED] * n_in
    log10p1 = [False] * n_in
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            clip_lo[j] = c["lo"]
            clip_hi[j] = c["hi"]
        elif c["op"] == "log10_1p":
            log10p1[j] = True
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")

    def t2l(key):
        return sd[key].to(torch.float32).tolist()

    w1, b1 = t2l("0.weight"), t2l("0.bias")
    w2, b2 = t2l("2.weight"), t2l("2.bias")
    wo, bo = t2l("4.weight"), t2l("4.bias")
    assert len(w1) == n_hid and len(w1[0]) == n_in
    assert len(w2) == n_hid and len(w2[0]) == n_hid
    assert len(wo) == n_out and len(wo[0]) == n_hid and len(bo) == n_out

    w1_t = [[w1[o][i] for o in range(n_hid)] for i in range(n_in)]
    w2_t = [[w2[o][i] for o in range(n_hid)] for i in range(n_hid)]
    wo_t = [[wo[o][i] for o in range(n_out)] for i in range(n_hid)]

    feat_comment = "\n".join(f"//  [{j:2d}] {nm}  <- cf column {src_col[j]}" for j, nm in enumerate(names))
    cmd = "python3 export_chain3b_weights.py " + " ".join(
        f"--{k} {getattr(args, k)}" for k in ("model", "norm", "out"))

    hdr = f"""// GENERATED by export_chain3b_weights.py -- DO NOT EDIT BY HAND.
// Sources:
//   model: {args.model}
//   norm:  {args.norm}
// Command:
//   {cmd}
// Model meta: best_epoch={blob.get('best_epoch')} best_sel={blob.get('best_sel')}
//             best_val_meta={blob.get('best_val_meta')} seed={blob.get('seed')} arch={arch}
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
{feat_comment}
#ifndef PROTOTYPE_CHAIN3B_MLP_WEIGHTS_H
#define PROTOTYPE_CHAIN3B_MLP_WEIGHTS_H

namespace chain3bmlp {{

constexpr int kInput = {n_in};
constexpr int kHidden = {n_hid};
constexpr int kOutput = {n_out};

constexpr float kFeatMean[kInput] = {fmt_array_1d(mean)};

constexpr float kFeatStd[kInput] = {fmt_array_1d(std)};

constexpr float kClipLo[kInput] = {fmt_array_1d(clip_lo)};

constexpr float kClipHi[kInput] = {fmt_array_1d(clip_hi)};

constexpr bool kLog10p1[kInput] = {{{", ".join("true" if v else "false" for v in log10p1)}}};

// ChainFeatures column feeding each input; -1 = the per-chain dcaXY argument.
constexpr int kSrcCol[kInput] = {{{", ".join(str(v) for v in src_col)}}};

constexpr float wgt_l1[kInput][kHidden] = {fmt_array_2d(w1_t)};

constexpr float bias_l1[kHidden] = {fmt_array_1d(b1)};

constexpr float wgt_l2[kHidden][kHidden] = {fmt_array_2d(w2_t)};

constexpr float bias_l2[kHidden] = {fmt_array_1d(b2)};

constexpr float wgt_out[kHidden][kOutput] = {fmt_array_2d(wo_t)};

constexpr float bias_out[kOutput] = {fmt_array_1d(bo)};

}}  // namespace chain3bmlp

#endif
"""
    with open(args.out, "w") as fh:
        fh.write(hdr)
    n_par = n_in * n_hid + n_hid + n_hid * n_hid + n_hid + n_hid * n_out + n_out
    print(f"wrote {args.out}: kInput={n_in} kHidden={n_hid} kOutput={n_out} "
          f"({n_par} parameters)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
