#!/usr/bin/env python3
"""M13 mission-1, part 2: the TRUE cost of killing chain TCs, and the shadow-fake yield.

(A) EFFICIENCY-COST-AWARE KILL FRONTIER. A killed TRUE chain TC only costs efficiency if
    it was the SOLE >0.75 match of an accepted sim. 71% of true chain TCs are duplicates,
    so "true loss" in a naive margin sweep massively overstates the efficiency price.
    Here: sweep a margin, kill from the bottom, count (a) fakes removed, (b) ACCEPTED sims
    that lose their last matching TC -> the actual delta-efficiency.
(B) SHADOW-FAKE YIELD (the plan's QUEUED dedup-as-fake-killer lever, measured at w7):
    fraction of surviving fake chain TCs that sit within dR of a NON-FAKE TC with
    compatible pt, split by branch / neighbour type / victim margin.

READ-ONLY. Consumes m13_mf_accepted.pkl + m13_mf_w7.root.
"""
import pickle

import numpy as np
import uproot

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT, ETACUT = 0.9, 4.5
DCASPLIT = 0.5
DENOM_ALL = 573648
PIX_FAKES = 13541
BASE_FR = 21620 / 475212

A = pickle.load(open(P + "m13_mf_accepted.pkl", "rb"))
o = uproot.open(P + "m13_mf_w7.root:tree")
od = o.arrays(["tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_isChain", "tc_simIdxAll",
               "sim_pt", "sim_eta", "sim_q", "sim_vx", "sim_vy", "sim_vz"], library="np")
nev = len(od["tc_pt"])

# --------------------------------------------------------------------------- (A) ----
# Per event: map accepted-sim full rows -> matching TC rows; chain TCs are the tail.
tot_sim_matched = 0
tot_sim_denom = 0
sole = []          # per chain TC: number of accepted sims for which it is the SOLE match
n_acc_sims_ev = []
for i in range(nev):
    isch = np.asarray(od["tc_isChain"][i]) > 0
    simall = od["tc_simIdxAll"][i]
    nAccSim = len(od["sim_pt"][i])
    spt = np.asarray(od["sim_pt"][i])
    # EXACT harness efficiency selection (performance.cc fillEfficiencySet, eta hist):
    # q != 0, pt > 0.9, |vz| < 30, sqrt(vx^2+vy^2) < 2.5. Verified: denom 22784, numer 18638.
    inDen = ((np.asarray(od["sim_q"][i]) != 0) & (spt > PTCUT)
             & (np.abs(np.asarray(od["sim_vz"][i])) < 30.0)
             & (np.hypot(np.asarray(od["sim_vx"][i]), np.asarray(od["sim_vy"][i])) < 2.5))
    cnt = np.zeros(nAccSim, dtype=np.int32)
    owner = np.full(nAccSim, -1, dtype=np.int64)
    for t in range(len(isch)):
        for s in simall[t]:
            if 0 <= s < nAccSim:
                cnt[s] += 1
                owner[s] = t
    tot_sim_denom += int(inDen.sum())
    tot_sim_matched += int(((cnt > 0) & inDen).sum())
    ci = np.nonzero(isch)[0]
    pos = {int(t): k for k, t in enumerate(ci)}
    sv = np.zeros(len(ci), dtype=np.int32)
    for s in np.nonzero((cnt == 1) & inDen)[0]:
        t = int(owner[s])
        if t in pos:
            sv[pos[t]] += 1
    sole.append(sv)
    n_acc_sims_ev.append(int(inDen.sum()))
sole = np.concatenate(sole)
print("sim bookkeeping: accepted sims in denom %d (harness n_sim_denom 63603); "
      "matched %d -> eff %.4f (compare_ab eff_overall_incut 0.8180)"
      % (tot_sim_denom, tot_sim_matched, tot_sim_matched / tot_sim_denom))
print("chain TCs: %d ; sole-provider chain TCs (killing them loses an accepted sim): %d (%.4f)"
      % (len(sole), (sole > 0).sum(), (sole > 0).mean()))

