#ifndef RecoTracker_LSTCore_interface_ChainEdgesSoA_h
#define RecoTracker_LSTCore_interface_ChainEdgesSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  // One row per EDGE of the chain-tracking triplet graph: an ordered pair of triplets that overlap
  // on a detector element, oriented inner -> outer. ChainBuildEdges enumerates and fills the rows,
  // ChainEdgeInference scores them, and the weld is the only consumer.
  //
  // The collection is allocated with exactly nE1 + nE2 rows, both counts taken from the incidence
  // degree arithmetic sum_key degIn(key) * degOut(key) (ChainIncidenceSoA.h). Rows [0, nE1) are the
  // MD-keyed E1 family (the T5 relation, inner.md2 == outer.md0, so the pair spans one layer more
  // than either triplet) and rows [nE1, nE1 + nE2) are the Segment-keyed E2 family (the T4 relation,
  // inner.ls1 == outer.ls0, same layer span as its members). Every row is written by exactly one
  // thread at its own index, so there is no compaction atomic and no capped reservation anywhere in
  // the pipeline.
  //
  // Pairs that are not real edges are kept as HOLES with type == 0 rather than compacted away, which
  // is what keeps the enumeration index-decoded. Two cases produce one:
  //   - inner == outer (a triplet welding to itself; needs md2 == md0 within one T3)
  //   - an E2 pair that is already an E1 pair (needs md2(inner) == md0(outer) == md1(inner))
  // Both are structurally impossible for well-formed triplets and neither has ever been observed;
  // the tests are kept because they are O(1). Every consumer tests type first, so a hole can never
  // be scored, be welded, or win an argmax.
  GENERATE_SOA_LAYOUT(ChainEdgesSoALayout,
                      SOA_COLUMN(uint32_t, inner),  // dense chain-node index of the inner triplet
                      SOA_COLUMN(uint32_t, outer),  // dense chain-node index of the outer triplet
                      SOA_COLUMN(uint8_t, type),    // 0 = hole, 1 = E1 (shared MD), 2 = E2 (shared LS)
                      // The edge head's margin of "some real track" over "fake",
                      // max(zPrompt, zDisp) - zFake, and the only edge quantity anything downstream
                      // reads: the weld ranks on it, the chain score sums it, and chain features
                      // 2/3/4/18 aggregate it. Set to 0 at enumeration and overwritten by the head
                      // for every non-hole row.
                      SOA_COLUMN(float, logOdds),
                      // Stable tie-break of the weld ranking: the XOR of the two endpoints'
                      // stableId (ChainNodesSoA.h), hence a function of hit rows alone. Exact-logit
                      // ties are common, and the edge index cannot break them because it permutes
                      // between two identical runs; see ChainWeld.h.
                      SOA_COLUMN(uint32_t, tie),
                      // Weld ELIGIBILITY bar this edge must clear, resolved by ChainEdgeInference
                      // and tested by the weld as `logOdds < weldBar` -> ineligible. It is a bar per
                      // edge FAMILY and per (pt, |eta|) cell of the inner node (ChainNodesSoA::wpBin),
                      // so it cannot be a kernel-argument scalar; the inference is the one stage that
                      // already holds both the family and the node row in registers, and the weld
                      // runs 2 * kChainWeldSweeps times over every edge, so resolving it there
                      // instead would repeat the lookup 4-8x per edge.
                      // With the per-cell table off, this is just the matching per-family scalar
                      // from ChainConfig. Rows with type == 0 are left UNSET: both weld kernels test
                      // the type first and never reach this column for them.
                      SOA_COLUMN(float, weldBar))

  using ChainEdgesSoA = ChainEdgesSoALayout<>;
  using ChainEdges = ChainEdgesSoA::View;
  using ChainEdgesConst = ChainEdgesSoA::ConstView;

  // Frozen edge-feature contract of the edge head:
  //  0 etype  1 dKappa  2 dKappaRel  3 chargeAgree  4 dTanLambda  5 kinkPhi  6 kinkTheta
  //  7 centerDist  8 centerDistRel  9 sharedLayer  10 sharedIsPS  11 sharedIsBarrel
  // 12 degIn  13 degOut
  // These are NOT a column of this layout: they are built in registers inside ChainEdgeInference and
  // consumed immediately. Materialising them would cost 6.2 MB/event on average plus a whole extra
  // pass over the edge list, for a value nothing downstream reads.
  static constexpr int kChainEdgeFeatures = 14;

}  // namespace lst

#endif
