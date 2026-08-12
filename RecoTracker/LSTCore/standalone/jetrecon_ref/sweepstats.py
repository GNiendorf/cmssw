#!/usr/bin/env python3
"""Aggregate the per-event isolated jet sweep (jetrecon_ref/pe1000/) into
distributions, the E1 scaling law, and the crash census."""
import glob
import os
import re
import sys
import numpy as np

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pe1000")
STAGES = ["Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Reset", "Total"]


def parse(i):
    lg = os.path.join(DIR, "evt%d.log" % i)
    er = os.path.join(DIR, "evt%d.err" % i)
    if not os.path.exists(lg):
        return None
    txt = open(lg, errors="replace").read()
    d = {"evt": i, "crash": 0}
    m = re.search(r"\[CHAIN\] nodes\(nT3\)=(\d+) E1=(\d+) E2=(\d+) E=(\d+)", txt)
    if m:
        d["nT3"], d["E1"], d["E2"], d["E"] = (int(m.group(k)) for k in (1, 2, 3, 4))
    m = re.search(r"\[CHAIN\] MD/E1: keys=(\d+) used=(\d+).*?maxDegIn=(\d+) maxDegOut=(\d+)", txt)
    if m:
        d["mdKeys"], d["mdUsed"], d["mxDgIn"], d["mxDgOut"] = (int(m.group(k)) for k in (1, 2, 3, 4))
    m = re.search(r"\[CHAIN\] LS/E2: keys=(\d+) used=(\d+).*?maxDegIn=(\d+) maxDegOut=(\d+)", txt)
    if m:
        d["lsKeys"], d["lsUsed"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"\[MEM\] MiniDoublets: (\d+) allocated", txt)
    if m:
        d["nMDalloc"] = int(m.group(1))
    m = re.search(r"\[MEM\] Segments: (\d+) allocated", txt)
    if m:
        d["nLSalloc"] = int(m.group(1))
    m = re.search(r"\[MEM\] Triplets: (\d+) allocated", txt)
    if m:
        d["nT3alloc"] = int(m.group(1))
    m = re.search(r"\[MEM\] Hits: (\d+) allocated", txt)
    if m:
        d["nHits"] = int(m.group(1))
    m = re.search(r"\[MEM\] ChainEdges: (\d+) allocated \(([\d.]+) MB\)", txt)
    if m:
        d["edgeRows"], d["edgeMB"] = int(m.group(1)), float(m.group(2))
    m = re.search(r"\[MEM\] Total: ([\d.]+) MB", txt)
    if m:
        d["memMB"] = float(m.group(1))
    m = re.search(r"^ +0 +" + r" +".join([r"([\d.]+)"] * 11), txt, re.M)
    if m:
        for k, s in enumerate(STAGES):
            d[s] = float(m.group(k + 1))
    if os.path.exists(er):
        etxt = open(er, errors="replace").read()
        if "segmentation violation" in etxt or "Break" in etxt:
            d["crash"] = 1
        mm = re.search(r"Maximum resident set size \(kbytes\): (\d+)", etxt)
        if mm:
            d["rssMB"] = int(mm.group(1)) / 1024.0
    return d