fake = np.asarray(A["fake"]) > 0
pt = np.asarray(A["pt"])
nL = np.asarray(A["nL_d"])
dca = np.asarray(A["dca"])
mX, mP, mD = np.asarray(A["mX"]), np.asarray(A["mP"]), np.asarray(A["mD"])
br = np.where(nL <= 4, np.where(dca < DCASPLIT, 0, 1), np.where(dca < DCASPLIT, 2, 3))
assert len(fake) == len(sole)
incut = pt > PTCUT
print("  sole-provider TCs that are fakes: %d (must be 0)" % ((sole > 0) & fake).sum())
print("  sole-provider chain TCs by branch: IP-T4 %d  ex-T4 %d  IP-5+ %d  ex-5+ %d"
      % tuple(((sole > 0) & (br == b)).sum() for b in range(4)))


def frontier(sel, score, name, cuts=None):
    s = np.asarray(sel)
    sc = np.asarray(score)
    order = np.argsort(sc[s], kind="stable")
    idx = np.nonzero(s)[0][order]
    f = fake[idx] & incut[idx]
    sl = sole[idx]
    cf = np.cumsum(f)
    cs = np.cumsum(sl)
    cd = np.cumsum(incut[idx])
    print("\n  %-34s (n=%d, fakes-in-cut=%d)" % (name, len(idx), int(f.sum())))
    print("     %8s %8s %9s %9s %9s %10s" % ("cut", "killed", "fakes", "sims lost", "d_eff", "aggFR"))
    for k in (cuts or [500, 1000, 2000, 4000, 8000, 12000, 16000, 20000, 30000, 45000]):
        if k >= len(idx):
            break
        agg = (PIX_FAKES + int(fake[incut].sum()) - int(cf[k - 1])) / (DENOM_ALL - int(cd[k - 1]))
        print("     %8.3f %8d %9d %9d %9.4f %10.4f"
              % (sc[idx[k - 1]], k, cf[k - 1], cs[k - 1], -cs[k - 1] / tot_sim_denom, agg))
    # the cut that reaches baseline aggregate FR
    aggs = (PIX_FAKES + int(fake[incut].sum()) - cf) / (DENOM_ALL - cd)
    hit = np.nonzero(aggs <= BASE_FR)[0]
    if len(hit):
        k = int(hit[0])
        print("     >>> reaches BASELINE FR %.4f at cut %.3f: %d killed, %d fakes, %d sims lost,"
              " d_eff %+.4f (eff %.4f -> %.4f)"
              % (BASE_FR, sc[idx[k]], k + 1, cf[k], cs[k], -cs[k] / tot_sim_denom,
                 tot_sim_matched / tot_sim_denom, (tot_sim_matched - cs[k]) / tot_sim_denom))
    else:
        print("     >>> never reaches baseline FR on this axis alone")


print("\n=== (A) EFFICIENCY-COST-AWARE KILL FRONTIER (kill lowest-margin chain TCs first) ===")
frontier(np.ones(len(fake), bool), mX, "ALL chain TCs, axis = mX")
frontier(np.ones(len(fake), bool), np.where(br == 1, mD, np.where(br == 3, mX, mP)),
         "ALL, axis = per-branch governing margin")
frontier(br == 3, mX, "exempt-5+ only, axis = mX")
frontier(br == 2, mP, "IP-5+ only, axis = mP")
frontier(br == 1, mD, "exempt-T4 only, axis = mD")
frontier((br == 3) | (br == 2), mX, "all 5+ (IP + exempt), axis = mX")

# oracle: kill fakes only, cheapest-first (the perfect classifier)
print("\n  ORACLE (perfect classifier, kills only fakes): aggregate FR %.4f, d_eff 0.0000"
      % (PIX_FAKES / (DENOM_ALL - int(fake[incut].sum()))))

