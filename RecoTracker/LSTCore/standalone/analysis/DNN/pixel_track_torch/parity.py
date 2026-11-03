# parity.py: C++ parity check of the shipped model and working points. Input: a dump made with PTT_DEP1 set to the
# shipped settings (see run_all.sh), so column dep[1] is the in-menu decision of PixelTrackTorchHighPuritySelector
# with the exported model and the working-point tables. The same decision is recomputed offline from the dumped inputs
# with the exported TorchScript file and the JSON tables; the two must agree.
#   python3 parity.py --dump <parity dump dir> --model <file.pt> --wp <wp.json> --out <parity.json>
import argparse
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pttlib  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--dump', required=True)
ap.add_argument('--model', required=True)
ap.add_argument('--wp', required=True)
ap.add_argument('--out', required=True)
args = ap.parse_args()

wp = json.load(open(args.wp))
d = pttlib.load_dumps(args.dump, branches=['run', 'lumi', 'evt', 'f', 'nHits', 'dep', 'isTrue'])
X = d['f']
fin = np.isfinite(X).all(axis=1)
m = torch.jit.load(args.model, map_location='cpu').eval()
with torch.no_grad():
    sc = np.concatenate([pttlib.t2n(m(pttlib.n2t(X[i:i + (1 << 18)]))).reshape(-1) for i in range(0, len(X), 1 << 18)])
nE = len(wp['absEtaEdges']) + 1
cell = pttlib.bin_index(X[:, 7], wp['ptEdges']) * nE + pttlib.bin_index(np.abs(X[:, 3]), wp['absEtaEdges'])
thr = np.where(d['nHits'] == 3, np.asarray(wp['thresholdsThreeHit'])[cell], np.asarray(wp['thresholds'])[cell])
off = fin & (sc >= thr)
cpp = d['dep'][:, 1]
dis = off != cpp
margin = np.abs(sc - thr)
res = {'rows': int(len(X)), 'agree': float(np.mean(~dis)), 'disagree': int(dis.sum()),
       'disagree_offline_pass': int((dis & off).sum()), 'disagree_cpp_pass': int((dis & cpp).sum()),
       'max_margin_of_disagreements': float(margin[dis].max()) if dis.any() else 0.,
       'pass_fraction_cpp': float(cpp.mean()), 'pass_fraction_offline': float(off.mean())}
json.dump(res, open(args.out, 'w'), indent=1)
print(res)
