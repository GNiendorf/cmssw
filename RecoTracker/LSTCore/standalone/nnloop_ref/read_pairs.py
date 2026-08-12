#!/usr/bin/env python3
"""Reader / validator for the LST_CHAIN_PAIR_DUMP attach pair sidecar.

The instrument is documented in standalone/nnloop_ref/PAIRDUMP_FORMAT.md; it is written by
LSTEvent::dumpChainPairs (src/alpaka/LSTEvent.dev.cc) once per event, in append mode.

Usage
-----
  read_pairs.py pairs.bin                          # parse, per-event summary, structural checks
  read_pairs.py pairs.bin --log run.log            # also cross-check against [CHAIN K8] scored=
  read_pairs.py pairs.bin --chains chains.bin      # also cross-check the chain count per event
  read_pairs.py pairs.bin --weights .../AttachNetworkWeights.h --roundtrip 8
                                                   # re-run the head over the dumped x, compare logit
  read_pairs.py pairs.bin --npz rows.npz           # export the rows for the trainer

Exit status is 0 only if every check that was requested passed.
"""

import argparse
import os
import re
import struct
import sys

import numpy as np

MAGIC = 0x50414952  # 'PAIR'
HEADER_FIELDS = ("magic", "version", "ievt", "nChains", "nT3", "nRows", "nDrop", "dsA", "dsB", "nFeat")
HEADER_BYTES = 4 * len(HEADER_FIELDS)

STAGE_CHAIN = 0  # chain target, nLayers >= 5 -- counted by [CHAIN K8] scored=
STAGE_T3 = 1  # bare-T3 target (stage B)
STAGE_AUX4L = 2  # chain target from the aux 4-layer (-XC4) tail -- score-only, not in the census


def row_dtype(n_feat):
    return np.dtype(
        [("stage", "<u4"), ("target", "<u4"), ("pls", "<u4"), ("logit", "<f4"), ("x", "<f4", (n_feat,))]
    )


def read_events(path):
    """Yield (header dict, structured row array) per event record, in file order."""
    size = os.path.getsize(path)
    with open(path, "rb") as fh:
        while fh.tell() < size:
            raw = fh.read(HEADER_BYTES)
            if len(raw) < HEADER_BYTES:
                raise ValueError("truncated header at offset %d" % (fh.tell() - len(raw)))
            hdr = dict(zip(HEADER_FIELDS, struct.unpack("<%dI" % len(HEADER_FIELDS), raw)))
            if hdr["magic"] != MAGIC:
                raise ValueError("bad magic 0x%08X at offset %d" % (hdr["magic"], fh.tell() - HEADER_BYTES))
            dt = row_dtype(hdr["nFeat"])
            need = hdr["nRows"] * dt.itemsize
            payload = fh.read(need)
            if len(payload) < need:
                raise ValueError("truncated payload for event %d" % hdr["ievt"])
            yield hdr, np.frombuffer(payload, dtype=dt)


def scored_from_log(log_path):
    """The per-event stage-A and stage-B `scored=` censuses printed by [CHAIN K8] / [CHAIN K8B]."""
    a, b = [], []
    with open(log_path, errors="replace") as fh:
        for line in fh:
            m = re.search(r"scored=(\d+)", line)
            if not m:
                continue
            if "[CHAIN K8B]" in line:
                b.append(int(m.group(1)))
            elif "[CHAIN K8]" in line:
                a.append(int(m.group(1)))
    return a, b


def chain_counts_from_dump(path):
    """nChains per event out of a LST_CHAIN_CHAIN_DUMP file (magic 'P22C', header 5 x uint32)."""
    out = []
    size = os.path.getsize(path)
    with open(path, "rb") as fh:
        while fh.tell() < size:
            raw = fh.read(20)
            if len(raw) < 20:
                raise ValueError("truncated chain-dump header")
            magic, ievt, n_nodes, n_edges, n_chains = struct.unpack("<5I", raw)
            if magic != 0x50323243:
                raise ValueError("bad chain-dump magic 0x%08X" % magic)
            out.append(n_chains)
            # Skip the per-chain payload: 7 uint32 + 8 float + nFeat float, then preN node ids,
            # preN-1 edge types, 2*nMDs hit rows. preN / n / m are the first three words.
            for _ in range(n_chains):
                pre_n, _n, m = struct.unpack("<3I", fh.read(12))
                fh.read(4 * 4)  # nLayers, branch, drop, flags
                fh.read(4 * 8)  # score, dcaXY, zF, zP, zD, mP, mD, mX
                n_feat = _chain_feat_count(fh)
                fh.read(4 * n_feat)
                fh.read(4 * pre_n)
                fh.read(4 * max(pre_n - 1, 0))
                fh.read(4 * 2 * m)
    return out


