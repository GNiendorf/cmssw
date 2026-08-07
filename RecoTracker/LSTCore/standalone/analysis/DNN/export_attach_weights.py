#!/usr/bin/env python3
"""Export the K8 attach-head MLP (torch state_dict + norm json) to a constexpr C++
header, following export_chain_weights.py / the production src/alpaka/NeuralNetwork.h
convention: weights are emitted TRANSPOSED (torch Linear stores [out,in]; the header
stores wgt[in][out]) so the C++ inner loop is output[o] += input[i] * wgt[i][o].

Feature order (must match train_attach.py / the frozen PixelAttach.h contract):
The DEPLOYED head is 20 inputs (af_00..af_19), arch 20->24->24->1: the 19-slot frozen
contract plus af_rphiResidInwards. Any input width the checkpoint declares is accepted.

Conditioning: the norm json carries a "conditioning" spec (clip / log10_1p ops
applied BEFORE standardization), baked into kClipLo/kClipHi (+-1e30 = no-op) and
kLog10p1 -- same encoding as chain_mlp_weights.h / edge_mlp_weights.h.

The output header's existence FLIPS AttachInference.cc from the sentinel path to the
real MLP (__has_include switch, the PixelAttach.h contract) -- rebuild after export.

Usage:
  python3 export_attach_weights.py --model attach_mlp_v1.pt --norm attach_norm_v1.json
"""

import argparse
import json
import os
import sys

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))

# R1: the input width is whatever the checkpoint says (19 for the M19 "r2" head, up to
# kAttachFeat for a head trained on the R1 matching block); only the 2-hidden-layer
# scalar-output SHAPE is structural, because AttachInference.cc hardcodes it.
EXPECTED_HIDDEN_LAYERS = 2
UNCLIPPED = 1e30


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "attach_mlp_v1.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "attach_norm_v1.json"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "attach_mlp_weights.h"))
    return p.parse_args()


def f32(v):
    """Format a value as a float literal that round-trips float32 exactly."""
    import numpy as np
    s = f"{float(np.float32(v)):.9g}"
    if "." not in s and "e" not in s and "n" not in s and "i" not in s:
        s += ".0"
    return s + "f"


