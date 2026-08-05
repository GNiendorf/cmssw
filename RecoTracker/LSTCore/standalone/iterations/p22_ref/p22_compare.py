#!/usr/bin/env python3
"""P2.2 parity gate: production Chains sidecar vs the frozen prototype reference.

usage: p22_compare.py <ref.bin> <prod.bin>

Both sidecars carry, per event, one variable-length record per welded chain:
  u32 preNodes, postNodes, nMD, nLayers; i32 branch; u32 trimAction, flags;
  f32 score, dcaXY, zFake, zPrompt, zDisp, mP, mD, mX;  25 x f32 features;
  preNodes x u32 nodeIdx;  (preNodes-1) x u32 edgeType;  nMD x (u32 anchorHit, u32 otherHit)

Chains are matched on the PRE-trim member-node tuple, which is the welded chain's identity: node
indices are the dense chain-node numbering, i.e. the same module-ordered sequence the ntuple's t3
rows use, so no remapping is needed. MiniDoublets are matched on their (anchor, other) ph2 hit-row
pair because the two implementations number MDs differently (ntuple order vs SoA order).
"""
import sys
import struct
import numpy as np

MAGIC = 0x50323243
HDR = struct.Struct("<IIIII")          # magic ievt nT3 nEdgeRows nChains
FIX = struct.Struct("<IIIIiII8f25f")   # the fixed part of a chain record
NFEAT = 25
FEATNAMES = ["nNodes", "nLayers", "sumEdgeLogit", "minEdgeLogit", "meanEdgeLogit",
             "fullFitChi2PerHit", "rzLineChi2PerHit", "fitKappa", "dKappaFitVsMedianT3", "ptEst",
             "innermostLayer", "layerSpan", "nPS", "nBarrel", "maxJunctionDegProduct",
             "chargeConsistency", "maxXyResid", "maxRzResid", "stdEdgeLogit", "maxBridgeChi2",
             "minT3FakeScore", "maxT3FakeScore", "meanT3PromptScore", "minT3DisplacedScore",
             "meanT3DisplacedScore"]


