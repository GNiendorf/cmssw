#ifndef PROTOTYPE_LABELS_H
#define PROTOTYPE_LABELS_H

// Track-level edge labels (plan 5c): an edge is TRUE iff BOTH T3s are MD-matched to the
// SAME sim track, where a T3 is MD-matched to sim s iff s appears in the md_simIdxAll list
// of at least 2 of its 3 MDs (md_simIdxAll stores >75%-matched sims per MD, i.e. both MD
// hits). This is deliberately the plan's definition, not t3_pMatched-based: duplicates of
// the same sim track label as true, object-level purity does not enter.
//
// v1 keeps labels binary (no ambiguity band at edge level); the 0.75-0.95 exclusion band
// applies to CHAIN labels later, not here.

#include <cstdint>
#include <vector>

#include "EventData.h"
#include "Stages.h"

struct EdgeLabels {
  std::vector<int8_t> label;  // 1 = true edge, 0 = fake
  std::vector<int> simIdx;    // matched sim row for true edges, -1 for fakes. Index
                              // space = md_simIdxAll's = FULL tracking-ntuple sim rows
                              // (VERIFIED against write_lst_ntuple.cc
                              // setMiniDoubletBranches and empirically; see Labels.cc).
                              // Accepted sims are the contiguous prefix of that space,
                              // so simIdx < ev.sim_pt.size() indexes ev.sim_* directly;
                              // larger values are pileup sims (kinematics stay -999).
  // Kinematics of the matched sim (from the LST ntuple sim block; -999 for fakes):
  std::vector<float> simPt, simEta, simVxy;
};

// Per-T3 matched-sim sets (>=2/3 MDs), computed once and reused by labelEdges.
// Sorted unique sim indices per T3.
struct T3SimSets {
  std::vector<std::vector<int>> sims;  // size nT3
};

void buildT3SimSets(const LSTEventData& ev, T3SimSets& out);
void labelEdges(const LSTEventData& ev, const ChainGraph& g, const T3SimSets& t3sims, EdgeLabels& out);

// Chain labels (M6 chain gate): label = 1 iff the intersection of ALL member T3 sim sets
// is non-empty, i.e. one sim track is MD-matched (>= 2/3 MDs) to EVERY member T3. By this
// definition every braid variant of a real track -- chains built from different MD/hit
// duplicates of the same sim -- is label 1: track-level truth, object-level purity and
// duplication do not enter (plan 5c; same principle as EdgeLabels).
// simIdx/simPt/simVxy follow the labelEdges convention: among ACCEPTED sims in the
// intersection pick the highest sim_pt; a pileup-only match keeps label 1 with
// simIdx = the smallest full-row index and kinematics -999.
struct ChainLabels {
  std::vector<int8_t> label;         // 1 = TRUE chain (see labelChains / labelChainsHarness)
  std::vector<int> simIdx;           // FULL tracking-ntuple sim row (see EdgeLabels), -1 if label 0
  std::vector<float> simPt, simVxy;  // accepted-sim kinematics, -999 otherwise
  // ---- M12 additions (populated only by labelChainsHarness) ----------------------------
  std::vector<int8_t> labelOld;  // the pre-M12 >=2/3-MD-intersection label, kept for the
                                 // flip matrix / ablation. Empty after plain labelChains().
  std::vector<float> matchFrac;  // best hit-level match fraction from the production
                                 // matcher (pmatched), -1 when no hits. Empty after
                                 // plain labelChains().
};

void labelChains(const LSTEventData& ev, const Chains& chains, const T3SimSets& t3sims, ChainLabels& out);

// -------------------------------------------------------------------------------------
// M12 LABEL RETARGET (plan 10.4c M10 "label bug", judge-specified fix).
//
// The labelChains() rule above is an OBJECT-level proxy: a chain is true when one sim is
// >=2/3-MD-matched to every member T3. M10 measured that 23.4% of accepted HARNESS-FAKE
// chain TCs carry that label (two T3s each keeping 2/3 MDs + a shared middle = 3/5 = 60%
// hit coverage), which floors the chain-slice FR of a perfect classifier at 0.1235.
//
// labelChainsHarness() replaces it with EXACTLY the rule the scoring harness applies to
// the assembled TC: run the production matcher (proto::matchedSimTrkIdxsAndFracs, the
// verbatim trkCore port already used by OutputWriter) over the chain's FULL hit list --
// per member MD, in K6 order, the anchor hit then the other hit, all Phase2OT, exactly
// k10AssembleChainTCs' list -- and call the chain TRUE iff some sim's hit fraction is
// STRICTLY > 0.75. Train and serve then agree on what "fake" means.
//
// simIdx/simPt/simVxy follow the labelChains convention: kinematics exist only for
// ACCEPTED sims (full row < ev.sim_pt.size()), so among the matched sims (already sorted
// by fraction desc) the first ACCEPTED one supplies them; a pileup-only match keeps
// label 1 with simIdx = the top-fraction full row and -999 kinematics.
// labelOld is filled from labelChains() on the same chains for the flip matrix.
void labelChainsHarness(
    const LSTEventData& ev, const TrkEventData& trk, const Chains& chains, const T3SimSets& t3sims, ChainLabels& out);

#endif
