#!/usr/bin/env python3
"""Rung 4: the builders' selection replayed from probe records (import only).  CURRENT = the head's rule."""
import numpy as np

T4_RZ_ETA = np.array([63.0164, 49.1308, 57.8446, 71.1812, 64.1492, 45.2832, 78.2952, 82.1582, 60.5490, 80.5764, 62.4270, 35.7822, 18.0594,
                      15.3724, 5.5182, 10.1174, 12.8028, 7.4696, 8.9536, 10.6174, 30.9070, 28.2214, 46.5556, 36.7286, 52.6552], np.float32)
T4_RZ_LINEAR = np.float32(11.678)
T5_RZ_LINEAR = np.float32(4.677)
INF = np.float32(np.inf)
# passT5RZConstraint: layers -> cut (inf = 'return true' region)
T5_RZ = {(7, 8, 9, 10, 11): INF, (7, 8, 9, 10, 16): 37.956, (7, 8, 9, 15, 16): 11.622, (1, 7, 8, 9, 10): INF, (1, 7, 8, 9, 15): 37.941,
         (1, 2, 7, 8, 9): INF, (1, 2, 7, 8, 14): 52.561, (1, 2, 7, 13, 14): 13.76, (1, 2, 3, 7, 8): 44.247, (1, 2, 3, 7, 13): 33.752,
         (1, 2, 3, 12, 13): 21.213, (1, 2, 3, 4, 5): 29.035, (1, 2, 3, 4, 12): 23.037, (2, 7, 8, 9, 15): 41.036, (2, 7, 8, 14, 15): 14.092,
         (2, 3, 7, 8, 14): 23.748, (2, 3, 7, 13, 14): 17.945, (2, 3, 4, 5, 6): 8.803, (2, 3, 4, 5, 12): 7.930, (2, 3, 4, 12, 13): 7.626}


def eta1(rec):
    n = rec["xyz"].shape[1] // 3
    xyz = rec["xyz"].reshape(-1, n, 3)
    return np.abs(np.arcsinh(xyz[:, 0, 2] / np.hypot(xyz[:, 0, 0], xyz[:, 0, 1])))


def t4_rz_cut(rec):
    e = eta1(rec)
    b = np.where(e > 2.5, 24, np.minimum((e / np.float32(0.1)).astype(int), 24))
    return np.where(rec["rzLinear"] == 1, T4_RZ_LINEAR, T4_RZ_ETA[b])


def t5_rz_cut(rec, extra=None):
    tab = dict(T5_RZ)
    if extra:
        tab.update(extra)
    cut = np.full(len(rec), INF, np.float32)
    named = np.zeros(len(rec), bool)
    L = rec["lstLayers"]
    for k, v in tab.items():
        m = np.all(L == np.array(k, L.dtype), axis=1)
        cut[m] = v
        named |= m
    cut = np.where(rec["rzLinear"] == 1, T5_RZ_LINEAR, cut)
    return cut, named


def t4_parts(rec, wp_scale_disp=1.0, wp_fake=None, flag="either", rzcut=None):
    """dict of component passes for a T4 rule."""
    charge = rec["charge"][:, 0] == rec["charge"][:, 1]
    fk = rec["wpFake"] if wp_fake is None else wp_fake
    dnn = (rec["scores"][:, 2] > np.float32(wp_scale_disp) * rec["wpDisp"]) & (rec["scores"][:, 0] < fk)
    dbeta = rec["dBeta"] * rec["dBeta"] <= rec["dBetaCut2"]
    rz = rec["rzChi2"] < (t4_rz_cut(rec) if rzcut is None else rzcut)
    f0 = (rec["t3Flags"][:, 0] & 2) != 0
    f1 = (rec["t3Flags"][:, 1] & 2) != 0
    fl = {"either": ~(f0 | f1), "both": ~(f0 & f1), "off": np.ones(len(rec), bool)}[flag]
    return dict(charge=charge, dnn=dnn, dbeta=dbeta, rz=rz, flag=fl)


