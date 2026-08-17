#!/usr/bin/env python3
"""Parity check for the attach head: checkpoint <-> generated header <-> the DEPLOYED C++ inference.

WHY THIS FILE EXISTS.  `attach_parity.py` beside it is stale: it hardcodes `N_ATTACH_FEAT = 18`
(the head is 23 wide today) and it wants a ROOT file with a "pairs" tree that nothing in this
repository -- or in the build -- writes any more. It cannot pass, so it cannot gate a deploy.
This file replaces it with three checks that all run off artefacts the current pipeline produces:

  1. HEADER vs NORM JSON.  Parse `kFeatMean`, `kFeatStd`, `kClipLo`, `kClipHi`, `kLog10p1` out of
     the generated header text and compare them elementwise, at float32, against the norm json's
     mean / std / conditioning list. This is the check that catches a preprocessing constant that
     did not travel with the weights -- the failure the exporter's own banner cannot show.
  2. HEADER vs CHECKPOINT.  Parse `wgt_l1` / `bias_l1` / `wgt_l2` / `bias_l2` / `wgt_out` /
     `bias_out` and compare against the checkpoint's state_dict, honouring the header's TRANSPOSED
     storage convention (`wgt[in][out]`).
  3. END TO END, on the deployed binary's own rows.  A pair dump carries, per scored pair, the
     STANDARDIZED inputs `x` exactly as the head consumed them AND the logit the C++ produced.
     Recomputing the forward pass in numpy from `x` and comparing against that logit column tests
     the actual compiled inference on real rows -- a strictly stronger statement than re-running
     the Python model against itself. It requires a dump made with THIS header deployed; a dump
     from any other build will fail here, which is the point.

Note what check 3 does and does not cover: the dump's `x` is already standardized by the C++, so
check 3 alone would pass even if every mean/std were wrong. Check 1 is what closes that, by
comparing the header's own preprocessing constants against the json the training run wrote. The
two together cover the whole path from checkpoint to deployed logit.

Run:
  python3 attach_parity2.py --model M.pt --norm M_norm.json --header <AttachNetworkWeights.h> \
      [--dump <dumpdir with pairs.bin> --n-events 5] [--tol 3e-5]
Exit status is 0 only if every requested check passes; anything else must block a deploy.
"""
import argparse
import json
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

PAIR_MAGIC = 0x50414952


