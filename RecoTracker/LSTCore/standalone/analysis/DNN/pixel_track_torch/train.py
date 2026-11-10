# train.py: train the Torch pixel-track classifier on on-policy dumps (cfg/dump_cfg.py), with the recipe of the
# chain attach head: 60/20/20 split by event; class balance by pos_weight; true displaced tuples x8 (vxy 1-5 cm) and
# x16 (vxy >= 5 cm); the QCD sample at 25% of the loss; a focal factor sigmoid(z)^2 on fakes; Adam, lr 3e-3 with a
# cosine decay to 1e-5, batch 16384; the kept epoch maximises the worst per-sample validation AUC.
# Rows whose inputs are not finite are dropped: the selector rejects them before the score is used.
#   python3 train.py --dump <dump dir> --out <out dir> [--widths 32,32] [--epochs 200] [--device cuda]
import argparse
import json
import os
import sys
import time

import numpy as np
import torch
from sklearn.metrics import roc_auc_score

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pttlib  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--dump', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--widths', default='32,32')
ap.add_argument('--epochs', type=int, default=200)
ap.add_argument('--batch', type=int, default=16384)
ap.add_argument('--lr', type=float, default=3e-3)
ap.add_argument('--lr-min', type=float, default=1e-5)
ap.add_argument('--qcd-share', type=float, default=0.25)
ap.add_argument('--focal-gamma', type=float, default=2.0)
ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
ap.add_argument('--seed', type=int, default=7)
ap.add_argument('--threads', type=int, default=16)
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
torch.manual_seed(args.seed)
np.random.seed(args.seed)
torch.set_num_threads(args.threads)
t0 = time.time()

d = pttlib.load_dumps(args.dump)
ok = np.isfinite(d['f']).all(axis=1)
d = {k: v[ok] for k, v in d.items()}
trn, val, tst = pttlib.split_by_event(d)
X, y, s = d['f'], d['isTrue'].astype(np.float32), d['sample']
nev = {}
for part, m in (('train', trn), ('val', val), ('test', tst)):
    for k, sn in enumerate(pttlib.SAMPLES):
        mk = m & (s == k)
        nev[part + '_' + sn] = len(set(zip(d['run'][mk], d['lumi'][mk], d['evt'][mk])))
print('rows %d (non-finite dropped %d); true fraction %.3f; events %s' % (len(y), int((~ok).sum()), y.mean(), nev), flush=True)

pre = pttlib.Pre(X[trn])
print('log10(1+|x|/m) on', [pttlib.FEATS[j] for j in range(len(pttlib.FEATS)) if pre.dolog[j]], flush=True)
Z = pre.standardise(X)

# weights: displaced true tuples, then the QCD share of the total, then class balance through pos_weight
w = np.ones(len(y), np.float64)
vxy = d['tpVxy']
w[(y > 0) & (vxy >= 1) & (vxy < 5)] *= 8.
w[(y > 0) & (vxy >= 5)] *= 16.
Wt, Wq = w[trn & (s == 0)].sum(), w[trn & (s == 1)].sum()
w[s == 1] *= args.qcd_share / (1. - args.qcd_share) * Wt / Wq
pos_weight = w[trn & (y == 0)].sum() / w[trn & (y > 0)].sum()
print('pos_weight %.4f; QCD share of the training weight %.3f' % (pos_weight, w[trn & (s == 1)].sum() / w[trn].sum()), flush=True)

dev = torch.device(args.device)
Zt = pttlib.n2t(Z[trn]).to(dev)
yt = pttlib.n2t(y[trn]).to(dev)
wt = pttlib.n2t(w[trn].astype(np.float32)).to(dev)
Zv = pttlib.n2t(Z[val]).to(dev)
widths = [int(x) for x in args.widths.split(',')]
model = pttlib.MLP(widths).to(dev)
opt = torch.optim.Adam(model.parameters(), lr=args.lr)
nb = (len(yt) + args.batch - 1) // args.batch
sch = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs * nb, eta_min=args.lr_min)
pw = torch.tensor(pos_weight, dtype=torch.float32, device=dev)


