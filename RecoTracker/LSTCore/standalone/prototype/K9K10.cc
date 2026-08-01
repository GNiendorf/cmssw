#include "Stages.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

// K9 + K10 (Stages.h contracts, plan section 3 K9/K10, 10.4/10.5).
//
// K9: score-ordered greedy hit-claim arbitration over chains. Serial stand-in for the
// packed-atomicMax kernel: on CPU a single best-first pass with a claimed-MD flag array
// is physics-equivalent and deterministic (order = score desc, chain index asc).
//
// K10: accepted chains -> OutTC-ready ChainTC records (type / pt / eta / phi / hit list).

namespace {

// One-time warning helper for missing pixel-consumption flags (fields are filled by the
// reader only when the input ntuple carries the t3_partOfPT* branches).
void warnNoPixFlags() {
  static bool warned = false;
  if (!warned) {
    std::fprintf(stderr,
                 "k9Arbitrate: dropPixelConsumed requested but t3_partOfPT5/PT3 not "
                 "filled for this event; no chains dropped\n");
    warned = true;
  }
}

}  // namespace

void k9Arbitrate(const LSTEventData& ev,
                 const Chains& chains,
                 const ArbitrationParams& params,
                 std::vector<int>& acceptedChains) {
  acceptedChains.clear();
  const int nChains = static_cast<int>(chains.score.size());
  const int nMD = static_cast<int>(ev.md_anchorHitIdx.size());
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());

  // Pixel-consumption flags are optional input (reader fills them only from ntuples that
  // carry the branches). Guard sizes so a missing fill degrades loudly, not silently UB.
  const bool havePixFlags = static_cast<int>(ev.t3_partOfPT5.size()) == nT3 &&
                            static_cast<int>(ev.t3_partOfPT3.size()) == nT3;
  if (params.dropPixelConsumed && !havePixFlags && nChains > 0)
    warnNoPixFlags();

  // 1) Candidate list: score gate + pixel-consumed drop (structural mimic of the baseline
  //    CrossCleanT5/pT3: those sim tracks are already delivered by the kept pixel TCs).
  std::vector<int> order;
  order.reserve(nChains);
  for (int c = 0; c < nChains; ++c) {
    if (chains.score[c] < params.thetaFor(chains.nLayers[c]))
      continue;
    if (params.dropPixelConsumed && havePixFlags) {
      bool consumed = false;
      for (int k = chains.offsets[c]; k < chains.offsets[c + 1] && !consumed; ++k) {
        const int t3 = chains.items[k];
        consumed = ev.t3_partOfPT5[t3] || ev.t3_partOfPT3[t3];
      }
      if (consumed)
        continue;
    }
    order.push_back(c);
  }

  // 2) Deterministic best-first order: score desc, chain index asc on ties.
  std::sort(order.begin(), order.end(), [&chains](int a, int b) {
    if (chains.score[a] != chains.score[b])
      return chains.score[a] > chains.score[b];
    return a < b;
  });

  // 3) Greedy accept: a chain survives iff the fraction of its MDs already claimed by
  //    better chains is <= maxClaimedFrac; survivors claim all their MDs.
  std::vector<char> claimed(nMD, 0);
  for (int c : order) {
    const int b = chains.mdOffsets[c];
    const int e = chains.mdOffsets[c + 1];
    const int total = e - b;
    int nClaimed = 0;
    for (int k = b; k < e; ++k)
      nClaimed += claimed[chains.mdItems[k]];
    // total >= 4 for any welded chain (2 T3s sharing an LS); guard div anyway.
    const float frac = (total > 0) ? static_cast<float>(nClaimed) / static_cast<float>(total) : 0.f;
    if (frac > params.maxClaimedFrac)
      continue;
    for (int k = b; k < e; ++k)
      claimed[chains.mdItems[k]] = 1;
    acceptedChains.push_back(c);
  }
}

void k10AssembleChainTCs(const LSTEventData& ev,
                         const Chains& chains,
                         const std::vector<int>& acceptedChains,
                         std::vector<ChainTC>& out) {
  out.clear();
  out.reserve(acceptedChains.size());
  std::vector<float> pts;  // scratch for the median

  for (int c : acceptedChains) {
    const int nL = chains.nLayers[c];
    if (nL < 4)
      continue;  // too short for a TC without pixel help

    ChainTC tc;
    tc.type = (nL >= 5) ? 4 : 9;  // T5-class : T4-class (LSTObjType convention)

    // pt = median of the member T3 pts. Even count: LOWER median (sorted element
    // (n-1)/2) — deterministic, always an actual member value, and pt only serves as a
    // histogram coordinate downstream, so the half-gap shift vs the midpoint average is
    // irrelevant at this stage.
    const int ib = chains.offsets[c];
    const int ie = chains.offsets[c + 1];
    pts.clear();
    for (int k = ib; k < ie; ++k)
      pts.push_back(ev.t3_pt[chains.items[k]]);
    const size_t mid = (pts.size() - 1) / 2;
    std::nth_element(pts.begin(), pts.begin() + mid, pts.end());
    tc.pt = pts[mid];

    // eta/phi from the innermost member (chain items are innermost-first by contract).
    const int t3Inner = chains.items[ib];
    tc.eta = ev.t3_eta[t3Inner];
    tc.phi = ev.t3_phi[t3Inner];

    // Hit list: per MD (innermost-first, deduped by K6) anchor hit then other hit.
    // All ph2 rows: chains are pure outer-tracker objects, pLS pseudo-MDs can never be
    // T3 members (asserted defensively).
    const int mb = chains.mdOffsets[c];
    const int me = chains.mdOffsets[c + 1];
    for (int k = mb; k < me; ++k) {
      const int md = chains.mdItems[k];
      assert(!ev.md_isPLS[md] && "chain member MD is a pLS pseudo-MD");
      tc.hitIdxs.push_back(static_cast<unsigned int>(ev.md_anchorHitIdx[md]));
      tc.hitIdxs.push_back(static_cast<unsigned int>(ev.md_otherHitIdx[md]));
    }
    tc.nhitOT = 2 * (me - mb);

    out.push_back(std::move(tc));
  }
}
