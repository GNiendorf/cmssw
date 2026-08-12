#!/usr/bin/env python3
"""M3, offline companion to (b): what does M1's PER-SHARED-MD cap do to the WELDED edges on jets,
and does the KEEP RULE matter?

M1's P2 keeps the FIRST min(deg, C) triplets of each shared-MD slice in CSR order (ascending node
index on CPU, atomicAdd arrival order on GPU) and states honestly that a better rule is not
affordable in-kernel. That is exactly the kind of question a dump answers for free: the label (which
edges the weld actually consumed) is known, the shared-MD keys are reconstructible from
LST_CHAIN_NODE_DUMP, and the CSR order on CPU is just ascending node index.

For each E1 edge (a -> b) with shared key k = md2[a] = md0[b]:
    kept(C) <=> rank_in(a, k) < C  AND  rank_out(b, k) < C
under three rules for the rank:
    first     ascending node index          == what M1's patch actually does on CPU
    rand      a random permutation per key  == the GPU's arrival order (3 seeds, spread quoted)
    oracle    descending (max logOdds of that node's edges at k) == the best any per-key node
              selection could do, i.e. the ceiling M1's "score-ranked keeping" idea aims at

usage: mdcap.py <evt,evt,...> [--json out.json]
"""
import json
import sys

import numpy as np

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
sys.path.insert(0, S + "/jetrecon_ref")
sys.path.insert(0, S + "/nnloop_ref/s1_work")
sys.path.insert(0, S + "/m3_ref")
from degrees import events, keys_of          # noqa: E402
from dumpio import iter_edges                # noqa: E402
from chainnodes import iter_chain_nodes, welded_pairs   # noqa: E402

CS = [64, 128, 256, 512, 1024]


def node_fakescore(path):
    """read ONLY the 13-float node table of the first event of an edgefeat.bin ('P21F') --
    column 12 is fakeScoreT3, the per-T3 quality score that already exists BEFORE the edge
    enumeration, i.e. the one keep-rule input a per-key cap could actually afford."""
    import struct
    with open(path, "rb") as f:
        magic, ievt, nN, kf = struct.unpack("<IIII", f.read(16))
        assert magic == 0x50323146 and kf == 13
        nf = np.frombuffer(f.read(4 * nN * kf), dtype=np.float32).reshape(nN, kf)
        return nf[:, 12].copy()


def rank_within(key, order_val, n):
    """rank of each item inside its key group, ordered by order_val ascending (0 = kept first)."""
    o = np.lexsort((order_val, key))
    r = np.empty(n, dtype=np.int64)
    ks = key[o]
    new = np.empty(len(ks), dtype=bool)
    new[0] = True
    new[1:] = ks[1:] != ks[:-1]
    idx = np.arange(len(ks))
    start = np.maximum.accumulate(np.where(new, idx, 0))
    r[o] = idx - start
    return r