# ---------------------------------------------------------------------------------------------
# Header parsing. The generated header is plain C++ initialiser text: the array name, then a
# braced list of literals. Nested braces (a matrix) are flattened, which is what the comparisons
# below want. Comments are stripped first so a commented-out number cannot be read as data.
def parse_header(path):
    txt = open(path).read()
    txt = re.sub(r"//[^\n]*", "", txt)
    out = {}
    for m in re.finditer(r"\b(kFeatMean|kFeatStd|kClipLo|kClipHi|kLog10p1|wgt_l1|bias_l1|"
                         r"wgt_l2|bias_l2|wgt_out)\s*\[[^=]*=\s*", txt):
        name = m.group(1)
        i = txt.index("{", m.end() - 1)
        depth, j = 0, i
        while True:
            if txt[j] == "{":
                depth += 1
            elif txt[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        body = txt[i:j + 1].replace("{", " ").replace("}", " ")
        toks = [t.strip() for t in body.split(",") if t.strip()]
        if name == "kLog10p1":
            out[name] = np.array([t == "true" for t in toks], bool)
        else:
            out[name] = np.array([float(t.rstrip("f")) for t in toks], np.float64)
    m = re.search(r"\bbias_out\s*=\s*([-\d.eE+]+)f?\s*;", txt)
    out["bias_out"] = np.array([float(m.group(1))], np.float64)
    out["kInput"] = int(re.search(r"kInput\s*=\s*(\d+)", txt).group(1))
    out["kHidden"] = int(re.search(r"kHidden\s*=\s*(\d+)", txt).group(1))
    return out


def cmp(tag, a, b, tol, fails):
    a = np.asarray(a, np.float64).ravel()
    b = np.asarray(b, np.float64).ravel()
    if a.shape != b.shape:
        print("  FAIL %-12s shape %s vs %s" % (tag, a.shape, b.shape))
        fails.append(tag)
        return
    d = np.abs(a - b)
    rel = d / np.maximum(np.abs(b), 1e-12)
    bad = int(((d > tol) & (rel > tol)).sum())
    print("  %-4s %-12s n=%5d  max|d|=%.3e  max rel=%.3e" %
          ("FAIL" if bad else "ok", tag, len(a), d.max() if len(a) else 0.0,
           rel.max() if len(a) else 0.0))
    if bad:
        fails.append(tag)


def iter_pairs(path, nev):
    """Yield (x, logit) per event from a pair dump; widths come from the header, never assumed."""
    f = open(path, "rb")
    for _ in range(nev):
        pre = f.read(8)
        if len(pre) < 8:
            return
        magic, ver = struct.unpack("<2I", pre)
        assert magic == PAIR_MAGIC and ver == 2, (magic, ver)
        (ievt, nC, nT3, nR, nDrop, dsA, dsB, nFeat, nProbe) = struct.unpack("<9I", f.read(36))
        rb = 16 + 4 * (nFeat + nProbe)
        buf = np.frombuffer(f.read(rb * nR), dtype=np.uint8).reshape(nR, rb)
        lg = buf[:, 12:16].copy().view(np.float32).ravel()
        x = buf[:, 16:16 + 4 * nFeat].copy().view(np.float32).reshape(nR, nFeat)
        yield x, lg


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", required=True)
    p.add_argument("--norm", required=True)
    p.add_argument("--header", required=True)
    p.add_argument("--dump", help="dump directory holding pairs.bin, made with THIS header")
    p.add_argument("--n-events", type=int, default=5)
    p.add_argument("--tol", type=float, default=3e-5)
    a = p.parse_args()

    import torch
    blob = torch.load(a.model, map_location="cpu", weights_only=False)
    sd = blob["state_dict"]
    tonp = lambda t: np.array(t.detach().cpu().tolist(), np.float64)
    W1, B1 = tonp(sd["0.weight"]), tonp(sd["0.bias"])
    W2, B2 = tonp(sd["2.weight"]), tonp(sd["2.bias"])
    WO, BO = tonp(sd["4.weight"]), tonp(sd["4.bias"])
    nm = json.load(open(a.norm))
    H = parse_header(a.header)
    fails = []

    n_in, n_hid = W1.shape[1], W1.shape[0]
    print("checkpoint %d -> %d -> %d -> %d ; header kInput=%d kHidden=%d ; norm names %d"
          % (n_in, n_hid, n_hid, WO.shape[0], H["kInput"], H["kHidden"], len(nm["feature_names"])))
    if not (n_in == H["kInput"] == len(nm["feature_names"]) and n_hid == H["kHidden"]):
        print("  FAIL width")
        fails.append("width")

    print("[1] header vs norm json")
    clo = np.full(n_in, -1e30); chi = np.full(n_in, 1e30); l10 = np.zeros(n_in, bool)
    for c in nm.get("conditioning", []):
        i = nm["feature_names"].index(c["feature"])
        if c["op"] == "log10_1p":
            l10[i] = True
        elif c["op"] == "clip":
            clo[i], chi[i] = c["lo"], c["hi"]
        else:
            raise SystemExit("unknown conditioning op %s" % c["op"])
    cmp("kFeatMean", H["kFeatMean"], nm["mean"], a.tol, fails)
    cmp("kFeatStd", H["kFeatStd"], nm["std"], a.tol, fails)
    cmp("kClipLo", H["kClipLo"], clo, a.tol, fails)
    cmp("kClipHi", H["kClipHi"], chi, a.tol, fails)
    ok = bool((H["kLog10p1"] == l10).all())
    print("  %-4s kLog10p1     n=%5d" % ("ok" if ok else "FAIL", n_in))
    if not ok:
        fails.append("kLog10p1")

    print("[2] header vs checkpoint (header stores wgt[in][out])")
    cmp("wgt_l1", H["wgt_l1"], W1.T.ravel(), a.tol, fails)
    cmp("bias_l1", H["bias_l1"], B1, a.tol, fails)
    cmp("wgt_l2", H["wgt_l2"], W2.T.ravel(), a.tol, fails)
    cmp("bias_l2", H["bias_l2"], B2, a.tol, fails)
    cmp("wgt_out", H["wgt_out"], WO.ravel(), a.tol, fails)
    cmp("bias_out", H["bias_out"], BO, a.tol, fails)

    if a.dump:
        print("[3] deployed C++ logits vs numpy forward pass, on the dump's own standardized rows")
        w1 = W1.astype(np.float32); b1 = B1.astype(np.float32)
        w2 = W2.astype(np.float32); b2 = B2.astype(np.float32)
        wo = WO.astype(np.float32).ravel(); bo = float(BO[0])
        tot, worst, nbad = 0, 0.0, 0
        for x, lg in iter_pairs(os.path.join(a.dump, "pairs.bin"), a.n_events):
            if x.shape[1] != n_in:
                print("  FAIL dump width %d != %d" % (x.shape[1], n_in))
                fails.append("dumpwidth")
                break
            h1 = np.maximum(x @ w1.T + b1, 0.0)
            h2 = np.maximum(h1 @ w2.T + b2, 0.0)
            z = (h2 @ wo + bo).astype(np.float32)
            d = np.abs(z - lg)
            tot += len(z); worst = max(worst, float(d.max()) if len(d) else 0.0)
            nbad += int((d > a.tol * np.maximum(1.0, np.abs(lg))).sum())
        print("  %-4s pairs        n=%d  max|d|=%.3e  above tol: %d"
              % ("FAIL" if nbad else "ok", tot, worst, nbad))
        if nbad:
            fails.append("deployed_logits")

    print("PARITY %s%s" % ("FAILED: " if fails else "PASSED",
                           ", ".join(fails) if fails else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
