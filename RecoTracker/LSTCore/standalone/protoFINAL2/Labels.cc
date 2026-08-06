// Track-level edge labels (plan 5c). See Labels.h for the definition.
//
// Index space of md_simIdxAll (VERIFIED, task M2):
// write_lst_ntuple.cc setMiniDoubletBranches stores the simidx list returned by
// matchedSimTrkIdxsAndFracs(), whose values are trk_simhit_simTrkIdx entries, i.e.
// FULL tracking-ntuple sim rows -- the writer uses them to index its
// n_total_simtrk-sized sim_mdIdxAll bookkeeping vectors, and only >matchfrac (0.75)
// entries are kept (for a 2-hit MD that means both hits matched, frac == 1).
// The LST ntuple sim_* block is the accepted subset (bunchCrossing==0 && event==0),
// which the tracking ntuple stores as a CONTIGUOUS PREFIX of the full sim list (the
// writer relies on this when truncating sim_mdIdxAll to n_accepted_simtrk rows).
// Verified empirically on smoke_test.root + trackingNtuple_ttbar_PU200.root:
//   - accepted rows are exactly rows [0, n_accepted) of the trk sim block, and the
//     LST sim_pt is a bit-exact copy of that prefix;
//   - md_simIdxAll maxima track the FULL sim count (~32k-41k vs ~700-1400 accepted)
//     with zero out-of-bounds values across all events;
//   - md_eta vs full-row trk sim_eta agreement is the same for prefix rows and
//     pileup rows (~57-60%), confirming the full-row read positively.
// Therefore: a value s < ev.sim_pt.size() indexes ev.sim_* directly; s >= that size
// is a pileup / out-of-time sim with no kinematics in the LST ntuple.

#include "Labels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Matching.h"

void buildT3SimSets(const LSTEventData& ev, T3SimSets& out) {
  const std::size_t nMD = ev.md_simIdxAll.size();
  const std::size_t nT3 = ev.t3_lsIdx0.size();

  // md_simIdxAll can list the same sim more than once for one MD (the writer pushes
  // one entry per hit-assignment permutation), so dedupe per MD first: the >=2-of-3
  // vote must count MDs, not raw list entries.
  std::vector<std::vector<int>> mdUniq(nMD);
  for (std::size_t m = 0; m < nMD; ++m) {
    mdUniq[m].assign(ev.md_simIdxAll[m].begin(), ev.md_simIdxAll[m].end());
    std::sort(mdUniq[m].begin(), mdUniq[m].end());
    mdUniq[m].erase(std::unique(mdUniq[m].begin(), mdUniq[m].end()), mdUniq[m].end());
  }

  out.sims.assign(nT3, {});
  std::vector<int> merged;
  for (std::size_t t = 0; t < nT3; ++t) {
    const int mds[3] = {ev.t3_md0[t], ev.t3_md1[t], ev.t3_md2[t]};
    merged.clear();
    for (int m : mds)
      merged.insert(merged.end(), mdUniq[m].begin(), mdUniq[m].end());
    std::sort(merged.begin(), merged.end());
    // After per-MD dedup, a sim in >= 2 of the 3 MD lists appears >= 2 times here.
    auto& sims = out.sims[t];
    for (std::size_t i = 0; i + 1 < merged.size();) {
      if (merged[i] == merged[i + 1]) {
        sims.push_back(merged[i]);
        std::size_t j = i + 1;
        while (j < merged.size() && merged[j] == merged[i])
          ++j;
        i = j;
      } else {
        ++i;
      }
    }
  }
}

void labelEdges(const LSTEventData& ev, const ChainGraph& g, const T3SimSets& t3sims, EdgeLabels& out) {
  const std::size_t nE = g.edges.size();
  out.label.assign(nE, 0);
  out.simIdx.assign(nE, -1);
  out.simPt.assign(nE, -999.f);
  out.simEta.assign(nE, -999.f);
  out.simVxy.assign(nE, -999.f);

  const int nAccepted = static_cast<int>(ev.sim_pt.size());
  std::vector<int> common;
  for (std::size_t e = 0; e < nE; ++e) {
    const auto& a = t3sims.sims[g.edges[e].inner];
    const auto& b = t3sims.sims[g.edges[e].outer];
    common.clear();
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
    if (common.empty())
      continue;
    out.label[e] = 1;
    // Kinematics exist only for accepted sims (rows < nAccepted, see note above);
    // among those pick the highest sim_pt. A pileup-only match is still a true edge
    // (both T3s follow the same real particle) but keeps -999 kinematics.
    int best = -1;
    float bestPt = -1.f;
    for (int s : common) {
      if (s < nAccepted && ev.sim_pt[s] > bestPt) {
        best = s;
        bestPt = ev.sim_pt[s];
      }
    }
    if (best >= 0) {
      out.simIdx[e] = best;
      out.simPt[e] = ev.sim_pt[best];
      out.simEta[e] = ev.sim_eta[best];
      out.simVxy[e] = std::sqrt(ev.sim_vx[best] * ev.sim_vx[best] + ev.sim_vy[best] * ev.sim_vy[best]);
    } else {
      out.simIdx[e] = common.front();  // pileup full-row index; no ev.sim_* kinematics
    }
  }
}

