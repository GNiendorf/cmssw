#include "AttachDelivery.h"

#include <cmath>
#include <limits>
#include <unordered_map>

#include "AttachInference.h"
#include "PixelAttachPairs.h"

namespace {

  constexpr float kNegInf = -std::numeric_limits<float>::infinity();

  // Shared contention core. `best*` are per-TARGET-position picks already thresholded;
  // this resolves the ONE-pLS-ONE-OWNER rule: higher logit wins, tie keeps the earlier
  // target position (deterministic, identical convention to k8AttachPixels).
  void resolveContention(std::vector<int>& tgtPls, std::vector<float>& tgtLogit) {
    std::unordered_map<int, int> owner;  // pLS row -> target position currently holding it
    owner.reserve(tgtPls.size() * 2 + 1);
    for (int pos = 0; pos < static_cast<int>(tgtPls.size()); ++pos) {
      const int p = tgtPls[pos];
      if (p < 0)
        continue;
      auto it = owner.find(p);
      if (it == owner.end()) {
        owner.emplace(p, pos);
        continue;
      }
      const int prev = it->second;
      if (tgtLogit[pos] > tgtLogit[prev]) {
        tgtPls[prev] = -1;
        tgtLogit[prev] = kNegInf;
        it->second = pos;
      } else {
        tgtPls[pos] = -1;
        tgtLogit[pos] = kNegInf;
      }
    }
  }

}  // namespace

void gaInit(const LSTEventData& ev, const Chains& chains, GeneralAttach& out) {
  const int nChains = static_cast<int>(chains.score.size());
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  const int nPls = static_cast<int>(ev.pLS_pt.size());
  out.chainPls.assign(nChains, -1);
  out.chainLogit.assign(nChains, kNegInf);
  out.t3Pls.assign(nT3, -1);
  out.t3Logit.assign(nT3, kNegInf);
  out.plsOwned.assign(nPls, 0);
  out.plsBestChainLogit.assign(nPls, kNegInf);
  out.plsBestT3Logit.assign(nPls, kNegInf);
  out.nPairs = out.nScored = out.nChainAttached = out.nT3Attached = 0;
  out.pairLog.clear();  // instrument state; recordPairs is set by the caller AFTER gaInit
}

void gaStageChains(const LSTEventData& ev,
                   const Chains& chains,
                   const std::vector<int>& targetChains,
                   const ChainFeatures& cf,
                   const std::vector<float>& chainGateLogits,
                   const GeneralAttachParams& params,
                   GeneralAttach& io) {
  const int nTgt = static_cast<int>(targetChains.size());
  if (nTgt == 0)
    return;

  std::vector<AttachPair> pairs;
  k8EnumeratePrefilteredPairs(ev, chains, targetChains, cf, chainGateLogits, params.pref, pairs);
  io.nPairs += static_cast<long long>(pairs.size());

  std::vector<int> tgtPls(nTgt, -1);
  std::vector<float> tgtLogit(nTgt, kNegInf);
  // B02: per-eta-band acceptance margin (see GeneralAttachParams). Unset bins follow the
  // barrel value, so an untouched command line takes the identical branch every pair.
  const float thA0 = params.pref.thetaAttach;
  const float thAT = (params.thetaAttachT < 1e8f) ? params.thetaAttachT : thA0;
  const float thAE = (params.thetaAttachE < 1e8f) ? params.thetaAttachE : thA0;
  const int nPlsEta = static_cast<int>(ev.pLS_eta.size());
  for (const AttachPair& pr : pairs) {
    const float lo = attachLogit(pr.f);
    ++io.nScored;
    // Instrument only (see GeneralAttach::recordPairs): the pair as the decision saw it.
    if (io.recordPairs)
      io.pairLog.push_back({static_cast<int8_t>(kAttachTargetChain), targetChains[pr.chainPos], pr.plsRow, lo});
    if (params.writeBestLogit && pr.plsRow >= 0 && pr.plsRow < static_cast<int>(io.plsBestChainLogit.size()) &&
        lo > io.plsBestChainLogit[pr.plsRow])
      io.plsBestChainLogit[pr.plsRow] = lo;
    // The band lookup runs ALWAYS (not only when the bins differ) so that the default
    // command line exercises this exact code and the no-op gate is a real gate.
    float thr = thA0;
    if (pr.plsRow >= 0 && pr.plsRow < nPlsEta) {
      const float ae = std::fabs(ev.pLS_eta[pr.plsRow]);
      thr = (ae < 1.1f) ? thA0 : ((ae < 1.7f) ? thAT : thAE);
    }
    if (lo < thr)
      continue;
    // B02 stage A2: pLS already owned by stage A1 are out of scope entirely.
    if (params.skipOwnedPls && pr.plsRow >= 0 && pr.plsRow < static_cast<int>(io.plsOwned.size()) &&
        io.plsOwned[pr.plsRow])
      continue;
    if (tgtPls[pr.chainPos] < 0 || lo > tgtLogit[pr.chainPos]) {
      tgtPls[pr.chainPos] = pr.plsRow;
      tgtLogit[pr.chainPos] = lo;
    }
  }

  resolveContention(tgtPls, tgtLogit);

  for (int pos = 0; pos < nTgt; ++pos) {
    if (tgtPls[pos] < 0)
      continue;
    const int c = targetChains[pos];
    io.chainPls[c] = tgtPls[pos];
    io.chainLogit[c] = tgtLogit[pos];
    io.plsOwned[tgtPls[pos]] = 1;
    ++io.nChainAttached;
  }
}

