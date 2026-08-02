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

// B4: the claim-tolerance predicate. maxClaimedItems < 0 reproduces the legacy
// fractional test EXACTLY (same float comparison, same operand order).
inline bool claimOk(const ArbitrationParams& params, int nClaimed, float frac) {
  if (params.maxClaimedItems < 0)
    return !(frac > params.maxClaimedFrac);
  if (params.claimCountExclusive)
    return nClaimed <= params.maxClaimedItems;
  return (nClaimed <= params.maxClaimedItems) || !(frac > params.maxClaimedFrac);
}

}  // namespace

void k9Arbitrate(const LSTEventData& ev,
                 const Chains& chains,
                 const ArbitrationParams& params,
                 std::vector<int>& acceptedChains,
                 const std::vector<char>* bypassPT5Drop) {
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
    if (chains.score[c] < params.thetaForChain(c, chains.nLayers[c]))
      continue;
    if (params.dropPixelConsumed && havePixFlags) {
      // K8 attach bypass (M7): an attached chain skips the partOfPT5 half of the drop
      // (it replaces the baseline pT5 delivery) but still respects partOfPT3.
      const bool bypassPT5 = bypassPT5Drop != nullptr && c < static_cast<int>(bypassPT5Drop->size()) &&
                             (*bypassPT5Drop)[c] != 0;
      bool consumed = false;
      for (int k = chains.offsets[c]; k < chains.offsets[c + 1] && !consumed; ++k) {
        const int t3 = chains.items[k];
        consumed = (ev.t3_partOfPT5[t3] && !bypassPT5) || ev.t3_partOfPT3[t3];
      }
      if (consumed)
        continue;
    }
    order.push_back(c);
  }

  // 2) Deterministic best-first order: key desc, chain index asc on ties. The key is
  //    chains.score unless the caller supplied a separate ordering key (A8 -B: legacy
  //    score minus a fake-suspicion penalty; thresholds still cut on chains.score).
  const std::vector<float>* keyVec = &chains.score;
  if (params.orderKey != nullptr && params.orderKey->size() == chains.score.size())
    keyVec = params.orderKey;
  const std::vector<float>& key = *keyVec;
  std::sort(order.begin(), order.end(), [&key](int a, int b) {
    if (key[a] != key[b])
      return key[a] > key[b];
    return a < b;
  });

  // 3) Greedy accept: a chain survives iff the fraction of its MDs already claimed by
  //    better chains is <= maxClaimedFrac; survivors claim all their MDs.
  //    owner[md] = index of the accepted chain that claimed md, -1 = free (the A8 braid
  //    rule needs the owner identity, not just the claimed bit; owner >= 0 is exactly the
  //    old claimed flag).
  //    -H 1 switches the claim universe from MDs to HITS: a per-chain hit list (anchor +
  //    other hit of every member MD, deduped) is built once and the identical greedy runs
  //    on it. MD-level and hit-level differ exactly where duplicate MD objects sit on the
  //    same hits -- the genuine chain-chain braid population.
  // B1 (-PU): pixel owners = the kept baseline pixel TCs' OT hit lists. Empty/nullptr =
  // legacy (every expression below collapses to the pre-B1 code path bit-exactly).
  const std::vector<std::vector<int>>* pixOwn =
      (params.preClaimMode > 0) ? params.preClaimOwners : nullptr;
  const int nPix = (pixOwn != nullptr) ? static_cast<int>(pixOwn->size()) : 0;

  std::vector<int> chainKeyOff, chainKeyItems;  // -H 1 only: per-chain deduped hit list
  int nKeyUniverse = nMD;
  if (params.hitLevelClaim) {
    int maxHit = -1;
    for (int i = 0; i < nMD; ++i)
      maxHit = std::max(maxHit, std::max(ev.md_anchorHitIdx[i], ev.md_otherHitIdx[i]));
    for (int p = 0; p < nPix; ++p)
      for (int h : (*pixOwn)[p])
        maxHit = std::max(maxHit, h);
    nKeyUniverse = maxHit + 1;
    chainKeyOff.assign(nChains + 1, 0);
    chainKeyItems.reserve(static_cast<std::size_t>(chains.mdItems.size()) * 2);
    std::vector<int> scratch;
    for (int c = 0; c < nChains; ++c) {
      scratch.clear();
      for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k) {
        const int md = chains.mdItems[k];
        scratch.push_back(ev.md_anchorHitIdx[md]);
        scratch.push_back(ev.md_otherHitIdx[md]);
      }
      std::sort(scratch.begin(), scratch.end());
      scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());
      chainKeyItems.insert(chainKeyItems.end(), scratch.begin(), scratch.end());
      chainKeyOff[c + 1] = static_cast<int>(chainKeyItems.size());
    }
  }
  const std::vector<int>& keyOff = params.hitLevelClaim ? chainKeyOff : chains.mdOffsets;
  const std::vector<int>& keyItems = params.hitLevelClaim ? chainKeyItems : chains.mdItems;

  // owner[] encoding: -1 = free, >= 0 = accepted chain index, <= -2 = pixel owner
  // p == -(owner) - 2 (B1). "claimed" is therefore owner != -1, not owner >= 0.
  std::vector<int> owner(nKeyUniverse, -1);

  // B1: pixel owners claim first, unconditionally, in list order. ownedPix[p] counts the
  // slots p actually holds (ties among pixel owners go to the earlier row), which is the
  // exact analogue of a chain's keyOff span: an accepted chain owns ALL of its slots and
  // never loses one, so the braid denominator stays "slots the owner holds".
  std::vector<int> ownedPix;
  if (nPix > 0) {
    ownedPix.assign(nPix, 0);
    if (params.hitLevelClaim) {
      for (int p = 0; p < nPix; ++p) {
        for (int h : (*pixOwn)[p]) {
          if (h < 0 || h >= nKeyUniverse || owner[h] != -1)
            continue;
          owner[h] = -(p + 2);
          ++ownedPix[p];
        }
      }
    } else {
      // MD-level: an MD belongs to pixel owner p iff BOTH of its hits are p's hits.
      // hitOwner is a scratch map over the ph2 rows the MDs can reference.
      int maxHit = -1;
      for (int i = 0; i < nMD; ++i)
        maxHit = std::max(maxHit, std::max(ev.md_anchorHitIdx[i], ev.md_otherHitIdx[i]));
      std::vector<int> hitOwner(maxHit + 1, -1);
      for (int p = 0; p < nPix; ++p)
        for (int h : (*pixOwn)[p])
          if (h >= 0 && h <= maxHit && hitOwner[h] == -1)
            hitOwner[h] = p;
      for (int m = 0; m < nMD; ++m) {
        const int ha = ev.md_anchorHitIdx[m];
        const int hb = ev.md_otherHitIdx[m];
        if (ha < 0 || hb < 0 || ha > maxHit || hb > maxHit)
          continue;
        const int pa = hitOwner[ha];
        if (pa < 0 || hitOwner[hb] < 0)
          continue;
        owner[m] = -(pa + 2);
        ++ownedPix[pa];
      }
    }
    if (params.stats != nullptr)
      for (int p = 0; p < nPix; ++p)
        params.stats->preClaimedSlots += ownedPix[p];
  }
  // Braid participation of pixel owners is opt-in (-PU 2); at -PU 1 they only supply
  // claimed slots to the maxClaimedFrac test.
  const bool pixBraid = nPix > 0 && params.preClaimMode >= 2;

  const bool braid = params.braidFrac > 0.f;
  // Owner-overlap tally, allocated once: cnt[slot] valid only for slot in touched.
  // Slot of owner o: o >= 0 -> o (chain); o <= -2 -> nChains + (-o - 2) (pixel owner).
  std::vector<int> cnt;
  std::vector<int> touched;
  if (braid) {
    cnt.assign(static_cast<std::size_t>(nChains) + static_cast<std::size_t>(nPix), 0);
    touched.reserve(32);
  }
  for (int c : order) {
    const int b = keyOff[c];
    const int e = keyOff[c + 1];
    const int total = e - b;
    int nClaimed = 0;
    int nPixClaimed = 0;
    for (int k = b; k < e; ++k) {
      const int o = owner[keyItems[k]];
      nClaimed += (o != -1) ? 1 : 0;
      nPixClaimed += (o <= -2) ? 1 : 0;
    }
    // total >= 4 for any welded chain (2 T3s sharing an LS); guard div anyway.
    const float frac = (total > 0) ? static_cast<float>(nClaimed) / static_cast<float>(total) : 0.f;
    // COMPOSED (B1 x B4): the pixel-unified claim count feeds the B4 tolerance predicate.
    // With -FC off this is bit-identical to B1's `frac > maxClaimedFrac` test.
    if (!claimOk(params, nClaimed, frac)) {
      if (params.stats != nullptr && nPixClaimed > 0)
        ++params.stats->killedByPixFrac;
      continue;
    }
    if (braid && nClaimed > 0) {
      // Owner-relative overlap: does this candidate swallow >= braidFrac of any single
      // already-accepted owner? If so it is a welder-braid sibling, not a new track.
      touched.clear();
      for (int k = b; k < e; ++k) {
        const int o = owner[keyItems[k]];
        if (o == -1)
          continue;
        if (o <= -2 && !pixBraid)
          continue;
        const int slot = (o >= 0) ? o : (nChains + (-o - 2));
        if (cnt[slot] == 0)
          touched.push_back(slot);
        ++cnt[slot];
      }
      bool killed = false, killedByPix = false;
      for (int slot : touched) {
        const int oTot = (slot < nChains) ? (keyOff[slot + 1] - keyOff[slot]) : ownedPix[slot - nChains];
        if (oTot > 0 && static_cast<float>(cnt[slot]) >= params.braidFrac * static_cast<float>(oTot)) {
          killed = true;
          killedByPix = killedByPix || (slot >= nChains);
        }
        cnt[slot] = 0;
      }
      if (killed) {
        if (params.stats != nullptr && killedByPix)
          ++params.stats->killedByPixBraid;
        continue;
      }
    }
    for (int k = b; k < e; ++k)
      owner[keyItems[k]] = c;
    acceptedChains.push_back(c);
  }
}

