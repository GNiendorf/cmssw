#!/usr/bin/env python3
"""C++ parity of the shipped otpull pair network: the dump build (dump.patch) writes, per pair, the raw inputs and the
logit that otpulldnn::runInference computed in CMSSW; this evaluates OTPullNeuralNetworkWeights.h in numpy on the same
inputs (and, with --model, the trained float64 model) and compares.
  python3 parity_otpull.py rows/parity.npz [--header ../../../../src/alpaka/OTPullNeuralNetworkWeights.h] [--model m.json]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import mlp_numpy, read_header  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--header", default=os.path.join(HERE, "../../../../src/alpaka/OTPullNeuralNetworkWeights.h"))
    ap.add_argument("--model")
    ap.add_argument("--tol", type=float, default=1e-3)
    a = ap.parse_args()
    z = np.load(a.rows)
    X, cpp = z["X"], z["logit"]
    lo, hi, layers = read_header(a.header)
    py = mlp_numpy(X, layers, lo, hi)
    d = np.abs(py - cpp)
    rel = d / np.maximum(1.0, np.abs(cpp))
    print(f"{len(X)} pairs: max |numpy(header) - C++| logit {d.max():.3e}, median {np.median(d):.1e}, "
          f"fraction within {a.tol:g} (relative) {(rel < a.tol).mean():.6f}")
    s_py, s_cpp = 1 / (1 + np.exp(-py)), 1 / (1 + np.exp(-cpp))
    print(f"  max |score diff| {np.abs(s_py - s_cpp).max():.3e}")
    if a.model:
        from wp_otpull import logits
        m = logits(json.load(open(a.model)), X)
        print(f"  max |model (float64) - C++| logit {np.abs(m - cpp).max():.3e}")
    ok = (rel < a.tol).mean() > 0.9999
    print("PARITY", "OK" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