def fmt_array_1d(vals, per_line=6, indent="    "):
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append(indent + ", ".join(f32(v) for v in vals[i:i + per_line]) + ",")
    body = "\n".join(lines)[:-1]  # drop the trailing comma
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
    assert isinstance(arch, list) and len(arch) == EXPECTED_HIDDEN_LAYERS + 2, \
        f"unexpected arch {arch}: AttachInference.cc implements in->h->h->1 only"
    assert arch[1] == arch[2] and arch[3] == 1, f"unexpected arch {arch}"
    n_in, n_hid = arch[0], arch[1]

    with open(args.norm) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    mean = norm["mean"]
    std = norm["std"]
    assert len(names) == n_in == len(mean) == len(std), "norm json size mismatch"
    for j, nm in enumerate(names):
        assert nm.startswith("af_"), f"slot {j}: {nm} not af_*"
    model_names = blob.get("feature_names")
    if model_names is not None:
        assert model_names == names, "model/norm feature_names disagree"

    clip_lo = [-UNCLIPPED] * n_in
    clip_hi = [UNCLIPPED] * n_in
    log10p1 = [False] * n_in
    conditioning = norm.get("conditioning") or []
    for c in conditioning:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            clip_lo[j] = c["lo"]
            clip_hi[j] = c["hi"]
        elif c["op"] == "log10_1p":
            log10p1[j] = True
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")

    def t2l(key):
        return sd[key].to(torch.float32).tolist()  # no numpy interop in CMSSW torch

    w1 = t2l("0.weight")   # [hid, in]  torch convention
    b1 = t2l("0.bias")
    w2 = t2l("2.weight")   # [hid, hid]
    b2 = t2l("2.bias")
    wo = t2l("4.weight")   # [1, hid]
    bo = t2l("4.bias")
    assert len(w1) == n_hid and len(w1[0]) == n_in
    assert len(w2) == n_hid and len(w2[0]) == n_hid
    assert len(wo) == 1 and len(wo[0]) == n_hid and len(bo) == 1

    # Transpose to [in][out] (NeuralNetwork.h linear_layer convention).
    w1_t = [[w1[o][i] for o in range(n_hid)] for i in range(n_in)]
    w2_t = [[w2[o][i] for o in range(n_hid)] for i in range(n_hid)]
    wo_t = wo[0]  # final layer has 1 output: flat [hid] vector, logit = dot + bias

    feat_comment = "\n".join(
        f"//  [{j:2d}] {nm}" for j, nm in enumerate(names))
    cmd = "python3 export_attach_weights.py " + " ".join(
        f"--{k} {getattr(args, k)}" for k in ("model", "norm", "out"))

    hdr = f"""// GENERATED by export_attach_weights.py -- DO NOT EDIT BY HAND.
// Sources:
//   model: {args.model}
//   norm:  {args.norm}
// Command:
//   {cmd}
// Model meta: best_epoch={blob.get('best_epoch')} best_val_auc={blob.get('best_val_auc')}
//             seed={blob.get('seed')} arch={arch}
//
// Convention (mirrors src/alpaka/NeuralNetwork.h): weights are stored TRANSPOSED,
// wgt[in][out], so the inference inner loop is output[o] += input[i] * wgt[i][o].
//
// Per-input preprocessing, applied in this order (bakes in the norm json's
// "conditioning" spec):
//   1. if (kLog10p1[i]) x = log10(1 + x)
//   2. x = min(max(x, kClipLo[i]), kClipHi[i])   (+-1e30 = unclipped)
//   3. x = (x - kFeatMean[i]) / kFeatStd[i]
//
// Input feature order (the LEADING {n_in} slots of the PixelAttach.h kAttachFeat layout;
// AttachInference.cc feeds f[0..kInput-1], so this list must be a PREFIX of
// PixelAttach.cc kAttachFeatNames):
{feat_comment}
#ifndef PROTOTYPE_ATTACH_MLP_WEIGHTS_H
#define PROTOTYPE_ATTACH_MLP_WEIGHTS_H

namespace attachmlp {{

constexpr int kInput = {n_in};
constexpr int kHidden = {n_hid};

constexpr float kFeatMean[kInput] = {fmt_array_1d(mean)};

constexpr float kFeatStd[kInput] = {fmt_array_1d(std)};

constexpr float kClipLo[kInput] = {fmt_array_1d(clip_lo)};

constexpr float kClipHi[kInput] = {fmt_array_1d(clip_hi)};

constexpr bool kLog10p1[kInput] = {{{", ".join("true" if v else "false" for v in log10p1)}}};

constexpr float wgt_l1[kInput][kHidden] = {fmt_array_2d(w1_t)};

constexpr float bias_l1[kHidden] = {fmt_array_1d(b1)};

constexpr float wgt_l2[kHidden][kHidden] = {fmt_array_2d(w2_t)};

constexpr float bias_l2[kHidden] = {fmt_array_1d(b2)};

constexpr float wgt_out[kHidden] = {fmt_array_1d(wo_t)};

constexpr float bias_out = {f32(bo[0])};

}}  // namespace attachmlp

#endif
"""
    with open(args.out, "w") as fh:
        fh.write(hdr)
    n_par = n_in * n_hid + n_hid + n_hid * n_hid + n_hid + n_hid + 1
    print(f"wrote {args.out}: kInput={n_in} kHidden={n_hid} ({n_par} parameters), "
          f"{sum(1 for c in conditioning if c['op'] == 'clip')} clips, "
          f"{sum(log10p1)} log10_1p ops")
    return 0


if __name__ == "__main__":
    sys.exit(main())