void gaStageT3(const LSTEventData& ev,
               const Chains& chains,
               const std::vector<int>& acceptedChains,
               const ChainFeatures& cf,
               const std::vector<float>& chainGateLogits,
               const GeneralAttachParams& params,
               GeneralAttach& io) {
  std::vector<char> bareMask;
  k8BuildBareT3Mask(ev, chains, acceptedChains, bareMask);

  // A11 -T3F: target admission on the upstream t3dnn fake score (see AttachDelivery.h).
  // Applied to the MASK, so the cut targets never reach the candidate finder, never get
  // scored, and never write plsBestT3Logit -- one place, no other coupling.
  if (params.t3FakeMax < 1e8f) {
    const int nMask = static_cast<int>(bareMask.size());
    const int nScore = static_cast<int>(ev.t3_fakeScore.size());
    for (int t = 0; t < nMask; ++t) {
      if (!bareMask[t])
        continue;
      if (t >= nScore || !(ev.t3_fakeScore[t] <= params.t3FakeMax))
        bareMask[t] = 0;
    }
  }

  // Chain-target list intentionally EMPTY: stage A already resolved that universe and
  // its decisions are final (see the AttachDelivery.h ordering note).
  static const std::vector<int> kNoChains;
  std::vector<AttachPair> pairs;
  k8EnumeratePrefilteredPairsGeneral(ev, chains, kNoChains, cf, chainGateLogits, bareMask, params.pref, pairs);
  io.nPairs += static_cast<long long>(pairs.size());

  // Per bare-T3 target: best pLS that stage A left free, above the per-class margin.
  // Keyed by t3 row directly (targets are unique T3 rows), so no position map is needed.
  for (const AttachPair& pr : pairs) {
    const float lo = attachLogit(pr.f);
    ++io.nScored;
    if (io.recordPairs)
      io.pairLog.push_back({static_cast<int8_t>(kAttachTargetT3), pr.t3Row, pr.plsRow, lo});
    const int p = pr.plsRow;
    if (p < 0 || p >= static_cast<int>(io.plsOwned.size()))
      continue;
    if (lo > io.plsBestT3Logit[p])
      io.plsBestT3Logit[p] = lo;
    if (io.plsOwned[p])
      continue;  // stage A owns it: one pLS, one owner, across all target types
    if (lo < params.thetaAttachT3)
      continue;
    const int t = pr.t3Row;
    if (t < 0 || t >= static_cast<int>(io.t3Pls.size()))
      continue;
    if (io.t3Pls[t] < 0 || lo > io.t3Logit[t]) {
      io.t3Pls[t] = p;
      io.t3Logit[t] = lo;
    }
  }

  // pLS contention among the T3 targets. Compact to the bidding targets first so the
  // shared resolver's "earlier position wins the tie" is "lower T3 row wins".
  std::vector<int> bidT3;
  for (int t = 0; t < static_cast<int>(io.t3Pls.size()); ++t)
    if (io.t3Pls[t] >= 0)
      bidT3.push_back(t);
  std::vector<int> tgtPls(bidT3.size());
  std::vector<float> tgtLogit(bidT3.size());
  for (std::size_t i = 0; i < bidT3.size(); ++i) {
    tgtPls[i] = io.t3Pls[bidT3[i]];
    tgtLogit[i] = io.t3Logit[bidT3[i]];
  }
  resolveContention(tgtPls, tgtLogit);
  for (std::size_t i = 0; i < bidT3.size(); ++i) {
    const int t = bidT3[i];
    io.t3Pls[t] = tgtPls[i];
    io.t3Logit[t] = tgtLogit[i];
    if (tgtPls[i] >= 0) {
      io.plsOwned[tgtPls[i]] = 1;
      ++io.nT3Attached;
    }
  }
}
