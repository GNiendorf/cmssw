# pttlib.py: shared helpers of the pixel-track Torch classifier training (dump reading, split, preprocessing, model).
import glob
import os

import numpy as np
import torch
import torch.nn as nn

FEATS = ['chi2', 'dzError', 'dxyError', 'eta', 'nHits', 'phi', 'phiError', 'pt', 'qOverPtError', 'dzBS', 'dxyBS',
         'nLayers', 'cotThetaError', 'covCotThetaDz', 'covDxyQOverPt', 'covPhiDxy', 'covPhiQOverPt']
# inputs never log-transformed: angles and hit/layer counts
NO_LOG = {'eta', 'phi', 'nHits', 'nLayers'}
BRANCHES = ['run', 'lumi', 'evt', 'idx', 'nPre', 'f', 'quality', 'nHits', 'layerMask', 'dep', 'isTrue', 'share',
            'tpPt', 'tpEta', 'tpVxy', 'tpVz', 'tpPdg', 'tpBx', 'tpSig', 'tpKey']
SAMPLES = ('ttbar', 'qcd')
# the heavy-tail test: an input gets the log transform when its 99.9% / 50% quantile ratio of |x| exceeds this
HEAVY_TAIL_RATIO = 10.0


def t2n(t):
    """torch -> numpy through a buffer copy (the CMSSW torch build has no numpy bridge)"""
    import ctypes
    t = t.detach().cpu().contiguous()
    dt = {torch.float32: (ctypes.c_float, np.float32), torch.float64: (ctypes.c_double, np.float64),
          torch.int64: (ctypes.c_int64, np.int64), torch.bool: (ctypes.c_bool, np.bool_)}
    if t.dtype not in dt:
        t = t.float()
    c, n = dt[t.dtype]
    if t.numel() == 0:
        return np.zeros(tuple(t.shape), n)
    return np.ctypeslib.as_array((c * t.numel()).from_address(t.data_ptr())).copy().astype(n).reshape(tuple(t.shape))


def n2t(a):
    a = np.ascontiguousarray(a)
    m = {np.dtype(np.float32): torch.float32, np.dtype(np.float64): torch.float64, np.dtype(np.int64): torch.int64,
         np.dtype(np.bool_): torch.bool}
    if a.size == 0:
        return torch.zeros(a.shape, dtype=m[a.dtype])
    return torch.frombuffer(bytearray(a.tobytes()), dtype=m[a.dtype]).reshape(a.shape)


def load_dumps(dumpdir, branches=BRANCHES):
    """all rows of dump/<sample>/dump_*.root; adds 'sample' (index in SAMPLES)"""
    import uproot
    out = []
    for k, s in enumerate(SAMPLES):
        for fn in sorted(glob.glob(os.path.join(dumpdir, s, 'dump_*.root'))):
            a = uproot.open(fn)['pttDump/tuples'].arrays(branches, library='np')
            if 'f' in a:
                a['f'] = np.asarray(a['f'], dtype=np.float32).reshape(-1, len(FEATS))
            if 'dep' in a:
                a['dep'] = np.asarray(a['dep'], dtype=bool).reshape(-1, 2)
            a['sample'] = np.full(len(a[branches[0]]), k, dtype=np.int8)
            out.append(a)
    return {key: np.concatenate([o[key] for o in out]) for key in out[0]}


def split_by_event(d):
    """60/20/20 train/validation/test by event, from a hash of (sample, run, lumi, event)"""
    key = (d['sample'].astype(np.uint64) * np.uint64(1000003) + d['run'].astype(np.uint64)) * np.uint64(1000003)
    key = (key + d['lumi'].astype(np.uint64)) * np.uint64(2654435761) + d['evt'].astype(np.uint64)
    key = (key * np.uint64(0x9E3779B97F4A7C15)) >> np.uint64(40)
    u = key.astype(np.float64) / float(1 << 24)
    return u < 0.6, (u >= 0.6) & (u < 0.8), u >= 0.8


