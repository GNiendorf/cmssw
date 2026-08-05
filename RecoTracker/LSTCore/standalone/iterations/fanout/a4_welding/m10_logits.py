#!/usr/bin/env python3
"""m10_logits.py - compute + cache the v3 edge-MLP logit for every row of the edge dump.

READ-ONLY: reads edges_300evt.root and edge_mlp_weights.h (the ACTIVE v3 weights the
m8_h4b anchor was produced with) and writes a float32 cache to the scratchpad, plus the
dump's label/simIdx/etype columns.
"""
import sys
import time

import numpy as np
import uproot

import m10_weights

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

ALL = ([f"ni_{i:02d}" for i in range(13)] + [f"no_{i:02d}" for i in range(13)] +
       [f"ef_{i:02d}" for i in range(14)])
CHUNK = 2_000_000


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else f"{SA}/prototype/edges_300evt.root"
    tag = sys.argv[2] if len(sys.argv) > 2 else "300"
    W = m10_weights.parse(f"{SA}/prototype/edge_mlp_weights.h", "edgemlp")
    t = uproot.open(src)["edges"]
    n = t.num_entries
    out = np.empty(n, dtype=np.float32)
    lab = np.empty(n, dtype=np.int8)
    sim = np.empty(n, dtype=np.int32)
    ety = np.empty(n, dtype=np.int8)
    t0 = time.time()
    for lo in range(0, n, CHUNK):
        hi = min(lo + CHUNK, n)
        a = t.arrays(ALL + ["label", "simIdx", "etype"], entry_start=lo, entry_stop=hi, library="np")
        X = np.empty((hi - lo, 40), dtype=np.float32)
        for i, nm in enumerate(ALL):
            X[:, i] = a[nm]
        out[lo:hi] = m10_weights.logit(X, W).astype(np.float32)
        lab[lo:hi] = a["label"]
        sim[lo:hi] = a["simIdx"]
        ety[lo:hi] = a["etype"]
        print(f"  {hi}/{n}  {time.time()-t0:.1f}s", flush=True)
    np.savez(f"{SCRATCH}/m10_edgecache_{tag}.npz", logit=out, label=lab, simIdx=sim, etype=ety)
    print(f"cached {n} edges; pass(theta=0) = {(out>=0).sum()} ({(out>=0).mean():.4f}); "
          f"true = {(lab==1).sum()}")


if __name__ == "__main__":
    main()