def q(a, p):
    return float(np.percentile(np.asarray(a, dtype=float), p)) if len(a) else float("nan")


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    recs = [r for r in (parse(i) for i in range(n)) if r]
    ok = [r for r in recs if not r["crash"] and "Total" in r]
    cr = [r for r in recs if r["crash"]]
    print("# events parsed=%d  completed=%d  CRASHED=%d (%.1f%%)" % (
        len(recs), len(ok), len(cr), 100.0 * len(cr) / max(len(recs), 1)))
    print("# crashed event indices:", " ".join(str(r["evt"]) for r in cr))
    print()
    print("## per-stage ms/evt over the %d completed events" % len(ok))
    print("%-8s %9s %9s %9s %9s %9s" % ("stage", "mean", "median", "p90", "p99", "max"))
    for s in STAGES:
        v = [r[s] for r in ok if s in r]
        print("%-8s %9.2f %9.2f %9.2f %9.2f %9.2f" % (s, np.mean(v), q(v, 50), q(v, 90), q(v, 99), max(v)))
    print()
    print("## graph size distribution (all events with a [CHAIN] line, crashers included)")
    have = [r for r in recs if "E1" in r]
    for key in ("nHits", "nMDalloc", "mdUsed", "nT3", "E1", "E2", "E"):
        v = [r[key] for r in have if key in r]
        print("%-8s mean %12.0f  median %10.0f  p90 %10.0f  p99 %10.0f  max %10.0f" % (
            key, np.mean(v), q(v, 50), q(v, 90), q(v, 99), max(v)))
    WALL = 2**32 // 21
    over = [r for r in have if r["E"] > WALL]
    print()
    print("## the 4 GiB ChainEdges wall (E > %d rows)" % WALL)
    print("#   events over the wall: %d / %d (%.2f%%)" % (len(over), len(have), 100.0 * len(over) / len(have)))
    for r in sorted(have, key=lambda r: -r["E"])[:12]:
        print("#   evt %-4d nT3=%-8d E1=%-11d E2=%-9d E=%-11d needs %8.1f MB  %s" % (
            r["evt"], r.get("nT3", 0), r["E1"], r["E2"], r["E"], r["E"] * 21 / 1e6,
            "OVER WALL / crash" if r["E"] > WALL else ""))
    print()
    print("## E1 scaling law")
    a = np.array([[r["nT3"], r["mdUsed"], r["E1"], r.get("nHits", 0), r.get("nMDalloc", 0)] for r in have],
                 dtype=float)
    nT3, used, E1 = a[:, 0], a[:, 1], a[:, 2]
    print("#   mean-field prediction E1 ~ nT3^2 / nMDkeysUsed:")
    ratio = E1 * used / nT3**2
    print("#     E1 / (nT3^2/K):  median %.3f  p10 %.3f  p90 %.3f   (1.0 = uniform degrees)" % (
        q(ratio, 50), q(ratio, 10), q(ratio, 90)))
    for x, name in ((nT3, "nT3"), (a[:, 3], "nHits"), (a[:, 4], "nMDalloc")):
        m = (x > 0) & (E1 > 0)
        p = np.polyfit(np.log(x[m]), np.log(E1[m]), 1)
        pred = np.exp(np.polyval(p, np.log(x[m])))
        r2 = 1 - np.sum((np.log(E1[m]) - np.log(pred))**2) / np.sum((np.log(E1[m]) - np.mean(np.log(E1[m])))**2)
        print("#   E1 = %.3e * %s^%.3f    (log-log R^2 = %.3f)" % (np.exp(p[1]), name, p[0], r2))
    m = nT3 > 0
    p = np.polyfit(np.log(nT3[m] ** 2 / used[m]), np.log(E1[m]), 1)
    pred = np.exp(np.polyval(p, np.log(nT3[m] ** 2 / used[m])))
    r2 = 1 - np.sum((np.log(E1[m]) - np.log(pred))**2) / np.sum((np.log(E1[m]) - np.mean(np.log(E1[m])))**2)
    print("#   E1 = %.3f * (nT3^2/K)^%.3f    (log-log R^2 = %.3f)" % (np.exp(p[1]), p[0], r2))
    print()
    print("## Graph ms vs E (the chain block is edge-count bound)")
    g = np.array([[r["E"], r["Graph"]] for r in ok if "E" in r and "Graph" in r], dtype=float)
    if len(g):
        p = np.polyfit(g[:, 0], g[:, 1], 1)
        print("#   Graph_ms = %.4f us/edge * E + %.2f ms   (corr %.4f)" % (
            p[0] * 1000, p[1], np.corrcoef(g[:, 0], g[:, 1])[0, 1]))
    print()
    print("## peak RSS")
    v = [r["rssMB"] for r in ok if "rssMB" in r]
    print("#   median %.0f MB  p90 %.0f MB  max %.0f MB" % (q(v, 50), q(v, 90), max(v)))
    print()
    print("csv,evt,crash,nHits,nMDalloc,mdUsed,nT3,E1,E2,E,edgeMB,memMB,rssMB,T3ms,Graphms,Chainms,Totalms")
    for r in recs:
        print("csv,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.2f" % (
            r["evt"], r["crash"], r.get("nHits", 0), r.get("nMDalloc", 0), r.get("mdUsed", 0),
            r.get("nT3", 0), r.get("E1", 0), r.get("E2", 0), r.get("E", 0), r.get("edgeMB", 0),
            r.get("memMB", 0), r.get("rssMB", 0), r.get("T3", 0), r.get("Graph", 0),
            r.get("Chain", 0), r.get("Total", 0)))


if __name__ == "__main__":
    main()