class Pre:
    """log10(1 + |x| / m) with the sign of x on heavy-tailed inputs (m = median |x|), clip to the training
    quantiles, standardise; all constants fitted on the training rows"""

    def __init__(self, X=None, state=None):
        if state is not None:
            self.__dict__.update({k: np.asarray(v, dtype=np.float32) if isinstance(v, list) else v for k, v in state.items()})
            self.dolog = np.asarray(self.dolog, dtype=bool)
            return
        a = np.abs(X)
        q50 = np.quantile(a, 0.5, axis=0)
        q999 = np.quantile(a, 0.999, axis=0)
        ratio = q999 / np.maximum(q50, np.finfo(np.float32).tiny)
        self.dolog = np.array([(FEATS[j] not in NO_LOG) and ratio[j] > HEAVY_TAIL_RATIO for j in range(len(FEATS))])
        self.scale = np.where(self.dolog, np.maximum(q50, np.finfo(np.float32).tiny), 1.).astype(np.float32)
        Z = self.transform(X, clip=False)
        self.lo = np.quantile(Z, 0.0005, axis=0).astype(np.float32)
        self.hi = np.quantile(Z, 0.9995, axis=0).astype(np.float32)
        Z = np.clip(Z, self.lo, self.hi)
        self.mean = Z.mean(axis=0).astype(np.float32)
        self.std = (Z.std(axis=0) + 1e-6).astype(np.float32)
        self.ratio = ratio

    def transform(self, X, clip=True):
        Z = np.where(self.dolog, np.sign(X) * np.log10(1. + np.abs(X) / self.scale), X).astype(np.float32)
        if clip:
            Z = np.clip(Z, self.lo, self.hi)
        return Z

    def standardise(self, X):
        return ((self.transform(X) - self.mean) / self.std).astype(np.float32)

    def state(self):
        return {k: (v.tolist() if isinstance(v, np.ndarray) else v) for k, v in self.__dict__.items()}


class MLP(nn.Module):
    def __init__(self, widths):
        super().__init__()
        layers, d = [], len(FEATS)
        for w in widths:
            layers += [nn.Linear(d, w), nn.ReLU()]
            d = w
        layers += [nn.Linear(d, 1)]
        self.net = nn.Sequential(*layers)

    def forward(self, x):
        return self.net(x).squeeze(-1)


class PixelTrackClassifier(nn.Module):
    """Deployed form: forward(track_features) -> probability [-1, 1], raw selector features in. The heavy-tail
    transform and the clip are explicit; the standardisation is folded into the first linear layer."""

    def __init__(self, pre, mlp):
        super().__init__()
        self.register_buffer('dolog', torch.tensor(pre.dolog.tolist(), dtype=torch.bool))
        self.register_buffer('scale', n2t(pre.scale.astype(np.float32)))
        self.register_buffer('lo', n2t(pre.lo.astype(np.float32)))
        self.register_buffer('hi', n2t(pre.hi.astype(np.float32)))
        lin = [m for m in mlp.net if isinstance(m, nn.Linear)]
        first = nn.Linear(lin[0].in_features, lin[0].out_features)
        with torch.no_grad():
            W = lin[0].weight.detach().cpu().float()
            b = lin[0].bias.detach().cpu().float()
            std = n2t(pre.std.astype(np.float32))
            mean = n2t(pre.mean.astype(np.float32))
            first.weight.copy_(W / std.reshape(1, -1))
            first.bias.copy_(b - (W * (mean / std).reshape(1, -1)).sum(dim=1))
        rest = []
        for m in list(mlp.net)[1:]:
            if isinstance(m, nn.Linear):
                c = nn.Linear(m.in_features, m.out_features)
                with torch.no_grad():
                    c.weight.copy_(m.weight.detach().cpu().float())
                    c.bias.copy_(m.bias.detach().cpu().float())
                rest.append(c)
            else:
                rest.append(nn.ReLU())
        self.first = first
        self.rest = nn.Sequential(*rest)

    def forward(self, track_features: torch.Tensor) -> torch.Tensor:
        x = track_features
        x = torch.where(self.dolog.reshape(1, -1), torch.sign(x) * torch.log10(1. + torch.abs(x) / self.scale.reshape(1, -1)), x)
        x = torch.maximum(torch.minimum(x, self.hi.reshape(1, -1)), self.lo.reshape(1, -1))
        z = self.rest(torch.relu(self.first(x)))
        return torch.sigmoid(z).reshape(-1, 1)


def bin_index(x, edges):
    """number of edges at or below x (the selector's working-point bin)"""
    return np.searchsorted(np.asarray(edges, dtype=np.float32), x.astype(np.float32), side='right')
