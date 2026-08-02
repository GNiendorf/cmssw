#ifndef RecoTracker_LSTCore_interface_ChainIncidenceSoA_h
#define RecoTracker_LSTCore_interface_ChainIncidenceSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  // CSR incidence bookkeeping for the chain-tracking triplet graph (port map phase P2.0, stage K1).
  // Two instances are used: one keyed by MiniDoublet index (source of the E1 edge family) and one
  // keyed by Segment index (source of the E2 edge family).
  //
  // For a key k, with the triplet read innermost -> outermost:
  //   "out" side = triplets whose FIRST  MD (resp. inner Segment) is k, i.e. edges leaving  k
  //   "in"  side = triplets whose SECOND MD (resp. outer Segment) is k, i.e. edges arriving at k
  // A chain edge joins an "in" triplet to an "out" triplet at the same key, so the number of
  // enumerable edges at key k is exactly degIn(k) * degOut(k) and the total edge count is
  // E = sum_k degIn(k) * degOut(k). That arithmetic is what makes the P2.1 edge buffer exactly
  // sized with no capped reservation.
  //
  // Column lengths: the collection is allocated with nKeys + 1 rows. The count columns use rows
  // [0, nKeys), the offset/prefix columns use rows [0, nKeys] so that the last entry is the total.
  GENERATE_SOA_LAYOUT(ChainIncidenceSoALayout,
                      SOA_COLUMN(uint32_t, t3OutCounts),     // K1a tally; reused as the K1c scatter cursor
                      SOA_COLUMN(uint32_t, t3InCounts),      // K1a tally; reused as the K1c scatter cursor
                      SOA_COLUMN(uint32_t, t3OutOffsets),    // K1b exclusive prefix of t3OutCounts
                      SOA_COLUMN(uint32_t, t3InOffsets),     // K1b exclusive prefix of t3InCounts
                      SOA_COLUMN(uint32_t, edgeProdPrefix),  // K1b exclusive prefix of degIn * degOut
                      SOA_SCALAR(uint32_t, nEdgesExact))     // == edgeProdPrefix[nKeys]

  using ChainIncidenceSoA = ChainIncidenceSoALayout<>;
  using ChainIncidence = ChainIncidenceSoA::View;
  using ChainIncidenceConst = ChainIncidenceSoA::ConstView;

}  // namespace lst

#endif
