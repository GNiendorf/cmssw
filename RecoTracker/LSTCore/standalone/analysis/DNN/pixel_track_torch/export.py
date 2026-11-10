# export.py: TorchScript export of the trained classifier for PixelTrackTorchHighPuritySelector (FP32 inference,
# inferenceHalf False). Input: the selector's raw 17 features; output: the probability. The standardisation is folded
# into the first linear layer. Checks the exported scores against the training model on the dump rows.
#   python3 export.py --train <train dir> --dump <dump dir> --out <file.pt>
import argparse
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pttlib  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--train', required=True)
ap.add_argument('--dump', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--check-rows', type=int, default=500000)
args = ap.parse_args()

ck = torch.load(os.path.join(args.train, 'model.pt'), map_location='cpu')
pre = pttlib.Pre(state=ck['pre'])
mlp = pttlib.MLP(ck['widths'])
mlp.load_state_dict(ck['state_dict'])
mlp.eval()
dep = pttlib.PixelTrackClassifier(pre, mlp).eval()
sm = torch.jit.script(dep)
sm.save(args.out)

d = pttlib.load_dumps(args.dump, branches=['f', 'run', 'lumi', 'evt', 'isTrue'])
X = d['f'][np.isfinite(d['f']).all(axis=1)][:args.check_rows]
with torch.no_grad():
    s_train = pttlib.t2n(torch.sigmoid(mlp(pttlib.n2t(pre.standardise(X)))))
    s_exp = pttlib.t2n(torch.jit.load(args.out).eval()(pttlib.n2t(X))).reshape(-1)
diff = np.abs(s_train - s_exp)
res = {'out': args.out, 'rows': int(len(X)), 'max_abs_diff': float(diff.max()), 'mean_abs_diff': float(diff.mean()),
       'decision_agreement_at_0.5': float(np.mean((s_train >= 0.5) == (s_exp >= 0.5)))}
json.dump(res, open(args.out + '.export.json', 'w'), indent=1)
print(res)