_CHAIN_FEAT = [None]


def _chain_feat_count(fh):
    # The chain dump does not record its feature count; Params_ChainFeat::kFeatures is 25 in the
    # shipped build (interface/ChainsSoA.h -- FROZEN at 25). Override with CHAIN_FEAT if that
    # contract ever moves; only the optional cross-check reads it.
    if _CHAIN_FEAT[0] is None:
        _CHAIN_FEAT[0] = int(os.environ.get("CHAIN_FEAT", "25"))
    return _CHAIN_FEAT[0]


# ---------------------------------------------------------------------------------------------
# Offline replay of the attach head, straight out of the C++ weight header.


def _parse_array(text, name):
    m = re.search(r"\b%s\s*(\[[^;=]*\])?\s*=\s*\{" % re.escape(name), text)
    if m is None:
        raise ValueError("%s not found in the weight header" % name)
    i = text.index("{", m.end() - 1)
    depth, j = 0, i
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    body = text[i : j + 1]
    nums = re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?f?", body)
    return np.array([float(v.rstrip("f")) for v in nums], dtype=np.float32), body


def load_attach_weights(header_path):
    text = open(header_path).read()
    text = re.sub(r"//[^\n]*", "", text)
    k_in = int(re.search(r"kInput\s*=\s*(\d+)", text).group(1))
    k_h = int(re.search(r"kHidden\s*=\s*(\d+)", text).group(1))
    w1, _ = _parse_array(text, "wgt_l1")
    b1, _ = _parse_array(text, "bias_l1")
    w2, _ = _parse_array(text, "wgt_l2")
    b2, _ = _parse_array(text, "bias_l2")
    wo, _ = _parse_array(text, "wgt_out")
    bo = np.float32(float(re.search(r"bias_out\s*=\s*(-?[\d.eE+-]+)f?", text).group(1)))
    return dict(
        kInput=k_in,
        kHidden=k_h,
        w1=w1.reshape(k_in, k_h),
        b1=b1,
        w2=w2.reshape(k_h, k_h),
        b2=b2,
        wo=wo,
        bo=bo,
    )


def attach_logit(x, w):
    """attachHeadBatch over already-standardized inputs: 20 -> 24 -> 24 -> 1, ReLU, no output act."""
    x = np.asarray(x, dtype=np.float32).reshape(-1, w["kInput"])
    h1 = np.maximum(x @ w["w1"] + w["b1"], 0.0, dtype=np.float32)
    h2 = np.maximum(h1 @ w["w2"] + w["b2"], 0.0, dtype=np.float32)
    return (h2 @ w["wo"] + w["bo"]).astype(np.float32)


# ---------------------------------------------------------------------------------------------


