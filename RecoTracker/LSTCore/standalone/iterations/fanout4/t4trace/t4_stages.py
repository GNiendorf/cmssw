#!/usr/bin/env python3
"""Per-stage attrition table for the T4-band trace ledger produced by the instrumented
binary (PROTO_T4TRACE_OUT), split by LST delivering type (9 = T4, 4 = T5 control)."""
import json
import sys

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/t4trace"

cols = ("iev sim ltype t3a t3b edgeIdx edgeType edgeLogit chainA chainB adjacent "
        "weldedOurEdge nNodesA nLayersA nNodesB nLayersB scorePre dca mP mD mX exempt "
        "branch killed thetaPass pixDrop accepted isTC tcType stealAType stealBType "
        "stealALogit stealBLogit").split()
FLOAT = {"edgeLogit", "scorePre", "dca", "mP", "mD", "mX", "stealALogit", "stealBLogit"}

path = sys.argv[1] if len(sys.argv) > 1 else P + "/trace_anchor.txt"
rows = []
for line in open(path):
    if line.startswith("#"):
        continue
    v = line.split()
    if len(v) != len(cols):
        continue
    rows.append({c: (float(v[i]) if c in FLOAT else int(v[i])) for i, c in enumerate(cols)})

out = json.load(open(P + "/t4_outcome.json"))
led = {(r["iev"], r["sim"]): r for r in json.load(open(P + "/t4_targets.json"))}
for r in rows:
    k = (r["iev"], r["sim"])
    r["protoDeliv"] = out["%d_%d" % k]["delivered"]
    r["dxy"] = led[k]["dxy"]
    r["simpt"] = led[k]["pt"]
    r["simeta"] = led[k]["eta"]

BRANCH = {-1: "n/a", 0: "T4cls exempt (dca>=X)", 1: "T4cls IP (dca<X)",
          2: "5+ IP", 3: "5+ exempt"}


def funnel(rs, label):
    n = len(rs)
    print("\n===== %s : n=%d =====" % (label, n))
    s1 = rs  # node-set membership verified offline: 100%
    s2 = [r for r in s1 if r["edgeIdx"] >= 0]
    s3 = [r for r in s2 if r["edgeLogit"] >= 0.0]
    s4 = [r for r in s3 if r["adjacent"] == 1]
    s5 = [r for r in s4 if r["killed"] == 0]
    s6 = [r for r in s5 if r["thetaPass"] == 1 and r["pixDrop"] == 0]
    s7 = [r for r in s6 if r["accepted"] == 1]
    stages = [("1 both member T3s in node set", s1),
              ("2 pair edge enumerated (K2)", s2),
              ("3 edge logit >= thetaEdge(0)", s3),
              ("4 K6 weld keeps pair adjacent", s4),
              ("5 -G 6 gate not killed", s5),
              ("6a theta gate + pixel-consumed drop", s6),
              ("6b K9 claim accepted", s7)]
    prev = n
    for name, st in stages:
        print("  %-38s %3d  (lost %d)" % (name, len(st), prev - len(st)))
        prev = len(st)
    print("  final: proto delivers the sim (any route) = %d"
          % sum(r["protoDeliv"] for r in rs))
    # edge type / weld anatomy
    et = {}
    for r in s2:
        et[r["edgeType"]] = et.get(r["edgeType"], 0) + 1
    print("  edge types among enumerated: %s   (2 = E2/shared-LS, 1 = E1/shared-MD)" % et)
    below = [r for r in s2 if r["edgeLogit"] < 0.0]
    if below:
        print("  edge-logit failures: %d  logits=%s"
              % (len(below), sorted(round(r["edgeLogit"], 2) for r in below)))
    # welding anatomy for those that passed the edge cut but are not adjacent
    nonadj = [r for r in s3 if r["adjacent"] == 0]
    print("  weld-absorbed (edge live, pair NOT adjacent): %d" % len(nonadj))
    ET = {0: "none", 1: "E1", 2: "E2"}
    for r in nonadj:
        print("     ev%-4d sim%-5d dxy=%7.2f ourLogit=%6.2f | t3a OUT slot taken by %s(logit %6.2f)"
              " | t3b IN slot taken by %s(logit %6.2f)"
              % (r["iev"], r["sim"], r["dxy"], r["edgeLogit"],
                 ET.get(r["stealAType"], "?"), r["stealALogit"],
                 ET.get(r["stealBType"], "?"), r["stealBLogit"]))
    st = {}
    for r in nonadj:
        for k in ("stealAType", "stealBType"):
            if r[k]:
                st[ET[r[k]]] = st.get(ET[r[k]], 0) + 1
    print("  slot-stealing edge types: %s" % st)
    beat = sum(1 for r in nonadj
               if (r["stealAType"] and r["stealALogit"] > r["edgeLogit"])
               or (r["stealBType"] and r["stealBLogit"] > r["edgeLogit"]))
    print("  ... of which the stealing edge scored HIGHER than the true edge: %d/%d"
          % (beat, len(nonadj)))
    # gate anatomy
    print("  -- gate branch census over stage-4 survivors --")
    bc = {}
    for r in s4:
        key = (BRANCH[r["branch"]], r["killed"])
        bc[key] = bc.get(key, 0) + 1
    for k in sorted(bc, key=lambda x: (x[0], x[1])):
        print("     %-24s killed=%d : %d" % (k[0], k[1], bc[k]))
    for r in s4:
        if r["killed"]:
            print("     KILLED ev%-4d sim%-5d dxy=%7.2f nN=%d nL=%d dca=%7.3f mP=%6.2f mD=%6.2f mX=%6.2f br=%s"
                  % (r["iev"], r["sim"], r["dxy"], r["nNodesA"], r["nLayersA"], r["dca"],
                     r["mP"], r["mD"], r["mX"], BRANCH[r["branch"]]))
    # claim anatomy
    for r in s6:
        if r["accepted"] == 0:
            print("     CLAIM-LOST ev%-4d sim%-5d dxy=%7.2f nN=%d nL=%d score=%.2f"
                  % (r["iev"], r["sim"], r["dxy"], r["nNodesA"], r["nLayersA"], r["scorePre"]))
    for r in s5:
        if r["thetaPass"] == 0 or r["pixDrop"] == 1:
            print("     PRECLAIM-LOST ev%-4d sim%-5d thetaPass=%d pixDrop=%d nL=%d"
                  % (r["iev"], r["sim"], r["thetaPass"], r["pixDrop"], r["nLayersA"]))
    return stages


t4 = [r for r in rows if r["ltype"] == 9]
t5 = [r for r in rows if r["ltype"] == 4]
funnel(t4, "LST-T4-delivered band sims (tc_type 9)")
funnel(t5, "LST-T5-delivered band sims (tc_type 4) [CONTROL]")

print("\n===== chain-length anatomy of the T4 targets that DID form an adjacent pair =====")
for r in t4:
    if r["adjacent"]:
        print("  ev%-4d sim%-5d dxy=%7.2f nNodes=%d nLayers=%d killed=%d acc=%d deliv=%d"
              % (r["iev"], r["sim"], r["dxy"], r["nNodesA"], r["nLayersA"], r["killed"],
                 r["accepted"], r["protoDeliv"]))
