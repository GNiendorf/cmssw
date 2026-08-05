#!/usr/bin/env python3
"""m13_shadow.py -- MISSION 3: shadow-fake yield curve on the m12_w7 anchor (READ-ONLY).

For every SURVIVING chain TC of w7 (verified replica join, m13_replica.py) decide whether a
HIGHER-GATE-SCORED TC sits within dR in {0.02,0.05,0.1,0.2} with pt ratio < 2, and record
the quantities the maintainer's decision rule needs:
  (a) the victim's own absolute gate fake-margin (mX = max(prompt,displaced)-fake; mP/mD and
      the 2-class a2 gate logit ride along),
  (b) the victim's displaced margin mD (collateral check) + truth (simVxy/simPt),
  (c) local TC density (in-cut TCs within dR<0.1) -> decile (jet-safety check),
plus the efficiency-collateral key: is this TC the ONLY in-cut TC delivering its sim?

Writes <scratch>/m13_shadow.npy (one row per surviving chain TC).
"""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

WINDOWS = [0.02, 0.05, 0.10, 0.20]
PTRATIO = 2.0
DENS_DR = 0.10


def key(pt, eta, phi):
    return f"{pt:.7g}|{eta:.7g}|{phi:.7g}"


def main():
    R = np.load(f"{SCRATCH}/m13_chains.npy")
    byevt = {}
    for e in np.unique(R["evt"]):
        byevt[int(e)] = R[R["evt"] == e]

    t = uproot.open(f"{SA}/prototype/ab_m12_w7.root")["tree"]
    a = t.arrays(["tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_isChain", "tc_type",
                  "tc_isDuplicate", "tc_simIdxAll", "evt"], library="np")

    rows = []
    for i in range(len(a["evt"])):
        evt = int(a["evt"][i])
        if evt not in byevt:
            continue
        r = byevt[evt]
        pt = np.asarray(a["tc_pt"][i], np.float64)
        eta = np.asarray(a["tc_eta"][i], np.float64)
        phi = np.asarray(a["tc_phi"][i], np.float64)
        fk = np.asarray(a["tc_isFake"][i], np.int32)
        ch = np.asarray(a["tc_isChain"][i], np.int32)
        dup = np.asarray(a["tc_isDuplicate"][i], np.int32)
        sims = a["tc_simIdxAll"][i]

        inc = (pt > 0.9) & (np.abs(eta) < 4.5)
        gi = np.nonzero(inc)[0]                      # global row -> in-cut position
        pos = -np.ones(len(pt), np.int64)
        pos[gi] = np.arange(len(gi))
        P, E, F = pt[gi], eta[gi], phi[gi]
        FK, CH, DUP = fk[gi], ch[gi], dup[gi]

        # sim -> number of in-cut TCs delivering it
        simCount = {}
        for j in gi:
            for s in np.asarray(sims[j], np.int64):
                simCount[int(s)] = simCount.get(int(s), 0) + 1

        # join replica records to the ab chain rows (kinematic triple, verified exact)
        idx = {}
        for j in range(len(r)):
            idx.setdefault(key(r["pt"][j], r["eta"][j], r["phi"][j]), []).append(j)
        vic_global, vic_rep = [], []
        for j in gi:
            if ch[j] != 1:
                continue
            k = key(pt[j], eta[j], phi[j])
            lst = idx.get(k)
            if not lst:
                continue
            vic_rep.append(lst.pop())
            vic_global.append(j)
        if not vic_global:
            continue
        vg = np.asarray(vic_global, np.int64)
        vr = np.asarray(vic_rep, np.int64)
        vp = pos[vg]

        # neighbour score: chain TCs use the gate margin, pixel TCs are treated separately
        scoreX = np.full(len(gi), np.nan)
        scoreG = np.full(len(gi), np.nan)
        scoreX[vp] = r["mX"][vr]
        scoreG[vp] = r["gate"][vr]

        de = E[vp][:, None] - E[None, :]
        dp = (F[vp][:, None] - F[None, :] + np.pi) % (2 * np.pi) - np.pi
        dr = np.sqrt(de * de + dp * dp)
        rat = np.maximum(P[vp][:, None] / P[None, :], P[None, :] / P[vp][:, None])
        self_mask = np.zeros_like(dr, dtype=bool)
        self_mask[np.arange(len(vp)), vp] = True
        base = (rat < PTRATIO) & ~self_mask
        isChNb = (CH == 1)[None, :]
        dens = ((dr < DENS_DR) & ~self_mask).sum(axis=1)

        hiX = (scoreX[None, :] > scoreX[vp][:, None])
        hiG = (scoreG[None, :] > scoreG[vp][:, None])
        trueNb = (FK == 0)[None, :]

        out = np.zeros((len(vp), 4 * 5), dtype=np.int8)
        best = np.full((len(vp), 4 * 2), -99.0, dtype=np.float32)
        for w, W in enumerate(WINDOWS):
            m = base & (dr < W)
            mc = m & isChNb
            mp = m & ~isChNb
            out[:, 5 * w + 0] = (mc & hiX).any(1)          # higher-mX chain neighbour
            out[:, 5 * w + 1] = ((mc & hiX) | mp).any(1)   # + pixel treated as dominant
            out[:, 5 * w + 2] = (mc & hiG).any(1)          # higher 2-class-gate chain nb
            out[:, 5 * w + 3] = (mc & hiX & trueNb).any(1)  # higher-mX TRUE chain nb
            out[:, 5 * w + 4] = mp.any(1)                  # any pixel neighbour at all
            # strongest chain neighbour margin in the window (any / true only): lets any
            # "clearly stronger neighbour" gap rule be evaluated offline.
            best[:, 2 * w + 0] = np.where(mc.any(1),
                                          np.nanmax(np.where(mc, scoreX[None, :], -99.0), axis=1), -99.0)
            mt = mc & trueNb
            best[:, 2 * w + 1] = np.where(mt.any(1),
                                          np.nanmax(np.where(mt, scoreX[None, :], -99.0), axis=1), -99.0)

        for n, j in enumerate(vg):
            jr = vr[n]
            ss = np.asarray(sims[j], np.int64)
            uniq = 1 if (len(ss) > 0 and all(simCount.get(int(s), 0) <= 1 for s in ss)) else 0
            rows.append((evt, int(FK[vp[n]]), int(DUP[vp[n]]),
                         float(r["mX"][jr]), float(r["mP"][jr]), float(r["mD"][jr]),
                         float(r["gate"][jr]), float(r["score"][jr]), float(r["dca"][jr]),
                         int(r["nLayers"][jr]), int(r["exempt"][jr]),
                         float(r["simVxy"][jr]), float(r["simPt"][jr]),
                         float(pt[j]), float(eta[j]), int(dens[n]), uniq,
                         *[int(v) for v in out[n]], *[float(v) for v in best[n]]))
        if i % 40 == 0:
            print(f"  evt {i} ({evt}): inCut={len(gi)} victims={len(vg)}", flush=True)

    names = [("evt", "i8"), ("isFake", "i4"), ("isDup", "i4"), ("mX", "f4"), ("mP", "f4"),
             ("mD", "f4"), ("gate", "f4"), ("score", "f4"), ("dca", "f4"),
             ("nLayers", "i4"), ("exempt", "i4"), ("simVxy", "f4"), ("simPt", "f4"),
             ("pt", "f4"), ("eta", "f4"), ("dens", "i4"), ("uniqueDeliverer", "i4")]
    for W in WINDOWS:
        tag = f"{int(W * 100):02d}"
        names += [(f"sX{tag}", "i1"), (f"sXP{tag}", "i1"), (f"sG{tag}", "i1"),
                  (f"sXT{tag}", "i1"), (f"pix{tag}", "i1")]
    for W in WINDOWS:
        tag = f"{int(W * 100):02d}"
        names += [(f"bX{tag}", "f4"), (f"bT{tag}", "f4")]
    arr = np.array(rows, dtype=np.dtype(names))
    np.save(f"{SCRATCH}/m13_shadow.npy", arr)
    print(f"wrote {len(arr)} surviving chain TCs (fake {int((arr['isFake']==1).sum())})")


if __name__ == "__main__":
    main()
