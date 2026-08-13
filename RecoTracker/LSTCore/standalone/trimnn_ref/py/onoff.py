#!/usr/bin/env python3
"""TRIM-NN: where the terminal trim's win actually enters, from the two deployed chain dumps.

The trim ON and trim OFF arms are the same binary on the same events with one env flag apart, and
chain construction (K6a-K6e) is entirely UPSTREAM of the trim, so chain index c in one arm is the
same welded chain as chain index c in the other. That makes the two dumps paired row for row.

For every chain the trim EDITS this reports what the edit changed:
  * the gate verdict          (flags & 1, the -G 6 kill)
  * the branch the chain took (nL <= 4 / dca split)
  * the K6 score              which is the CLAIM ORDER KEY's base term
so the round can tell a PURITY effect (the chain is a better track) from a RANKING effect (the
chain is the same track but now wins a contest it used to lose).

Usage: onoff.py <ON.bin> <OFF.bin> [nevt]
"""
import os
import sys

import numpy as np

STAND = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(STAND, "nnloop_ref", "s2_work"))
from chainio import iter_events  # noqa: E402

# fix columns: preN nNodes nMDs nLayers branch drop flags score dcaXY zF zP zD mP mD mX
PRE, NN, NMD, NL, BR, DROP, FLAGS, SCORE, DCA = 0, 1, 2, 3, 4, 5, 6, 7, 8
MP, MD_, MX = 12, 13, 14


def main():
    on, off = sys.argv[1], sys.argv[2]
    nmax = int(sys.argv[3]) if len(sys.argv) > 3 else 10 ** 9
    agg = dict(nchain=0, nedit=0, inner=0, outer=0,
               kill_on=0, kill_off=0, edit_kill_off_alive_on=0, edit_alive_off_kill_on=0,
               branch_change=0, dscore=[], dmX=[], nl_drop=0)
    for k, (a, b) in enumerate(zip(iter_events(on), iter_events(off))):
        if k >= nmax:
            break
        assert a[0] == b[0], "event misalignment %d vs %d" % (a[0], b[0])
        A, B = a[3], b[3]
        assert len(A) == len(B), "chain count differs at evt %d: %d vs %d" % (a[0], len(A), len(B))
        # pairing check: the PRE-trim node count must agree row for row
        assert np.array_equal(A[:, PRE], B[:, PRE]), "pre-trim node runs differ at evt %d" % a[0]
        drop = A[:, DROP].astype(int)
        ed = drop != 0
        agg["nchain"] += len(A)
        agg["nedit"] += int(ed.sum())
        agg["inner"] += int((drop == 1).sum())
        agg["outer"] += int((drop == 2).sum())
        kon = A[:, FLAGS].astype(int) & 1
        koff = B[:, FLAGS].astype(int) & 1
        agg["kill_on"] += int(kon.sum())
        agg["kill_off"] += int(koff.sum())
        agg["edit_kill_off_alive_on"] += int((ed & (koff == 1) & (kon == 0)).sum())
        agg["edit_alive_off_kill_on"] += int((ed & (koff == 0) & (kon == 1)).sum())
        agg["branch_change"] += int((ed & (A[:, BR] != B[:, BR])).sum())
        agg["nl_drop"] += int((ed & (A[:, NL] < B[:, NL])).sum())
        # score is not comparable when one side is killed (gateKill = 1e9 is subtracted)
        cm = ed & (kon == 0) & (koff == 0)
        agg["dscore"].append(A[cm, SCORE] - B[cm, SCORE])
        agg["dmX"].append(A[cm, MX] - B[cm, MX])
    ds = np.concatenate(agg["dscore"]) if agg["dscore"] else np.zeros(0)
    dm = np.concatenate(agg["dmX"]) if agg["dmX"] else np.zeros(0)
    n = agg["nchain"]
    print("chains %d  edits %d (%.3f%%)  inner %d  outer %d"
          % (n, agg["nedit"], 100.0 * agg["nedit"] / n, agg["inner"], agg["outer"]))
    print("gate kills: ON %d (%.4f)  OFF %d (%.4f)" % (agg["kill_on"], agg["kill_on"] / n,
                                                       agg["kill_off"], agg["kill_off"] / n))
    print("of the edits: RESCUED at the gate (killed OFF, alive ON) %d | "
          "LOST at the gate (alive OFF, killed ON) %d | branch changed %d | nLayers fell %d"
          % (agg["edit_kill_off_alive_on"], agg["edit_alive_off_kill_on"],
             agg["branch_change"], agg["nl_drop"]))
    if len(ds):
        print("edits alive in BOTH arms (%d): d(score) mean %+.4f median %+.4f, worse in %.4f of them"
              % (len(ds), ds.mean(), np.median(ds), (ds < 0).mean()))
        print("                              d(mX)    mean %+.4f median %+.4f, HIGHER in %.4f of them"
              % (dm.mean(), np.median(dm), (dm > 0).mean()))


if __name__ == "__main__":
    main()