void k9ArbitrateTwoPass(const LSTEventData& ev,
                        const Chains& chains,
                        const ArbitrationParams& params,
                        const std::vector<int>& attachedPls,
                        std::vector<int>& acceptedPass1,
                        std::vector<int>& acceptedPass2) {
  // Pass 1: the legacy call itself (no bypass) -- -A 0 identity by construction.
  k9Arbitrate(ev, chains, params, acceptedPass1, nullptr);
  acceptedPass2.clear();

  const int nChains = static_cast<int>(chains.score.size());
  const int nMD = static_cast<int>(ev.md_anchorHitIdx.size());
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  const bool havePixFlags = static_cast<int>(ev.t3_partOfPT5.size()) == nT3 &&
                            static_cast<int>(ev.t3_partOfPT3.size()) == nT3;
  // Without the pixel drop nothing was pixdropped, so pass 2 has no candidates.
  if (!params.dropPixelConsumed || !havePixFlags)
    return;

  // Rebuild the post-pass-1 claimed-MD map (claiming all accepted chains' MDs gives
  // exactly the map k9Arbitrate ended with).
  std::vector<char> claimed(nMD, 0);
  for (int c : acceptedPass1)
    for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k)
      claimed[chains.mdItems[k]] = 1;

  // Pass-2 candidates: attached, theta-passing, dropped in pass 1 by partOfPT5 ONLY.
  std::vector<int> order;
  for (int c = 0; c < nChains; ++c) {
    if (c >= static_cast<int>(attachedPls.size()) || attachedPls[c] < 0)
      continue;
    if (chains.score[c] < params.thetaForChain(c, chains.nLayers[c]))
      continue;
    bool hasPT5 = false, hasPT3 = false;
    for (int k = chains.offsets[c]; k < chains.offsets[c + 1] && !hasPT3; ++k) {
      const int t3 = chains.items[k];
      hasPT5 = hasPT5 || ev.t3_partOfPT5[t3];
      hasPT3 = ev.t3_partOfPT3[t3];
    }
    if (!hasPT5 || hasPT3)
      continue;  // not pixdropped, or dropped by the still-respected partOfPT3 half
    order.push_back(c);
  }

  // Same deterministic greedy as pass 1, continuing on the pass-1 claim map.
  std::sort(order.begin(), order.end(), [&chains](int a, int b) {
    if (chains.score[a] != chains.score[b])
      return chains.score[a] > chains.score[b];
    return a < b;
  });
  for (int c : order) {
    const int b = chains.mdOffsets[c];
    const int e = chains.mdOffsets[c + 1];
    const int total = e - b;
    int nClaimed = 0;
    for (int k = b; k < e; ++k)
      nClaimed += claimed[chains.mdItems[k]];
    const float frac = (total > 0) ? static_cast<float>(nClaimed) / static_cast<float>(total) : 0.f;
    if (frac > params.maxClaimedFrac)
      continue;
    for (int k = b; k < e; ++k)
      claimed[chains.mdItems[k]] = 1;
    acceptedPass2.push_back(c);
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
