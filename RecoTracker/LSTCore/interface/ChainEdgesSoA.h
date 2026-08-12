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
                      SOA_COLUMN(uint32_t, tie),  // K2 stable weld tie-break, see ChainWeld.h
                      // S1. The weld ELIGIBILITY bar this edge must clear, materialized by K5 and
                      // compared by K6a/K6b (`logOdds < weldBar` -> ineligible). It replaces the two
                      // per-family scalars the weld kernels used to take as arguments: the bar is now
                      // a per-cell table entry keyed on the edge FAMILY and on the inner node's
                      // (pT, |eta|) cell (ChainNodesSoA::wpBin), and K5 is the one place that already
                      // has both the family and the node row in registers. K6 runs
                      // kChainWeldSweeps * 2 times over every edge, so resolving the bar there
                      // instead would repeat the lookup ~8-16x per edge.
                      // Rows with type == 0 (the K2 enumeration holes) are left UNSET: both weld
                      // kernels test the type first and never reach this column for them.
                      SOA_COLUMN(float, weldBar))
  // U5 DELETION: `SOA_SCALAR(uint32_t, nE1Exact)` and `SOA_SCALAR(uint32_t, nE2Exact)` stood here.
  // Both were WRITE-ONLY -- ChainBuildEdges' once_per_grid block set them (also removed) and NOTHING
  // in src/, interface/, standalone/code/ or RecoTracker/LST/ ever read them. The "consumer" the old
  // comment promised ("so a consumer can slice the row range without carrying the two counts
  // separately") does not exist; the host carries nE1 / nE2 itself.
  // WHY THESE TWO CAN GO WHILE claimFlags AND nChains CANNOT (U2's layout split, and it is the whole
  // reason this deletion is safe): these were the LAST TWO MEMBERS of the layout, so removing them
  // shifts NO other member's base address. ChainsSoA::claimFlags sits mid-layout with eight live
  // columns behind it and ChainsSoA::nChains is the first of three scalars with two live ones behind
  // it, so for those the STORE is removed and the MEMBER stays -- see the ~1 ms/event warning at
  // ChainsSoA.h:100-104. Free by construction here; not there.
  // RESIDUAL CAVEAT, stated because it is not literally zero risk: the collection's total byte size
  // changes, so the CMS caching allocator can land it in a different size bin. That is round 1's
  // "address lottery" ([T5 ~10:50]), not a layout shift, and it is bounded by 8 bytes.

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