def t5_parts(rec, wp_scale=1.0, flag="both", rzcut=None):
    f0 = (rec["t3Flags"][:, 0] & 2) != 0
    f1 = (rec["t3Flags"][:, 1] & 2) != 0
    fl = {"either": ~(f0 | f1), "both": ~(f0 & f1), "off": np.ones(len(rec), bool)}[flag]
    dnn = rec["dnnScore"] > np.float32(wp_scale) * rec["wp98"]
    d1 = rec["dBeta"][:, 0] ** 2 <= rec["dBetaCut2"][:, 0]
    d2 = rec["dBeta"][:, 1] ** 2 <= rec["dBetaCut2"][:, 1]
    cut = t5_rz_cut(rec)[0] if rzcut is None else rzcut
    rz = rec["rzChi2"] < cut
    return dict(flag=fl, dnn=dnn, dbeta1=d1, dbeta2=d2, rz=rz, start=rec["startValid"] == 1)


def allpass(parts):
    out = None
    for v in parts.values():
        out = v if out is None else (out & v)
    return out


# ---- the rung-4 rule, mirroring patch/apply_rule.py
T4_REGIONS = {(1, 2, 3, 4): 36.3, (1, 2, 3, 7): 39.4, (1, 2, 3, 12): 27.9, (1, 2, 7, 8): 40.9, (1, 2, 7, 13): 13.7, (1, 7, 8, 9): 31.1,
              (1, 7, 8, 14): 34.3, (2, 3, 7, 13): 15.9, (2, 7, 8, 9): 39.6, (2, 7, 8, 14): 12.2, (3, 4, 5, 6): 84.6, (3, 4, 5, 12): 63.7,
              (3, 4, 12, 13): 35.8, (7, 8, 9, 10): 33.9}
T4_REGIONS_NEW = {(3, 12, 13, 14): 58.5, (4, 12, 13, 14): 166.6, (2, 12, 13, 14): 28.8, (7, 13, 14, 15): 71.9, (8, 14, 15, 16): 75.7,
                  (12, 13, 14, 15): 92.8, (13, 14, 15, 16): 92.8}
T5_REGIONS_NEW = {(2, 3, 12, 13, 14): 10.1, (1, 2, 12, 13, 14): 10.1, (2, 7, 13, 14, 15): 27.4, (7, 8, 14, 15, 16): 20.9, (1, 7, 8, 14, 15): 33.0,
                  (7, 13, 14, 15, 16): 118.6, (2, 12, 13, 14, 15): 118.6}
K_T4_FAKE_WP_LOOSE = np.float32(0.99)
K_T5_WP_SCALE = np.float32(0.01)


def t4_rule_cut(rec, regions=False):
    tab = dict(T4_REGIONS)
    if regions:
        tab.update(T4_REGIONS_NEW)
    reg = np.zeros(len(rec), np.float32)
    L = rec["lstLayers"]
    for k, v in tab.items():
        reg[np.all(L == np.array(k, L.dtype), axis=1)] = v
    stock = t4_rz_cut(rec)
    return np.where(rec["rzLinear"] == 1, stock, np.maximum(stock, reg))


def t4_new(rec, parts=("t4flag", "t4dnn", "t4rz"), regions=False):
    p = t4_parts(rec, flag="off" if "t4flag" in parts else "either", rzcut=t4_rule_cut(rec, regions) if "t4rz" in parts else None)
    if "t4dnn" in parts:
        p["dnn"] = rec["scores"][:, 0] < K_T4_FAKE_WP_LOOSE
    return p


def t5_new(rec, parts=("t5dnn",), regions=False):
    cut = t5_rz_cut(rec, T5_REGIONS_NEW if regions else None)[0]
    return t5_parts(rec, wp_scale=K_T5_WP_SCALE if "t5dnn" in parts else 1.0, rzcut=cut)
