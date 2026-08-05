#!/usr/bin/env python3
"""ANGLE-B3 python-side golden-value reference for the 3-class EDGE head (plan 5c).

Recomputes the three raw class logits for the first N rows of an edge dump exactly the
way train_edge3.py builds its input matrix (conditioning -> standardize, both read back
from the norm json) and compares them against the C++ output of tools/edge3_parity.

  python3 edge3_parity.py --model edge3_mlp_v1.pt --norm edge3_norm_v1.json \
      --input edges_smoke.root --cpp tools/edge3_parity_cpp.json
"""
import argparse
import json
import os

import numpy as np

import train_edge as te

D = os.path.dirname(os.path.abspath(__file__))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=f"{D}/edge3_mlp_v1.pt")
    p.add_argument("--norm", default=f"{D}/edge3_norm_v1.json")
    p.add_argument("--input", default=f"{D}/edges_smoke.root")
    p.add_argument("--cpp", default=f"{D}/tools/edge3_parity_cpp.json")
    p.add_argument("--out", default=f"{D}/edge3_parity.json")
    p.add_argument("--n-edges", type=int, default=1000)
    p.add_argument("--tol", type=float, default=1e-3)
    a = p.parse_args()

    import torch

    meta, X, names, _, _ = te.load_dump(a.input)
    X = X[: a.n_edges].copy()
    nrm = json.load(open(a.norm))
    te.apply_conditioning(X, names, nrm["conditioning"])
    mean = np.asarray(nrm["mean"], dtype=np.float32)
    std = np.asarray(nrm["std"], dtype=np.float32)
    Z = (X - mean) / std

    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    sd = ck["state_dict"] if "state_dict" in ck else ck
    n_in, n_hid = Z.shape[1], sd["0.weight"].shape[0]
    model = torch.nn.Sequential(torch.nn.Linear(n_in, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, 3))
    model.load_state_dict(sd)
    model.eval()
    with torch.no_grad():
        zt = torch.tensor(Z.astype(np.float32).tolist(), dtype=torch.float32)
        py = np.asarray(model(zt).tolist(), dtype=np.float64)

    cpp = np.asarray(json.load(open(a.cpp))["logits"], dtype=np.float64)[: len(py)]
    d = np.abs(py - cpp)
    mP_py, mD_py = py[:, 1] - py[:, 0], py[:, 2] - py[:, 0]
    mP_c, mD_c = cpp[:, 1] - cpp[:, 0], cpp[:, 2] - cpp[:, 0]
    mX_py = np.maximum(mP_py, mD_py)
    mX_c = np.maximum(mP_c, mD_c)
    res = {
        "n": int(len(py)),
        "max_dlogit": float(d.max()),
        "max_dlogit_per_class": [float(d[:, k].max()) for k in range(3)],
        "max_dmP": float(np.abs(mP_py - mP_c).max()),
        "max_dmD": float(np.abs(mD_py - mD_c).max()),
        "max_dmX": float(np.abs(mX_py - mX_c).max()),
        "argmax_agreement": float((py.argmax(1) == cpp.argmax(1)).mean()),
        "eligibility_agreement_mX0": float(((mX_py >= 0) == (mX_c >= 0)).mean()),
    }
    json.dump(res, open(a.out, "w"), indent=1)
    print(f"\n=== EDGE3 PARITY over {res['n']} edges ===")
    print(f"  max |dLogit|  = {res['max_dlogit']:.3e}   (per class: "
          + " / ".join(f"{v:.3e}" for v in res["max_dlogit_per_class"]) + ")")
    print(f"  max |dmP|     = {res['max_dmP']:.3e}")
    print(f"  max |dmD|     = {res['max_dmD']:.3e}")
    print(f"  max |dmX|     = {res['max_dmX']:.3e}")
    print(f"  argmax agreement          = {res['argmax_agreement']:.6f}")
    print(f"  eligibility (mX>=0) agree = {res['eligibility_agreement_mX0']:.6f}")
    ok = res["max_dlogit"] < a.tol
    print(f"  RESULT: {'PASS' if ok else 'FAIL'} (tol {a.tol})")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