void labelChains(const LSTEventData& ev, const Chains& chains, const T3SimSets& t3sims, ChainLabels& out) {
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  out.label.assign(nChains, 0);
  out.simIdx.assign(nChains, -1);
  out.simPt.assign(nChains, -999.f);
  out.simVxy.assign(nChains, -999.f);

  const int nAccepted = static_cast<int>(ev.sim_pt.size());
  std::vector<int> common, tmp;
  for (int c = 0; c < nChains; ++c) {
    const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
    if (ib >= ie)
      continue;
    // Running intersection over the (sorted unique) member sim sets; early out on empty.
    common = t3sims.sims[chains.items[ib]];
    for (int k = ib + 1; k < ie && !common.empty(); ++k) {
      const auto& s = t3sims.sims[chains.items[k]];
      tmp.clear();
      std::set_intersection(common.begin(), common.end(), s.begin(), s.end(), std::back_inserter(tmp));
      common.swap(tmp);
    }
    if (common.empty())
      continue;
    out.label[c] = 1;
    int best = -1;
    float bestPt = -1.f;
    for (int s : common) {
      if (s < nAccepted && ev.sim_pt[s] > bestPt) {
        best = s;
        bestPt = ev.sim_pt[s];
      }
    }
    if (best >= 0) {
      out.simIdx[c] = best;
      out.simPt[c] = ev.sim_pt[best];
      out.simVxy[c] = std::sqrt(ev.sim_vx[best] * ev.sim_vx[best] + ev.sim_vy[best] * ev.sim_vy[best]);
    } else {
      out.simIdx[c] = common.front();  // pileup full-row index; kinematics stay -999
    }
  }
}

// -------------------------------------------------------------------------------------
// M12 label retarget: harness coverage rule (see Labels.h for the rationale).

void labelChainsHarness(
    const LSTEventData& ev, const TrkEventData& trk, const Chains& chains, const T3SimSets& t3sims, ChainLabels& out) {
  // Old rule first: it fills label/simIdx/simPt/simVxy, which we then move to labelOld
  // and overwrite with the harness result.
  labelChains(ev, chains, t3sims, out);
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  out.labelOld.assign(out.label.begin(), out.label.end());
  out.matchFrac.assign(nChains, -1.f);

  const int nAccepted = static_cast<int>(ev.sim_pt.size());
  std::vector<unsigned int> hitIdxs;
  std::vector<proto::HitType> hitTypes;
  for (int c = 0; c < nChains; ++c) {
    // Hit list == k10AssembleChainTCs': per member MD in K6 order, anchor then other,
    // all Phase2OT (chains are pure outer-tracker objects).
    const int mb = chains.mdOffsets[c];
    const int me = chains.mdOffsets[c + 1];
    hitIdxs.clear();
    hitTypes.clear();
    for (int k = mb; k < me; ++k) {
      const int md = chains.mdItems[k];
      hitIdxs.push_back(static_cast<unsigned int>(ev.md_anchorHitIdx[md]));
      hitIdxs.push_back(static_cast<unsigned int>(ev.md_otherHitIdx[md]));
      hitTypes.push_back(proto::HitType::Phase2OT);
      hitTypes.push_back(proto::HitType::Phase2OT);
    }
    out.label[c] = 0;
    out.simIdx[c] = -1;
    out.simPt[c] = -999.f;
    out.simVxy[c] = -999.f;
    if (hitIdxs.empty())
      continue;

    float pmatched = 0.f;
    auto [simidx, simfrac] = proto::matchedSimTrkIdxsAndFracs(
        hitIdxs, hitTypes, trk.simhit_simTrkIdx, trk.ph2_simHitIdx, trk.pix_simHitIdx, false, 0.75f, &pmatched);
    out.matchFrac[c] = pmatched;
    if (simidx.empty())
      continue;  // no sim above 0.75 -> harness FAKE

    out.label[c] = 1;
    out.simIdx[c] = simidx.front();  // highest fraction (matcher sorts desc)
    // Kinematics from the first ACCEPTED sim among the matches (pileup rows have none).
    for (std::size_t i = 0; i < simidx.size(); ++i) {
      const int s = simidx[i];
      if (s >= 0 && s < nAccepted) {
        out.simIdx[c] = s;
        out.simPt[c] = ev.sim_pt[s];
        out.simVxy[c] = std::sqrt(ev.sim_vx[s] * ev.sim_vx[s] + ev.sim_vy[s] * ev.sim_vy[s]);
        break;
      }
    }
  }
}
