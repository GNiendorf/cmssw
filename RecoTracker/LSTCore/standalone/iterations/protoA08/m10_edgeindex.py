#!/usr/bin/env python3
"""m10_edgeindex.py - build the (lumi,evt) -> [row_begin,row_end) index of the edge dump."""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"
EDGES = f"{SA}/prototype/edges_300evt.root"

t = uproot.open(EDGES)["edges"]
print("entries", t.num_entries)
evt = t["evt"].array(library="np")
lumi = t["lumi"].array(library="np")
key = lumi.astype(np.int64) * 100000 + evt.astype(np.int64)
# boundaries where key changes
chg = np.nonzero(np.diff(key))[0] + 1
starts = np.concatenate([[0], chg])
ends = np.concatenate([chg, [len(key)]])
keys = key[starts]
print("blocks:", len(starts), "distinct keys:", len(np.unique(keys)))
np.savez(f"{SCRATCH}/m10_edgeidx.npz", starts=starts, ends=ends, keys=keys,
         lumi=(keys // 100000), evt=(keys % 100000))
for i in range(3):
    print(f"  block {i}: lumi={keys[i]//100000} evt={keys[i]%100000} rows {starts[i]}..{ends[i]} n={ends[i]-starts[i]}")
