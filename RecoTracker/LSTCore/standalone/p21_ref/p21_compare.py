#!/usr/bin/env python3
"""P2.1 parity gate: production ChainEdges sidecar vs the frozen prototype reference.

usage: p21_compare.py <ref.bin> <prod.bin>
       p21_compare.py --feat <ref_feat.bin> <prod_feat.bin>

Both sidecars carry, per event: the exact E1/E2 degree-arithmetic counts, the kept counts,
and one record per kept edge (inner, outer, type, logit). Edges are matched on the key
(inner, outer, type); node indices are the dense chain-node numbering, which is the same
module-ordered sequence the ntuple's t3 rows use, so no remapping is needed.
"""
import sys
import struct
import numpy as np

EDGE_DTYPE = np.dtype([("inner", "<u4"), ("outer", "<u4"), ("type", "<u4"), ("logit", "<f4")])
HDR = struct.Struct("<IIIIQIIIII")  # magic ievt run lumi evt nT3 nE1ex nE2ex nE1kept nE2kept


def read_edges(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, run, lumi, evt, nT3, nE1ex, nE2ex, nE1k, nE2k = HDR.unpack_from(buf, off)
        assert magic == 0x50323145, f"bad magic at {off}"
        off += HDR.size
        n = nE1k + nE2k
        arr = np.frombuffer(buf, dtype=EDGE_DTYPE, count=n, offset=off)
        off += n * EDGE_DTYPE.itemsize
        events.append(dict(ievt=ievt, run=run, lumi=lumi, evt=evt, nT3=nT3,
                           nE1ex=nE1ex, nE2ex=nE2ex, nE1k=nE1k, nE2k=nE2k, edges=arr))
    return events


def key_of(arr):
    return (arr["inner"].astype(np.uint64) << np.uint64(34)) | \
           (arr["outer"].astype(np.uint64) << np.uint64(4)) | arr["type"].astype(np.uint64)


def compare_edges(ref_path, prod_path):
    ref = read_edges(ref_path)
    prod = read_edges(prod_path)
    print(f"ref  events: {len(ref)}   prod events: {len(prod)}")
    n = min(len(ref), len(prod))

    print()
    print(f"{'evt':>4} {'nT3 ref/prod':>16} {'E1exact ref/prod':>20} {'E2exact ref/prod':>20} "
          f"{'E1kept':>14} {'E2kept':>14}  count")
    all_counts_ok = True
    max_abs = 0.0
    n_above = 0
    n_pairs = 0
    n_unmatched = 0
    per_evt_max = []
    for i in range(n):
        r, p = ref[i], prod[i]
        ok = (r["nT3"] == p["nT3"] and r["nE1ex"] == p["nE1ex"] and r["nE2ex"] == p["nE2ex"]
              and r["nE1k"] == p["nE1k"] and r["nE2k"] == p["nE2k"])
        all_counts_ok = all_counts_ok and ok
        print(f"{i:>4} {r['nT3']:>7}/{p['nT3']:<8} {r['nE1ex']:>9}/{p['nE1ex']:<10} "
              f"{r['nE2ex']:>9}/{p['nE2ex']:<10} {r['nE1k']:>6}/{p['nE1k']:<7} "
              f"{r['nE2k']:>6}/{p['nE2k']:<7}  {'MATCH' if ok else 'DIFFER'}")

        rk, pk = key_of(r["edges"]), key_of(p["edges"])
        ro = np.argsort(rk, kind="stable")
        po = np.argsort(pk, kind="stable")
        rks, pks = rk[ro], pk[po]
        if rks.shape == pks.shape and np.array_equal(rks, pks):
            d = np.abs(r["edges"]["logit"][ro].astype(np.float64) -
                       p["edges"]["logit"][po].astype(np.float64))
            per_evt_max.append(float(d.max()) if d.size else 0.0)
            max_abs = max(max_abs, float(d.max()) if d.size else 0.0)
            n_above += int(np.count_nonzero(d > 1e-5))
            n_pairs += d.size
        else:
            common = np.intersect1d(rks, pks)
            n_unmatched += (rks.size - common.size) + (pks.size - common.size)
            ri = np.searchsorted(rks, common)
            pi = np.searchsorted(pks, common)
            d = np.abs(r["edges"]["logit"][ro][ri].astype(np.float64) -
                       p["edges"]["logit"][po][pi].astype(np.float64))
            per_evt_max.append(float(d.max()) if d.size else 0.0)
            max_abs = max(max_abs, float(d.max()) if d.size else 0.0)
            n_above += int(np.count_nonzero(d > 1e-5))
            n_pairs += d.size

    print()
    print(f"edge-set identity : {'EXACT (every (inner,outer,type) key matches)' if n_unmatched == 0 else f'{n_unmatched} UNMATCHED keys'}")
    print(f"edges compared    : {n_pairs}")
    print(f"max |dLogit|      : {max_abs:.6e}")
    print(f"N(|dLogit| > 1e-5): {n_above}")
    print(f"per-event max     : " + " ".join(f"{v:.2e}" for v in per_evt_max))
    print(f"count gate        : {'PASS' if all_counts_ok else 'FAIL'}")
    return 0 if (all_counts_ok and n_unmatched == 0) else 1


def read_feat(path):
    buf = open(path, "rb").read()
    off = 0
    nT3, kNode = struct.unpack_from("<II", buf, off)
    off += 8
    node = np.frombuffer(buf, dtype="<f4", count=nT3 * kNode, offset=off).reshape(nT3, kNode)
    off += nT3 * kNode * 4
    nE, kEdge = struct.unpack_from("<II", buf, off)
    off += 8
    rec = np.dtype([("inner", "<u4"), ("outer", "<u4"), ("type", "<u4"), ("f", "<f4", (kEdge,))])
    edge = np.frombuffer(buf, dtype=rec, count=nE, offset=off)
    return node, edge


NODE_NAMES = ["kappaSigned", "log10R", "tanLambda", "chordEta", "dphi01", "dz01", "dz12",
              "drt01", "drt12", "innermostLayer", "nBarrel", "nPS", "fakeScoreT3"]
EDGE_NAMES = ["etype", "dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
              "centerDist", "centerDistRel", "sharedLayer", "sharedIsPS", "sharedIsBarrel",
              "degIn", "degOut"]


def compare_feat(ref_path, prod_path):
    rn, re_ = read_feat(ref_path)
    pn, pe = read_feat(prod_path)
    print(f"nodes ref/prod = {rn.shape} / {pn.shape}   edges ref/prod = {re_.shape} / {pe.shape}")
    if rn.shape == pn.shape:
        d = np.abs(rn.astype(np.float64) - pn.astype(np.float64))
        print("\nNODE FEATURES (max |diff| per column, and #rows differing)")
        for k in range(rn.shape[1]):
            nb = int(np.count_nonzero(rn[:, k].view(np.uint32) != pn[:, k].view(np.uint32)))
            print(f"  [{k:2d}] {NODE_NAMES[k]:<16} max={d[:, k].max():.6e}  bitdiff_rows={nb}")

    rk, pk = key_of(re_), key_of(pe)
    ro, po = np.argsort(rk, kind="stable"), np.argsort(pk, kind="stable")
    if np.array_equal(rk[ro], pk[po]):
        rf = re_["f"][ro].astype(np.float64)
        pf = pe["f"][po].astype(np.float64)
        d = np.abs(rf - pf)
        print("\nEDGE FEATURES (max |diff| per column, and #rows differing bitwise)")
        for k in range(rf.shape[1]):
            nb = int(np.count_nonzero(re_["f"][ro][:, k].view(np.uint32) != pe["f"][po][:, k].view(np.uint32)))
            print(f"  [{k:2d}] {EDGE_NAMES[k]:<16} max={d[:, k].max():.6e}  bitdiff_rows={nb}")
    else:
        print("EDGE KEY SETS DIFFER")
    return 0


if __name__ == "__main__":
    if sys.argv[1] == "--feat":
        sys.exit(compare_feat(sys.argv[2], sys.argv[3]))
    sys.exit(compare_edges(sys.argv[1], sys.argv[2]))