def read(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nT3, nEdge, nC = HDR.unpack_from(buf, off)
        assert magic == MAGIC, f"bad magic at {off}"
        off += HDR.size
        chains = []
        for _ in range(nC):
            v = FIX.unpack_from(buf, off)
            off += FIX.size
            preN, postN, nMD, nLay, branch, trim, flags = v[0:7]
            scal = v[7:15]
            feats = np.array(v[15:15 + NFEAT], dtype=np.float32)
            nodes = np.frombuffer(buf, dtype="<u4", count=preN, offset=off)
            off += 4 * preN
            etypes = np.frombuffer(buf, dtype="<u4", count=preN - 1, offset=off)
            off += 4 * (preN - 1)
            mdhits = np.frombuffer(buf, dtype="<u4", count=2 * nMD, offset=off)
            off += 8 * nMD
            chains.append(dict(preN=preN, postN=postN, nMD=nMD, nLay=nLay, branch=branch,
                               trim=trim, flags=flags, scal=scal, feats=feats,
                               nodes=tuple(int(x) for x in nodes),
                               etypes=tuple(int(x) for x in etypes),
                               mds=tuple((int(mdhits[2 * i]), int(mdhits[2 * i + 1])) for i in range(nMD))))
        events.append(dict(ievt=ievt, nT3=nT3, nEdge=nEdge, chains=chains))
    return events


SCALNAMES = ["score", "dcaXY", "zFake", "zPrompt", "zDisp", "mP", "mD", "mX"]


def main(ref_path, prod_path):
    ref = read(ref_path)
    prod = read(prod_path)
    n = min(len(ref), len(prod))
    print(f"ref events: {len(ref)}   prod events: {len(prod)}   comparing {n}")

    print()
    print("(a) CHAIN MULTISET (key = sorted member-T3 list) and (b) WELDED EDGE SET")
    print(f"{'evt':>3} {'nT3 r/p':>15} {'chains r/p':>14} {'chainMultiset':>14} "
          f"{'weldEdges r/p':>16} {'edgeSet':>9}  nLayers hist (ref)")
    ok_a = ok_b = True
    matched = []          # (refchain, prodchain) pairs, keyed on the pre-trim node tuple
    for i in range(n):
        r, p = ref[i], prod[i]
        rkeys = sorted(tuple(sorted(c["nodes"])) for c in r["chains"])
        pkeys = sorted(tuple(sorted(c["nodes"])) for c in p["chains"])
        a_ok = (rkeys == pkeys)
        ok_a = ok_a and a_ok

        def edgeset(ev):
            s = set()
            for c in ev["chains"]:
                for k in range(c["preN"] - 1):
                    s.add((c["nodes"][k], c["nodes"][k + 1], c["etypes"][k]))
            return s
        re_, pe = edgeset(r), edgeset(p)
        b_ok = (re_ == pe)
        ok_b = ok_b and b_ok

        hist = {}
        for c in r["chains"]:
            hist[c["nLay"]] = hist.get(c["nLay"], 0) + 1
        histstr = " ".join(f"{k}:{hist[k]}" for k in sorted(hist))
        print(f"{i:>3} {r['nT3']:>7}/{p['nT3']:<7} {len(r['chains']):>6}/{len(p['chains']):<7} "
              f"{'MATCH' if a_ok else 'DIFFER':>14} {len(re_):>7}/{len(pe):<8} "
              f"{'MATCH' if b_ok else 'DIFFER':>9}  {histstr}")

        rmap = {}
        for c in r["chains"]:
            rmap.setdefault(c["nodes"], []).append(c)
        for c in p["chains"]:
            lst = rmap.get(c["nodes"])
            if lst:
                matched.append((lst.pop(0), c))

    print()
    print(f"(a) chain multiset      : {'PASS' if ok_a else 'FAIL'}")
    print(f"(b) welded edge set     : {'PASS' if ok_b else 'FAIL'}")
    print(f"    chains paired by node tuple: {len(matched)}")

    # (c) per-chain float parity
    print()
    print("(c) PER-CHAIN FLOAT PARITY (bit-identical rows out of paired chains)")
    rs = np.array([m[0]["scal"] for m in matched], dtype=np.float32)
    ps = np.array([m[1]["scal"] for m in matched], dtype=np.float32)
    rf = np.array([m[0]["feats"] for m in matched], dtype=np.float32)
    pf = np.array([m[1]["feats"] for m in matched], dtype=np.float32)
    allbit = True
    for k, nm in enumerate(SCALNAMES):
        nb = int(np.count_nonzero(rs[:, k].view(np.uint32) != ps[:, k].view(np.uint32)))
        d = np.abs(rs[:, k].astype(np.float64) - ps[:, k].astype(np.float64))
        allbit = allbit and nb == 0
        print(f"  {nm:<22} bitdiff={nb:<8} max|d|={d.max() if d.size else 0.0:.6e}")
    for k in range(NFEAT):
        nb = int(np.count_nonzero(rf[:, k].view(np.uint32) != pf[:, k].view(np.uint32)))
        d = np.abs(rf[:, k].astype(np.float64) - pf[:, k].astype(np.float64))
        allbit = allbit and nb == 0
        print(f"  f[{k:2d}] {FEATNAMES[k]:<17} bitdiff={nb:<8} max|d|={d.max() if d.size else 0.0:.6e}")
    print(f"(c) float parity        : {'BIT-ZERO' if allbit else 'NOT BIT-ZERO'}")

    # (d) kill decisions + branch code
    print()
    rk = np.array([m[0]["flags"] & 1 for m in matched])
    pk = np.array([m[1]["flags"] & 1 for m in matched])
    rb = np.array([m[0]["branch"] for m in matched])
    pb = np.array([m[1]["branch"] for m in matched])
    rex = np.array([(m[0]["flags"] >> 1) & 1 for m in matched])
    pex = np.array([(m[1]["flags"] >> 1) & 1 for m in matched])
    rz = np.array([(m[0]["flags"] >> 2) & 1 for m in matched])
    pz = np.array([(m[1]["flags"] >> 2) & 1 for m in matched])
    rc = np.array([(m[0]["flags"] >> 3) & 1 for m in matched])
    pc = np.array([(m[1]["flags"] >> 3) & 1 for m in matched])
    nkf = int(np.count_nonzero(rk != pk))
    nbf = int(np.count_nonzero(rb != pb))
    print(f"(d) KILL DECISIONS: ref killed {int(rk.sum())} / prod killed {int(pk.sum())} "
          f"of {len(matched)}   flips={nkf}")
    print(f"    branch code flips={nbf}   exempt flips={int(np.count_nonzero(rex != pex))}   "
          f"etaBand flips={int(np.count_nonzero(rz != pz))}   C25-cell flips={int(np.count_nonzero(rc != pc))}")
    if nkf:
        idx = np.nonzero(rk != pk)[0][:20]
        print("    first flipped chains (ref mP/mD/mX, branch, nLayers, dca):")
        for j in idx:
            r0, p0 = matched[j]
            print(f"      nodes={r0['nodes']}  refKill={rk[j]} prodKill={pk[j]} "
                  f"br={r0['branch']} nLay={r0['nLay']} dca={r0['scal'][1]:.6f} "
                  f"mP={r0['scal'][5]:.6f} mD={r0['scal'][6]:.6f} mX={r0['scal'][7]:.6f} "
                  f"| prod mP={p0['scal'][5]:.6f} mD={p0['scal'][6]:.6f} mX={p0['scal'][7]:.6f}")
    print(f"(d) kill decisions      : {'PASS (0 flips)' if nkf == 0 and nbf == 0 else 'FAIL'}")

    # (e) trim
    print()
    ntrimflip = sum(1 for r0, p0 in matched if r0["trim"] != p0["trim"])
    nmdiff = sum(1 for r0, p0 in matched if r0["mds"] != p0["mds"])
    nmsetdiff = sum(1 for r0, p0 in matched if set(r0["mds"]) != set(p0["mds"]))
    trimhist = {}
    for r0, _ in matched:
        trimhist[r0["trim"]] = trimhist.get(r0["trim"], 0) + 1
    print(f"(e) TRIM: action histogram (ref) {trimhist}   action flips={ntrimflip}")
    print(f"    trimmed-MD list differs (ordered)={nmdiff}   as a SET={nmsetdiff}")
    print(f"(e) trim                : {'PASS' if ntrimflip == 0 and nmdiff == 0 else 'FAIL'}")

    ok = ok_a and ok_b and nkf == 0 and nbf == 0 and ntrimflip == 0 and nmdiff == 0
    print()
    print(f"OVERALL (a,b,d,e): {'PASS' if ok else 'FAIL'}    (c) float: {'BIT-ZERO' if allbit else 'see table'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
