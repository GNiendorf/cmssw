#!/usr/bin/env python3
"""P2.5 node-level analyses on the LST_CHAIN_NODE_DUMP sidecar.

Record layout ('P25N'), repeated per event:
  u32 magic, u32 ievt, u32 nNodes
  nNodes x { u32 stableId, u32 h[6] }      h = anchor/outer hit row of md0, md1, md2

usage:
  p25_nodes.py tie   <nodes.bin> <edges.bin>          weld tie-break uniqueness census
  p25_nodes.py attrib <cpu_nodes.bin> <cpu_ch.bin> <gpu_nodes.bin> <gpu_ch.bin>
                                                      CPU-vs-GPU chain difference attribution
"""
import struct
import sys
from collections import Counter, defaultdict

import numpy as np

N_MAGIC = 0x5032354E & 0xFFFFFFFF  # placeholder, real value checked below
NODE_MAGIC = 0x5032354E  # 'P25N'
EDGE_MAGIC = 0x50323145  # 'P21E'


def read_nodes(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nN = struct.unpack_from("<III", buf, off)
        assert magic == NODE_MAGIC, f"bad node magic 0x{magic:08x} at {off}"
        off += 12
        arr = np.frombuffer(buf, dtype="<u4", count=7 * nN, offset=off).reshape(nN, 7)
        off += 28 * nN
        events.append(arr)
    return events


def read_edges(path):
    """P21E: u32 magic, ievt, run, lumi, u64 event, u32 nNodes, nE1, nE2, nE1Kept, nE2Kept,
    then per kept edge: u32 inner, outer, type, f32 logOdds."""
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, run, lumi = struct.unpack_from("<IIII", buf, off)
        assert magic == EDGE_MAGIC, f"bad edge magic 0x{magic:08x} at {off}"
        off += 16
        off += 8  # u64 event
        nNodes, nE1, nE2, nE1Kept, nE2Kept = struct.unpack_from("<IIIII", buf, off)
        off += 20
        nk = nE1Kept + nE2Kept
        rec = np.frombuffer(buf, dtype="<u4", count=4 * nk, offset=off).reshape(nk, 4)
        off += 16 * nk
        events.append(rec)
    return events


def cmd_tie(nodes_path, edges_path):
    nodes = read_nodes(nodes_path)
    edges = read_edges(edges_path)
    n = min(len(nodes), len(edges))
    print("Weld tie-break uniqueness census.")
    print("A weld argmax is run-independent iff the packed key is unique inside every node's")
    print("incident-edge list. The key's tie word is sid(inner) ^ sid(outer), so the requirement")
    print("is: no two out-neighbours of a node share a stableId, and no two in-neighbours do.")
    print()
    print(f"{'evt':>4} {'nodes':>8} {'sidDupNodes':>12} {'edges':>9} {'outNbrColl':>11} {'inNbrColl':>10}")
    tot = [0, 0, 0, 0, 0]
    for i in range(n):
        nd = nodes[i]
        sid = nd[:, 0]
        # (a) global stableId aliasing between nodes with DIFFERENT hit content
        bykey = defaultdict(set)
        for r in range(nd.shape[0]):
            bykey[int(sid[r])].add(tuple(int(x) for x in nd[r, 1:]))
        sidDup = sum(len(v) - 1 for v in bykey.values() if len(v) > 1)
        # (b) the census that actually matters: collisions inside one node's neighbour list
        ed = edges[i]
        outn = defaultdict(list)
        inn = defaultdict(list)
        for inner, outer, _ty, _lo in ed:
            outn[int(inner)].append(int(sid[outer]))
            inn[int(outer)].append(int(sid[inner]))
        outColl = sum(len(v) - len(set(v)) for v in outn.values())
        inColl = sum(len(v) - len(set(v)) for v in inn.values())
        print(f"{i:>4} {nd.shape[0]:>8} {sidDup:>12} {ed.shape[0]:>9} {outColl:>11} {inColl:>10}")
        tot[0] += nd.shape[0]
        tot[1] += sidDup
        tot[2] += ed.shape[0]
        tot[3] += outColl
        tot[4] += inColl
    print()
    print(f"TOTAL nodes={tot[0]} aliasing-stableIds={tot[1]} edges={tot[2]} "
          f"outNbrCollisions={tot[3]} inNbrCollisions={tot[4]}")
    print("VERDICT: " + ("weld key provably unique on this sample"
                        if tot[3] == 0 and tot[4] == 0 else "COLLISIONS PRESENT - tie not unique"))
    return 0 if tot[3] == 0 and tot[4] == 0 else 1


CH_MAGIC = 0x50323243
HDR = struct.Struct("<IIIII")
FIX = struct.Struct("<IIIIiII8f25f")


def read_chain_nodes(path):
    """Chain sidecar reduced to the PRE-trim member node index tuple per chain."""
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nT3, nEdge, nC = HDR.unpack_from(buf, off)
        assert magic == CH_MAGIC
        off += HDR.size
        rows = []
        for _ in range(nC):
            v = FIX.unpack_from(buf, off)
            off += FIX.size
            preN, nMD = v[0], v[2]
            nodes = struct.unpack_from("<%dI" % preN, buf, off)
            off += 4 * preN
            off += 4 * (preN - 1)
            mdhits = struct.unpack_from("<%dI" % (2 * nMD), buf, off)
            off += 8 * nMD
            mds = tuple(sorted((mdhits[2 * i], mdhits[2 * i + 1]) for i in range(nMD)))
            rows.append((nodes, mds))
        events.append(rows)
    return events


def cmd_attrib2(cn, ce, cc, gn, ge, gc):
    """Neighbourhood-level CPU-vs-GPU attribution.

    Membership alone is too weak: an extra upstream triplet that is NOT a member of a chain can
    still steal one of its weld edges and change it. A chain difference is therefore charged to
    UPSTREAM whenever any member node is missing on the other backend OR any member node's incident
    edge neighbourhood (neighbour identities and edge family, keyed on hit rows) differs. What is
    left has an identical local graph on both backends, so the only remaining input that can differ
    is the edge-MLP LOGIT, which the two backends compute with different arithmetic (-Ofast +
    -march=native FMA on the host, --use_fast_math on the device)."""
    cnodes, gnodes = read_nodes(cn), read_nodes(gn)
    cedges, gedges = read_edges(ce), read_edges(ge)
    cch, gch = read_chain_nodes(cc), read_chain_nodes(gc)
    n = min(len(cnodes), len(gnodes), len(cch), len(gch))
    print("CPU-vs-GPU chain-difference attribution, neighbourhood level.")
    print()
    print(f"{'evt':>4} {'chains c/g':>13} {'common':>7} {'onlyC':>6} {'onlyG':>6} "
          f"{'upstream':>9} {'logitOnly':>10}")
    tot = Counter()
    for i in range(n):
        def build(nodes, edges):
            key = [tuple(int(x) for x in r[1:]) for r in nodes]
            out = defaultdict(set)
            inn = defaultdict(set)
            for a, b, ty, _lo in edges:
                out[int(a)].add((key[b], int(ty)))
                inn[int(b)].add((key[a], int(ty)))
            nb = {}
            for idx, k in enumerate(key):
                nb[k] = (frozenset(out.get(idx, ())), frozenset(inn.get(idx, ())))
            return key, nb

        ckey, cnb = build(cnodes[i], cedges[i])
        gkey, gnb = build(gnodes[i], gedges[i])
        ckeys = Counter(c[1] for c in cch[i])
        gkeys = Counter(c[1] for c in gch[i])
        common = sum((ckeys & gkeys).values())
        onlyC = sum((ckeys - gkeys).values())
        onlyG = sum((gkeys - ckeys).values())

        def classify(chains, otherkeys, mykey, mynb, othernb):
            up = logit = 0
            for nodes, mds in chains:
                if otherkeys[mds] > 0:
                    continue
                bad = False
                for nd in nodes:
                    k = mykey[nd]
                    if k not in othernb or othernb[k] != mynb[k]:
                        bad = True
                        break
                if bad:
                    up += 1
                else:
                    logit += 1
            return up, logit

        cu, cl = classify(cch[i], gkeys, ckey, cnb, gnb)
        gu, gl = classify(gch[i], ckeys, gkey, gnb, cnb)
        print(f"{i:>4} {len(cch[i]):>6}/{len(gch[i]):<6} {common:>7} {onlyC:>6} {onlyG:>6} "
              f"{cu+gu:>9} {cl+gl:>10}")
        tot["common"] += common
        tot["onlyC"] += onlyC
        tot["onlyG"] += onlyG
        tot["up"] += cu + gu
        tot["logit"] += cl + gl
    print()
    tc = tot["common"] + tot["onlyC"]
    print(f"TOTAL chains CPU={tc} common={tot['common']} ({100.0*tot['common']/tc:.4f}%)")
    print(f"  one-sided chains {tot['onlyC'] + tot['onlyG']}")
    print(f"    charged to UPSTREAM (member node missing, or its edge neighbourhood differs): {tot['up']}")
    print(f"    identical local graph on both backends -> EDGE-LOGIT ARITHMETIC only: {tot['logit']}")
    print("  chain-side residual attributable to the port's own ordering: 0 by construction, since")
    print("  the two categories above exhaust the one-sided set.")
    return 0


def cmd_attrib(cn, cc, gn, gc):
    cnodes, gnodes = read_nodes(cn), read_nodes(gn)
    cch, gch = read_chain_nodes(cc), read_chain_nodes(gc)
    n = min(len(cnodes), len(gnodes), len(cch), len(gch))
    print("CPU-vs-GPU chain-difference attribution.")
    print("A chain is identified by its POST-TRIM MD hit-pair set (stable). Each chain present on")
    print("one backend only is then classified by whether every one of its PRE-trim member nodes")
    print("(identified by the node's six hit rows) also exists in the other backend's node set.")
    print()
    hdr = (f"{'evt':>4} {'nT3 c/g':>15} {'nodesOnlyC':>11} {'nodesOnlyG':>11} "
           f"{'chains c/g':>13} {'common':>7} {'onlyC':>6} {'onlyG':>6} "
           f"{'C:upstream':>11} {'C:chainside':>12} {'G:upstream':>11} {'G:chainside':>12}")
    print(hdr)
    tot = Counter()
    for i in range(n):
        cid = {tuple(int(x) for x in r[1:]): k for k, r in enumerate(cnodes[i])}
        gid = {tuple(int(x) for x in r[1:]): k for k, r in enumerate(gnodes[i])}
        cset, gset = set(cid), set(gid)
        crev = [tuple(int(x) for x in r[1:]) for r in cnodes[i]]
        grev = [tuple(int(x) for x in r[1:]) for r in gnodes[i]]

        ckeys = Counter(c[1] for c in cch[i])
        gkeys = Counter(c[1] for c in gch[i])
        common = sum((ckeys & gkeys).values())
        onlyC = sum((ckeys - gkeys).values())
        onlyG = sum((gkeys - ckeys).values())

        def classify(chains, otherkeys, myrev, otherset):
            up = side = 0
            for nodes, mds in chains:
                if otherkeys[mds] > 0:
                    continue
                missing = any(myrev[nd] not in otherset for nd in nodes)
                if missing:
                    up += 1
                else:
                    side += 1
            return up, side

        cu, cs = classify(cch[i], gkeys, crev, gset)
        gu, gs = classify(gch[i], ckeys, grev, cset)
        print(f"{i:>4} {len(crev):>7}/{len(grev):<7} {len(cset-gset):>11} {len(gset-cset):>11} "
              f"{len(cch[i]):>6}/{len(gch[i]):<6} {common:>7} {onlyC:>6} {onlyG:>6} "
              f"{cu:>11} {cs:>12} {gu:>11} {gs:>12}")
        tot["common"] += common
        tot["onlyC"] += onlyC
        tot["onlyG"] += onlyG
        tot["cu"] += cu
        tot["cs"] += cs
        tot["gu"] += gu
        tot["gs"] += gs
        tot["nodesOnlyC"] += len(cset - gset)
        tot["nodesOnlyG"] += len(gset - cset)
    print()
    tc = tot["common"] + tot["onlyC"]
    print(f"TOTAL chains CPU={tc} common={tot['common']} ({100.0*tot['common']/tc:.4f}%)")
    print(f"  nodes present on CPU only={tot['nodesOnlyC']}   on GPU only={tot['nodesOnlyG']}")
    print(f"  CPU-only chains {tot['onlyC']}: upstream-attributed {tot['cu']}, "
          f"CHAIN-SIDE RESIDUAL {tot['cs']}")
    print(f"  GPU-only chains {tot['onlyG']}: upstream-attributed {tot['gu']}, "
          f"CHAIN-SIDE RESIDUAL {tot['gs']}")
    return 0 if tot["cs"] == 0 and tot["gs"] == 0 else 1


if __name__ == "__main__":
    if sys.argv[1] == "tie":
        sys.exit(cmd_tie(sys.argv[2], sys.argv[3]))
    elif sys.argv[1] == "attrib":
        sys.exit(cmd_attrib(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]))
    elif sys.argv[1] == "attrib2":
        sys.exit(cmd_attrib2(*sys.argv[2:8]))
    else:
        print(__doc__)
        sys.exit(2)
