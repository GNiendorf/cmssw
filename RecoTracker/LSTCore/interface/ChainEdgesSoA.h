#ifndef RecoTracker_LSTCore_interface_ChainEdgesSoA_h
#define RecoTracker_LSTCore_interface_ChainEdgesSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  // Chain-tracking edge list (port map phase P2.1, stages K2 and K5).
  //
  // The collection is allocated with exactly nE1Exact + nE2Exact rows, where the two counts come
  // from the K1b degree arithmetic sum_key degIn(key) * degOut(key). Rows [0, nE1Exact) are the
  // MD-keyed E1 family (the T5 relation, inner.md2 == outer.md0) and rows
  // [nE1Exact, nE1Exact + nE2Exact) are the Segment-keyed E2 family (the T4 relation,
  // inner.ls1 == outer.ls0). Every row is written by exactly one K2 thread at its own index, so
  // there is no compaction atomic and no capped reservation anywhere in the pipeline.
  //
  // Rows that the reference implementation (prototype/Stages.cc k2BuildEdges) does not emit are
  // kept as holes with type == 0 instead of being compacted away:
  //   - inner == outer (a triplet welding to itself; needs md2 == md0 within one T3)
  //   - an E2 pair that is already an E1 pair (needs md2(inner) == md0(outer) == md1(inner))
  // Both are structurally impossible for well-formed triplets and neither has ever been observed;
  // the tests are kept because they are O(1) and the sentinel keeps K2 index-decoded.
  GENERATE_SOA_LAYOUT(ChainEdgesSoALayout,
                      SOA_COLUMN(uint32_t, inner),  // dense chain-node index of the inner triplet
                      SOA_COLUMN(uint32_t, outer),  // dense chain-node index of the outer triplet
                      SOA_COLUMN(uint8_t, type),    // 0 = hole, 1 = E1 (shared MD), 2 = E2 (shared LS)
                      SOA_COLUMN(float, logOdds),   // K5 edge-MLP logit; the quantity K6 will sum
                      SOA_COLUMN(uint32_t, tie),    // K2 stable weld tie-break, see ChainWeld.h
                      SOA_SCALAR(uint32_t, nE1Exact),
                      SOA_SCALAR(uint32_t, nE2Exact))

  using ChainEdgesSoA = ChainEdgesSoALayout<>;
  using ChainEdges = ChainEdgesSoA::View;
  using ChainEdgesConst = ChainEdgesSoA::ConstView;

  // Frozen edge-feature contract of the edge head (prototype/Features.h, kEdgeFeat = 14):
  //  0 etype  1 dKappa  2 dKappaRel  3 chargeAgree  4 dTanLambda  5 kinkPhi  6 kinkTheta
  //  7 centerDist  8 centerDistRel  9 sharedLayer  10 sharedIsPS  11 sharedIsBarrel
  // 12 degIn  13 degOut
  // These are NOT a column of this layout: they are built in registers inside the K5 kernel and
  // consumed immediately (port map section 2.2 - materializing them would cost 6.2 MB/evt mean
  // and a whole extra pass over nEdges for a value nothing downstream reads).
  static constexpr int kChainEdgeFeatures = 14;

}  // namespace lst

#endif
