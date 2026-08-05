#!/usr/bin/env python3
"""P2.1 CPU vs CUDA consistency report.

Exact CPU/GPU logit parity is phase P2.5's job (the CUDA library is built with
--use_fast_math, so the two backends do not even evaluate the head with the same rounding).
This only checks that the edge arithmetic is self-consistent per backend and reports the
observed logit delta, per event and, where the node sets coincide, per edge.

usage: p21_cpugpu.py <cpu.bin> <gpu.bin>
"""
import sys
import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from p21_compare import read_edges, key_of  # noqa: E402


def main(cpu_path, gpu_path):
    cpu, gpu = read_edges(cpu_path), read_edges(gpu_path)
    print(f"{'evt':>4} {'nT3 cpu/gpu':>18} {'E1 cpu/gpu':>20} {'E2 cpu/gpu':>20}  same-graph")
    for i in range(min(len(cpu), len(gpu))):
        c, g = cpu[i], gpu[i]
        same = (c["nT3"] == g["nT3"] and c["nE1ex"] == g["nE1ex"] and c["nE2ex"] == g["nE2ex"])
        print(f"{i:>4} {c['nT3']:>8}/{g['nT3']:<9} {c['nE1ex']:>9}/{g['nE1ex']:<10} "
              f"{c['nE2ex']:>9}/{g['nE2ex']:<10}  {'yes' if same else 'no'}")

    # Distribution comparison always works, even when the node sets differ.
    cl = np.concatenate([e["edges"]["logit"] for e in cpu]).astype(np.float64)
    gl = np.concatenate([e["edges"]["logit"] for e in gpu]).astype(np.float64)
    qs = [0, 1, 5, 25, 50, 75, 95, 99, 100]
    print(f"\nlogit distribution        cpu (n={cl.size})        gpu (n={gl.size})")
    print(f"  mean/std   {cl.mean():>12.6f} {cl.std():>10.6f}   {gl.mean():>12.6f} {gl.std():>10.6f}")
    for q in qs:
        print(f"  p{q:<3}       {np.percentile(cl, q):>12.6f}              {np.percentile(gl, q):>12.6f}")
    edges = np.linspace(-30, 30, 61)
    hc, _ = np.histogram(cl, bins=edges)
    hg, _ = np.histogram(gl, bins=edges)
    rel = np.abs(hc - hg) / np.maximum(hc, 1)
    print(f"\n60-bin histogram over [-30, 30]: max per-bin relative difference = {rel.max():.3e} "
          f"(at logit {edges[np.argmax(rel)]:.1f}); "
          f"fraction >= thetaEdge 0: cpu {np.count_nonzero(cl >= 0) / cl.size:.6f} "
          f"gpu {np.count_nonzero(gl >= 0) / gl.size:.6f}")

    # Per-edge where the graphs coincide.
    tot, above, mx, flips = 0, 0, 0.0, 0
    for i in range(min(len(cpu), len(gpu))):
        c, g = cpu[i], gpu[i]
        if not (c["nT3"] == g["nT3"] and c["nE1ex"] == g["nE1ex"] and c["nE2ex"] == g["nE2ex"]):
            continue
        ck, gk = key_of(c["edges"]), key_of(g["edges"])
        co, go = np.argsort(ck, kind="stable"), np.argsort(gk, kind="stable")
        if not (ck[co].shape == gk[go].shape and np.array_equal(ck[co], gk[go])):
            continue
        a = c["edges"]["logit"][co].astype(np.float64)
        b = g["edges"]["logit"][go].astype(np.float64)
        d = np.abs(a - b)
        tot += d.size
        above += int(np.count_nonzero(d > 1e-5))
        mx = max(mx, float(d.max()))
        flips += int(np.count_nonzero((a >= 0) != (b >= 0)))
    if tot:
        print(f"\nper-edge on identical graphs: n={tot} max|dLogit|={mx:.4e} "
              f"N(>1e-5)={above} sign-flips-at-0={flips}")
    else:
        print("\nper-edge: no event had an identical CPU/GPU triplet graph "
              "(pre-existing CPU/GPU T3 multiplicity difference, see the P2.0 landing note)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