def main():
    evts = [int(x) for x in sys.argv[1].split(",")]
    res = {}
    for i in evts:
        rows = None
        for ievt, r in events("%s/jetrecon_ref/nodes/jets_evt%d.bin" % (S, i)):
            rows = r
            break
        m0, m1, m2, iLS, oLS, nmd = keys_of(rows)
        nN = len(rows)

        _, nN2, nE1, nE2, inner, outer, etyp, lo = next(
            iter_edges("%s/m3_ref/dumps/jet%d_edges.bin" % (S, i)))
        assert nN2 == nN, (nN2, nN)
        _, _, _, runs, _ = next(iter_chain_nodes("%s/m3_ref/dumps/jet%d_chains.bin" % (S, i)))
        wa, wb = welded_pairs(runs)
        key = inner.astype(np.int64) * nN + outer.astype(np.int64)
        wkey = np.unique(wa.astype(np.int64) * nN + wb.astype(np.int64))
        srt = np.argsort(key, kind="stable")
        pos = np.clip(np.searchsorted(key[srt], wkey), 0, len(key) - 1)
        okm = key[srt][pos] == wkey
        welded = np.zeros(len(key), dtype=np.uint8)
        welded[srt[pos[okm]]] = 1
        assert int(okm.sum()) == len(wkey), "welded pair missing from the edge dump"
        del key, srt, pos

        # which dumped rows are the MD family (E1)?  identify by the shared-key relation itself
        isE1 = m2[inner] == m0[outer]
        types = np.unique(etyp)
        print("evt %d: nodes %d edges %d (nE1=%d nE2=%d) types %s ; rows with md2[in]==md0[out]: %d"
              % (i, nN, len(inner), nE1, nE2, types.tolist(), int(isE1.sum())), flush=True)

        a = inner[isE1]
        b = outer[isE1]
        k = m2[a]                     # == m0[b]
        w = welded[isE1]
        loE1 = lo[isE1]
        nw = int(w.sum())
        print("   E1 rows %d, welded E1 %d of %d welded total" % (len(a), nw, int(welded.sum())))

        # per-node best logOdds at its (unique) key, for the oracle rule
        bo = np.full(nN, -1e30, dtype=np.float32)
        np.maximum.at(bo, a, loE1)
        bi = np.full(nN, -1e30, dtype=np.float32)
        np.maximum.at(bi, b, loE1)

        nodes = np.arange(nN)
        rules = {}
        rules["first"] = (rank_within(m2, nodes.astype(np.float64), nN),
                          rank_within(m0, nodes.astype(np.float64), nN))
        rules["oracle"] = (rank_within(m2, -bo.astype(np.float64), nN),
                           rank_within(m0, -bi.astype(np.float64), nN))
        for s in (0, 1, 2):
            rng = np.random.default_rng(s)
            rules["rand%d" % s] = (rank_within(m2, rng.random(nN), nN),
                                   rank_within(m0, rng.random(nN), nN))
        fs = node_fakescore("%s/m3_ref/dumps/jet%d_edgefeat.bin" % (S, i)).astype(np.float64)
        rules["fakeLo"] = (rank_within(m2, fs, nN), rank_within(m0, fs, nN))
        rules["fakeHi"] = (rank_within(m2, -fs, nN), rank_within(m0, -fs, nN))

        res[i] = {}
        for nm, (rin, rout) in rules.items():
            row = {}
            for C in CS:
                keep = (rin[a] < C) & (rout[b] < C)
                row["C%d" % C] = dict(recall=float(w[keep].sum()) / nw,
                                      keep=float(keep.sum()) / len(a),
                                      kept_welded=int(w[keep].sum()), kept_edges=int(keep.sum()))
            row["nE1"] = len(a)
            row["nweldedE1"] = nw
            res[i][nm] = row
            print("   %-7s " % nm + "  ".join(
                "C%-4d rec %.5f keep %.4f" % (C, row["C%d" % C]["recall"], row["C%d" % C]["keep"])
                for C in CS), flush=True)

    print("\n=== POOLED over %d jet events: welded-recall / fraction-of-E1-kept ===" % len(res))
    hdr = "%-8s" % "rule" + "".join("%22s" % ("C=%d" % C) for C in CS)
    print(hdr)
    print("-" * len(hdr))
    for nm in ("first", "rand0", "rand1", "rand2", "fakeLo", "fakeHi", "oracle"):
        line = "%-8s" % nm
        for C in CS:
            kw = sum(res[i][nm]["C%d" % C]["kept_welded"] for i in res)
            ke = sum(res[i][nm]["C%d" % C]["kept_edges"] for i in res)
            tw = sum(res[i][nm]["nweldedE1"] for i in res)
            te = sum(res[i][nm]["nE1"] for i in res)
            line += "%14.5f/%.4f" % (kw / tw, ke / te)
        print(line)
    if "--json" in sys.argv:
        with open(sys.argv[sys.argv.index("--json") + 1], "w") as f:
            json.dump(res, f, indent=1)
        print("wrote", sys.argv[sys.argv.index("--json") + 1])


if __name__ == "__main__":
    main()
