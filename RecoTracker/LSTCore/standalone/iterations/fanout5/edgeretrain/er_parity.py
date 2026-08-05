#!/usr/bin/env python3
"""C++/python parity for a retrained edge head.

The prototype has no logit-dump mode, but `-m chains` prints, per event,
`pass=<n>` = the number of edges with logOdds >= thetaEdge. Scanning thetaEdge
therefore samples the C++ score CDF. This script compares that CDF against the
python model evaluated on the SAME events' dumped features (edges_check5.root,
already verified byte-identical to the training dumps), which exercises the whole
exported header: conditioning -> standardization -> weights.

  er_parity.py --tag d1 --bin bin/chainproto_d1
"""
import argparse
import json
import os
import re
import subprocess
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
N_NODE, N_EDGE = 13, 14
THETAS = [-4.0, -2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0, 4.0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tag", required=True)
    p.add_argument("--bin", required=True)
    p.add_argument("--dump", default=f"{D}/edges_check5.root")
    p.add_argument("--nevt", type=int, default=5)
    a = p.parse_args()

    import torch
    import uproot

    norm = json.load(open(f"{D}/er_edge_norm_{a.tag}.json"))
    names = norm["feature_names"]
    br = ([f"ni_{i:02d}" for i in range(N_NODE)] + [f"no_{i:02d}" for i in range(N_NODE)]
          + [f"ef_{i:02d}" for i in range(N_EDGE)])
    t = uproot.open(a.dump)["edges"]
    arr = t.arrays(["evt"] + br, library="np")
    X = np.empty((len(arr["evt"]), len(br)), dtype=np.float32)
    for j, b in enumerate(br):
        X[:, j] = arr[b]
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        else:
            X[:, j] = np.log10(1.0 + X[:, j])
    X = (X - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
    blob = torch.load(f"{D}/er_edge_mlp_{a.tag}.pt", map_location="cpu", weights_only=False)
    m = torch.nn.Sequential(torch.nn.Linear(len(names), 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 1))
    m.load_state_dict(blob["state_dict"])
    m.eval()
    with torch.no_grad():
        s = np.asarray(m(torch.tensor(np.ascontiguousarray(X))).squeeze(1).tolist(),
                       dtype=np.float64)
    evt = arr["evt"]
    uevt = list(dict.fromkeys(evt.tolist()))  # dump order == C++ event order
    print(f"python: {len(s)} edges over {len(uevt)} events")

    worst = 0
    for th in THETAS:
        out = subprocess.run(
            [a.bin, "-m", "chains", "-n", str(a.nevt), "-e", str(th), "-L", "0.5",
             "-i", f"{S}/LSTNtuple_PU200RelVal_300evt.root",
             "-t", "/data2/segmentlinking/CMSSW_12_5_0_pre3/"
                   "RelValTTbar_14TeV_CMSSW_12_5_0_pre3/"],
            capture_output=True, text=True)
        cpp = [int(x) for x in re.findall(r"edges=\d+ pass=(\d+)", out.stdout)]
        py = [int((s[evt == e] >= th).sum()) for e in uevt]
        d = [c - q for c, q in zip(cpp, py)]
        tot_c, tot_p = sum(cpp), sum(py)
        rel = abs(tot_c - tot_p) / max(tot_p, 1)
        worst = max(worst, rel)
        print(f"  theta={th:+6.2f}  cpp={tot_c:>7d} py={tot_p:>7d} diff={tot_c - tot_p:+5d} "
              f"({rel * 100:.4f}%)  per-evt diff={d}")
    print(f"\nWORST relative pass-count disagreement: {worst * 100:.4f}% "
          f"({'PASS' if worst < 1e-3 else 'CHECK'})")


if __name__ == "__main__":
    sys.exit(main())
