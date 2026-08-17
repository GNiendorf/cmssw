#!/usr/bin/env python3
"""[ARM-RETRAIN] Build tdca.npy for a labelled attach corpus: the TARGET chain's fitted dcaXY per pair row,
aligned to the corpus row order (= pairs.bin dump order, the order label_attach.py wrote).
Stage-A rows (stage 0/2) get chains.bin dcaXY[target]; stage-B rows get NaN (bare-T3 targets
are never displaced-judged, ChainAttach.h dispTarget comment). Usage:
  python3 mk_tdca.py <chains.bin> <pairs.bin> <outdir-with-y.npy>
"""
import os, struct, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pair_dump_io as pio

CHDR = struct.Struct("<IIIII")
CFIX = struct.Struct("<IIIIiiI" + "f" * (8 + 25))  # preN n m nLayers branch drop flags | score dcaXY zF zP zD mP mD mX | 25 feat


def iter_chain_dca(path):
    """Yield (ievt, dcaXY-array) per event, skipping variable payload."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        while f.tell() < size:
            h = f.read(20)
            if len(h) < 20:
                raise ValueError("truncated chain header")
            magic, ievt, nN, nE, nC = CHDR.unpack(h)
            assert magic == 0x50323243
            dca = np.empty(nC, np.float32)
            for c in range(nC):
                v = CFIX.unpack(f.read(CFIX.size))
                pre_n, m = v[0], v[2]
                dca[c] = v[8]
                f.seek(4 * pre_n + 4 * max(pre_n - 1, 0) + 8 * m, 1)
            yield ievt, dca


def main():
    chains_bin, pairs_bin, outdir = sys.argv[1:4]
    nrow = np.load(os.path.join(outdir, "y.npy"), mmap_mode="r").shape[0]
    out = np.lib.format.open_memmap(os.path.join(outdir, "tdca.npy"), mode="w+", dtype=np.float32, shape=(nrow,))
    itc = iter_chain_dca(chains_bin)
    pos = 0
    nA = 0
    for hdr, rows in pio.read_events(pairs_bin):
        cevt, dca = next(itc)
        assert cevt == hdr["ievt"], (cevt, hdr["ievt"])
        st = rows["stage"]
        tg = rows["target"].astype(np.int64)
        v = np.full(len(rows), np.nan, np.float32)
        isch = (st == 0) | (st == 2) | (st == 3)  # stage 3 = claim-rescue chain target
        if isch.any():
            assert tg[isch].max() < len(dca), (hdr["ievt"], tg[isch].max(), len(dca))
            v[isch] = dca[tg[isch]]
            nA += int(isch.sum())
        out[pos:pos + len(rows)] = v
        pos += len(rows)
    assert pos == nrow, (pos, nrow)
    out.flush()
    disp = ~(out[:] < 0.5)  # NaN-safe: matches !(dcaXY < dcaSplit)
    stA = ~np.isnan(out[:])
    print("rows %d stageA %d dispTarget-among-stageA %d (%.4f)" % (nrow, nA, int((disp & stA).sum()), (disp & stA).sum() / max(nA, 1)))
    print("TDCA-DONE", outdir)


if __name__ == "__main__":
    main()
