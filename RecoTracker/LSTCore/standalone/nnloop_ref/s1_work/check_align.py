#!/usr/bin/env python3
"""S1 gate: prove the truth join AND the event alignment with the shipped head's own logit.

If my label replication and the record<->entry alignment are both right, the SHIPPED
logOdds dumped alongside each edge must separate my labels at the head's own quoted
quality (val AUC .9610).  A one-event misalignment must destroy it.
"""
import os
import sys

import numpy as np
from sklearn.metrics import roc_auc_score

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpio import iter_edges  # noqa: E402

LAB = sys.argv[1]
N = int(sys.argv[2])
D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round1"

lo, et, labs = [], [], []
for i, (ievt, nN, nE1, nE2, ei, eo, ety, l) in enumerate(iter_edges(D + "/edges.bin")):
    if i >= N:
        break
    z = np.load(os.path.join(LAB, "ev%04d.npz" % i))
    lo.append(l)
    et.append(ety)
    labs.append(z["label"])
lo = np.concatenate(lo)
et = np.concatenate(et)
lab = np.concatenate(labs)
print("rows %d  true %.4f  E1 true %.4f  E2 true %.4f" %
      (len(lab), lab.mean(), lab[et == 1].mean(), lab[et == 2].mean()))
print("ALIGNED   AUC(shipped logit vs my label) = %.5f  (E1 %.5f, E2 %.5f)" %
      (roc_auc_score(lab, lo), roc_auc_score(lab[et == 1], lo[et == 1]),
       roc_auc_score(lab[et == 2], lo[et == 2])))

# shift control: label event k against edges of event k+1 (per-event, same length only)
sl, sh = [], []
for i, (ievt, nN, nE1, nE2, ei, eo, ety, l) in enumerate(iter_edges(D + "/edges.bin")):
    if i >= N - 1:
        break
    z = np.load(os.path.join(LAB, "ev%04d.npz" % (i + 1)))
    m = min(len(l), len(z["label"]))
    sl.append(z["label"][:m])
    sh.append(l[:m])
sl = np.concatenate(sl)
sh = np.concatenate(sh)
print("SHIFTED+1 AUC = %.5f   (must collapse toward 0.5)" % roc_auc_score(sl, sh))
