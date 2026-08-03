#ifndef RecoTracker_LSTCore_interface_ChainNodesSoA_h
#define RecoTracker_LSTCore_interface_ChainNodesSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  struct Params_ChainNode {
    // Frozen node-feature contract of the edge head (prototype/Features.h, kNodeFeat = 13):
    //  0 kappaSigned  1 log10R  2 tanLambda  3 chordEta  4 dphi01  5 dz01  6 dz12
    //  7 drt01  8 drt12  9 innermostLayer  10 nBarrel  11 nPS  12 fakeScoreT3
    // 13 + 13 + 14 edge floats == edgemlp::kInput == 40. Do not reorder.
    static constexpr int kFeatures = 13;
    using ArrayFxFeat = edm::StdArray<float, kFeatures>;
  };

  // Dense triplet-node view used by the chain-tracking graph (port map phase P2.0, stage K0/K1c).
  //
  // LST stores triplets per-lower-module with gaps (ObjectRanges::tripletModuleIndices gives the
  // start of each module slice, TripletsOccupancy::nTriplets gives how many of that slice were
  // actually filled). The chain graph needs a dense global node index instead, so K0 compacts the
  // module-segmented store: node n in [0, nNodes) maps to the sparse triplet tripletIndex()[n].
  //
  // The four item columns are the CSR payloads of the two ChainIncidence instances. Each of them
  // is a permutation of [0, nNodes): every node appears exactly once in every item array, because
  // every triplet has exactly one first MD, one last MD, one inner Segment and one outer Segment.
  //
  // The collection is allocated with exactly nNodes rows, so metadata().size() is the node count.
  //
  // The feature block is phase P2.1 (stage K3). It is stored rather than recomputed per edge
  // because every node is read by ~4 edges on average (E ~ 2 * nNodes, each edge touching two
  // nodes), so recomputing would triple the transcendental work for 2.9 MB of savings.
  // The four chord-angle columns are the per-node quantities the EDGE feature build needs
  // (prototype/Features.cc computeEdgeFeatures hoists exactly these out of the edge loop).
  //
  // stableId is the phase P2.5 determinism anchor. The dense node index, the sparse triplet index
  // it maps to, and every CSR slot derived from them are handed out by atomicAdd, so they permute
  // between two identical runs and between the CPU and CUDA backends. stableId is instead a mix of
  // the six hit ROWS of the node's three MDs (anchor and outer hit of md0, md1, md2). Hit rows are
  // the input order of the hit collection, which is fixed by the event data alone, so stableId is
  // identical run to run and backend to backend. Its only consumer is the weld tie-break.
  GENERATE_SOA_LAYOUT(ChainNodesSoALayout,
                      SOA_COLUMN(uint32_t, tripletIndex),  // dense node index -> sparse triplet index
                      SOA_COLUMN(uint32_t, mdT3OutItems),  // payload of ChainIncidence(MD).t3OutOffsets
                      SOA_COLUMN(uint32_t, mdT3InItems),   // payload of ChainIncidence(MD).t3InOffsets
                      SOA_COLUMN(uint32_t, lsT3OutItems),  // payload of ChainIncidence(LS).t3OutOffsets
                      SOA_COLUMN(uint32_t, lsT3InItems),   // payload of ChainIncidence(LS).t3InOffsets
                      SOA_COLUMN(uint32_t, stableId),      // K1c, run/backend-invariant node identity
                      SOA_COLUMN(Params_ChainNode::ArrayFxFeat, features),  // K3, frozen 13-float row
                      SOA_COLUMN(float, phiC01),  // atan2(c01y, c01x)
                      SOA_COLUMN(float, phiC12),  // atan2(c12y, c12x)
                      SOA_COLUMN(float, thetaC01),  // atan2(|c01_xy|, c01z)
                      SOA_COLUMN(float, thetaC12),  // atan2(|c12_xy|, c12z)
                      // P2.6c. The two DENSE incidence keys of this node's "in" side: the dense
                      // index of its last MD and of its outer Segment (ChainPrefixKeyModules in
                      // ChainGraph.h defines the dense numbering). These are exactly the keys the
                      // E1 and E2 junction-degree lookups need, and both of those lookups sit in
                      // per-EDGE loops (ChainEdges.h K5 and ChainGate.h K7a), where recovering the
                      // key from the raw index would cost two dependent SoA loads plus a module
                      // lookup on every edge. Stored once per node instead. Appended LAST so no
                      // pre-existing column's offset within the row moves.
                      SOA_COLUMN(uint32_t, mdKeyIn),
                      SOA_COLUMN(uint32_t, lsKeyIn))

  using ChainNodesSoA = ChainNodesSoALayout<>;
  using ChainNodes = ChainNodesSoA::View;
  using ChainNodesConst = ChainNodesSoA::ConstView;

}  // namespace lst

#endif
