#!/usr/bin/env python3
"""P2.3 gate (a): TC-level multiset parity, ported pipeline vs the frozen prototype.

usage: p23_compare_tc.py <proto.root> <prod.bin>

The reference leg is the prototype's impersonated LST ntuple written with PROTO_DUMP_TCHITS=1,
which carries tc_type plus tc_hitOT (the outer-tracker hit rows of every emitted TC, in the
tracking-ntuple ph2 numbering). The ported leg is the LST_CHAIN_TC_DUMP sidecar, which writes the
same content straight out of TrackCandidatesBase.

The comparison key is (type, sorted hit list) as a MULTISET per event: row order is not part of
the contract on either side (the prototype emits carried rows then chain rows; the port keeps the
carried rows in place and appends), and a duplicate hit row inside one TC is kept because K10 emits
per MD without deduplication, so the multiset must be compared, not the set.
"""
import struct
import sys
from collections import Counter

import uproot

MAGIC = 0x50323354


def read_prod(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nTC = struct.unpack_from("<III", buf, off)
        assert magic == MAGIC, f"bad magic 0x{magic:08x} at {off}"
        off += 12
        rows = []
        for _ in range(nTC):
            ty, nOT = struct.unpack_from("<II", buf, off)
            off += 8
            hits = struct.unpack_from("<%dI" % nOT, buf, off) if nOT else ()
            off += 4 * nOT
            rows.append((ty, tuple(sorted(hits))))
        events.append((ievt, rows))
    return events


def read_proto(path):
    with uproot.open(path)["tree"] as t:
        types = t["tc_type"].array(library="np")
        hits = t["tc_hitOT"].array(library="np")
    out = []
    for i in range(len(types)):
        rows = [(int(ty), tuple(sorted(int(h) for h in hl))) for ty, hl in zip(types[i], hits[i])]
        out.append((i, rows))
    return out


def main():
    proto = read_proto(sys.argv[1])
    prod = read_prod(sys.argv[2])
    n = min(len(proto), len(prod))
    if len(proto) != len(prod):
        print(f"WARNING: event count differs proto={len(proto)} prod={len(prod)}; comparing {n}")

    print(f"{'evt':>4} {'proto':>7} {'prod':>7} {'common':>7} {'onlyP':>6} {'onlyR':>6}  per-type only(proto/prod)")
    tot_only_p = tot_only_r = tot_common = 0
    for i in range(n):
        cp = Counter(proto[i][1])
        cr = Counter(prod[i][1])
        common = cp & cr
        only_p = cp - cr
        only_r = cr - cp
        nc = sum(common.values())
        np_, nr = sum(only_p.values()), sum(only_r.values())
        tot_common += nc
        tot_only_p += np_
        tot_only_r += nr
        bytype = {}
        for (ty, _), c in only_p.items():
            bytype.setdefault(ty, [0, 0])[0] += c
        for (ty, _), c in only_r.items():
            bytype.setdefault(ty, [0, 0])[1] += c
        desc = " ".join(f"t{ty}:{a}/{b}" for ty, (a, b) in sorted(bytype.items()))
        print(f"{i:>4} {sum(cp.values()):>7} {sum(cr.values()):>7} {nc:>7} {np_:>6} {nr:>6}  {desc}")

    print()
    print(f"TOTAL common={tot_common} onlyProto={tot_only_p} onlyProd={tot_only_r}")
    if tot_only_p == 0 and tot_only_r == 0:
        print("GATE (a): PASS - exact TC multiset match on every event")
        return 0
    print("GATE (a): MISMATCH")
    return 1


if __name__ == "__main__":
    sys.exit(main())
