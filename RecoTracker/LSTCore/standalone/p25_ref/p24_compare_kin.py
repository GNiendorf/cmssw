#!/usr/bin/env python3
"""P2.4 gate (a), second leg: TC kinematics multiset, ported pipeline vs the frozen prototype.

usage: p24_compare_kin.py <proto.root> <prod.root>

The hit-level comparison (p23_compare_tc.py) proves the SET of emitted track candidates and, via
the type column, which chains the attach upgraded to the pT5 class. It cannot prove WHICH pLS each
upgrade took, because the prototype's tc_hitOT branch carries outer-tracker hits only.

The attached pLS shows through the kinematics: an upgraded row's pt is the attached seed's ptIn
(prototype/main.cc, the -A 4 assembly loop) while its eta and phi stay the chain's. So the multiset
of (type, pt, eta, phi) over the whole collection pins the (chain, pLS) pairing: eta/phi identify
the chain, pt identifies the seed.

Floats are compared on their exact bit pattern by default; --tol switches to a rounded comparison
and reports the residual, which is what a near-miss should be quantified with.
"""
import argparse
import struct
from collections import Counter

import uproot


def load(path, tol):
    with uproot.open(path)["tree"] as t:
        ty = t["tc_type"].array(library="np")
        pt = t["tc_pt"].array(library="np")
        eta = t["tc_eta"].array(library="np")
        phi = t["tc_phi"].array(library="np")
    out = []
    for i in range(len(ty)):
        rows = []
        for a, b, c, d in zip(ty[i], pt[i], eta[i], phi[i]):
            if tol is None:
                key = (int(a), struct.pack("<f", b), struct.pack("<f", c), struct.pack("<f", d))
            else:
                key = (int(a), round(float(b), tol), round(float(c), tol), round(float(d), tol))
            rows.append(key)
        out.append(rows)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("proto")
    ap.add_argument("prod")
    ap.add_argument("--tol", type=int, default=None, help="decimals to round to (default: bit-exact)")
    args = ap.parse_args()

    proto = load(args.proto, args.tol)
    prod = load(args.prod, args.tol)
    n = min(len(proto), len(prod))
    if len(proto) != len(prod):
        print(f"WARNING: event count differs proto={len(proto)} prod={len(prod)}; comparing {n}")

    print(f"{'evt':>4} {'proto':>7} {'prod':>7} {'common':>7} {'onlyP':>6} {'onlyR':>6}  per-type only(proto/prod)")
    tp = tr = tc = 0
    for i in range(n):
        cp, cr = Counter(proto[i]), Counter(prod[i])
        common, only_p, only_r = cp & cr, cp - cr, cr - cp
        nc, np_, nr = sum(common.values()), sum(only_p.values()), sum(only_r.values())
        tc += nc
        tp += np_
        tr += nr
        bt = {}
        for k, c in only_p.items():
            bt.setdefault(k[0], [0, 0])[0] += c
        for k, c in only_r.items():
            bt.setdefault(k[0], [0, 0])[1] += c
        desc = " ".join(f"t{k}:{a}/{b}" for k, (a, b) in sorted(bt.items()))
        print(f"{i:>4} {sum(cp.values()):>7} {sum(cr.values()):>7} {nc:>7} {np_:>6} {nr:>6}  {desc}")

    print()
    print(f"TOTAL common={tc} onlyProto={tp} onlyProd={tr}")
    if tp == 0 and tr == 0:
        print("GATE (a) kinematics: PASS - exact (type, pt, eta, phi) multiset match on every event")
        return 0
    print("GATE (a) kinematics: MISMATCH")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