def keep_hash(target, pls, keep):
    """attachPairKeep, in numpy. Lets the downsampling be verified from the dump alone."""
    if keep <= 1:
        return np.ones(np.shape(target), dtype=bool)
    t = np.asarray(target, dtype=np.uint64) * np.uint64(2654435761)
    p = np.asarray(pls, dtype=np.uint64) * np.uint64(40503)
    h = (t & np.uint64(0xFFFFFFFF)) ^ (p & np.uint64(0xFFFFFFFF))
    return (h % np.uint64(keep)) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--log", help="run log, for the [CHAIN K8] scored= cross-check")
    ap.add_argument("--chains", help="LST_CHAIN_CHAIN_DUMP file, for the chain-count cross-check")
    ap.add_argument("--weights", help="AttachNetworkWeights.h, for the logit round-trip")
    ap.add_argument("--roundtrip", type=int, default=0, help="how many stage-A pairs to replay")
    ap.add_argument("--npz", help="write the pooled rows here")
    ap.add_argument("--max-events", type=int, default=0)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    failures = []
    pooled = []
    n_a_dump, n_b_dump, n_aux_dump = [], [], []
    headers = []

    for hdr, rows in read_events(args.path):
        if args.max_events and len(headers) >= args.max_events:
            break
        headers.append(hdr)
        stage = rows["stage"]
        n_a = int(np.count_nonzero(stage == STAGE_CHAIN))
        n_b = int(np.count_nonzero(stage == STAGE_T3))
        n_x = int(np.count_nonzero(stage == STAGE_AUX4L))
        n_a_dump.append(n_a)
        n_b_dump.append(n_b)
        n_aux_dump.append(n_x)

        if n_a + n_b + n_x != len(rows):
            failures.append("evt %d: unknown stage code present" % hdr["ievt"])
        if hdr["nDrop"]:
            failures.append("evt %d: %d rows DROPPED (raise LST_CHAIN_PAIR_CAP)" % (hdr["ievt"], hdr["nDrop"]))

        # Every chain-kind target index must be a valid chain row of this event.
        chain_side = rows[(stage == STAGE_CHAIN) | (stage == STAGE_AUX4L)]
        if len(chain_side) and int(chain_side["target"].max()) >= hdr["nChains"]:
            failures.append(
                "evt %d: chain target index %d >= nChains %d"
                % (hdr["ievt"], int(chain_side["target"].max()), hdr["nChains"])
            )
        t3_side = rows[stage == STAGE_T3]
        if len(t3_side) and int(t3_side["target"].max()) >= hdr["nT3"]:
            failures.append(
                "evt %d: bare-T3 target index %d >= nT3 %d"
                % (hdr["ievt"], int(t3_side["target"].max()), hdr["nT3"])
            )
        if len(rows) and not np.isfinite(rows["x"]).all():
            failures.append("evt %d: non-finite feature in the dump" % hdr["ievt"])

        # The downsampling is a pure function of (target, pls), so it is checkable here.
        if len(t3_side) and not keep_hash(t3_side["target"], t3_side["pls"], hdr["dsB"]).all():
            failures.append("evt %d: a stage-B row fails attachPairKeep" % hdr["ievt"])
        if len(chain_side) and hdr["dsA"] != 1:
            failures.append("evt %d: stage A is expected whole (dsA=1), got %d" % (hdr["ievt"], hdr["dsA"]))

        if not args.quiet:
            print(
                "evt %4d  rows %7d = A %6d + B %6d + aux4L %5d | nChains %5d nT3 %7d "
                "dsA %d dsB %2d drop %d"
                % (
                    hdr["ievt"],
                    len(rows),
                    n_a,
                    n_b,
                    n_x,
                    hdr["nChains"],
                    hdr["nT3"],
                    hdr["dsA"],
                    hdr["dsB"],
                    hdr["nDrop"],
                )
            )
        if args.npz or args.roundtrip:
            pooled.append(rows)

    if not headers:
        print("no records in %s" % args.path)
        return 1
    if len({h["nFeat"] for h in headers}) != 1:
        failures.append("nFeat is not constant across the file")

    # ---- cross-check A: the stage-A row count against the [CHAIN K8] census ------------------
    if args.log:
        a_log, b_log = scored_from_log(args.log)
        n = min(len(a_log), len(n_a_dump))
        if n == 0:
            failures.append("no [CHAIN K8] scored= lines in %s (run with LST_CHAIN_TIMING=1)" % args.log)
        for i in range(n):
            # EXACT equality is the contract: stage A is dumped whole, and the census excludes the
            # aux 4-layer targets, which the dump keeps under stage code 2.
            if a_log[i] != n_a_dump[i]:
                failures.append("evt %d: stage-A rows %d != [CHAIN K8] scored= %d" % (i, n_a_dump[i], a_log[i]))
        nb = min(len(b_log), len(n_b_dump))
        for i in range(nb):
            # Stage B is downsampled, so only the ratio is checkable. 1/dsB +- 3% is a wide band on
            # 1e6 pairs and catches a wrong keep factor or a wrong stage tag.
            exp = b_log[i] / float(headers[i]["dsB"])
            if exp > 0 and abs(n_b_dump[i] - exp) / exp > 0.03:
                failures.append(
                    "evt %d: stage-B rows %d vs scored=%d / dsB=%d -> %.0f (off by more than 3%%)"
                    % (i, n_b_dump[i], b_log[i], headers[i]["dsB"], exp)
                )
        if not args.quiet and n:
            print(
                "\n[K8 census] stage A dump vs log: %s"
                % ("all %d events EQUAL" % n if not any("stage-A rows" in f for f in failures) else "MISMATCH")
            )
            print(
                "[K8B census] stage B dump vs log/dsB: %s"
                % (
                    "all %d events within 3%%" % nb
                    if not any("stage-B rows" in f for f in failures)
                    else "MISMATCH"
                )
            )

    # ---- cross-check B: nChains against the chain dump ----------------------------------------
    if args.chains:
        try:
            counts = chain_counts_from_dump(args.chains)
        except Exception as exc:  # the chain record is variable-length; a skew is a real finding
            failures.append("chain dump unreadable: %s" % exc)
            counts = []
        n = min(len(counts), len(headers))
        for i in range(n):
            if counts[i] != headers[i]["nChains"]:
                failures.append(
                    "evt %d: pair-dump nChains %d != chain-dump nChains %d"
                    % (i, headers[i]["nChains"], counts[i])
                )
        if not args.quiet and n:
            print("[chains] nChains agrees on %d events" % n)

    # ---- cross-check C: the 20 floats reproduce the logit ------------------------------------
    if args.roundtrip:
        if not args.weights:
            failures.append("--roundtrip needs --weights")
        else:
            w = load_attach_weights(args.weights)
            allrows = np.concatenate(pooled)
            if w["kInput"] != headers[0]["nFeat"]:
                failures.append("weight header kInput %d != dump nFeat %d" % (w["kInput"], headers[0]["nFeat"]))
            for tag, sel in (("A", STAGE_CHAIN), ("B", STAGE_T3), ("aux4L", STAGE_AUX4L)):
                sub = allrows[allrows["stage"] == sel]
                if not len(sub):
                    continue
                take = sub[:: max(1, len(sub) // args.roundtrip)][: args.roundtrip]
                got = attach_logit(take["x"], w)
                ref = take["logit"].astype(np.float32)
                dev = np.abs(got - ref)
                tol = 2e-5 * np.maximum(1.0, np.abs(ref))
                bad = int(np.count_nonzero(dev > tol))
                if not args.quiet:
                    print(
                        "[roundtrip %-5s] n=%d  max|dumped - replayed| = %.3e  (max|logit| %.3f)  bad=%d"
                        % (tag, len(take), dev.max(), np.abs(ref).max(), bad)
                    )
                if bad:
                    worst = int(np.argmax(dev))
                    failures.append(
                        "roundtrip %s: %d/%d pairs off, worst dumped %.7f vs replayed %.7f"
                        % (tag, bad, len(take), ref[worst], got[worst])
                    )

    if args.npz:
        allrows = np.concatenate(pooled)
        ievt = np.concatenate([np.full(n, h["ievt"], dtype=np.uint32) for h, n in zip(headers, map(len, pooled))])
        np.savez_compressed(
            args.npz,
            ievt=ievt,
            stage=allrows["stage"],
            target=allrows["target"],
            pls=allrows["pls"],
            logit=allrows["logit"],
            x=allrows["x"],
            dsA=np.array([h["dsA"] for h in headers], dtype=np.uint32),
            dsB=np.array([h["dsB"] for h in headers], dtype=np.uint32),
        )
        print("wrote %s: %d rows" % (args.npz, len(allrows)))

    print(
        "\n%d events | rows/evt A %.0f B %.0f aux4L %.0f | drops %d"
        % (
            len(headers),
            np.mean(n_a_dump),
            np.mean(n_b_dump),
            np.mean(n_aux_dump),
            sum(h["nDrop"] for h in headers),
        )
    )
    if failures:
        print("\nFAILED %d check(s):" % len(failures))
        for f in failures[:40]:
            print("  " + f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
