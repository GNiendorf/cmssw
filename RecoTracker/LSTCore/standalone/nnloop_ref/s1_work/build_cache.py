#!/usr/bin/env python3
"""S1: assemble the on-policy edge training cache from the round-1 dumps.

X = the EXACT 40 inputs the deployed head consumed (node[inner].13 + node[outer].13 +
edge.14, raw as K5 built them), with the shipped CONDITIONING spec applied (clip /
log10_1p -- fixed, not fitted).  Standardization is NOT applied here: it is fitted on
train rows only and applied on the fly, so the same cache serves every arm.

Output in <out>/:
  X.f32        memmap float32 [N,40]  conditioned features, dump order
  meta.npz     label u8, type u8, ptbin u8, etabin u8, evt i16, logit f32 (shipped head),
               simVxy f32, simPt f32, evt_off i64[1001]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpio import iter_feat, iter_edges  # noqa: E402

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round1"
NAMES = ([f"ni_{n}" for n in ["kappaSigned", "log10R", "tanLambda", "chordEta", "dphi01", "dz01",
                              "dz12", "drt01", "drt12", "innermostLayer", "nBarrel", "nPS",
                              "fakeScoreT3"]]
         + [f"no_{n}" for n in ["kappaSigned", "log10R", "tanLambda", "chordEta", "dphi01", "dz01",
                                "dz12", "drt01", "drt12", "innermostLayer", "nBarrel", "nPS",
                                "fakeScoreT3"]]
         + [f"ef_{n}" for n in ["etype", "dKappa", "dKappaRel", "chargeAgree", "dTanLambda",
                                "kinkPhi", "kinkTheta", "centerDist", "centerDistRel",
                                "sharedLayer", "sharedIsPS", "sharedIsBarrel", "degIn", "degOut"]])
# shipped conditioning spec (prototype/edge_norm_v3.json "conditioning"), in order
COND = [("ni_kappaSigned", "clip", -1.0, 1.0),
        ("no_kappaSigned", "clip", -1.0, 1.0),
        ("ni_log10R", "clip", 0.0, 9.0),
        ("no_log10R", "clip", 0.0, 9.0),
        ("ef_dKappa", "clip", -2.0, 2.0),
        ("ef_centerDist", "log10_1p", 0, 0)]
NTOT = 111913544
NEV = 1000


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    X = np.lib.format.open_memmap(os.path.join(out, "X.npy"), mode="w+",
                                  dtype=np.float32, shape=(NTOT, 40))
    lab = np.empty(NTOT, dtype=np.uint8)
    ety = np.empty(NTOT, dtype=np.uint8)
    ptb = np.empty(NTOT, dtype=np.uint8)
    etb = np.empty(NTOT, dtype=np.uint8)
    evt = np.empty(NTOT, dtype=np.int16)
    logit = np.empty(NTOT, dtype=np.float32)
    svxy = np.empty(NTOT, dtype=np.float32)
    spt = np.empty(NTOT, dtype=np.float32)
    off = np.zeros(NEV + 1, dtype=np.int64)

    cidx = [(NAMES.index(c[0]), c[1], c[2], c[3]) for c in COND]
    pos = 0
    ed = iter_edges(D + "/edges.bin")
    for iev, (ievt, nf, ei, eo, et, ef) in enumerate(iter_feat(D + "/edgefeat.bin")):
        e = next(ed)
        assert e[0] == ievt == iev
        assert np.array_equal(e[4], ei) and np.array_equal(e[6], et)
        n = len(ei)
        z = np.load("lab/ev%04d.npz" % iev)
        assert len(z["label"]) == n

        blk = np.empty((n, 40), dtype=np.float32)
        blk[:, 0:13] = nf[ei.astype(np.int64)]
        blk[:, 13:26] = nf[eo.astype(np.int64)]
        blk[:, 26:40] = ef
        np.nan_to_num(blk, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
        for j, op, lo, hi in cidx:
            if op == "clip":
                np.clip(blk[:, j], lo, hi, out=blk[:, j])
            else:
                blk[:, j] = np.log10(1.0 + blk[:, j])
        X[pos:pos + n] = blk
        lab[pos:pos + n] = z["label"]
        ety[pos:pos + n] = et.astype(np.uint8)
        ptb[pos:pos + n] = z["ptbin"]
        etb[pos:pos + n] = z["etabin"]
        evt[pos:pos + n] = iev
        logit[pos:pos + n] = e[7]
        svxy[pos:pos + n] = z["simVxy"]
        spt[pos:pos + n] = z["simPt"]
        pos += n
        off[iev + 1] = pos
        if iev % 100 == 0:
            print("evt %4d  rows %d" % (iev, pos), flush=True)
    assert pos == NTOT, (pos, NTOT)
    X.flush()
    np.savez(os.path.join(out, "meta.npz"), label=lab, type=ety, ptbin=ptb, etabin=etb,
             evt=evt, logit=logit, simVxy=svxy, simPt=spt, evt_off=off)
    print("done: %d rows" % pos)


if __name__ == "__main__":
    main()