def loss_fn(z, yy, ww):
    sp_neg = torch.nn.functional.softplus(-z)  # -log sigmoid(z)
    sp_pos = torch.nn.functional.softplus(z)  # -log(1 - sigmoid(z))
    focal = torch.sigmoid(z) ** args.focal_gamma
    per = yy * pw * sp_neg + (1. - yy) * focal * sp_pos
    return (ww * per).sum() / ww.sum()


@torch.no_grad()
def scores(Zd):
    model.eval()
    out = [model(Zd[i:i + (1 << 18)]) for i in range(0, len(Zd), 1 << 18)]
    return pttlib.t2n(torch.sigmoid(torch.cat(out)))


def aucs(sc, mask):
    r = {}
    for k, n in enumerate(pttlib.SAMPLES):
        m = mask & (s == k)
        r[n] = float(roc_auc_score(y[m], sc[m])) if 0 < y[m].sum() < m.sum() else float('nan')
        for tag, hm in (('3hit', d['nHits'] == 3), ('4hit', d['nHits'] >= 4)):
            mm = m & hm
            r[n + '_' + tag] = float(roc_auc_score(y[mm], sc[mm])) if 0 < y[mm].sum() < mm.sum() else float('nan')
    return r


hist, best = [], (-1., None, 0)
for ep in range(1, args.epochs + 1):
    model.train()
    te = time.time()
    perm = torch.randperm(len(yt), device=dev)
    tl = 0.
    for i in range(nb):
        j = perm[i * args.batch:(i + 1) * args.batch]
        opt.zero_grad()
        l = loss_fn(model(Zt[j]), yt[j], wt[j])
        l.backward()
        opt.step()
        sch.step()
        tl += float(l.detach())
    sv = np.zeros(len(y), np.float32)
    sv[val] = scores(Zv)
    a = aucs(sv, val)
    worst = min(a['ttbar'], a['qcd'])
    hist.append({'epoch': ep, 'loss': tl / nb, 'val_auc': a, 'worst': worst, 'sec': time.time() - te})
    if worst > best[0]:
        best = (worst, {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}, ep)
    if ep % 10 == 0 or ep <= 3:
        print('epoch %3d loss %.5f val AUC ttbar %.5f qcd %.5f (3-hit %.4f/%.4f, >=4-hit %.4f/%.4f) best %.5f @%d %.1fs' % (
            ep, tl / nb, a['ttbar'], a['qcd'], a['ttbar_3hit'], a['qcd_3hit'], a['ttbar_4hit'], a['qcd_4hit'],
            best[0], best[2], time.time() - te), flush=True)

model.load_state_dict(best[1])
model = model.cpu().eval()
allsc = np.zeros(len(y), np.float32)
Zall = pttlib.n2t(Z)
with torch.no_grad():
    allsc[:] = np.concatenate([pttlib.t2n(torch.sigmoid(model(Zall[i:i + (1 << 18)]))) for i in range(0, len(Zall), 1 << 18)])
res = {'args': vars(args), 'best_epoch': best[2], 'best_worst_val_auc': best[0], 'pos_weight': pos_weight,
       'events': nev, 'rows': int(len(y)), 'val_auc': aucs(allsc, val), 'test_auc': aucs(allsc, tst),
       'preprocessing': pre.state(), 'history': hist, 'sec': time.time() - t0}
torch.save({'widths': widths, 'state_dict': model.state_dict(), 'pre': pre.state()}, os.path.join(args.out, 'model.pt'))
json.dump(res, open(os.path.join(args.out, 'train.json'), 'w'), indent=1, default=float)
np.savez_compressed(os.path.join(args.out, 'scores.npz'), score=allsc, trn=trn, val=val, tst=tst)
print('best epoch %d, worst-sample val AUC %.5f; test AUC %s; %.0f s' % (best[2], best[0], res['test_auc'], time.time() - t0), flush=True)