# --------------------------------------------------------------------------- (B) ----
print("\n=== (B) SHADOW-FAKE YIELD: does a surviving fake chain TC sit next to a real TC? ===")
DRS = [0.02, 0.05, 0.1, 0.2]
res = {d: np.zeros(4, dtype=np.int64) for d in DRS}     # per branch
resT = {d: np.zeros(3, dtype=np.int64) for d in DRS}    # neighbour type: chain/pixel/any
tot_br = np.zeros(4, dtype=np.int64)
marg_hit, marg_miss = [], []
ptr = 0
for i in range(nev):
    isch = np.asarray(od["tc_isChain"][i]) > 0
    e = np.asarray(od["tc_eta"][i], dtype=np.float64)
    p = np.asarray(od["tc_phi"][i], dtype=np.float64)
    q = np.asarray(od["tc_pt"][i], dtype=np.float64)
    fk = np.asarray(od["tc_isFake"][i]) > 0
    nch = int(isch.sum())
    gs = slice(ptr, ptr + nch)
    ptr += nch
    myfake = fake[gs] & incut[gs]
    if not myfake.any():
        continue
    ci = np.nonzero(isch)[0]
    vic = ci[myfake]
    good = np.nonzero(~fk)[0]                      # candidate "real" neighbours
    de = e[vic][:, None] - e[good][None, :]
    dp = np.abs(p[vic][:, None] - p[good][None, :])
    dp = np.minimum(dp, 2 * np.pi - dp)
    dr = np.sqrt(de * de + dp * dp)
    rr = q[vic][:, None] / np.maximum(q[good][None, :], 1e-6)
    ptok = (rr < 2.0) & (rr > 0.5)
    isChainN = isch[good][None, :]
    b = br[gs][myfake]
    for k in range(4):
        tot_br[k] += int((b == k).sum())
    for d in DRS:
        hit = (dr < d) & ptok
        anyhit = hit.any(1)
        for k in range(4):
            res[d][k] += int(anyhit[b == k].sum())
        resT[d][0] += int((hit & isChainN).any(1).sum())
        resT[d][1] += int((hit & ~isChainN).any(1).sum())
        resT[d][2] += int(anyhit.sum())
        if d == 0.05:
            marg_hit.append(mX[gs][myfake][anyhit])
            marg_miss.append(mX[gs][myfake][~anyhit])
NFI = int(tot_br.sum())
print("  surviving in-cut fake chain TCs: %d" % NFI)
print("  %-8s %10s | %9s %9s %9s %9s" % ("dR", "any-real", "IP-T4", "ex-T4", "IP-5+", "ex-5+"))
for d in DRS:
    print("  %-8.2f %9.4f | %9.4f %9.4f %9.4f %9.4f"
          % (d, resT[d][2] / NFI, *[res[d][k] / max(tot_br[k], 1) for k in range(4)]))
print("  neighbour type at dR<0.05: chain %.4f  pixel %.4f  any %.4f"
      % (resT[0.05][0] / NFI, resT[0.05][1] / NFI, resT[0.05][2] / NFI))
mh, mm = np.concatenate(marg_hit), np.concatenate(marg_miss)
print("  victim mX at dR<0.05: shadowed  median %.3f (n=%d) | isolated median %.3f (n=%d)"
      % (np.median(mh), len(mh), np.median(mm), len(mm)))
print("  fraction of shadowed victims with mX < 0 : %.4f ; isolated with mX < 0 : %.4f"
      % ((mh < 0).mean(), (mm < 0).mean()))

# control: same measurement on TRUE chain TCs (the jet-core false-positive risk)
ptr = 0
tot_t, hit_t, hit_t_sole = 0, 0, 0
for i in range(nev):
    isch = np.asarray(od["tc_isChain"][i]) > 0
    e = np.asarray(od["tc_eta"][i], dtype=np.float64)
    p = np.asarray(od["tc_phi"][i], dtype=np.float64)
    q = np.asarray(od["tc_pt"][i], dtype=np.float64)
    fk = np.asarray(od["tc_isFake"][i]) > 0
    nch = int(isch.sum())
    gs = slice(ptr, ptr + nch)
    ptr += nch
    mt = (~fake[gs]) & incut[gs] & (sole[gs] > 0)
    if not mt.any():
        continue
    ci = np.nonzero(isch)[0]
    vic = ci[mt]
    good = np.nonzero(~fk)[0]
    de = e[vic][:, None] - e[good][None, :]
    dp = np.abs(p[vic][:, None] - p[good][None, :])
    dp = np.minimum(dp, 2 * np.pi - dp)
    dr = np.sqrt(de * de + dp * dp)
    rr = q[vic][:, None] / np.maximum(q[good][None, :], 1e-6)
    hit = (dr < 0.05) & (rr < 2.0) & (rr > 0.5)
    np.fill_diagonal(hit[:, np.searchsorted(good, vic)] if False else np.zeros((1, 1)), 0)
    # exclude self-match: a sole-provider true TC is itself in `good`
    for r, t in enumerate(vic):
        j = np.searchsorted(good, t)
        if j < len(good) and good[j] == t:
            hit[r, j] = False
    tot_t += len(vic)
    hit_t += int(hit.any(1).sum())
print("  CONTROL: SOLE-PROVIDER TRUE chain TCs with a real neighbour at dR<0.05: %d / %d = %.4f"
      % (hit_t, tot_t, hit_t / max(tot_t, 1)))
